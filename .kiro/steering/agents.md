---
inclusion: auto
---

# Agent Rules

## Git Safety

- Never push to any remote branch without explicit user permission.
- Never perform mutable git operations (force push, reset --hard, rebase, branch -D, amend pushed commits) without explicit user permission.
- Commits to the local working branch are allowed.
- Creating new local branches is allowed.

## Flash/Partition Awareness

- The ESP32 partition table defines hard limits on firmware binary size.
- The firmware binary must fit within the OTA partition size (currently 2000KB / 2MB per slot).
- Always verify binary size against partition limits before claiming a build is successful.
- The web UI (webapp/index.html) is gzipped and embedded in the firmware binary at build time. OTA updates the web UI and firmware together.

## Build Verification

- After making code changes, always verify the build succeeds before presenting the result.
- Run `make test` when changes affect testable logic (PID, NVS, JSON, events).
- Run `make lint` when modifying C/C++ source files.
