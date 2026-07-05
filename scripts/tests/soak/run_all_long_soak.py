#!/usr/bin/env python3
"""Runs all three boards' LONG-DURATION soak tests back-to-back (sequentially
-- see run_all_soak.py for why). Default 4 hours per board (12 hours total);
override with SOAK_LONG_DURATION_S (seconds).

    python scripts/tests/soak/run_all_long_soak.py

Exits 1 if any board's run failed, 0 if all passed.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "lib"))

from soak_runner import run_soak, long_duration_s  # noqa: E402

BOARDS = [
    dict(board_name="esp8266_16relay", com_port="COM6", relay_count=16, cpu="ESP8266",
         board_hardware_file="boards/esp8266-16relay.json"),
    dict(board_name="esp8266_8relay", com_port="COM4", relay_count=8, cpu="ESP8266",
         board_hardware_file="boards/esp8266-8relay.json"),
    dict(board_name="esp32_8relay", com_port="COM3", relay_count=8, cpu="ESP32",
         board_hardware_file="boards/esp32-8relay.json"),
]

if __name__ == "__main__":
    duration = long_duration_s()
    results = {}
    for board in BOARDS:
        print("\n" + "#" * 70)
        print(f"# Starting long soak test: {board['board_name']} ({duration}s)")
        print("#" * 70)
        try:
            results[board["board_name"]] = run_soak(duration_s=duration, **board)
        except SystemExit:
            raise
        except Exception as e:
            print(f"[{board['board_name']}] FAILED WITH EXCEPTION: {e}")
            results[board["board_name"]] = False

    print("\n" + "=" * 70)
    print("COMBINED SUMMARY")
    print("=" * 70)
    all_ok = True
    for name, ok in results.items():
        print(f"  {'PASS' if ok else 'FAIL'}  {name}")
        all_ok = all_ok and ok

    sys.exit(0 if all_ok else 1)
