# Web Interface Spec

## Overview

Add a lightweight HTTP-based web interface to replace the AWS IoT Thing Shadow configuration mechanism. The interface provides real-time status monitoring, configuration management, OTA firmware updates, and optional authentication for the coffee machine, accessible from any device on the local network.

## Requirements

### R1: HTTP Server
- Use ESP-IDF's built-in `esp_http_server` component (already available, zero additional flash cost)
- Serve on port 80 (default), configurable via NVS
- Maximum 5 concurrent connections
- Only active when WiFi is connected (NOT in AP provisioning mode)
- Hardware revision 2 only

### R2: Web UI
- Single Page Application (SPA) with dark theme, responsive (mobile + desktop)
- Pre-built, gzipped, served from the `data` FAT partition (7.1MB available)
- Sections: Status (home), Config, System (OTA + device info), Authentication
- Technology: vanilla HTML/CSS/JS or lightweight framework (Preact/Alpine.js ~3KB)
- Total bundle size target: < 100KB gzipped

### R3: Status Page (home)
- Replicate information shown on the LCD display:
  - Boiler temperature (current + setpoint + duty %)
  - Brew head temperature (current + setpoint)
  - Boiler level status (OK / refilling / error)
  - Power state (active / standby)
  - WiFi status + RSSI
  - System uptime, boot count
- Auto-refresh via polling (every 1-2 seconds when page is visible)

### R4: Configuration Page
- Subsections matching the former AWS named shadows:
  - **Boiler Temp** (`boiler_temp`): PID params (P, I, D, setpoints, over_setpoint_perc, mains_hz, temp_error_restart_time_sec)
  - **Brew Temp** (`brew_temp`): PID params, enabled, max_damping_perc, boiler_setpoint_hold_sec
  - **Boiler Refill** (`boiler_refill`): thresholds, hysteresis, timing params
  - **Schedules** (`schedules`): enabled, daily on/off times per day of week
- Each subsection shows current values and allows editing
- Save button persists to NVS immediately
- Reset-to-defaults button per section
- All NVS keys remain identical (backward compatible)

### R5: System Page
- **Device Info** (read-only):
  - Firmware version, hardware revision, thing type, thing ID
  - IDF version, build date
  - Boot count, crash count, last crash reason
  - Free heap, minimum free heap
- **OTA Firmware Update**:
  - File upload form for `.bin` firmware file
  - Pre-flash validation (see R9)
  - Progress indicator during upload/flash
  - Automatic reboot after successful flash
  - Display result (success / error with reason)
- **OTA Web UI Update**:
  - Separate upload for web UI assets (tar.gz or zip of gzipped files)
  - Writes to `data` FAT partition, replacing existing UI files
  - No reboot required (takes effect on next page load)
- **System Actions**:
  - Reboot button
  - Factory reset button (erases NVS)

### R6: Authentication
- Disabled by default (open access)
- User can set a password via the Authentication page
- Once set, HTTP Basic Auth is required for all API endpoints (static files remain open)
- Password stored as SHA256 hash in NVS (key: `http_auth_hash`)
- Auth enabled flag in NVS (key: `http_auth_enabled`)
- Clear password via physical reset button (existing factory reset mechanism)

### R7: REST API Design
- JSON request/response bodies (reuse existing `from_json`/`to_json` methods)
- Endpoints:
  ```
  GET  /api/status              — machine status (temperatures, state, wifi)
  GET  /api/config/:name        — get config section (boiler_temp, brew_temp, etc.)
  PUT  /api/config/:name        — update config section (partial JSON merge)
  POST /api/config/:name/reset  — reset section to defaults

  GET  /api/system/info         — device info, versions, metrics
  POST /api/system/ota          — upload firmware binary (multipart/form-data)
  POST /api/system/ota-ui       — upload web UI assets (tar.gz, written to data partition)
  POST /api/system/reboot       — trigger reboot
  POST /api/system/factory-reset — erase NVS and reboot

  GET  /api/auth                — auth status (enabled/disabled)
  POST /api/auth                — set/change password
  DELETE /api/auth              — disable auth (requires current password)
  ```
- All JSON responses < 1KB
- OTA upload is streamed (chunked receive, not buffered in RAM)

### R8: Flash/Memory Budget
- Firmware binary: must stay within 2MB app partition (currently 1.6MB, ~400KB headroom)
- HTTP server: ~20KB additional code
- OTA handler: ~5KB additional code
- Web UI assets: stored in `data` FAT partition (7.1MB available), not in firmware binary
- RAM: `esp_http_server` uses ~4KB per connection × 5 = ~20KB max
- OTA write buffer: 4KB (streamed, not full-image buffered)
- Total additional firmware cost: ~30-35KB

### R9: OTA Validation Guards
Before writing a received firmware binary to flash, validate:

