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

###############################################################################
# Targets
###############################################################################

.PHONY: build clean fullclean menuconfig shell setup submodules

## Build firmware
build:
	$(DOCKER_RUN) idf.py $(CMAKE_VARS) build

## Clean build artifacts
clean:
	$(DOCKER_RUN) idf.py fullclean

## Full clean including managed components
fullclean:
	$(DOCKER_RUN) bash -c "idf.py fullclean && rm -rf managed_components"

## Interactive menuconfig
menuconfig:
	$(DOCKER_COMPOSE) run --rm -it idf idf.py menuconfig

## Shell inside the build container
shell:
	$(DOCKER_COMPOSE) run --rm -it idf bash

## First-time setup: submodules + Docker image build
setup: submodules
	$(DOCKER_COMPOSE) build

## Init/update git submodules
submodules:
	git submodule update --init --recursive
