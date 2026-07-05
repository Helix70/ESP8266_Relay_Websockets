"""Shared orchestration for the per-board soak test entry-point scripts.

See C:\\Users\\lexus\\.claude\\plans\\eager-enchanting-pumpkin.md for the
approved design this implements.
"""
import json
import os
import tempfile
import time
import webbrowser
from pathlib import Path

import boards_api
import combos
import config_api
import safety
import templates_api
import theme_api
from credentials import get_wifi_credentials
from http_client import HttpClient
from report import Report
from serial_client import SerialClient
from ws_client import WsClient

REPORTS_DIR = Path(__file__).resolve().parent.parent / "reports"

DEFAULT_LONG_DURATION_S = 4 * 3600  # 4 hours


def long_duration_s():
    """Override with SOAK_LONG_DURATION_S (seconds) for a shorter/longer
    long-soak run than the 4-hour default -- e.g. for a quick check that the
    long-soak entry point itself works before committing to a full run."""
    return int(os.environ.get("SOAK_LONG_DURATION_S", DEFAULT_LONG_DURATION_S))


def run_soak(board_name, com_port, relay_count, cpu, board_hardware_file, duration_s=60):
    safety.confirm_or_exit(board_name, com_port, relay_count)

    serial = SerialClient(com_port).open()
    try:
        print(f"[{board_name}] Ensuring a known-clean serial state (reboot, no confirm needed)...")
        serial.ensure_clean_state()

        print(f"\n[{board_name}] Sending serial 'reset'...")
        serial.run_reset()

        ssid, password = get_wifi_credentials()

        print(f"[{board_name}] Reprovisioning WiFi (SSID={ssid})...")
        serial.run_wifi_wizard(ssid, password, use_dhcp=True)
        ip = serial.wait_for_reconnect(timeout=30)
        print(f"[{board_name}] Reconnected at {ip}")

        http = HttpClient(ip)
        netinfo = http.get("/netinfo")
        if not netinfo.ok:
            raise RuntimeError(f"/netinfo not reachable at {ip} after reprovision: {netinfo}")

        report = Report(board_name, ip)

        # 'reset' deliberately wipes hardwareVariant too (config_store.cpp:
        # kDefaultVariant = "" // empty = not yet configured) -- same as a
        # genuinely fresh device, it needs the board hardware re-selected
        # before relayCount is anything but 0. Confirmed via a real run: every
        # relay-count-dependent call failed with relay_count_mismatch until
        # this was added.
        result = boards_api.setactive(http, board_hardware_file)
        report.add("functional", "select board hardware after reset", result.ok,
                    "/api/boards", str(result.body), result.latency_ms)

        _open_browser_if_enabled(ip, cpu)

        http, ip = run_functional_coverage(http, serial, report, relay_count, cpu, ip)

        # NOTE: deliberately not reopening the browser here after the
        # /api/clearwifi drill's reboot. The firmware caps concurrent WS
        # clients at 2 (ws.cleanupClients(2)); this script's own connection
        # already holds one slot, so a second open() call here (creating a
        # second tab while the first is likely still connected) would risk
        # exceeding the cap and getting a connection force-closed. The first
        # tab will just show a disconnected page briefly across this reboot
        # and reconnect once the board is back (script.js auto-reconnects).
        http, ip = run_clearwifi_recovery_drill(http, serial, report, ssid, password)

        specs = combos.build_test_template_spec(relay_count)
        lip_title = "Soak Test LIP Template"
        lip_filename = "soak-test-lip-template.json"
        t0 = time.time()
        result = templates_api.create_content(http, lip_title, lip_filename, relay_count, specs)
        report.add("template_lip", "create purpose-built L/I/P/group template", result.ok,
                    "/api/templates", str(result.body), result.latency_ms)
        report.add_specific("lipTemplate", {
            "title": lip_title,
            "filename": lip_filename,
            "relayCount": relay_count,
            "relays": [dict(spec.to_template_entry(), id=spec.id) for spec in specs],
        })
        result = templates_api.setactive(http, lip_filename)
        report.add("template_lip", "activate L/I/P/group template", result.ok,
                    "/api/templates", str(result.body), result.latency_ms)

        run_relay_soak(http, serial, report, specs, ip, duration_s=duration_s)

        with WsClient(ip) as ws:
            home, _ = ws.get_home()
            remaining = ws.on_buttons(home)
            for rid in remaining:
                ws.toggle(rid)
            home, _ = ws.get_home()
            still_on = ws.on_buttons(home)
            report.add("cleanup", "all relays off after soak", still_on == [],
                        details=f"remaining on: {still_on}")

        result = templates_api.delete(http, lip_filename)
        report.add("template_lip", "delete purpose-built template (cleanup)", result.ok,
                    "/api/templates", str(result.body), result.latency_ms)

        print(f"\n[{board_name}] Final reset + reprovision...")
        serial.ensure_clean_state()
        serial.run_reset()
        serial.run_wifi_wizard(ssid, password, use_dhcp=True)
        final_ip = serial.wait_for_reconnect(timeout=30)
        print(f"[{board_name}] Back online at {final_ip} with default settings")

        # Reselect the board hardware one last time so the device is left
        # genuinely usable (relayCount != 0) rather than stuck in the
        # "not yet configured" state 'reset' leaves it in -- matches "reset,
        # then reprovision WiFi so it's still usable afterward" intent for
        # everything, not just connectivity.
        final_http = HttpClient(final_ip)
        result = boards_api.setactive(final_http, board_hardware_file)
        report.add("functional", "select board hardware after final reset", result.ok,
                    "/api/boards", str(result.body), result.latency_ms)

        report.print_summary()
        out_path = REPORTS_DIR / board_name / f"{int(time.time())}.json"
        report.finish(out_path)
        print(f"[{board_name}] Report written to {out_path}")

        return report.summary()["failed"] == 0
    finally:
        serial.close()


