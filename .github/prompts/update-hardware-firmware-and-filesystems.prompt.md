---
name: "Update Hardware Firmware And Filesystems"
description: "Build and update ESP8266/ESP32 devices with firmware and filesystem artifacts, then verify health endpoints."
argument-hint: "Provide ESP8266 IP, ESP32 IP, and whether to skip build"
agent: "agent"
---

Perform a hardware update workflow using this input:

${input:update_input:ESP8266 IP, ESP32 IP, and optional flags such as SkipBuild}

Required workflow:

1. Build both environments unless SkipBuild is requested.
2. Upload firmware to both devices.
3. Upload filesystem to both devices.
4. Verify core health endpoints on both targets:
   - /
   - /api/templates
   - /api/boards
   - /netinfo
5. Report update status and any failures.

Use project guidance:

- [Project Instructions](../copilot-instructions.md)
- [Agent Standards](../../AGENTS.md)
- [AI Collaboration Runbook](../../docs/AI_COLLABORATION_RUNBOOK.md)
- [AI Workflow Scripts](../../scripts/ai/README.md)

Preferred command path:

1. Use release wrapper when possible:
   - pwsh ./scripts/ai/Run-AI-ReleaseGate.ps1 -Esp8266 <ESP8266_IP> -Esp32 <ESP32_IP> -UploadFirmware -UploadFilesystem
2. If only update steps are requested, execute build/upload commands directly and skip validation suites.

Output format:

1. Build summary
2. Firmware upload summary
3. Filesystem upload summary
4. Endpoint health check summary
