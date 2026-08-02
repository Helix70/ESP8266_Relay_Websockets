"""Local dev server that runs the REAL data/template-editor.html + .js the
firmware serves (unmodified), backed by genuine template read/write logic
instead of the mock server's faked "always ok" responses. Reuses
edit_template.py so validation/clamping stays in exactly one place and always
matches src/template_routes.cpp.

What's faked, and why it's fine for editing templates:
  - /ws only ever answers with one static "buttons" bootstrap message (enough
    for the page to render the label-editing grid). No real relay hardware,
    so on/off states are always false and gpio is always -1.
  - There's no concept of a live board's selected template; the dropdown's
    "(Active)" tag just never applies.
Real, not faked:
  - GET /api/templates lists actual files in data/templates/, filtered by
    relay count exactly like writeTemplateListEntriesJson does.
  - GET /templates/<file>.json serves the real file (plain static read).
  - POST /api/templates (the bare save the page's Save button uses) applies
    the same title/label/mode/group/pulse clamping as writeTemplateJsonFromRequest
    and actually writes data/templates/<slug>.json.

Usage: python scripts/dev/template_editor_server.py [--relays 8|16] [--port 8322]
"""
import argparse
import base64
import hashlib
import json
import os
import re
import struct
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import edit_template as et  # noqa: E402

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DATA_DIR = os.path.join(REPO_ROOT, "data")
TEMPLATES_DIR = os.path.join(DATA_DIR, "templates")

CONTENT_TYPES = {
    ".html": "text/html",
    ".css": "text/css",
    ".js": "application/javascript",
    ".json": "application/json",
}

WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

THEME_STATE = {
    "h": "#F8F7F9,#143642,#0f8b8d,#cf2700,#143642,#0f8b8d,#ffffff,#ffffff,#ffffff",
    "s": "classic",
}

# Set from --relays; used both for the fake /ws bootstrap and to filter
# /api/templates the same way the firmware filters by its board's relayCount.
RELAY_COUNT = 16


def build_bootstrap_message():
    buttons = []
    for i in range(RELAY_COUNT):
        buttons.append({
            "id": i + 1,
            "on": False,
            "d": False,
            "last": 0,
            "onLabel": "",
            "offLabel": "",
            "m": 0,
            "g": 0,
            "p": 0,
            "gpio": -1,
        })
    return json.dumps({
        "buttons": buttons,
        "boardName": f"Local Dev ({RELAY_COUNT}-relay)",
        "selectedRelayTemplate": "",
        "bootSessionId": "local-dev",
    })


def read_ws_frame(rfile):
    b1 = rfile.read(1)
    if not b1:
        return None
    b1 = b1[0]
    opcode = b1 & 0x0F
    b2 = rfile.read(1)
    if not b2:
        return None
    b2 = b2[0]
    masked = b2 & 0x80
    length = b2 & 0x7F
    if length == 126:
        length = struct.unpack('>H', rfile.read(2))[0]
    elif length == 127:
        length = struct.unpack('>Q', rfile.read(8))[0]
    mask_key = rfile.read(4) if masked else b''
    payload = rfile.read(length)
    if masked:
        payload = bytes(b ^ mask_key[i % 4] for i, b in enumerate(payload))
    return opcode, payload


def send_ws_text(wfile, text):
    payload = text.encode('utf-8')
    header = bytearray([0x81])
    length = len(payload)
    if length <= 125:
        header.append(length)
    elif length <= 0xFFFF:
        header.append(126)
        header += struct.pack('>H', length)
    else:
        header.append(127)
        header += struct.pack('>Q', length)
    wfile.write(bytes(header) + payload)
    wfile.flush()