def run_storage_pressure_test(http, report, relay_count):
    """Create a bounded batch of templates and confirm fsFreeBytes accounts
    for them correctly, then clean up and confirm it's restored.

    Does NOT attempt to actually reach ensureTemplateWriteHeadroom's 507
    rejection (a prior version of this test tried, by creating templates
    until the ~650KB-1.1MB partition was full) -- live hardware testing
    found that per-write latency on this board grows sharply with the
    /templates directory's entry count, independent of storage headroom:
    on the ESP32 board, create latency measured 387ms at 0 existing
    soak-test entries, 1.3s at 100, and >20s (client timeout) by 142, long
    before the ~280 templates actual exhaustion would need. Chasing genuine
    exhaustion this way makes the test slow, hardware-load-dependent, and
    prone to timing out on a code path (the simple `usage.free < required +
    kTemplateWriteSafetyBytes` check in ensureTemplateWriteHeadroom,
    template_routes.cpp) that's already straightforward to confirm correct
    by reading it. See src/CLAUDE.md for the full writeup -- this directory-
    growth latency characteristic is a real, if unusual-to-trigger, finding
    worth knowing about independent of this test.

    Label size is deliberately modest (40 chars, not hundreds) -- a separate
    live hardware repro found the underlying ESPAsyncWebServer plain-POST
    body parser silently drops the entire 'content' field (400 'content
    required', not a graceful size-limit error) once the url-encoded
    request body exceeds ~2.48KB. A 40-char label keeps every request
    comfortably under that ceiling regardless."""
    suite = "functional"
    diag = templates_api.diagnostics(http).json() or {}
    baseline_free = diag.get("fsFreeBytes")
    long_label = "X" * 40
    created = []
    # Comfortably inside the fast-latency zone observed on real hardware
    # (still sub-second per create at this count on every board tested).
    batch_size = 20
    create_failures = 0
    for i in range(batch_size):
        specs = [
            combos.RelaySpec(s.id, s.mode, s.group, s.pulse_timeout, on_label=long_label, off_label=long_label)
            for s in combos.build_test_template_spec(relay_count)
        ]
        # Short and fixed-width regardless of i's digit count -- see the
        # pulse-boundary test above for why this matters
        # (kMaxTemplateFilenameLength=26, so the real ceiling is 31 chars).
        filename = f"sp-{i}.json"
        result = templates_api.create_content(http, f"Soak Test Storage Pressure {i}", filename, relay_count, specs)
        if result.ok:
            created.append(filename)
        else:
            create_failures += 1
        time.sleep(0.05)
    report.add(suite, "storage pressure batch creates all succeed", create_failures == 0,
                "/api/templates", f"{len(created)}/{batch_size} created, {create_failures} failed")

    diag_mid = templates_api.diagnostics(http).json() or {}
    free_mid = diag_mid.get("fsFreeBytes")
    consumed_ok = baseline_free is not None and free_mid is not None and free_mid < baseline_free
    report.add(suite, "storage accounting reflects new templates (fsFreeBytes decreased)", consumed_ok,
                "/api/templates/diagnostics", f"before={baseline_free} afterCreates={free_mid}")

    delete_failures = 0
    for filename in created:
        result = templates_api.delete(http, filename)
        if not result.ok:
            delete_failures += 1
    report.add(suite, "storage pressure cleanup (delete all created templates)", delete_failures == 0,
                "/api/templates", f"{len(created)} created, {delete_failures} failed to delete")

    diag_after = templates_api.diagnostics(http).json() or {}
    free_after = diag_after.get("fsFreeBytes")
    # Paced deletes reproduced an exact match on real hardware (0 byte
    # drift); one block's worth of slack (the smallest unit LittleFS
    # allocates/frees in on this partition) covers ordinary filesystem noise
    # without masking a genuine leak.
    recovered_ok = baseline_free is not None and free_after is not None and abs(free_after - baseline_free) < 8192
    report.add(suite, "storage free space recovered after cleanup", recovered_ok,
                "/api/templates/diagnostics", f"before={baseline_free} after={free_after}")


