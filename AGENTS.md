# Agent Rules

Rules for any AI agent working on this codebase.

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

## Code Style

- Follow the project's `.clang-format` and `.clang-tidy` configuration.
- Use ESP-IDF conventions for component structure, naming, and error handling.
- All new C/C++ code must pass `make lint` and `make format-check` before committing.

## Architecture

- Build runs inside Docker (`make build`). Flash and monitor run on the host.
- The web interface backend is in `components/rebel-espresso/src/hw/r2/src/webserver/`.
- Config structs use `from_json()`/`to_json()` for serialization — maintain this pattern.
- All NVS keys must remain stable for backward compatibility with existing devices.
- Web server runs on port 8080 (HomeKit uses port 80).
- Hardware revision 2 is the active target (`HW_REVISION=2`).
