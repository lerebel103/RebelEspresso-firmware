# Web Interface Spec

## Overview

Add a lightweight HTTP-based web interface to replace the AWS IoT Thing Shadow configuration mechanism. The interface provides real-time status monitoring and configuration management for the coffee machine, accessible from any device on the local network.

## Requirements

### R1: HTTP Server
- Use ESP-IDF's built-in `esp_http_server` component (already available, zero additional flash cost)
- Serve on port 80 (default), configurable via NVS
- Maximum 5 concurrent connections
- Only active when WiFi is connected

### R2: Web UI
- Single Page Application (SPA) with dark theme, responsive (mobile + desktop)
- Pre-built, gzipped, served from the `data` FAT partition (7.1MB available)
- Three sections: Status (home), Config, Authentication
- Technology: vanilla HTML/CSS/JS or lightweight framework (Preact/Alpine.js ~3KB)
- Total bundle size target: < 100KB gzipped

### R3: Status Page (home)
- Replicate information shown on the LCD display:
  - Boiler temperature (current + setpoint)
  - Brew head temperature
  - Boiler level status (OK / refilling / error)
  - Power state (active / standby)
  - WiFi status + RSSI
  - System uptime, boot count
- Auto-refresh via polling (every 1-2 seconds when page is visible)

### R4: Configuration Page
- Subsections matching the former AWS named shadows:
  - **Boiler Temp** (`boiler_temp`): PID params (P, I, D, setpoints, mains_hz, etc.)
  - **Brew Temp** (`brew_temp`): PID params, damping, setpoint hold
  - **Boiler Refill** (`boiler_refill`): thresholds, hysteresis, timing
  - **Schedules** (`schedules`): daily on/off times per day of week
  - **Device Info** (read-only): firmware version, hardware rev, thing ID
- Each subsection shows current values and allows editing
- Save button persists to NVS immediately
- Reset-to-defaults button per section
- All NVS keys remain identical (backward compatible)

### R5: Authentication
- Disabled by default (open access)
- User can set a password via the Authentication page
- Once set, HTTP Basic Auth is required for all endpoints
- Password stored as bcrypt/SHA256 hash in NVS (key: `http_auth_hash`)
- Auth enabled flag in NVS (key: `http_auth_enabled`)
- Clear password via physical reset button (existing factory reset mechanism)

### R6: REST API Design
- JSON request/response bodies (reuse existing `from_json`/`to_json` methods)
- Endpoints:
  ```
  GET  /api/status          — machine status (temperatures, state, wifi)
  GET  /api/config/:name    — get config section (boiler_temp, brew_temp, etc.)
  PUT  /api/config/:name    — update config section (partial JSON merge)
  POST /api/config/:name/reset — reset section to defaults
  GET  /api/auth            — auth status (enabled/disabled)
  POST /api/auth            — set/change password
  DELETE /api/auth          — disable auth (requires current password)
  ```
- All responses < 1KB (fits in single TCP segment)

### R7: Flash/Memory Budget
- Firmware binary: must stay within 2MB app partition (currently 1.6MB, ~400KB headroom)
- HTTP server: ~20KB additional code
- Web UI assets: stored in `data` FAT partition (7.1MB available), not in firmware binary
- RAM: `esp_http_server` uses ~4KB per connection × 5 = ~20KB max
- Total additional firmware cost: ~25-30KB

### R8: Hardware Scope
- Hardware revision 2 only (guarded by `HARDWARE_REVISION_MAJOR == 2`)

## Design Decisions

### Static File Serving
- Web UI built externally (Node.js build step in Docker), output gzipped
- Files written to `data` FAT partition at flash time
- ESP-IDF `httpd_resp_set_hdr("Content-Encoding", "gzip")` for transparent decompression
- `index.html` + `app.js` + `style.css` (all gzipped) — 3 files total

### JSON for Config (keep existing approach)
- Pro: all structs already implement `from_json()`/`to_json()`
- Pro: cJSON already linked (~15KB), web frontend handles JSON natively
- Pro: NVS keys unchanged — config written by old MQTT mechanism still loads
- Payload sizes are tiny (~200-500 bytes per config section)
- No need for protobuf/msgpack/custom binary — complexity not justified

### No WebSocket
- Polling-based status updates (1-2 second interval)
- WebSocket adds ~15KB code and per-connection memory; not worth it for 1Hz updates
- Simple `GET /api/status` is adequate

## Implementation Plan

### Phase 1: Backend (ESP-IDF HTTP server + REST API)
1. Add `esp_http_server` to rebel-espresso component
2. Implement `/api/status` endpoint
3. Implement `/api/config/:name` GET/PUT/reset endpoints
4. Implement `/api/auth` endpoints
5. Add NVS keys for auth
6. Add FAT filesystem mount for `data` partition
7. Static file serving from FAT

### Phase 2: Frontend (Web UI)
1. Set up minimal build toolchain (or hand-written vanilla JS)
2. Status page with auto-refresh
3. Config page with edit/save per section
4. Auth page
5. Dark theme, responsive CSS
6. Gzip and deploy to `data` partition

### Phase 3: Integration
1. Wire into `controller_init()` for r2
2. Test on device
3. Update README

## Open Questions
- Should the web server start in AP mode during provisioning as well?
- Do we want OTA firmware upload via the web UI in future?
