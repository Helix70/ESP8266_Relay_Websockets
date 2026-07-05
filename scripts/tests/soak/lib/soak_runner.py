"""Shared orchestration for the per-board soak test entry-point scripts.

See C:\\Users\\lexus\\.claude\\plans\\eager-enchanting-pumpkin.md for the
approved design this implements.
"""
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

        _open_browser_if_enabled(ip)

        run_functional_coverage(http, serial, report, relay_count, cpu)

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


def run_functional_coverage(http, serial, report, relay_count, cpu):
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
    result = boards_api.create_new(http, "Soak Test Board", cpu, relay_count,
                                     "shiftregister" if relay_count == 16 else "gpio")
    report.add(suite, "create new board config", result.ok, "/api/boards", str(result.body), result.latency_ms)
    new_board_filename = (result.json() or {}).get("filename")

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

    ws = WsClient(ip).connect()
    start = time.time()
    did_page_nav = False
    did_theme_change = False
    toggle_count = 0
    reconnect_count = 0

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
            try:
                t_before = time.time()
                _, latency_ms = ws.toggle(relay_id)
                heap_after = serial.heap_near(time.time())
                heap_before = serial.heap_near(t_before)
                toggle_count += 1
                report.add("relay_toggle", f"toggle relay {relay_id}", latency_ms is not None,
                            endpoint="/ws", details=note, latency_ms=latency_ms,
                            heap_before=heap_before, heap_after=heap_after)

                if relay_id in pulsed_wait:
                    time.sleep(pulsed_wait[relay_id] + 0.5)
                    home, _ = ws.get_home()
                    still_on = relay_id in ws.on_buttons(home)
                    report.add("pulsed_autooff", f"relay {relay_id} auto-off after {pulsed_wait[relay_id]}s pulse",
                                not still_on, details=f"still_on={still_on}")
            except Exception as e:
                # A dropped WS connection mid-soak (seen in practice: the
                # firmware can forcibly reset a client under heavy combined
                # load) shouldn't abort 60s of otherwise-good coverage --
                # record it as a real result, reconnect, and keep going.
                reconnect(f"{type(e).__name__}: {e}")
                continue

            elapsed = time.time() - start
            if not did_page_nav and elapsed > duration_s * 0.4:
                did_page_nav = True
                for path in pages + assets:
                    status, _, latency = http.get_raw(path)
                    heap = serial.heap_near(time.time())
                    report.add("page_nav", f"GET {path} during live relay combo", status == 200,
                                endpoint=path, latency_ms=latency, heap_after=heap)
            if not did_theme_change and elapsed > duration_s * 0.6:
                did_theme_change = True
                result = theme_api.set_theme(http, "#556677,#112233,#445566,#778899,#ffeedd,#ccbbaa,#998877,#eeeeee,#111111", "soft")
                report.add("functional", "live theme change during relay soak", result.ok,
                            "/api/theme", str(result.body), result.latency_ms)
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


def _open_browser_if_enabled(ip):
    """Opens a local 'parked' page (not the board's own page) with a link to
    watch the board live. This page makes NO connection to the board itself
    -- only clicking the link does -- so it can't contend for the firmware's
    2-slot WS client cap (ws.cleanupClients(2)) the way directly opening the
    board's page did (confirmed as the likely cause of a mid-soak WS timeout
    in an earlier run: this script's own long-lived WS connection got evicted
    once a second client -- the auto-opened tab -- connected). The parked
    page also explicitly tells you to close everything when done, since
    webbrowser.open() gives no handle to close a tab programmatically.

    Off by default -- opt in with SOAK_OPEN_BROWSER=1."""
    if os.environ.get("SOAK_OPEN_BROWSER", "0") != "1":
        return
    try:
        html = f"""<!DOCTYPE html>
<html><head><title>Soak test -- {ip}</title></head>
<body style="font-family: sans-serif; max-width: 640px; margin: 3em auto;">
<h2>Soak test running against {ip}</h2>
<p>This tab makes no connection to the board itself, so it won't compete for
its 2-slot WebSocket client limit.</p>
<p><a href="http://{ip}/" target="_blank">Click here to watch relays toggle
live</a> in a new tab -- opening it uses one of the board's 2 WS client
slots, so only do this if the test isn't also depending on that slot.</p>
<p><strong>Close this tab (and the live-view tab, if opened) once the test
finishes.</strong></p>
</body></html>"""
        tmp_path = Path(tempfile.gettempdir()) / f"soak_test_parked_{ip.replace('.', '_')}.html"
        tmp_path.write_text(html, encoding="utf-8")
        webbrowser.open(tmp_path.as_uri())
        print(f"  opened a parked info page (not connected to the board) at {tmp_path}")
    except Exception as e:
        print(f"  (could not open browser automatically: {e})")
