#!/usr/bin/env python3
"""Soak test entry point for the 8-relay ESP8266 board (COM4 / esp8266_ota_8relay).

Run directly: python scripts/tests/soak/esp8266_8relay_soak.py
Or via PlatformIO: pio run -e esp8266_ota_8relay -t soaktest
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "lib"))

from soak_runner import run_soak  # noqa: E402

if __name__ == "__main__":
    ok = run_soak(
        board_name="esp8266_8relay",
        com_port="COM4",
        relay_count=8,
        cpu="ESP8266",
        board_hardware_file="boards/esp8266-8relay.json",
        duration_s=60,
    )
    sys.exit(0 if ok else 1)
