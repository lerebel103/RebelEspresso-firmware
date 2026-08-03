# RebelEspresso Firmware

ESP32 coffee machine controller with HomeKit and a built-in web interface. WiFi provisioning via Soft-AP captive portal — no companion app needed.

## Prerequisites

- [Docker](https://docs.docker.com/get-docker/)
- `make`
- `esptool` — for flashing and monitoring (`brew install esptool`)

## Quick Start

```bash
make setup      # submodules + Docker image + git hooks (first time only)
make build      # build firmware + web UI
make test       # run 35 unit tests in QEMU
make flash      # flash everything to device
make monitor    # serial monitor (115200 baud)
```

## Build Targets

| Target | Description |
|--------|-------------|
| `make build` | Build firmware (includes embedded web UI) |
| `make test` | Run unit tests in QEMU emulator |
| `make lint` | Static analysis (clang-tidy) |
| `make format` | Format source files (clang-format) |
| `make format-check` | CI-friendly format check |
| `make menuconfig` | Interactive Kconfig editor |
| `make clean` | Clean build artifacts |
| `make shell` | Drop into the Docker container |
| `make webapp-dev` | Local web UI dev server (http://localhost:8080) |

## Flashing

All flash targets auto-detect the serial port. Override with `PORT=/dev/cu.xxx`.

| Target | Description |
|--------|-------------|
| `make flash` | Full flash (bootloader + firmware + partition table) |
| `make flash-app` | Firmware only (preserves NVS config) |
| `make monitor` | Serial monitor |

NVS (user configuration) is never overwritten except by `esptool.py erase_flash`.

## Web Interface

Built-in HTTP interface at `http://<device-ip>:8080` (starts after WiFi connects or in AP mode).

- **Status** — live temperatures, power state, refill status
- **WiFi** — scan networks, enter credentials, connect (primary setup mechanism)
- **Config** — PID parameters, schedules, refill thresholds
- **System** — device info, OTA firmware upload, config export/import, reboot
- **Auth** — optional password protection (token-based, no browser popup, bypassed during AP setup)

## WiFi Provisioning

The device uses a Soft-AP captive portal for WiFi setup — no phone app required.

**First boot / no stored credentials:**
1. Device starts a WiFi access point: `RebelEspresso-XXXXXX` (open, no password)
2. Connect to this network from any phone/laptop
3. A captive portal auto-redirects to the setup page (or navigate to `http://192.168.4.1:8080`)
4. Select your WiFi network, enter the password, and tap Connect
5. Device connects to your network and the AP shuts down

**If WiFi disconnects** (router reboot, network change):
- The AP automatically reactivates after 30 seconds of failed reconnection
- Repeat the setup process to enter new credentials

**Authentication during AP mode:**
- HTTP auth is temporarily bypassed so you can always access the setup page
- Auth re-engages as soon as the device connects to your WiFi

### Development

```bash
make webapp-dev    # mock API at http://localhost:8080
```

Edit `webapp/index.html` and refresh — no build or hardware needed. The web UI is automatically gzipped and embedded in the firmware binary during `make build`.

## Configuration

| Variable | Default | Description |
|----------|---------|-------------|
| `BUILD_STAGE` | `dev` | Build stage: `dev`, `stg`, `prd` |
| `BUILD_TYPE` | `Release` | CMake build type: `Debug`, `Release` |
| `PORT` | auto-detect | Serial port for flash/monitor |
| `BAUD` | `2000000` | Flash baud rate |

## Testing

35 unit tests run inside QEMU (no hardware needed):

```bash
make test
```

Covers: PID algorithm, JSON config serialization, NVS persistence, event loop, config export/import format.

## Project Structure

```
├── CMakeLists.txt              # ESP-IDF project (simplified, r2-only)
├── Makefile                    # Developer targets (build/test/flash/lint)
├── Dockerfile                  # Reproducible build container (IDF v6.0.2)
├── docker-compose.yml          # Container orchestration
├── sdkconfig.defaults          # Non-default Kconfig settings
├── partitions.csv              # Flash partition table (16MB)
├── main/                       # Application entry point
├── components/
│   ├── rebel-espresso/         # Main application logic
│   │   └── src/               # Firmware source, grouped by responsibility
│   │       ├── runtime/       #   real-time scan engine + shared process image
│   │       ├── machine/       #   coffee-machine domain control (PID, brew, refill)
│   │       ├── comms/         #   outside-world I/O: iot, webserver/, homekit/
│   │       ├── device/        #   R2 board drivers + device services (+ thing_info)
│   │       └── utils/         #   PID, state machine, measure, nvram_store (NVS)
│   ├── esp-connectivity/       # WiFi STA/AP, captive portal, SNTP, NVS, identity
│   │   ├── src/wifi/           # WiFi manager, Soft-AP, DNS server, scan
│   │   ├── src/sntp/           # NTP time sync
│   │   └── src/common/         # NVS init, device identity, event bits
│   ├── esp-homekit-sdk/        # Apple HomeKit (submodule)
│   ├── ESP32-MAX31865/         # RTD temperature sensor driver
│   ├── esp-ssr-controller/     # SSR duty-cycle controller
│   └── tft-driver/             # ST7796 TFT display driver
├── test_app/                   # QEMU unit tests
├── webapp/                     # Web UI source (embedded in firmware at build time)
├── scripts/                    # Lint scripts, pre-commit hook
└── .github/                    # CI pipeline + Dependabot
```

## Updating ESP-IDF Version

1. `Dockerfile` — change `FROM espressif/idf:vX.Y.Z`
2. `.github/workflows/ci.yaml` — update container image
3. `.devcontainer/devcontainer.json` — update if using dev containers

Then: `make docker`

## VS Code Dev Container

Open the project and select "Reopen in Container" for a pre-configured environment with the full ESP-IDF toolchain.
