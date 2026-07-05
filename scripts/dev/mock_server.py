"""Mock relay-board server for local web UI development.

Serves data/ statically (edits show on refresh) and fakes /api/theme with
the same style validation as the firmware. No /ws — to populate the main
page's relay grid, run this in the browser console:

    maxRelays = 8; renderRelayTable(8);
    document.getElementById('relay-table').removeAttribute('data-loading');

Usage: python scripts/dev/mock_server.py [port]   (default 8321)
"""
import json
import os
import re
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

DATA_DIR = os.path.join(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
    "data",
)
STATE = {
    "h": "#F8F7F9,#143642,#0f8b8d,#cf2700,#143642,#0f8b8d,#ffffff,#ffffff,#ffffff",
    "s": "classic",
}

CONTENT_TYPES = {
    ".html": "text/html",
    ".css": "text/css",
    ".js": "application/javascript",
    ".json": "application/json",
}


class Handler(BaseHTTPRequestHandler):
    def _send(self, code, body, ctype="application/json"):
        payload = body if isinstance(body, bytes) else body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/api/theme":
            return self._send(200, json.dumps(STATE))
        if path == "/":
            path = "/index.html"
        fpath = os.path.join(DATA_DIR, path.lstrip("/").replace("/", os.sep))
        if os.path.isfile(fpath):
            ext = os.path.splitext(fpath)[1]
            with open(fpath, "rb") as f:
                return self._send(200, f.read(), CONTENT_TYPES.get(ext, "application/octet-stream"))
        return self._send(404, '{"error":"not found"}')

    def do_POST(self):
        path = self.path.split("?")[0]
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length).decode() if length else ""
        if path == "/api/theme":
            from urllib.parse import parse_qs
            form = parse_qs(body)
            # Matches firmware: any lowercase token up to 11 chars (themeStyle[12]
            # in app_state.cpp), not a fixed list — new styles are a pure
            # data/theme.js + data/style.css change, no firmware validation to update.
            if "s" in form:
                style = form["s"][0]
                if not (0 < len(style) <= 11 and re.fullmatch(r"[a-z]+", style)):
                    return self._send(400, '{"ok":false,"error":"invalid style"}')
            if "h" in form:
                STATE["h"] = form["h"][0]
            if "s" in form:
                STATE["s"] = form["s"][0]
            return self._send(200, '{"ok":true}')
        return self._send(200, '{"ok":true}')

    def log_message(self, fmt, *args):
        pass


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8321
    print("mock server on http://127.0.0.1:%d" % port, flush=True)
    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    server.daemon_threads = True
    while True:
        try:
            server.serve_forever()
        except Exception as exc:  # noqa: BLE001 - keep the mock alive
            print("serve error: %r" % exc, flush=True)
