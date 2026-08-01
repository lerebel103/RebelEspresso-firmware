# RebelEspresso Firmware

ESP32 coffee machine controller with HomeKit, BLE provisioning, and a built-in web interface.

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
| `make build` | Build firmware + web UI inside Docker |
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
| `make flash` | Full flash (bootloader + firmware + web UI) |
| `make flash-app` | Firmware only (preserves NVS config) |
| `make flash-ui` | Web UI only (no firmware change) |
| `make monitor` | Serial monitor |

NVS (user configuration) is never overwritten except by `esptool.py erase_flash`.

## Web Interface

Built-in HTTP interface at `http://<device-ip>:8080` (starts after WiFi connects).

- **Status** — live temperatures, power state, refill status
- **Config** — PID parameters, schedules, refill thresholds
- **System** — device info, OTA firmware upload, config export/import, reboot
- **Auth** — optional password protection (HTTP Basic)

### Development

```bash
make webapp-dev    # mock API at http://localhost:8080
```

Edit `webapp/index.html` and refresh — no build or hardware needed.

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
│   │   ├── src/hw/base/        # Hardware-independent control logic
│   │   ├── src/hw/r2/src/      # Hardware-specific (display, webserver)
│   │   ├── src/utils/          # PID, state machine, helpers
│   │   └── src/sys/            # NVS abstraction
│   ├── connectivity/           # WiFi, SNTP, NVS init, identity
│   ├── esp-homekit-sdk/        # Apple HomeKit (submodule)
│   ├── ESP32-MAX31865/         # RTD temperature sensor driver
│   ├── esp-ssr-controller/     # SSR duty-cycle controller
│   └── tft-driver/             # ST7796 TFT display driver
├── test_app/                   # QEMU unit tests
├── webapp/                     # Web UI source + dev server
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
