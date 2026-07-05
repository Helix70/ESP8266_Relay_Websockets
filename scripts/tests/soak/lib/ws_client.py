"""WebSocket client for the relay board's /ws endpoint.

Handles the broadcast-vs-response race discovered during manual testing this
session: actions like a template switch trigger an unsolicited notifyClients()
broadcast to every open connection (buttons carry only id/on/d/last, no
onLabel), which can arrive interleaved with the response to an explicit "home"
query on the same connection. Always distinguish by shape (onLabel present),
never assume the first message received is the answer to the last message sent.
"""
import json
import time

import websocket


class WsClient:
    def __init__(self, host, timeout=5):
        self.host = host
        self.url = f"ws://{host}/ws"
        self.ws = None
        self.timeout = timeout

    def connect(self):
        self.ws = websocket.create_connection(self.url, timeout=self.timeout)
        self.ws.settimeout(self.timeout)
        return self

    def close(self):
        if self.ws:
            try:
                self.ws.close()
            except Exception:
                pass
            self.ws = None

    def __enter__(self):
        return self.connect()

    def __exit__(self, *exc):
        self.close()

    def get_home(self, max_attempts=6):
        """Send 'home' and return the full per-client state, skipping any
        broadcast messages that arrive first. Returns (state_dict, latency_ms)."""
        t0 = time.perf_counter()
        self.ws.send("home")
        for _ in range(max_attempts):
            msg = json.loads(self.ws.recv())
            buttons = msg.get("buttons") or []
            if buttons and "onLabel" in buttons[0]:
                latency_ms = (time.perf_counter() - t0) * 1000
                return msg, latency_ms
        raise RuntimeError(f"no full home response after {max_attempts} attempts on {self.host}")

    def toggle(self, button_id, settle_s=0.15, recv_timeout=2):
        """Toggle a relay by id. Returns (response_dict_or_None, latency_ms)."""
        t0 = time.perf_counter()
        self.ws.send(f"button{button_id}")
        time.sleep(settle_s)
        try:
            self.ws.settimeout(recv_timeout)
            msg = json.loads(self.ws.recv())
            latency_ms = (time.perf_counter() - t0) * 1000
            return msg, latency_ms
        except Exception:
            latency_ms = (time.perf_counter() - t0) * 1000
            return None, latency_ms
        finally:
            self.ws.settimeout(self.timeout)

    @staticmethod
    def on_buttons(state):
        return sorted(b["id"] for b in state.get("buttons", []) if b.get("on"))
