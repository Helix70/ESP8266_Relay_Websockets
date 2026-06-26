# Claude Usage Guide

This folder provides Claude-oriented workflow templates aligned with project standards.

## Files

1. `../CLAUDE.md`
   - Global Claude project instructions.
   - Covers priorities, compatibility, testing, documentation, and root-cause expectations.

2. `commands/implement-change.md`
   - Standard implementation workflow.
   - Use for normal feature or fix requests.

3. `commands/root-cause-and-fix.md`
   - Failure investigation workflow.
   - Use when tests or runtime behavior fail.

4. `commands/release-readiness.md`
   - Build, device update, full validation, and soak checklist.
   - Use before release decisions.

## Recommended Session Flow

1. Start by applying instructions from `../CLAUDE.md`.
2. Pick one command template matching the task type.
3. Execute build and validation commands from the chosen template.
4. If any failure occurs, switch to `commands/root-cause-and-fix.md`.
5. Update docs and tests before considering the task complete.

## Project Priorities Recap

1. Main page performance is Priority 1.
2. Maintain ESP8266 and ESP32 compatibility.
3. Update tests and docs for any meaningful change.
4. Ensure tests pass.
5. Find and fix root cause for all issues.
