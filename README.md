# RebelEspresso Firmware

ESP32 coffee machine controller with AWS IoT OTA, HomeKit, and BLE provisioning.

## Prerequisites

- [Docker](https://docs.docker.com/get-docker/)
- `make`
- `esptool` — for flashing and monitoring from the host

Install esptool:

```bash
brew install esptool
# or
pip install esptool
```

## Building

All compilation happens inside a Docker container pinned to ESP-IDF v5.4.1. No local IDF installation or environment sourcing required.

```bash
# First-time setup (submodules + Docker image)
make setup

# Build firmware
make build

# Build with options
make build BUILD_STAGE=prd BUILD_TYPE=Release HW_REVISION=2
```

### Other build targets

```bash
make test         # run unit tests in QEMU (no hardware needed)
make menuconfig   # interactive Kconfig editor
make clean        # clean build artifacts
make fullclean    # clean everything including managed components
make shell        # drop into the container with full IDF toolchain
```

## Testing

Unit tests run inside QEMU (an ESP32 emulator) within Docker — no physical hardware required.

```bash
make test
```

This builds the test app, creates a QEMU flash image, and runs all tests in the emulator. Tests cover:

- **PID controller** — algorithm correctness, reset behavior, config validation
- **JSON config** — cJSON serialization round-trips, partial updates, array handling
- **NVS persistence** — store/retrieve for all value types, default handling, erase
- **Event loop** — handler registration, event delivery, data passing, FreeRTOS primitives

Tests are in `test_app/main/test_*.cpp`. To add a new test, create a `TEST_CASE` in any `.cpp` file listed in `test_app/main/CMakeLists.txt`.

```bash
# Run tests interactively inside the container
make shell
cd test_app
idf.py build
./run_qemu.sh
```

## Web Interface

The firmware includes a built-in HTTP web interface for configuration and monitoring (hardware revision 2 only). It starts automatically after WiFi connects.

### Development

```bash
make webapp-dev    # local dev server at http://localhost:8080 (mock API)
```

Edit `webapp/index.html` and refresh the browser — no build or hardware needed.

### REST API

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | Live machine status (temps, power, refill, wifi) |
| `/api/config/:name` | GET | Read config section (boiler_temp, brew_temp, boiler_refill, schedules) |
| `/api/config/:name` | PUT | Update config (JSON body, partial merge) |
| `/api/config/:name/reset` | POST | Reset section to defaults |
| `/api/system/info` | GET | Device info, versions, heap, uptime |
| `/api/system/ota` | POST | Upload firmware binary |
| `/api/system/ota-ui` | POST | Upload web UI (gzipped HTML) |
| `/api/system/reboot` | POST | Reboot device |
| `/api/system/factory-reset` | POST | Erase NVS and reboot |
| `/api/auth` | GET | Auth status |
| `/api/auth` | POST | Set password (enables auth) |
| `/api/auth` | DELETE | Disable auth |

### Authentication

Disabled by default. Once a password is set via the Auth page, all API endpoints require HTTP Basic Auth. Static files (the UI itself) remain open so the login prompt appears.

## Flashing

After `make build`, the build tree at `build/` contains everything needed to flash. The build system generates `flash_args` which tells esptool exactly what to write and where — no manual addresses required.

```bash
# Auto-detect port and flash
cd build && esptool.py --chip esp32 -p /dev/cu.usbserial-* -b 460800 \
    --before default_reset --after hard_reset write_flash @flash_args
```

Or with an explicit port:

```bash
cd build && esptool.py --chip esp32 -p /dev/cu.usbserial-0001 -b 460800 \
    --before default_reset --after hard_reset write_flash @flash_args
```

### Erase flash

```bash
esptool.py --chip esp32 -p /dev/cu.usbserial-* erase_flash
```

## Monitoring

Use pyserial's miniterm (bundled with esptool):

```bash
python3 -m serial.tools.miniterm /dev/cu.usbserial-* 115200
```

Press `Ctrl+]` to exit.

## Device Provisioning

Flash NVS provisioning data for a new device:

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python3 bin/flash_provisioning_nvram.py
```

## Build Configuration

| Variable | Default | Description |
|----------|---------|-------------|
| `BUILD_STAGE` | `dev` | Target stage: `dev`, `stg`, `prd` |
| `BUILD_TYPE` | `Release` | CMake build type: `Debug`, `Release` |
| `HW_REVISION` | `2` | Hardware revision major number |
| `THING_TYPE` | `coffee-drivah` | IoT thing type identifier |

## VS Code Dev Container

Open the project and select "Reopen in Container" when prompted. The dev container provides the full ESP-IDF v5.4.1 toolchain, IntelliSense, and all Python dependencies pre-installed.

## Project Structure

```
├── CMakeLists.txt          # ESP-IDF project CMake
├── Dockerfile              # Build container (espressif/idf:v5.4.1)
├── docker-compose.yml      # Container orchestration
├── Makefile                # Developer build targets
├── sdkconfig.defaults      # Non-default Kconfig settings (tracked)
├── requirements.txt        # Python deps for provisioning tools
├── constraints.txt         # Pinned versions for reproducible installs
├── config.cmake            # Stage-specific CMake config
├── components/             # ESP-IDF components (submodules + local)
├── main/                   # Application source
│   └── idf_component.yml   # IDF Component Manager manifest
├── test_app/               # QEMU-based unit tests
│   ├── main/test_*.cpp     # Test source files
│   ├── run_qemu.sh         # QEMU test runner script
│   └── sdkconfig.defaults  # Minimal config for test builds
└── .devcontainer/          # VS Code Dev Container config
```

## How It Works

1. Docker image (`espressif/idf:v5.4.1`) provides the complete toolchain with `export.sh` pre-sourced
2. `idf.py` is the standard ESP-IDF build entry point
3. `sdkconfig.defaults` tracks only project-specific overrides; the full `sdkconfig` is regenerated at build time
4. The build tree (`build/`) contains generated `flash_args` used by esptool
5. CI uses the same container image for build parity

## Updating ESP-IDF Version

Update the tag in:

1. `Dockerfile` — `FROM espressif/idf:vX.Y.Z`
2. `.devcontainer/devcontainer.json` — venv paths under `/opt/esp`
3. `.github/workflows/deploy.yaml` — container image tag

Then: `docker compose build`
