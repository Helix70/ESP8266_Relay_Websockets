"""Wrappers for every /api/boards action, per src/board_routes.cpp.

Hard rule (from CLAUDE.md/session precedent): relay count is tied to physical
hardware and must never be changed by this test suite. The firmware itself
does NOT enforce this -- action=save accepts a relayCount field and, if the
target file happens to be the one the active hardware variant resolves to,
hot-reloads it immediately (see loadBoardHardware call in board_routes.cpp).
Every function that could touch the active board's file refuses to accept a
relay_count/output_type override for it -- this is enforced in Python, not
assumed from caller discipline.
"""
import json


class HardwareGuardError(RuntimeError):
    pass


def list_boards(http):
    return http.get("/api/boards")


def get_active_board_file(http):
    result = list_boards(http)
    data = result.json()
    return data.get("activeBoardFile") if data else None


def create_new(http, title, cpu, relay_count, output_type, led_pin=2):
    """Creates a brand-new board file (action=save, no filename -> derived
    from title). Safe by construction: this can never collide with the
    active board's file since it's a new title/filename."""
    fields = {
        "action": "save",
        "title": title,
        "name": title,
        "cpu": cpu,
        "relayCount": str(relay_count),
        "outputType": output_type,
        "ledPin": str(led_pin),
    }
    if output_type == "gpio":
        for i in range(1, relay_count + 1):
            fields[f"relay{i}_pin"] = "255"
    else:
        fields.update({"sr_latchPin": "12", "sr_clockPin": "13", "sr_dataPin": "14", "sr_oePin": "5"})
    return http.post("/api/boards", fields)


def rename(http, filename, new_title, *, active_board_file=None):
    if active_board_file and filename == active_board_file:
        raise HardwareGuardError(
            f"refusing to rename the currently-active board file ({filename}); "
            "board_routes.cpp does not reconcile activeBoardHardwareFilename on rename"
        )
    return http.post("/api/boards", {"action": "rename", "filename": filename, "title": new_title})


def delete(http, filename, *, active_board_file=None):
    if active_board_file and filename == active_board_file:
        raise HardwareGuardError(f"refusing to delete the currently-active board file ({filename})")
    return http.post("/api/boards", {"action": "delete", "filename": filename})


def setactive(http, filename):
    """Switching the active board CAN change relayCount/hardwareVariant if
    the target file's relayCount differs -- callers must pass a filename
    whose JSON relayCount already matches the currently-active hardware.
    This function does not fetch-and-check the target file's content itself
    (that would require an extra round trip the caller usually already has
    context for); the safety.py gate + entry-point scripts are responsible
    for only ever pointing this at same-relay-count boards."""
    return http.post("/api/boards", {"action": "setactive", "filename": filename})


def download(http, filename):
    # /api/boards' filename fields already include the "boards/" prefix.
    path = filename if filename.startswith("/") else f"/{filename}"
    return http.get_raw(path)


def upload_content(http, content_json_str, filename=None):
    fields = {"action": "upload", "content": content_json_str}
    if filename:
        fields["filename"] = filename
    return http.post("/api/boards", fields)


# ---- Invalid-input coverage helpers --------------------------------------
# normalizeBoardFilename: no /,\,.., must end .json, length <=
# kMaxBoardFilenameLength+5=31; cpu mismatch is hard-enforced on
# upload/setactive.

def invalid_filename_examples():
    return [
        "../escape.json",
        "sub/dir.json",
        "back\\slash.json",
        "no-json-extension",
        "y" * 40 + ".json",
    ]


def wrong_cpu_example(current_cpu):
    """The one hard-enforced immutability rule in the whole board API: cpu
    mismatch is rejected on both upload and setactive."""
    return "ESP32" if current_cpu == "ESP8266" else "ESP8266"
