---
name: "Release Readiness Check"
description: "Run release-readiness validation: build both platforms, update devices as needed, run full tests + soak, and produce a ship/no-ship summary."
argument-hint: "Provide ESP8266 IP, ESP32 IP, and optional soak iterations"
agent: "agent"
---

Perform a release-readiness check with this input:

${input:release_input:ESP8266 IP, ESP32 IP, changed areas, optional soak iterations}

Required checks:

1. Build both platforms (`esp8266_serial`, `esp32_serial`).
2. Update impacted devices (firmware and filesystem if required by changed files).
3. Run full functional tests on both targets.
4. Run soak tests for stability.
5. If any failure occurs, find root cause and fix it, then re-run validation.
6. Confirm docs/tests are updated for shipped changes.

Priority and standards:

1. Main page performance remains Priority 1.
2. Cross-platform compatibility is mandatory.

Use project references:

- [Project Instructions](../copilot-instructions.md)
- [Agent Standards](../../AGENTS.md)
- [AI Collaboration Runbook](../../docs/AI_COLLABORATION_RUNBOOK.md)
- [Test Harness Guide](../../scripts/tests/README.md)

Output format:

1. Build/deploy summary
2. Full test and soak results (with report paths)
3. Issues found and root-cause outcomes
4. Final recommendation: ship / hold
