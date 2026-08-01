cmake_minimum_required(VERSION 3.16)

###############################################################################
# Project Configuration
###############################################################################

IF (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE INTERNAL "" FORCE)
ENDIF()
MESSAGE(STATUS "${CMAKE_BUILD_TYPE} build selected")

IF (NOT THING_TYPE)
    set(THING_TYPE "coffee-drivah" CACHE INTERNAL "" FORCE)
ENDIF()

IF (NOT BUILD_STAGE)
    set(BUILD_STAGE "dev" CACHE INTERNAL "" FORCE)
ENDIF()
MESSAGE(STATUS "Building ${THING_TYPE}, stage: ${BUILD_STAGE}")

###############################################################################
# Version from Git
###############################################################################

find_package(Git REQUIRED)

execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_SHORT_HASH
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _git_result
)
if(NOT _git_result EQUAL 0)
    set(GIT_SHORT_HASH "unknown")
endif()

execute_process(
    COMMAND ${GIT_EXECUTABLE} for-each-ref refs/tags --sort=-taggerdate --count=1 --format=%\(refname:short\)
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_LAST_TAG
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _git_result
)
if(_git_result EQUAL 0 AND GIT_LAST_TAG)
    string(REPLACE "." ";" VERSION_LIST ${GIT_LAST_TAG})
    list(GET VERSION_LIST 0 PROJECT_VERSION_MAJOR)
    list(GET VERSION_LIST 1 PROJECT_VERSION_MINOR)
else()
    set(PROJECT_VERSION_MAJOR 0)
    set(PROJECT_VERSION_MINOR 0)
endif()
set(PROJECT_VERSION_PATCH 0)

SET(PROJECT_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}" CACHE INTERNAL "" FORCE)
SET(FIRMWARE_VERSION ${PROJECT_VERSION} CACHE INTERNAL "" FORCE)
MESSAGE(STATUS "Firmware version: ${PROJECT_VERSION} (${GIT_SHORT_HASH})")

# Compile-time defines
add_definitions(
    -DCMAKE_THING_TYPE="${THING_TYPE}"
    -DCMAKE_FIRMWARE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    -DCMAKE_FIRMWARE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    -DCMAKE_FIRMWARE_VERSION_BUILD=${PROJECT_VERSION_PATCH}
    -DCMAKE_HARDWARE_REVISION_MAJOR=2
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
)

###############################################################################
# Build Configuration
###############################################################################

set(SDKCONFIG_DEFAULTS "${CMAKE_SOURCE_DIR}/sdkconfig.defaults")

# HomeKit SDK
if(DEFINED ENV{HOMEKIT_PATH})
    set(HOMEKIT_PATH $ENV{HOMEKIT_PATH})
else()
    set(HOMEKIT_PATH ${CMAKE_CURRENT_LIST_DIR}/components/esp-homekit-sdk)
endif()
set(EXTRA_COMPONENT_DIRS
    ${HOMEKIT_PATH}/components
    ${HOMEKIT_PATH}/components/homekit
)

# Compiler settings
SET(CMAKE_CXX_STANDARD 17)
SET(CMAKE_C_STANDARD 17)

STRING(TOLOWER ${CMAKE_BUILD_TYPE} _build_type_lower)
IF (_build_type_lower MATCHES debug)
    SET(CMAKE_C_FLAGS "-O0 -Os -ffast-math ${CMAKE_C_FLAGS}")
    SET(CMAKE_CXX_FLAGS "-O0 -Os -ffast-math ${CMAKE_CXX_FLAGS}")
ELSEIF (_build_type_lower MATCHES release)
    SET(CMAKE_C_FLAGS "-O3 -ffast-math ${CMAKE_C_FLAGS}")
    SET(CMAKE_CXX_FLAGS "-O3 -ffast-math ${CMAKE_CXX_FLAGS}")
ELSE()
    MESSAGE(FATAL_ERROR "Unknown build type: ${CMAKE_BUILD_TYPE}")
ENDIF()

SET(CMAKE_C_FLAGS "-Wno-error=incompatible-pointer-types ${CMAKE_C_FLAGS}")
SET(CMAKE_CXX_FLAGS "-Wno-error=incompatible-pointer-types ${CMAKE_CXX_FLAGS}")

###############################################################################
# ESP-IDF Project
###############################################################################

set(IDF_TARGET esp32)
set(PROJECT_NAME coffee-drivah-firmware)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(${PROJECT_NAME} VERSION ${PROJECT_VERSION})

###############################################################################
# Post-Build: Deploy Package
###############################################################################

set(DEPLOY_BASE_DIR ${CMAKE_BINARY_DIR}/deploy)
set(FIRMWARE_TARGET_DIR ${DEPLOY_BASE_DIR}/firmware)

add_custom_command(TARGET app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -fr "${DEPLOY_BASE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${FIRMWARE_TARGET_DIR}"

    COMMAND ${CMAKE_COMMAND} -E echo "${FIRMWARE_VERSION}" > "${FIRMWARE_TARGET_DIR}/firmware_version.txt"
    COMMAND ${CMAKE_COMMAND} -E echo "r2" > "${FIRMWARE_TARGET_DIR}/hardware_revision.txt"

    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/*firmware.bin" "${FIRMWARE_TARGET_DIR}/firmware.bin"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/*firmware.elf" "${FIRMWARE_TARGET_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/*firmware.map" "${FIRMWARE_TARGET_DIR}"
)

add_custom_command(TARGET bootloader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/partition_table/*.bin" "${FIRMWARE_TARGET_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/bootloader/*.bin" "${FIRMWARE_TARGET_DIR}"
)

add_custom_target(deploy ALL
    DEPENDS app bootloader
    COMMAND zip -r deploy.zip deploy
)

###############################################################################
# Web UI: FAT Filesystem Image
###############################################################################

if(EXISTS "${CMAKE_SOURCE_DIR}/data")
    fatfs_create_spiflash_image(data ${CMAKE_SOURCE_DIR}/data FLASH_IN_PROJECT)
endif()
