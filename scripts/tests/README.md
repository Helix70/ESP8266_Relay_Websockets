# Board Functional Test Harness

This folder contains automated smoke/functional API tests for deployed boards.

## Scripts

- `Invoke-BoardFunctionalHarness.ps1`
  - Main test harness.
  - Supports `Smoke` and `Full` modes.
  - Produces a JSON report.

- `Run-SmokeTests.ps1`
  - Convenience wrapper for ESP8266 + ESP32 targets.

- `Run-SoakTests.ps1`
  - Repeats the functional harness for stability/flakiness tracking.
  - Produces aggregate flake-rate report.

## What Is Tested

### Smoke mode
- Basic HTTP route reachability:
  - `/`
  - `/relay-config.html`
  - `/boards.html`
  - `/api/templates`
  - `/api/templates/diagnostics`
  - `/api/boards`
  - `/netinfo`
- Template CRUD workflow:
  - create
  - list contains created
  - set active
  - rename
  - delete
  - restore previously selected template (when pre-existing selected template is valid)

### Full mode
Includes Smoke mode, plus:
- Board CRUD workflow:
  - create
  - list contains created
  - rename
  - set active
  - restore previous active board
  - delete
- WebSocket check:
  - connect to `/ws`
  - send `home`
  - verify JSON state payload is received

## Usage

Run smoke tests for default board IPs:

```powershell
pwsh ./scripts/tests/Run-SmokeTests.ps1
```

Run smoke tests for custom IPs:

```powershell
pwsh ./scripts/tests/Run-SmokeTests.ps1 -Esp8266 192.168.2.154 -Esp32 192.168.2.195
```

Run full harness for custom targets:

```powershell
pwsh ./scripts/tests/Invoke-BoardFunctionalHarness.ps1 -Targets @('192.168.2.154','192.168.2.195') -Mode Full
```

Run via one-command AI wrapper:

```powershell
pwsh ./scripts/ai/Run-AI-FullValidation.ps1 -Esp8266 192.168.2.154 -Esp32 192.168.2.195 -Mode Full
```

## Report Output

The harness writes a JSON report to:

- `scripts/tests/last-functional-report.json`

The report includes:
- start/end/duration
- mode and targets
- pass/fail summary
- endpoint coverage entries (`coverage`) indicating exercised routes
- per-test results and details

Soak report output:

- `scripts/tests/soak-report.json`

Full-mode soak report output (when `-OutPath` is set):

- `scripts/tests/soak-report-full.json`

Soak usage example:

```powershell
pwsh ./scripts/tests/Run-SoakTests.ps1 -Esp8266 192.168.2.154 -Esp32 192.168.2.195 -Mode Smoke -Iterations 20 -PauseSeconds 2
```

Release-gate wrapper example:

```powershell
pwsh ./scripts/ai/Run-AI-ReleaseGate.ps1 -Esp8266 192.168.2.154 -Esp32 192.168.2.195 -UploadFirmware -UploadFilesystem
```

## Notes

- Board and template tests are stateful by design and include restore steps.
- Full mode performs board mutations; run it when it is safe to briefly switch active board during testing.
- This harness validates API/workflow correctness. Hardware-level relay electrical verification is still a manual/bench test.
