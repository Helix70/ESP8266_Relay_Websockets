#!/usr/bin/env python3
"""Long-duration soak test entry point for the 16-relay ESP8266 board
(COM6 / esp8266_ota_16relay). Same reset/reprovision/functional-coverage
flow as esp8266_16relay_soak.py, but the relay-combination phase runs for
hours instead of 60s -- a short burst can hide slow heap fragmentation that
only shows up over a genuinely long run.

Default duration: 4 hours. Override with SOAK_LONG_DURATION_S (seconds),
e.g. SOAK_LONG_DURATION_S=600 for a quick sanity check of this entry point
itself before committing to a full run.

Run directly: python scripts/tests/soak/esp8266_16relay_long_soak.py
Or via PlatformIO: pio run -e esp8266_ota_16relay -t soaktest_long
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "lib"))

from soak_runner import run_soak, long_duration_s  # noqa: E402

if __name__ == "__main__":
    ok = run_soak(
        board_name="esp8266_16relay",
        com_port="COM6",
        relay_count=16,
        cpu="ESP8266",
        board_hardware_file="boards/esp8266-16relay.json",
        duration_s=long_duration_s(),
    )
    sys.exit(0 if ok else 1)
