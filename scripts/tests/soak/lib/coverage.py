"""Estimated behavioral coverage of the firmware's HTTP/WS/serial surface.

This is NOT instrumented line coverage (gcov/lcov) -- that would need a
separate host-side build and is out of scope (see the approved plan at
C:\\Users\\lexus\\.claude\\plans\\eager-enchanting-pumpkin.md). This is a
manually-curated map of every distinct route/action/command discovered
during this suite's design (src/template_routes.cpp, src/board_routes.cpp,
src/config_system_routes.cpp, src/serial_commands.cpp,
src/serial_provision.cpp), each marked covered/not-covered with a reason.
Keep this in sync by hand when either the suite or the firmware's route
surface changes -- there is no automated cross-check.
"""

SURFACE = [
    # -- templates (src/template_routes.cpp) --
    {"area": "templates", "item": "GET /api/templates/bootstrap", "covered": False,
     "note": "UI-only payload for relay-config.html's initial load; no distinct server logic beyond GET /api/templates + active board/relay state already covered elsewhere"},
    {"area": "templates", "item": "GET /api/templates/diagnostics", "covered": True,
     "note": "used to observe fsFreeBytes before/after the storage-pressure test"},
    {"area": "templates", "item": "GET /api/templates (list)", "covered": True, "note": None},
    {"area": "templates", "item": "POST /api/templates action=save (form-fields path)", "covered": True, "note": None},
    {"area": "templates", "item": "POST /api/templates action=upload (content-based path)", "covered": True, "note": None},
    {"area": "templates", "item": "POST /api/templates action=setactive", "covered": True, "note": None},
    {"area": "templates", "item": "POST /api/templates action=rename", "covered": True, "note": None},
    {"area": "templates", "item": "POST /api/templates action=delete", "covered": True, "note": None},
    {"area": "templates", "item": "DELETE /api/templates", "covered": True, "note": None},
    {"area": "templates", "item": "GET /templates/<file>.json (static download)", "covered": True, "note": None},
    {"area": "templates", "item": "POST /api/templates/select (dead, shadowed route)", "covered": True,
     "note": "hit deliberately to confirm the shadowing behavior stays reproducible"},
    {"area": "templates", "item": "POST /api/templates/upload (dead, shadowed route)", "covered": False,
     "note": "same shadowed-route class as /api/templates/select; one representative sample was taken rather than doubling up on an identical finding"},
    {"area": "templates", "item": "storage-near-full rejection (507 insufficient storage)", "covered": False,
     "note": "attempted via live exhaustion, but reaching it needs ~80-280 templates and per-write latency on real hardware grows sharply with /templates directory entry count (387ms at 0 entries, >20s by ~140 on the ESP32 board) -- reaching 507 this way is impractically slow/hardware-load-dependent. The rejection logic itself (ensureTemplateWriteHeadroom's usage.free < required+512 check, template_routes.cpp) was verified correct by direct source reading instead. A bounded 20-template batch create/cleanup exercises the surrounding accounting (fsFreeBytes decreasing and recovering) without chasing the boundary itself."},

    # -- boards (src/board_routes.cpp) --
    {"area": "boards", "item": "GET /api/boards (list)", "covered": True, "note": None},
    {"area": "boards", "item": "POST /api/boards action=save (create)", "covered": True, "note": None},
    {"area": "boards", "item": "POST /api/boards action=upload", "covered": True,
     "note": "exercised via the cpu-mismatch rejection case"},
    {"area": "boards", "item": "POST /api/boards action=rename", "covered": True, "note": None},
    {"area": "boards", "item": "POST /api/boards action=delete", "covered": True, "note": None},
    {"area": "boards", "item": "POST /api/boards action=setactive", "covered": True,
     "note": "also exercised operationally: re-selecting hardware after every reset"},
    {"area": "boards", "item": "GET /<file> (static board-config download)", "covered": True, "note": None},

    # -- system config (src/config_system_routes.cpp) --
    {"area": "config", "item": "POST /api/config (name, DHCP/static-IP)", "covered": True,
     "note": "doDelay/connectStrongestOnStartup/delaySeconds fields are sent but not separately asserted on"},
    {"area": "config", "item": "POST /api/clearwifi", "covered": True,
     "note": "both the missing-confirm rejection and the confirmed reboot path"},
    {"area": "config", "item": "POST /api/reboot", "covered": True,
     "note": "exercised directly (distinct from ensure_clean_state's hardware RTS pulse), verified via a new bootSessionId after the reboot"},
    {"area": "config", "item": "POST /api/wifi/rescan", "covered": True, "note": None},
    {"area": "config", "item": "GET /api/theme", "covered": True, "note": None},
    {"area": "config", "item": "POST /api/theme (valid + invalid hex/style)", "covered": True, "note": None},
    {"area": "config", "item": "POST /api/labels (direct label edit)", "covered": True, "note": None},

    # -- websocket (src/web_runtime.cpp, src/relay_runtime.cpp) --
    {"area": "websocket", "item": "WS \"home\" query", "covered": True, "note": None},
    {"area": "websocket", "item": "WS \"buttonN\" toggle, all modes/groups", "covered": True, "note": None},
    {"area": "websocket", "item": "WS malformed/unrecognized message", "covered": True,
     "note": "sends non-JSON, binary-ish, oversized, and empty frames, then confirms a subsequent 'home' query still succeeds"},

    # -- serial console (src/serial_commands.cpp, src/serial_provision.cpp) --
    {"area": "serial", "item": "help", "covered": True,
     "note": "sent directly via the command dispatcher, distinct from the identical banner printed once automatically at boot"},
    {"area": "serial", "item": "reboot (serial text command)", "covered": True,
     "note": "sent directly (distinct from ensure_clean_state's hardware RTS pulse), verified the board reconnects and /netinfo responds afterward"},
    {"area": "serial", "item": "reset (with y/N confirm)", "covered": True, "note": None},
    {"area": "serial", "item": "wifi (with y/N confirm + SSID/password/DHCP wizard)", "covered": True, "note": None},
]


def summary():
    total = len(SURFACE)
    covered = sum(1 for s in SURFACE if s["covered"])
    return {
        "totalItems": total,
        "coveredItems": covered,
        "estimatedCoveragePct": round(covered / total * 100, 1),
        "byArea": _by_area(),
        "items": SURFACE,
        "methodology": (
            "Manually-curated map of every distinct HTTP/WS route+action and serial "
            "command discovered while designing this suite -- not instrumented line "
            "coverage (gcov/lcov), which would need a separate host-side build. "
            "Treat as a behavioral-surface estimate, not a compiled-code percentage."
        ),
    }


def _by_area():
    areas = {}
    for s in SURFACE:
        a = areas.setdefault(s["area"], {"total": 0, "covered": 0})
        a["total"] += 1
        if s["covered"]:
            a["covered"] += 1
    for a in areas.values():
        a["pct"] = round(a["covered"] / a["total"] * 100, 1)
    return areas