def run_ws_malformed_test(http, ip, report):
    """Send garbage/non-JSON text to /ws and confirm the board doesn't
    crash -- a subsequent 'home' query must still succeed."""
    suite = "functional"
    survived = False
    try:
        with WsClient(ip) as ws:
            garbage_messages = [
                "not json at all {{{",
                "\x00\x01\x02binary-ish-garbage\xff\xfe",
                "a" * 5000,
                "",
            ]
            for msg in garbage_messages:
                try:
                    ws.ws.send(msg)
                except Exception:
                    pass
            time.sleep(0.5)
            home, _ = ws.get_home()
            survived = home is not None and "buttons" in home
    except Exception:
        survived = False
    report.add(suite, "board survives malformed/garbage WS messages", survived,
                "/ws", "sent 4 garbage frames, confirmed 'home' still works afterward")


def run_functional_coverage(http, serial, report, relay_count, cpu, ip):
    suite = "functional"

    # -- Board name change --
    long_name = config_api.long_board_name_example()
    result = config_api.set_board_config(http, long_name, use_dhcp=True)
    report.add(suite, "set long board name (expect truncation, not rejection)", result.ok,
                "/api/config", str(result.body), result.latency_ms)
    result = config_api.set_board_config(http, f"{report.board_name} Soak Test", use_dhcp=True)
    report.add(suite, "set normal board name", result.ok, "/api/config", str(result.body), result.latency_ms)

    # -- Theme --
    baseline = theme_api.get(http)
    baseline_json = baseline.json() or {}
    result = theme_api.set_theme(http, "#112233,#445566,#778899,#aabbcc,#ddeeff,#001122,#334455", "outline")
    report.add(suite, "set valid theme", result.ok, "/api/theme", str(result.body), result.latency_ms)
    for bad_h in theme_api.invalid_hex_examples():
        result = theme_api.set_theme(http, bad_h, "classic")
        report.add(suite, f"reject invalid theme hex ({bad_h[:20]}...)", result.status == 400,
                    "/api/theme", str(result.body), result.latency_ms)
    for bad_s in theme_api.invalid_style_examples():
        result = theme_api.set_theme(http, "#112233,#445566,#778899,#aabbcc,#ddeeff,#001122,#334455", bad_s)
        report.add(suite, f"reject invalid theme style ({bad_s!r})", result.status == 400,
                    "/api/theme", str(result.body), result.latency_ms)
    if baseline_json:
        result = theme_api.set_theme(http, baseline_json.get("h", ""), baseline_json.get("s", ""))
        report.add(suite, "revert theme to baseline", result.ok, "/api/theme", str(result.body), result.latency_ms)

    # -- Network: DHCP -> static (same IP) -> DHCP --
    netinfo = http.get("/netinfo").json() or {}
    prev_boot = netinfo.get("bootSessionId")
    result = config_api.set_board_config(
        http, netinfo.get("boardName", report.board_name), use_dhcp=False,
        ip=netinfo.get("ipAddress"), dns=netinfo.get("dns"),
        gateway=netinfo.get("gateway"), subnet=netinfo.get("subnet"),
    )
    report.add(suite, "switch to static IP (matching current address)", result.ok,
                "/api/config", str(result.body), result.latency_ms)
    _wait_for_new_boot_session(http, prev_boot, timeout=30)

    # -- Invalid static IP fields should be rejected without a reboot --
    netinfo = http.get("/netinfo").json() or {}
    for bad_ip in config_api.invalid_static_ip_examples():
        result = config_api.set_board_config(
            http, netinfo.get("boardName", report.board_name), use_dhcp=False, **bad_ip
        )
        report.add(suite, f"reject invalid static IP fields ({bad_ip})", result.status == 400,
                    "/api/config", str(result.body), result.latency_ms)

    netinfo = http.get("/netinfo").json() or {}
    prev_boot = netinfo.get("bootSessionId")
    result = config_api.set_board_config(http, netinfo.get("boardName", report.board_name), use_dhcp=True)
    report.add(suite, "revert to DHCP", result.ok, "/api/config", str(result.body), result.latency_ms)
    _wait_for_new_boot_session(http, prev_boot, timeout=30)

    # -- wifi/rescan, labels --
    result = config_api.wifi_rescan(http)
    report.add(suite, "trigger wifi rescan", result.ok, "/api/wifi/rescan", str(result.body), result.latency_ms)

    label_specs = combos.build_test_template_spec(relay_count)
    result = config_api.set_labels(http, relay_count, label_specs)
    report.add(suite, "direct label edit via /api/labels", result.ok, "/api/labels", str(result.body), result.latency_ms)

    # -- Pulse timeout boundary clamp (kMaxPulseTimeoutSeconds=60): 0 and 61
    # should both clamp to kDefaultPulseTimeoutSeconds=1 on write, per
    # template_routes.cpp's writeTemplateJson. Verified by uploading a
    # one-off template with relay 1 forced Pulsed at the boundary value, then
    # downloading it back and reading the actual stored 'p'. --
    for bad_pulse in (0, 61):
        boundary_specs = list(label_specs)
        first = boundary_specs[0]
        boundary_specs[0] = combos.RelaySpec(first.id, combos.MODE_PULSED, first.group, pulse_timeout=bad_pulse)
        # Short and fixed-width regardless of bad_pulse's digit count --
        # kMaxTemplateFilenameLength is 26 (not the 40 the title limit uses),
        # so "soak-test-pulse-boundary-NN.json" (32 chars for a 2-digit N)
        # blows past the real 31-char ceiling and gets rejected as an
        # "invalid filename" 400 that has nothing to do with the pulse clamp
        # being tested -- confirmed via a live hardware repro.
        boundary_filename = f"soak-pulse-b-{bad_pulse}.json"
        result = templates_api.create_content(http, "Soak Test Pulse Boundary", boundary_filename,
                                                relay_count, boundary_specs)
        clamped_to = None
        if result.ok:
            status, body, _ = templates_api.download(http, boundary_filename)
            if status == 200:
                try:
                    clamped_to = json.loads(body)["l"][0].get("p")
                except Exception:
                    clamped_to = None
            templates_api.delete(http, boundary_filename)
        report.add(suite, f"pulse timeout {bad_pulse} clamps to 1 (kDefaultPulseTimeoutSeconds)",
                    result.ok and clamped_to == 1, "/api/templates",
                    f"created={result.ok} storedPulse={clamped_to}", result.latency_ms)

    # -- clearwifi negative path only here (positive path is its own drill) --
    result = config_api.clear_wifi(http, confirm=False)
    report.add(suite, "reject /api/clearwifi without confirm", result.status == 400,
                "/api/clearwifi", str(result.body), result.latency_ms)

    # -- Templates: full CRUD + coverage of shadowed/dead routes + known bug --
    result = templates_api.create_form(http, "Soak Test Form Template", label_specs)
    report.add(suite, "create template (form-fields path)", result.ok, "/api/templates",
                str(result.body), result.latency_ms)
    form_filename = (result.json() or {}).get("filename")

    listing = templates_api.list_templates(http)
    filenames = [t["filename"] for t in (listing.json() or {}).get("templates", [])]
    report.add(suite, "created template appears in list", form_filename in filenames,
                "/api/templates", str(filenames))

    if form_filename:
        status, body, latency = templates_api.download(http, form_filename)
        report.add(suite, "download template (static file GET)", status == 200,
                    f"/templates/{form_filename}", f"status={status}", latency)

        result = templates_api.rename(http, form_filename, "Soak Test Form Template Renamed")
        report.add(suite, "rename template", result.ok, "/api/templates", str(result.body), result.latency_ms)
        renamed_filename = (result.json() or {}).get("filename", form_filename)

        for bad_fn in templates_api.invalid_filename_examples():
            result = templates_api.create_content(http, "Soak Test Invalid Filename", bad_fn, relay_count, label_specs)
            report.add(suite, f"reject invalid template filename ({bad_fn!r})", result.status == 400,
                        "/api/templates", str(result.body), result.latency_ms)

        result = templates_api.delete(http, renamed_filename)
        report.add(suite, "delete template", result.ok, "/api/templates", str(result.body), result.latency_ms)

    result = templates_api.hit_dead_select_route(http, "western-tower.json")
    report.add(suite, "dead /api/templates/select route stays shadowed (expect title-required 400)",
                result.status == 400, "/api/templates/select", str(result.body), result.latency_ms)

    result = templates_api.create_content_bug_regression(http, "Soak Test Bug Regression",
                                                            "soak-bug-regression.json", relay_count)
    report.add(suite, "relay-config.js upload bug stays reproducible (expect content-required 400)",
                result.status == 400, "/api/templates", str(result.body), result.latency_ms)

    # -- Boards: CRUD on a NEW board file only, never the active one --
    active_board_file = boards_api.get_active_board_file(http)
    new_board_output_type = "shiftregister" if relay_count == 16 else "gpio"
    result = boards_api.create_new(http, "Soak Test Board", cpu, relay_count, new_board_output_type)
    report.add(suite, "create new board config", result.ok, "/api/boards", str(result.body), result.latency_ms)
    new_board_filename = (result.json() or {}).get("filename")
    report.add_specific("newBoardConfig", {
        "title": "Soak Test Board",
        "filename": new_board_filename,
        "cpu": cpu,
        "relayCount": relay_count,
        "outputType": new_board_output_type,
    })

    if new_board_filename:
        status, body, latency = boards_api.download(http, new_board_filename)
        report.add(suite, "download board config (static file GET)", status == 200,
                    f"/{new_board_filename}", f"status={status}", latency)

        result = boards_api.rename(http, new_board_filename, "Soak Test Board Renamed",
                                    active_board_file=active_board_file)
        report.add(suite, "rename new (non-active) board config", result.ok,
                    "/api/boards", str(result.body), result.latency_ms)
        renamed_board = (result.json() or {}).get("filename", new_board_filename)

        result = boards_api.upload_content(
            http, '{"name":"Soak Test Wrong CPU","cpu":"' + boards_api.wrong_cpu_example(cpu) +
            f'","relayCount":{relay_count},"outputType":"gpio"}}',
            filename="soak-test-wrong-cpu.json")
        report.add(suite, "reject board upload with mismatched cpu", result.status == 400,
                    "/api/boards", str(result.body), result.latency_ms)

        result = boards_api.delete(http, renamed_board, active_board_file=active_board_file)
        report.add(suite, "delete new (non-active) board config", result.ok,
                    "/api/boards", str(result.body), result.latency_ms)

    # -- Storage pressure + WS malformed-message resilience --
    run_storage_pressure_test(http, report, relay_count)
    run_ws_malformed_test(http, ip, report)

    # -- Serial 'help' (no state change) --
    serial.run_help()
    report.add(suite, "serial 'help' command reprints command list", True,
                "serial:help", "saw 'Available commands:' after sending help")

    # -- Serial 'reboot' text command (distinct from ensure_clean_state's
    # hardware RTS pulse -- this exercises serial_commands.cpp's own reboot
    # handler, not just a raw hardware reset) --
    since = time.time()
    serial.run_reboot_command()
    ip = serial.wait_for_reconnect(timeout=30, since_ts=since)
    http = HttpClient(ip)
    netinfo = http.get("/netinfo")
    report.add(suite, "serial 'reboot' text command", netinfo.ok,
                "serial:reboot", f"reconnected at {ip}: {netinfo.body}", netinfo.latency_ms)

    # -- POST /api/reboot --
    netinfo = http.get("/netinfo").json() or {}
    prev_boot = netinfo.get("bootSessionId")
    result = config_api.reboot(http)
    report.add(suite, "POST /api/reboot", result.ok, "/api/reboot", str(result.body), result.latency_ms)
    _wait_for_new_boot_session(http, prev_boot, timeout=30)

    return http, ip


