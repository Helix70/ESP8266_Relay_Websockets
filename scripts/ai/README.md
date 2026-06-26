# AI Workflow Scripts

These wrappers provide one-command validation and release gating across AI engines.

## 1) Full Validation

Run functional validation quickly:

```powershell
pwsh ./scripts/ai/Run-AI-FullValidation.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -Mode Full
```

Notes:

1. `-Mode` accepts `Smoke` or `Full`.
2. Reports are written by the underlying harness to `scripts/tests/last-functional-report.json`.

## 2) Release Gate

Run build, optional device updates, full validation, and soak validation:

```powershell
pwsh ./scripts/ai/Run-AI-ReleaseGate.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -UploadFirmware -UploadFilesystem
```

Optional flags:

1. `-SkipBuild` to skip build steps.
2. `-SoakIterations <n>` to override soak iteration count.
3. `-PauseSeconds <n>` to control pause between soak iterations.

Output reports:

1. `scripts/tests/last-functional-report.json`
2. `scripts/tests/soak-report-full.json`
