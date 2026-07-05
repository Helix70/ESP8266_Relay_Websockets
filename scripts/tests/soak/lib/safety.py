"""Pre-run confirmation gate.

This suite is a standing, reusable tool that will physically actuate every
relay 1..N on whatever board it's pointed at, run a serial `reset` (erasing
all stored settings including WiFi credentials), and reprovision WiFi. Since
future runs could be pointed at a board with real equipment wired to it,
require an explicit typed confirmation every time -- never skip this, even
for automation-friendly re-runs (use SOAK_SKIP_CONFIRM=1 only if you have
already confirmed once in the same terminal session and are re-running
immediately after a fix, per the interactive feedback-loop workflow).
"""
import os


def confirm_or_exit(board_name, com_port, relay_count):
    print("=" * 70)
    print(f"SOAK TEST -- {board_name} (serial port {com_port})")
    print("=" * 70)
    print("This test WILL, in order:")
    print("  1. Send a serial 'reset' -- erases ALL stored settings (WiFi,")
    print("     theme, board name, template selection) on this board.")
    print("  2. Reprovision WiFi via the serial wizard.")
    print(f"  3. Physically actuate every relay 1..{relay_count} on this board,")
    print("     in many combinations, continuously, for at least 60 seconds.")
    print("  4. Reset and reprovision again at the end.")
    print()
    print("If this board has real equipment wired to any relay (radios,")
    print("antennas, PTT lines, etc.), make sure it is safe to actuate ALL")
    print("of them before continuing.")
    print("=" * 70)

    if os.environ.get("SOAK_SKIP_CONFIRM") == "1":
        print("SOAK_SKIP_CONFIRM=1 set -- skipping interactive confirmation.")
        return

    answer = input("Type YES to proceed: ").strip()
    if answer != "YES":
        print("Not confirmed -- aborting.")
        raise SystemExit(1)