def run_clearwifi_recovery_drill(http, serial, report, ssid, password):
    """Exercises /api/clearwifi's positive path (distinct code path from the
    serial 'reset'/'wifi' commands: only clears WiFi creds, drops straight
    into the SoftAP portal) without spending a whole extra full-reset cycle
    -- folds into the existing reprovisioning budget as agreed in the plan."""
    since = time.time()
    result = config_api.clear_wifi(http, confirm=True)
    report.add("functional", "/api/clearwifi with confirm triggers reboot", result.ok,
                "/api/clearwifi", str(result.body), result.latency_ms)
    serial.wait_for("Provisioning", timeout=20, since_ts=since)
    serial.run_wifi_wizard(ssid, password, use_dhcp=True)
    new_ip = serial.wait_for_reconnect(timeout=30, since_ts=since)
    print(f"  recovered from /api/clearwifi drill at {new_ip}")
    return HttpClient(new_ip), new_ip


def run_relay_soak(http, serial, report, specs, ip, duration_s=60):
    print(f"\n[relay soak] running combinations for at least {duration_s}s...")
    pages = ["/", "/config.html", "/relay-config.html", "/boards.html", "/template-editor.html"]
    assets = ["/style.css", "/script.js", "/config.js", "/relay-config.js", "/boards.js", "/template-editor.js"]
    pulsed_wait = {s.id: s.pulse_timeout for s in specs if s.mode == combos.MODE_PULSED}

    # Page-nav / theme-change fire periodically rather than once, so a
    # genuine multi-hour long-soak keeps exercising them throughout instead
    # of only near the start -- interval scales with duration but is capped
    # so a short 60s run still behaves like a single pass near 40%/60%
    # elapsed (matching the original one-shot design), while an hours-long
    # run repeats every few minutes.
    page_nav_interval = min(max(duration_s * 0.4, 20), 300)
    theme_change_interval = min(max(duration_s * 0.6, 30), 600)
    progress_interval = min(max(duration_s * 0.1, 30), 300)

    # A short run logs every toggle (matches prior behavior); a long run
    # would otherwise produce tens of thousands of report entries (a toggle
    # round-trip is ~150-300ms, so a 4-hour run is ~50-100k toggles) -- too
    # large for the JSON report or the dashboard to render sensibly. Sample
    # instead, targeting roughly a few hundred logged toggles regardless of
    # run length; pulsed-relay toggles and their auto-off checks are always
    # logged in full since those are correctness assertions, not just
    # latency samples.
    log_stride = max(1, round(duration_s / 100))

    ws = WsClient(ip).connect()
    start = time.time()
    next_page_nav_at = start + page_nav_interval
    next_theme_change_at = start + theme_change_interval
    next_progress_at = start + progress_interval
    toggle_count = 0
    reconnect_count = 0
    theme_toggle = False

    def reconnect(reason):
        nonlocal ws, reconnect_count
        reconnect_count += 1
        report.add("relay_toggle", "WS connection dropped, reconnecting", False,
                    endpoint="/ws", details=f"{reason} (reconnect #{reconnect_count})")
        try:
            ws.close()
        except Exception:
            pass
        time.sleep(0.5)
        ws = WsClient(ip).connect()

    try:
        for relay_id, note in combos.iter_combinations(specs, duration_s=duration_s):
            is_pulsed = relay_id in pulsed_wait
            should_log = is_pulsed or (toggle_count % log_stride == 0)
            try:
                t_before = time.time()
                _, latency_ms = ws.toggle(relay_id)
                toggle_count += 1
                if should_log:
                    heap_after = serial.heap_near(time.time())
                    heap_before = serial.heap_near(t_before)
                    report.add("relay_toggle", f"toggle relay {relay_id}", latency_ms is not None,
                                endpoint="/ws", details=note, latency_ms=latency_ms,
                                heap_before=heap_before, heap_after=heap_after)

                if is_pulsed:
                    time.sleep(pulsed_wait[relay_id] + 0.5)
                    home, _ = ws.get_home()
                    still_on = relay_id in ws.on_buttons(home)
                    report.add("pulsed_autooff", f"relay {relay_id} auto-off after {pulsed_wait[relay_id]}s pulse",
                                not still_on, details=f"still_on={still_on}")
            except Exception as e:
                # A dropped WS connection mid-soak (seen in practice: the
                # firmware can forcibly reset a client under heavy combined
                # load) shouldn't abort the soak -- record it as a real
                # result, reconnect, and keep going.
                reconnect(f"{type(e).__name__}: {e}")
                continue

            now = time.time()
            if now >= next_page_nav_at:
                next_page_nav_at = now + page_nav_interval
                for path in pages + assets:
                    status, _, latency = http.get_raw(path)
                    heap = serial.heap_near(time.time())
                    report.add("page_nav", f"GET {path} during live relay combo", status == 200,
                                endpoint=path, latency_ms=latency, heap_after=heap)
            if now >= next_theme_change_at:
                next_theme_change_at = now + theme_change_interval
                theme_toggle = not theme_toggle
                theme = ("#556677,#112233,#445566,#778899,#ffeedd,#ccbbaa,#998877,#eeeeee,#111111", "soft") if theme_toggle \
                    else ("#112233,#445566,#778899,#aabbcc,#ddeeff,#001122,#334455,#556677,#778899", "outline")
                result = theme_api.set_theme(http, *theme)
                report.add("functional", "live theme change during relay soak", result.ok,
                            "/api/theme", str(result.body), result.latency_ms)
            if now >= next_progress_at:
                next_progress_at = now + progress_interval
                heap = serial.heap_near(now)
                elapsed_min = (now - start) / 60
                print(f"  [relay soak progress] {elapsed_min:.1f}min elapsed, {toggle_count} toggles, "
                      f"{reconnect_count} reconnects, heap~{heap if heap is not None else '?'}")
                report.add("soak_progress", f"checkpoint at {elapsed_min:.1f}min", True,
                            details=f"toggles={toggle_count} reconnects={reconnect_count}", heap_after=heap)
    finally:
        ws.close()

    print(f"[relay soak] completed {toggle_count} toggles over {time.time()-start:.1f}s"
          + (f" ({reconnect_count} WS reconnects)" if reconnect_count else ""))