class Handler(BaseHTTPRequestHandler):
    def _send_json(self, code, obj):
        payload = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(payload)

    def _serve_static(self, path, body_transform=None):
        fpath = os.path.join(DATA_DIR, path.lstrip("/").replace("/", os.sep))
        if not os.path.isfile(fpath):
            return self._send_json(404, {"error": "not found"})
        ext = os.path.splitext(fpath)[1]
        with open(fpath, "rb") as f:
            body = f.read()
        if body_transform is not None:
            body = body_transform(body)
        self.send_response(200)
        self.send_header("Content-Type", CONTENT_TYPES.get(ext, "application/octet-stream"))
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _serve_template_editor_html(self):
        global RELAY_COUNT
        query = parse_qs(self.path.split("?", 1)[1]) if "?" in self.path else {}
        relays_param = query.get("relays", [None])[0]
        if relays_param in ("8", "16"):
            RELAY_COUNT = int(relays_param)

        def inject_switcher(body: bytes) -> bytes:
            html = body.decode("utf-8")
            switcher = (
                '<p style="margin:0 0 0.75rem;font-size:0.85rem">'
                'Editing: '
                f'<a href="/template-editor.html?relays=8"{" style=\"font-weight:bold\"" if RELAY_COUNT == 8 else ""}>8-relay</a>'
                ' | '
                f'<a href="/template-editor.html?relays=16"{" style=\"font-weight:bold\"" if RELAY_COUNT == 16 else ""}>16-relay</a>'
                ' templates</p>'
            )
            return html.replace('<div class="topnav">', '<div class="topnav">' + switcher, 1).encode("utf-8")

        return self._serve_static("/template-editor.html", body_transform=inject_switcher)

    def _serve_template_editor_js(self):
        def fix_gateway_port(body: bytes) -> bytes:
            # The shipped file uses window.location.hostname, which drops the
            # port — correct for the real device (always port 80) but wrong
            # for this dev server. window.location.host includes the port
            # when present and is identical to hostname when it's not, so
            # this is a no-op on real hardware and only changes behavior here.
            return body.replace(b"window.location.hostname", b"window.location.host")

        return self._serve_static("/template-editor.js", body_transform=fix_gateway_port)

    def _handle_ws_upgrade(self):
        key = self.headers.get("Sec-WebSocket-Key", "")
        accept = base64.b64encode(hashlib.sha1((key + WS_GUID).encode()).digest()).decode()
        self.send_response(101, "Switching Protocols")
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", accept)
        self.end_headers()
        self.wfile.flush()

        while True:
            frame = read_ws_frame(self.rfile)
            if frame is None:
                break
            opcode, _payload = frame
            if opcode == 0x8:  # close
                break
            if opcode in (0x1, 0x2):  # text / binary
                send_ws_text(self.wfile, build_bootstrap_message())
        self.close_connection = True

    def do_GET(self):
        path = self.path.split("?")[0]

        if path == "/ws":
            return self._handle_ws_upgrade()

        if path == "/api/theme":
            return self._send_json(200, THEME_STATE)

        if path == "/api/templates":
            entries = []
            if os.path.isdir(TEMPLATES_DIR):
                for fname in sorted(os.listdir(TEMPLATES_DIR)):
                    if not fname.endswith(".json"):
                        continue
                    try:
                        with open(os.path.join(TEMPLATES_DIR, fname), "r", encoding="utf-8") as f:
                            doc = json.load(f)
                        n = doc.get("n", len(doc.get("l", [])))
                        if n != RELAY_COUNT:
                            continue
                        entries.append({"filename": fname, "t": doc.get("t", fname), "n": n})
                    except Exception:  # noqa: BLE001 - skip unreadable/corrupt files, like the firmware's openFailures
                        continue
            return self._send_json(200, {"selectedTemplate": "", "templates": entries})

        if path in ("/", "/index.html", "/template-editor.html"):
            return self._serve_template_editor_html()

        if path == "/template-editor.js":
            return self._serve_template_editor_js()

        return self._serve_static(path)

    def do_POST(self):
        path = self.path.split("?")[0]
        length = int(self.headers.get("Content-Length", 0))
        raw_body = self.rfile.read(length).decode() if length else ""

        if path == "/api/theme":
            form = parse_qs(raw_body)
            if "s" in form:
                style = form["s"][0]
                if not (0 < len(style) <= 11 and re.fullmatch(r"[a-z]+", style)):
                    return self._send_json(400, {"ok": False, "error": "invalid style"})
                THEME_STATE["s"] = style
            if "h" in form:
                THEME_STATE["h"] = form["h"][0]
            return self._send_json(200, {"ok": True})

        if path == "/api/templates":
            form = parse_qs(raw_body)

            def field(name, default=""):
                return form.get(name, [default])[0]

            title = field("title").strip()
            if not title:
                return self._send_json(400, {"ok": False, "error": "title required"})

            warnings = []
            title = et.clamp_title(title, warnings)

            rc = RELAY_COUNT
            doc = {"t": title, "n": rc, "l": [{} for _ in range(rc)]}
            for i in range(1, rc + 1):
                prefix = f"relay{i}_"
                try:
                    et.clamp_label(doc, i - 1, "o", field(prefix + "on"), warnings)
                    et.clamp_label(doc, i - 1, "f", field(prefix + "off"), warnings)
                    et.clamp_label(doc, i - 1, "m", field(prefix + "mode", "L"), warnings)
                    et.clamp_label(doc, i - 1, "g", field(prefix + "group", "0"), warnings)
                    et.clamp_label(doc, i - 1, "p", field(prefix + "pulse", "0"), warnings)
                except ValueError as exc:
                    return self._send_json(400, {"ok": False, "error": str(exc)})

            et.apply_pulse_mode_rule(doc, warnings)

            if not os.path.isdir(TEMPLATES_DIR):
                os.makedirs(TEMPLATES_DIR)

            filename = et.sanitize_template_slug(title) + ".json"
            fpath = os.path.join(TEMPLATES_DIR, filename)
            with open(fpath, "w", encoding="utf-8", newline="\n") as f:
                f.write(et.serialize_template(doc))

            for w in warnings:
                print(f"warning: {w}", file=sys.stderr)

            return self._send_json(200, {"ok": True, "filename": filename})

        return self._send_json(200, {"ok": True})

    def log_message(self, fmt, *args):
        pass


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--relays", type=int, choices=(8, 16), default=16,
                         help="relay count for the fake board the /ws bootstrap and template list use (default 16)")
    parser.add_argument("--port", type=int, default=8322)
    args = parser.parse_args()

    RELAY_COUNT = args.relays

    print(f"template editor on http://127.0.0.1:{args.port}/template-editor.html "
          f"({RELAY_COUNT}-relay board)", flush=True)
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    server.daemon_threads = True
    server.serve_forever()
