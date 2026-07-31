###############################################################################
# RebelEspresso Firmware — Build Orchestration
#
# All build targets run inside Docker. No local ESP-IDF installation required.
#
# For flashing and monitoring, use esptool directly with the build tree
# output. See README.md for details.
###############################################################################

# Project configuration
BUILD_STAGE  ?= dev
BUILD_TYPE   ?= Release
HW_REVISION  ?= 2
THING_TYPE   ?= coffee-drivah

# Docker
DOCKER_COMPOSE = docker compose
DOCKER_RUN     = $(DOCKER_COMPOSE) run --rm idf

# CMake variables forwarded to idf.py
CMAKE_VARS = \
	-DBUILD_STAGE=$(BUILD_STAGE) \
	-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
	-DHARDWARE_REVISION_MAJOR=$(HW_REVISION) \
	-DTHING_TYPE=$(THING_TYPE)

# Sentinel file tracking when the Docker image was last built.
# Rebuilds only when Dockerfile, requirements, or constraints change.
.docker-image: Dockerfile requirements.txt constraints.txt
	$(DOCKER_COMPOSE) build
	@touch .docker-image

###############################################################################
# Targets
###############################################################################

.PHONY: build test lint format clean fullclean menuconfig shell setup submodules docker webapp

## Build firmware
build: .docker-image webapp
	$(DOCKER_RUN) idf.py $(CMAKE_VARS) build

## Run unit tests in QEMU
test: .docker-image
	$(DOCKER_RUN) bash -c "cd test_app && idf.py build && /workspace/test_app/run_qemu.sh"

## Static analysis with clang-tidy (requires build first for compile_commands.json)
lint: .docker-image
	$(DOCKER_RUN) /workspace/scripts/lint.sh

## Lint with auto-fix
lint-fix: .docker-image
	$(DOCKER_RUN) /workspace/scripts/lint.sh --fix

## Format source files with clang-format
format: .docker-image
	$(DOCKER_RUN) bash -c "find components/rebel-espresso/src components/esp32-aws-connector/src main \
		-name '*.cpp' -o -name '*.c' -o -name '*.h' | xargs clang-format -i"

## Check formatting (dry-run, exits non-zero if changes needed)
format-check: .docker-image
	$(DOCKER_RUN) bash -c "find components/rebel-espresso/src components/esp32-aws-connector/src main \
		-name '*.cpp' -o -name '*.c' -o -name '*.h' | xargs clang-format --dry-run --Werror"

## Clean build artifacts
clean:
	$(DOCKER_RUN) idf.py fullclean

## Full clean including managed components and test build
fullclean:
	$(DOCKER_RUN) bash -c "idf.py fullclean && rm -rf managed_components test_app/build"

## Interactive menuconfig
menuconfig: .docker-image
	$(DOCKER_COMPOSE) run --rm -it idf idf.py menuconfig

## Shell inside the build container
shell: .docker-image
	$(DOCKER_COMPOSE) run --rm -it idf bash

## Build web UI (gzip static files into data/ directory)
webapp:
	@webapp/build.sh

## First-time setup: submodules + Docker image + hooks
setup: submodules .docker-image setup-hooks

## Force rebuild the Docker image
docker:
	$(DOCKER_COMPOSE) build
	@touch .docker-image

## Init/update git submodules
submodules:
	git submodule update --init --recursive

## Install git hooks
setup-hooks:
	@cp scripts/pre-commit .git/hooks/pre-commit
	@chmod +x .git/hooks/pre-commit
	@echo "Pre-commit hook installed."
