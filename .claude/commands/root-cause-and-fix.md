# Root Cause And Fix

Use this workflow when a test or runtime behavior fails.

## Inputs

- Failing endpoint/test
- Error payload and status
- Target platform/device and report path

## Workflow

1. Reproduce with minimal deterministic steps.
2. Capture evidence (logs, payloads, reports).
3. Isolate root cause (not symptoms).
4. Implement durable fix with minimal blast radius.
5. Add/update tests to prevent regression.
6. Re-run Full and Soak validation as required.
7. Update documentation.

## Output

- Root cause summary
- Code and test changes
- Validation evidence
- Remaining risks (if any)
