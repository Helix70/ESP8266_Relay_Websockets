---
name: "Root Cause And Fix"
description: "Investigate a failing test/behavior, reproduce reliably, find root cause, implement a durable fix, and verify with full regression + soak when needed."
argument-hint: "Paste failure details, endpoint, test name, board, and report path"
agent: "agent"
---

Investigate and fix this failure:

${input:failure_details:Include failing target, endpoint/test, error payload, and report file path}

Required process:

1. Reproduce with the smallest deterministic scenario.
2. Capture exact failure evidence (status/body/logs and platform scope).
3. Isolate root cause (not symptoms).
4. Implement the smallest robust fix.
5. Update/add tests that would catch this in future.
6. Run full validation and report outcomes.
7. Update documentation for behavior/workflow changes.

Validation expectations:

- Full functional pass on both targets.
- Soak validation for stability-sensitive failures.

Use project guidance:

- [Project Instructions](../copilot-instructions.md)
- [Agent Standards](../../AGENTS.md)
- [AI Collaboration Runbook](../../docs/AI_COLLABORATION_RUNBOOK.md)

Output format:

1. Root cause summary
2. Code/test/doc changes
3. Validation evidence
4. Remaining risks (if any)