def _wait_for_new_boot_session(http, prev_boot, timeout=30):
    deadline = time.time() + timeout
    while time.time() < deadline:
        result = http.get("/netinfo", timeout=3)
        info = result.json()
        if result.ok and info and info.get("bootSessionId") != prev_boot:
            return info
        time.sleep(1)
    raise RuntimeError(f"board did not reboot with a new bootSessionId within {timeout}s")


def _open_browser_if_enabled(ip, cpu):
    """Opens a local 'parked' page (not the board's own page) with a link to
    watch the board live. This page makes NO connection to the board itself
    -- only clicking the link does -- so it can't contend for the firmware's
    WS client cap the way directly opening the board's page did (confirmed
    as the likely cause of a mid-soak WS timeout in an earlier run, back
    when every platform shared the same 2-slot cap: this script's own
    long-lived WS connection got evicted once a second client -- the
    auto-opened tab -- connected). The parked page also explicitly tells you
    to close everything when done, since webbrowser.open() gives no handle
    to close a tab programmatically.

    ESP32 now runs a platform-specific cap of 8 (see main.cpp's
    kWsMaxClients), comfortably covering this script's connection plus a
    live-view tab plus normal UI use at the same time -- ESP8266 stays at 2
    given its much tighter heap margin, where a live-view tab is still real
    contention. Off by default regardless of platform -- opt in with
    SOAK_OPEN_BROWSER=1."""
    if os.environ.get("SOAK_OPEN_BROWSER", "0") != "1":
        return
    ws_slots = 8 if cpu == "ESP32" else 2
    try:
        html = f"""<!DOCTYPE html>
<html><head><title>Soak test -- {ip}</title></head>
<body style="font-family: sans-serif; max-width: 640px; margin: 3em auto;">
<h2>Soak test running against {ip} ({cpu})</h2>
<p>This tab makes no connection to the board itself, so it won't compete for
its {ws_slots}-slot WebSocket client limit.</p>
<p><a href="http://{ip}/" target="_blank">Click here to watch relays toggle
live</a> in a new tab -- opening it uses one of the board's {ws_slots} WS client
slots. {"There's ample headroom on this platform for the test, this view, and normal use simultaneously." if ws_slots > 2 else "Only do this if the test isn't also depending on that slot -- ESP8266's margin is tight."}</p>
<p><strong>Close this tab (and the live-view tab, if opened) once the test
finishes.</strong></p>
</body></html>"""
        tmp_path = Path(tempfile.gettempdir()) / f"soak_test_parked_{ip.replace('.', '_')}.html"
        tmp_path.write_text(html, encoding="utf-8")
        webbrowser.open(tmp_path.as_uri())
        print(f"  opened a parked info page (not connected to the board) at {tmp_path}")
    except Exception as e:
        print(f"  (could not open browser automatically: {e})")
