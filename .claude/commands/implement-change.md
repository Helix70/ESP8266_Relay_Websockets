# Implement Change (Project Standards)

Use this workflow to implement a requested change while preserving project standards.

## Inputs

- Change request details
- Affected areas (src/data/scripts/docs)
- Target devices/IPs for validation

## Workflow

1. Understand current behavior and constraints.
2. Implement smallest safe change set.
3. Keep main-page performance priority.
4. Preserve ESP8266/ESP32 compatibility.
5. Update/add tests for changed behavior.
6. Run full validation commands.
7. If failures occur, find and fix root cause.
8. Update documentation.

## Output

- Files changed and why
- Test/validation results
- Root-cause notes (if applicable)
- Documentation updates
