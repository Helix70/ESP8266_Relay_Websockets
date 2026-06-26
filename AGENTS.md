# Agent Standards

This file defines expected standards for AI agents and contributors working in this repository.

## Core Expectations

1. Main page performance is Priority 1.
2. Preserve ESP8266 and ESP32 compatibility for all changed behavior.
3. Keep reliability-first behavior for filesystem and persistence paths.
4. Prefer root-cause fixes over retries-only workarounds.

## Engineering Standards

1. Keep changes minimal and focused; avoid unrelated refactors.
2. Maintain existing API semantics unless an intentional contract change is requested.
3. For storage writes, favor atomic temp-write + rename patterns and contention-safe behavior.
4. Avoid memory-heavy patterns on ESP8266; cap temporary allocations and scopes.

## Testing Standards

1. Update tests whenever behavior changes.
2. Add new tests for new routes, state transitions, or persistence behaviors.
3. Require passing functional validation before considering work complete.

Required checks:

```powershell
pwsh ./scripts/tests/Run-SmokeTests.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -Mode Full
pwsh ./scripts/tests/Run-SoakTests.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -Mode Full -Iterations 5 -PauseSeconds 1
```

## Documentation Standards

1. Update docs whenever behavior, test harness expectations, device update process, or endpoints change.
2. Keep README and test docs aligned with current commands and expected outputs.
3. Include rationale for non-obvious platform-specific changes.

## Device Interaction Standards

1. Build both platforms when shared code changes.
2. Flash impacted devices before final validation.
3. Upload filesystem when web assets or static JSON files change.
4. Verify target IPs and capture report artifacts for traceability.