1. **Magic byte check**: first 4 bytes must be ESP32 image magic (`0xE9`)
2. **Project name match**: `esp_app_desc_t.project_name` must equal `"coffee-drivah-firmware"` (prevents flashing wrong firmware)
3. **Version check**: incoming version must differ from running version (prevents re-flashing same build)
4. **Size check**: binary must fit in the OTA partition (≤ 2000KB, as defined by `partitions.csv`)

After flashing and rebooting:

5. **Rollback safety**: `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` is already configured. The new firmware must call `esp_ota_mark_app_valid_and_cancel_rollback()` after successful boot. If the new firmware crashes before this call, the bootloader automatically rolls back to the previous version on next reboot.
6. **Self-test**: the app marks itself valid only after WiFi connects successfully and the HTTP server starts (proving the critical path works)

No code signing required — validation is structural (correct binary format + correct project) not cryptographic.

### R10: Hardware Scope
- All web interface functionality is for hardware revision 2 only
- Guarded by `HARDWARE_REVISION_MAJOR == 2` at compile time

## Design Decisions

### Static File Serving
- Web UI built externally (Node.js build step or hand-written), output gzipped
- Files written to `data` FAT partition at flash time (via `fatfs_create_spiflash_image`)
- ESP-IDF `httpd_resp_set_hdr("Content-Encoding", "gzip")` for transparent decompression by browser
- `index.html` + `app.js` + `style.css` (all gzipped individually) — 3 files max
- FAT chosen over SPIFFS because: faster reads, better for serving static content, already allocated

### JSON for Config (keep existing approach)
- All structs already implement `from_json()`/`to_json()`
- cJSON already linked (~15KB in flash)
- Web frontend handles JSON natively
- NVS keys unchanged — any config written by old MQTT mechanism still loads correctly
- Payload sizes are tiny (~200-500 bytes per config section)

### No WebSocket
- Polling-based status updates (1-2 second interval)
- WebSocket adds ~15KB code and per-connection state; not justified for 1Hz updates
- Simple `GET /api/status` is adequate and works through proxies/firewalls

### OTA via HTTP Upload (not pull-based)
- User uploads `.bin` directly from browser (simple, no external server needed)
- Streamed write: receive chunks → write to OTA partition (4KB buffer, no full-image RAM allocation)
- `esp_app_desc_t` at offset 0x20 in the binary provides validation metadata
- ESP-IDF's `esp_ota_begin/write/end` + `esp_ota_set_boot_partition` handles the low-level flash ops
- Rollback handled entirely by bootloader — firmware just needs to "self-test" and call mark-valid

### Rollback Self-Test Strategy
On every boot, the firmware:
1. Checks if it's running from a new OTA partition (`esp_ota_check_rollback_is_possible()`)
2. If yes, starts a self-test timer (30 seconds)
3. If WiFi connects AND HTTP server starts within the timer → call `esp_ota_mark_app_valid_and_cancel_rollback()`
4. If timer expires without validation → do NOT mark valid; on next crash/reboot, bootloader rolls back

## Implementation Plan

### Phase 1: Backend (HTTP server + REST API)
1. Add `esp_http_server` and FAT filesystem mount
2. Static file serving from `data` partition (gzipped)
3. `/api/status` endpoint
4. `/api/config/:name` GET/PUT/reset endpoints (wire to existing from_json/to_json)
5. `/api/system/info` endpoint
6. `/api/auth` endpoints + NVS auth storage
7. HTTP Basic Auth middleware

### Phase 2: OTA
1. `/api/system/ota` endpoint with chunked upload
2. Pre-flash validation (magic, project name, version, size)
3. Stream-to-flash implementation
4. Post-flash reboot
5. Rollback self-test on boot

### Phase 3: Frontend (Web UI)
1. Status page with auto-refresh
2. Config page with edit/save/reset per section
3. System page (device info + OTA upload + reboot/reset)
4. Auth page
5. Dark theme, responsive CSS
6. Gzip and deploy to `data` partition

### Phase 4: Integration & Build
1. Add web UI build step to Makefile (npm/node in Docker for the frontend)
2. `fatfs_create_spiflash_image` to package UI into flash image
3. Wire HTTP server into `controller_init()` for r2
4. Rollback self-test logic in `app_main()`
5. Update README

## NVS Keys (new)

| Key | Namespace | Type | Description |
|-----|-----------|------|-------------|
| `http_auth_enabled` | `sys` | u8 | 0=disabled, 1=enabled |
| `http_auth_hash` | `sys` | str | SHA256 hex of password |
| `http_port` | `sys` | u16 | HTTP server port (default 80) |

## Open Questions
- Maximum firmware file size to accept is determined by the OTA partition size (2000KB). The build must verify the binary fits.
- Future consideration: should the `factory` partition also be updatable, or remain as a known-good fallback?
