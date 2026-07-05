---
name: "Implement Change With Standards"
description: "Implement a requested change while enforcing project priorities: main page performance first, ESP8266/ESP32 compatibility, docs+tests updates, and root-cause-first quality."
argument-hint: "Describe the change to implement and any constraints"
agent: "agent"
---

Implement this change request:

${input:change_request:Describe the requested change in detail}

Follow these required project rules:

1. Main page performance is Priority 1.
2. Keep compatibility across both ESP8266 and ESP32.
3. Update or add tests for behavior changes.
4. Run relevant tests and ensure they pass.
5. If failures occur, perform root-cause analysis and fix the cause.
6. Update documentation for all meaningful behavior/workflow changes.

Use these project references while working:

- [Project Instructions](../copilot-instructions.md)
- [Agent Standards](../../AGENTS.md)
- [AI Collaboration Runbook](../../docs/AI_COLLABORATION_RUNBOOK.md)

Output format:

1. What changed (files and behavior)
2. Test and validation results
3. Root cause and fix details if any issue was encountered
4. Documentation updates completed
