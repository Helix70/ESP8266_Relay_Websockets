#!/usr/bin/env python3
"""Soak test entry point for the ESP32 8-relay board (COM3 / esp32_ota_8relay).

Run directly: python scripts/tests/soak/esp32_8relay_soak.py
Or via PlatformIO: pio run -e esp32_ota_8relay -t soaktest
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "lib"))

from soak_runner import run_soak  # noqa: E402

if __name__ == "__main__":
    ok = run_soak(
        board_name="esp32_8relay",
        com_port="COM3",
        relay_count=8,
        cpu="ESP32",
        board_hardware_file="boards/esp32-8relay.json",
        duration_s=60,
    )
    sys.exit(0 if ok else 1)
