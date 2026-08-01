# WiFi AP Provisioning Spec

## Overview

Replace BLE-based WiFi provisioning with a Soft-AP captive portal approach. When the device cannot connect to WiFi (no stored credentials, or stored credentials fail), it broadcasts an open Soft-AP. Clients join the AP, get auto-redirected to the embedded SPA via captive portal DNS, and enter WiFi credentials through a new WiFi setup page. Once valid credentials are submitted, the AP shuts down and normal STA mode resumes.

This also internalizes the `connectivity` git submodule into a new first-party component (`esp-connectivity`), removing the external dependency and all BLE provisioning code.

## Requirements

### R1: Remove BLE Provisioning
- Remove all usage of `network_provisioning` and `scheme_ble` components
- Remove `qrcode` component dependency
- Remove `CONFIG_BLE_WIFI_PROV_ENABLED` and related Kconfig entries
- Remove QR code generation logic (`wifi_get_prov_qr()`, `wifi_get_prov_qr_len()`)
- Remove `WIFI_PROVISIONED_BIT` event (no longer a distinct state — device either has creds or doesn't)
- Free up flash/RAM previously consumed by Bluetooth stack (~60-80KB)

### R2: Internalize Connectivity Component
- Remove `components/connectivity` git submodule
- Create new component `components/esp-connectivity` as a first-party IDF component
- Copy and retain from the old submodule:
  - `src/common/nvs.cpp` + `nvs.h` — NVS initialization
  - `src/common/identity.cpp` + `identity.h` — MAC-based thing identity
  - `src/common/events_common.h` — event group bit definitions
  - `src/sntp/sntp_sync.cpp` + `sntp_sync.h` — SNTP time sync
  - `src/wifi/wifi_connect.cpp` + `wifi_connect.h` — WiFi STA logic (rewritten, see R3)
  - `src/connectivity.cpp` + `connectivity.h` — entry point
- Drop entirely: `fleet_provisioning/`, `mqtt/`, `ota/`, `shadow/` directories (already unused)
- New component must have its own `CMakeLists.txt` and `Kconfig` suitable for eventual extraction as a standalone IDF component
- Update `components/rebel-espresso/CMakeLists.txt` to depend on `esp-connectivity` instead of `connectivity`
- Update `.gitmodules` to remove the connectivity submodule entry

### R3: WiFi STA Mode with AP Fallback
- On boot, attempt WiFi STA connection using credentials stored in NVS (standard `nvs.net80211` namespace, same keys Espressif's provisioning manager uses)
- If no credentials are stored, or if STA connection fails after 15 seconds of retries, activate Soft-AP mode
- STA connection failure includes: auth failure, AP not found, DHCP timeout
- If STA connects successfully, Soft-AP is never started
- If STA disconnects during normal operation and cannot reconnect within 30 seconds, re-activate Soft-AP
- When new credentials are submitted via the SPA and STA connects successfully, immediately shut down Soft-AP
- State machine:
  ```
  BOOT
    → Read NVS creds
    → [creds exist] → Try STA (15s timeout)
        → [success] → CONNECTED (normal operation)
        → [fail]    → START_AP
    → [no creds]   → START_AP

  CONNECTED
    → [disconnect] → Retry STA (30s)
        → [reconnect] → CONNECTED
        → [timeout]   → START_AP

  START_AP
    → Soft-AP active + captive portal + HTTP server (auth bypassed)
    → [user submits creds] → Try STA
        → [success] → Stop AP → CONNECTED
        → [fail]    → Report failure to user, stay in AP mode
  ```

### R4: Soft-AP Configuration
- SSID: `RebelEspresso-XXXXXX` where `XXXXXX` is the last 6 hex digits of the device MAC address
- Security: Open (no password) — this is intentional for frictionless onboarding
- Channel: Auto (1)
- Max connections: 4
- IP address: `192.168.4.1`
- Subnet: `255.255.255.0`
- DHCP range: `192.168.4.2` – `192.168.4.5`

### R5: Captive Portal (DNS Redirect)
- When Soft-AP is active, run a lightweight DNS server on UDP port 53
- Respond to ALL DNS A-record queries with the device's AP IP (`192.168.4.1`)
- This triggers automatic captive portal detection on iOS, Android, Windows, and macOS
- The OS opens a browser/webview pointed at the device, landing on the SPA
- The HTTP server redirects any request to an unknown host to `http://192.168.4.1:8080/`
- DNS server stops when Soft-AP is deactivated

### R6: WiFi Setup API Endpoints
- New REST endpoints (no authentication required when in AP mode):
  ```
  GET  /api/wifi/scan     — Trigger a WiFi scan and return results
  GET  /api/wifi/status   — Current WiFi state (mode, SSID, IP, signal)
  POST /api/wifi/connect  — Submit SSID + PSK, attempt STA connection
  ```
- `GET /api/wifi/scan` response:
  ```json
  {
    "networks": [
      {"ssid": "MyNetwork", "rssi": -45, "auth": "wpa2", "channel": 6},
      {"ssid": "Neighbor", "rssi": -72, "auth": "wpa2", "channel": 11}
    ]
  }
  ```
- `GET /api/wifi/status` response:
  ```json
  {
    "mode": "ap" | "sta" | "ap+sta",
    "sta": {"connected": false, "ssid": "", "ip": "", "rssi": 0},
    "ap": {"active": true, "ssid": "RebelEspresso-A1B2C3", "clients": 1}
  }
  ```
- `POST /api/wifi/connect` request:
  ```json
  {"ssid": "MyNetwork", "psk": "mypassword"}
  ```
  Response (after connection attempt, ~10s timeout):
  ```json
  {"success": true, "ip": "192.168.1.42"}
  ```
  or:
  ```json
  {"success": false, "error": "auth_failed"}
  ```
- WiFi scan uses `esp_wifi_scan_start()` / `esp_wifi_scan_get_ap_records()`
- Credentials stored in NVS using the same keys as Espressif's WiFi provisioning (`nvs.net80211` namespace: `sta.ssid`, `sta.pswd`)

### R7: SPA WiFi Setup Page
- New section/page in the existing SPA accessible from the navigation
- Workflow:
  1. Page loads → auto-triggers scan → displays list of available networks sorted by signal strength
  2. User taps a network (or enters SSID manually for hidden networks)
  3. PSK input field appears
  4. User taps "Connect"
  5. Loading state shown while device attempts connection (~10s)
  6. Success: show confirmation with new IP address, message "Device is now on your network. You can access it at http://[IP]:8080. This setup page will become unavailable shortly."
  7. Failure: show error, allow retry with different credentials
- "Rescan" button to refresh the network list
- Show signal strength indicator (icon or bars) and auth type per network
- Show current WiFi status at the top (connected/disconnected, current SSID if any)
- Page is functional both in AP mode (primary onboarding) and in STA mode (changing networks)

### R8: Authentication Bypass in AP Mode
- When Soft-AP is active, HTTP authentication is fully bypassed for ALL endpoints
- Rationale: the AP is open (no WPA), so HTTP auth adds no real security; it would only create a UX dead-end for users who don't remember their password after a network change
- Auth is automatically re-enabled when AP mode is deactivated and STA connects
- Implementation: `web_auth_check()` returns `true` immediately when AP mode is active
- The existing auth state (enabled/disabled, password hash) is preserved in NVS — only the enforcement is suspended during AP mode

### R9: NVS Compatibility
- WiFi credentials are read/written via `esp_wifi_get_config()` / `esp_wifi_set_config()`, which is the ESP-IDF WiFi driver's own persistence mechanism
- Zero migration needed — devices provisioned via old BLE mechanism already have credentials stored in the format the driver reads
- The `network_prov_mgr_is_wifi_provisioned()` check is replaced with `esp_wifi_get_config()` + checking if SSID is non-empty
- All other NVS usage (identity, auth, configs) is unchanged

### R10: Emulator/Test Coverage
- Unit tests for the WiFi state machine logic (mock WiFi HAL layer):
  - No creds → AP starts
  - Creds exist, connection succeeds → no AP
  - Creds exist, connection fails → AP starts after timeout
  - New creds submitted → STA attempt → success → AP stops
  - New creds submitted → STA attempt → fail → stays in AP, error reported
  - STA disconnect during operation → reconnect timeout → AP restarts
- Integration tests for HTTP API endpoints:
  - `/api/wifi/scan` returns valid JSON with expected fields
  - `/api/wifi/status` reflects current state
  - `/api/wifi/connect` stores creds and triggers connection attempt
- Captive portal DNS server: test that all queries return the AP IP
- Auth bypass: verify endpoints are accessible without auth when AP is active
- Tests run via the existing emulator/QEMU infrastructure

## Design Decisions

### Why Soft-AP + Captive Portal (not BLE)
- BLE provisioning requires a companion app (Espressif's app or custom)
- Soft-AP works with any device that has WiFi + a browser — zero app install
- Captive portal provides automatic redirect — user doesn't need to know an IP
- Simpler code path, smaller flash footprint (no Bluetooth stack)
- Better UX for technical and non-technical users alike

### Why Open AP (no WPA on the setup network)
- The AP is temporary (active only during setup or network failure)
- Requiring a password on the AP creates a chicken-and-egg problem (how does user learn the AP password?)
- Could print it on the device label, but open AP is simpler and the security risk is minimal (local, temporary, limited to WiFi setup)

### Captive Portal Implementation
- A minimal DNS server (~100 lines of C) that responds to all queries with `192.168.4.1`
- This is the standard mechanism used by hotel WiFi, mobile hotspots, etc.
- iOS sends requests to `captive.apple.com`, Android to `connectivitycheck.gstatic.com`, Windows to `www.msftconnectcheck.com` — all get redirected to our device
- The HTTP server also handles redirect: any `Host` header that isn't `192.168.4.1` returns a 302 to `http://192.168.4.1:8080/`

### Component Structure (`esp-connectivity`)
```
components/esp-connectivity/
  CMakeLists.txt
  Kconfig
  src/
    connectivity.cpp          — public init entry point
    connectivity.h
    common/
      events_common.h         — event group bit definitions
      identity.cpp / .h       — MAC-based device identity
      nvs.cpp / .h            — NVS init wrapper
    wifi/
      wifi_manager.cpp / .h   — STA connection + AP fallback state machine
      wifi_ap.cpp / .h        — Soft-AP start/stop, DHCP config
      wifi_scan.cpp / .h      — WiFi scan wrapper
      dns_server.cpp / .h     — Captive portal DNS responder
    sntp/
      sntp_sync.cpp / .h      — SNTP time synchronization
```

### WiFi Credential Storage
- Use `esp_wifi_get_config(WIFI_IF_STA)` to check for stored credentials on boot
- Use `esp_wifi_set_config(WIFI_IF_STA)` to store new credentials (persists to NVS automatically)
- This is the same storage mechanism that ESP-IDF's WiFi driver and the old `network_prov_mgr` used
- Ensures backward compatibility with devices already provisioned via BLE — existing credentials are found immediately
- No custom NVS namespace or key names needed — the driver handles it

### STA Connection Timeout Strategy
- Initial boot: 15 seconds of STA attempts before falling back to AP
- During operation disconnect: 30 seconds of reconnect attempts before AP
- These are generous enough to handle momentary router reboots without unnecessary AP activation
- During the `/api/wifi/connect` flow: 10 second timeout for user feedback, then report success/failure

### AP Shutdown Sequencing
- When `/api/wifi/connect` succeeds, the HTTP response is sent FIRST, then AP shuts down after a 2-second delay
- This ensures the client receives the success response (with the new IP) before losing connectivity to the AP
- The delay is short enough that the overall UX feels instant

### HTTP Server Lifecycle
- The HTTP server starts as soon as either STA connects OR AP mode activates (whichever comes first)
- In AP mode: serves on `192.168.4.1:8080`
- In STA mode: serves on the assigned DHCP IP on port 8080
- The server instance is the same in both modes — only the network interface changes
- Port 8080 remains (port 80 reserved for HomeKit)

## Implementation Tasks

### Task 1: Create `esp-connectivity` component scaffold
- Create `components/esp-connectivity/` directory structure
- Copy `common/`, `sntp/` source files from old `connectivity` submodule (unchanged)
- Write new `CMakeLists.txt` (remove `network_provisioning`, `qrcode` deps; add `esp_netif`, `lwip`)
- Write new `Kconfig` (remove BLE provisioning config, add AP timeout configs)
- Update `events_common.h`: remove `WIFI_PROVISIONED_BIT`, add `WIFI_AP_ACTIVE_BIT`
- Stub out new `wifi/` source files

### Task 2: Implement WiFi STA manager (`wifi_manager.cpp`)
- Rewrite `wifi_connect_init()` → `wifi_manager_init()`
- Implement STA connection logic without BLE provisioning
- Read credentials directly from NVS (`nvs.net80211` namespace)
- Implement 15s boot timeout, 30s reconnect timeout
- Expose `wifi_manager_get_mode()`, `wifi_manager_get_metrics()`
- Fire events on state transitions (connected, disconnected, ap_started, ap_stopped)

### Task 3: Implement Soft-AP mode (`wifi_ap.cpp`)
- `wifi_ap_start()` — configure and start Soft-AP with open SSID `RebelEspresso-XXXXXX`
- `wifi_ap_stop()` — tear down AP and DHCP server
- Configure DHCP server on `192.168.4.1/24` range
- Track connected client count
- Expose `wifi_ap_is_active()`, `wifi_ap_get_info()`

### Task 4: Implement captive portal DNS server (`dns_server.cpp`)
- Lightweight UDP DNS responder on port 53
- Parse incoming DNS queries (extract question, ignore most header fields)
- Respond with A record pointing to `192.168.4.1` for all queries
- Start/stop tied to Soft-AP lifecycle
- Task-based: runs in its own FreeRTOS task (~2KB stack)

### Task 5: Implement WiFi scan wrapper (`wifi_scan.cpp`)
- `wifi_scan_start()` — non-blocking scan trigger
- `wifi_scan_get_results()` — return sorted AP list (by RSSI descending)
- Deduplicate SSIDs (keep strongest signal per SSID)
- Map auth modes to strings ("open", "wep", "wpa", "wpa2", "wpa3")
- Cap results at 20 networks

### Task 6: Add WiFi API endpoints to HTTP server
- `GET /api/wifi/scan` — call scan, return JSON array
- `GET /api/wifi/status` — return current mode, STA info, AP info
- `POST /api/wifi/connect` — receive SSID+PSK, store in NVS, trigger STA connect, wait up to 10s, return result
- Register in `web_server_start()` alongside existing API handlers
- New file: `web_api_wifi.cpp` / `web_api_wifi.h` in the webserver directory

### Task 7: Auth bypass in AP mode
- Modify `web_auth_check()` to query `wifi_ap_is_active()`
- If AP is active, return `true` unconditionally (skip auth)
- Add HTTP redirect handler: requests with non-local `Host` header → 302 to `http://192.168.4.1:8080/`
- Ensure auth re-engages automatically when AP stops

### Task 8: SPA WiFi setup page
- Add WiFi setup page/section to the SPA navigation
- Implement network list with signal strength indicators and auth type badges
- Manual SSID entry for hidden networks
- PSK input with show/hide toggle
- Connect button with loading state and timeout handling
- Success/failure feedback with appropriate messaging
- Rescan button
- Current status banner (connected network or "Not connected")
- Responsive, matches existing dark theme

### Task 9: Remove old connectivity submodule
- Remove `components/connectivity` submodule (`git rm components/connectivity`)
- Remove entry from `.gitmodules`
- Update `components/rebel-espresso/CMakeLists.txt`: `connectivity` → `esp-connectivity`
- Update any `#include` paths that reference the old component
- Verify build still compiles with the new component

### Task 10: Integration wiring
- Update `connectivity_init()` in the new component to call `wifi_manager_init()` instead of `wifi_connect_init()`
- Ensure HTTP server starts in both AP and STA modes
- Wire AP state into the existing status API (so the SPA status page shows AP mode)
- Update `wifi_connect_get_metrics()` → `wifi_manager_get_metrics()` for existing callers

### Task 11: Emulator tests
- WiFi state machine unit tests (mock `esp_wifi_*` calls)
- DNS server unit test (send query packet, verify response)
- HTTP API integration tests (`/api/wifi/scan`, `/api/wifi/status`, `/api/wifi/connect`)
- Auth bypass test (verify endpoints accessible without credentials when AP flag is set)
- NVS credential read/write test (verify correct namespace and keys)

### Task 12: Cleanup and documentation
- Update `sdkconfig.defaults` to remove BLE/Bluetooth related configs
- Update partition table if Bluetooth removal frees space
- Update README with new provisioning instructions
- Add brief CONTRIBUTING note about the `esp-connectivity` component structure

## Flash/RAM Impact

| Change | Flash | RAM |
|--------|-------|-----|
| Remove BLE/BT stack | -80KB | -30KB |
| Remove `network_provisioning` + `qrcode` | -15KB | -2KB |
| Add DNS server | +1KB | +2KB (task stack) |
| Add WiFi scan/AP code | +5KB | +3KB |
| Add WiFi API endpoints | +3KB | +1KB |
| SPA WiFi page (gzipped in FAT) | 0 (data partition) | 0 |
| **Net change** | **~-86KB** | **~-26KB** |

This is a net reduction in firmware size — removing Bluetooth is a big win.

## Open Questions (Resolved)

| Question | Decision |
|----------|----------|
| AP SSID format | `RebelEspresso-XXXXXX` (last 6 hex of MAC) |
| Auth bypass scope in AP mode | Full bypass (all endpoints) |
| STA timeout before AP activation | 15 seconds on boot |
| Reconnect timeout before AP re-activation | 30 seconds |
| Component name | `esp-connectivity` |
