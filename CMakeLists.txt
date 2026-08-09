cmake_minimum_required(VERSION 3.16)

###############################################################################
# Project Configuration
###############################################################################

IF (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE INTERNAL "" FORCE)
ENDIF()
MESSAGE(STATUS "${CMAKE_BUILD_TYPE} build selected")

IF (NOT THING_TYPE)
    set(THING_TYPE "rebel-espresso" CACHE INTERNAL "" FORCE)
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
    # Release tags are expected as vX.Y.Z (or X.Y.Z). Strip an optional leading
    # "v" so compile-time numeric defines remain valid integers.
    string(REGEX REPLACE "^v" "" GIT_LAST_TAG_SANITIZED ${GIT_LAST_TAG})
    string(REPLACE "." ";" VERSION_LIST ${GIT_LAST_TAG_SANITIZED})
    list(LENGTH VERSION_LIST VERSION_LIST_LEN)
    if(VERSION_LIST_LEN GREATER 0)
        list(GET VERSION_LIST 0 PROJECT_VERSION_MAJOR)
    else()
        set(PROJECT_VERSION_MAJOR 0)
    endif()
    if(VERSION_LIST_LEN GREATER 1)
        list(GET VERSION_LIST 1 PROJECT_VERSION_MINOR)
    else()
        set(PROJECT_VERSION_MINOR 0)
    endif()
    if(VERSION_LIST_LEN GREATER 2)
        list(GET VERSION_LIST 2 PROJECT_VERSION_PATCH)
    else()
        set(PROJECT_VERSION_PATCH 0)
    endif()
else()
    set(PROJECT_VERSION_MAJOR 0)
    set(PROJECT_VERSION_MINOR 0)
    set(PROJECT_VERSION_PATCH 0)
endif()

SET(PROJECT_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}" CACHE INTERNAL "" FORCE)
SET(FIRMWARE_VERSION "${PROJECT_VERSION}-${GIT_SHORT_HASH}" CACHE INTERNAL "" FORCE)
SET(FIRMWARE_VERSION_LABEL "v${FIRMWARE_VERSION}" CACHE INTERNAL "" FORCE)
MESSAGE(STATUS "Firmware version: ${FIRMWARE_VERSION_LABEL}")

# Compile-time defines
add_definitions(
    -DCMAKE_THING_TYPE="${THING_TYPE}"
    -DCMAKE_FIRMWARE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    -DCMAKE_FIRMWARE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    -DCMAKE_FIRMWARE_VERSION_BUILD=${PROJECT_VERSION_PATCH}
    -DCMAKE_HARDWARE_REVISION_MAJOR=2
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
)

# Expose as CMake variable for sub-components (e.g. tft-driver SPIFFS font image)
set(HARDWARE_REVISION_MAJOR "2")

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
set(PROJECT_NAME "${THING_TYPE}")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(${PROJECT_NAME} VERSION ${PROJECT_VERSION})

###############################################################################
# Post-Build: Deploy Package
###############################################################################

set(DEPLOY_BASE_DIR ${CMAKE_BINARY_DIR}/deploy)
set(FIRMWARE_TARGET_DIR ${DEPLOY_BASE_DIR}/firmware)
set(FIRMWARE_ARTIFACT_BASENAME "${PROJECT_NAME}-${FIRMWARE_VERSION_LABEL}")

add_custom_target(deploy ALL
    DEPENDS app bootloader
    COMMAND ${CMAKE_COMMAND} -E rm -fr "${DEPLOY_BASE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${FIRMWARE_TARGET_DIR}"

    COMMAND ${CMAKE_COMMAND} -E echo "${FIRMWARE_VERSION_LABEL}" > "${FIRMWARE_TARGET_DIR}/firmware_version.txt"
    COMMAND ${CMAKE_COMMAND} -E echo "r2" > "${FIRMWARE_TARGET_DIR}/hardware_revision.txt"

    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin" "${FIRMWARE_TARGET_DIR}/${FIRMWARE_ARTIFACT_BASENAME}.bin"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.elf" "${FIRMWARE_TARGET_DIR}/${FIRMWARE_ARTIFACT_BASENAME}.elf"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.map" "${FIRMWARE_TARGET_DIR}/${FIRMWARE_ARTIFACT_BASENAME}.map"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/partition_table/partition-table.bin" "${FIRMWARE_TARGET_DIR}/partition-table.bin"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/bootloader/bootloader.bin" "${FIRMWARE_TARGET_DIR}/bootloader.bin"

    COMMAND ${CMAKE_COMMAND} -E rm -f "${CMAKE_BINARY_DIR}/deploy.zip"
    COMMAND zip -r deploy.zip deploy
)

###############################################################################
# Web UI: Embedded in firmware (no separate partition needed)
###############################################################################
# The webapp is gzipped and embedded directly in the firmware binary.
# See components/rebel-espresso/CMakeLists.txt for the embedding logic.
