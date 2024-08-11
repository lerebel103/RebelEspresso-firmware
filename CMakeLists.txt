# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)


# If the environment does not specify a build type, set to Debug
IF (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug"
            CACHE INTERNAL "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel."
            FORCE)
ENDIF ()
MESSAGE(STATUS "${CMAKE_BUILD_TYPE} build selected")

# Target hardware revision for which this hardware is for
IF (NOT HARDWARE_REVISION_MAJOR)
    set(HARDWARE_REVISION_MAJOR 2
            CACHE INTERNAL "Set major hardware revision to build for."
            FORCE)
ENDIF ()

# If the environment does not specify a hardware revision to build for...
IF (NOT THING_TYPE)
    set(THING_TYPE "coffee-drivah"
            CACHE INTERNAL "Set hardware thing type to build for."
            FORCE)
ENDIF ()
MESSAGE(STATUS "Building for Thing Type ${THING_TYPE} r${HARDWARE_REVISION_MAJOR}.")

# If the environment does not specify a hardware revision to build for...
IF (NOT BUILD_STAGE)
    set(BUILD_STAGE "dev"
            CACHE INTERNAL "Set stage to dev|stg|prd."
            FORCE)
ENDIF ()
MESSAGE(STATUS "Build stage is ${BUILD_STAGE}")

include(config.cmake)

# Git
find_package(Git REQUIRED)

# Now get the current git hash so we can tag build with this
execute_process(
        COMMAND
        ${GIT_EXECUTABLE} rev-parse --short HEAD
        RESULT_VARIABLE
        GIT_SHORT_HASH_RESULT
        OUTPUT_VARIABLE
        GIT_SHORT_HASH
)
STRING(STRIP ${GIT_SHORT_HASH} GIT_SHORT_HASH)
SET(GIT_SHORT_HASH ${GIT_SHORT_HASH} CACHE INTERNAL "" FORCE)

# Cool, get our tag
execute_process(
        COMMAND
        ${GIT_EXECUTABLE} for-each-ref refs/tags --sort=-taggerdate --count=1 --format=%\(refname:short\)
        RESULT_VARIABLE
        GIT_LAST_TAG_RESULT
        OUTPUT_VARIABLE
        GIT_LAST_TAG
)
STRING(STRIP ${GIT_LAST_TAG} GIT_LAST_TAG)

string(REPLACE "." ";" VERSION_LIST ${GIT_LAST_TAG})
list(GET VERSION_LIST 0 PROJECT_VERSION_MAJOR)
list(GET VERSION_LIST 1 PROJECT_VERSION_MINOR)

MESSAGE(STATUS ">> Last tag from GIT is '${GIT_LAST_TAG}'")

# The patch version is informational from git so we can tag releases on github.
# Always re-compute this patch number regardless, from Major.Minor
execute_process(
        COMMAND
        ${GIT_EXECUTABLE} rev-list ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.. --count
        RESULT_VARIABLE
        GIT_PATCH_RESULT
        OUTPUT_VARIABLE
        PROJECT_VERSION_PATCH
)
STRING(STRIP ${PROJECT_VERSION_PATCH} PROJECT_VERSION_PATCH)
set(PROJECT_VERSION_PATCH 0)

# Versioning derived from last tag
add_definitions(-DCMAKE_THING_TYPE="${THING_TYPE}")
add_definitions(-DCMAKE_FIRMWARE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR})
add_definitions(-DCMAKE_FIRMWARE_VERSION_MINOR=${PROJECT_VERSION_MINOR})
add_definitions(-DCMAKE_FIRMWARE_VERSION_BUILD=${PROJECT_VERSION_PATCH})
add_definitions(-DCMAKE_HARDWARE_REVISION_MAJOR=${HARDWARE_REVISION_MAJOR})
add_definitions(-DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}")

SET(PROJECT_VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH} CACHE INTERNAL "" FORCE)
SET(FIRMWARE_VERSION ${PROJECT_VERSION} CACHE INTERNAL "" FORCE)
MESSAGE(STATUS "Firmware version is ${PROJECT_VERSION}")

SET(BUILD_TYPE ${CMAKE_BUILD_TYPE} CACHE INTERNAL "" FORCE)


IF (NOT NVRAM_STORAGE_PARTITION)
    SET(NVRAM_STORAGE_PARTITION "storage" CACHE INTERNAL "NVS partition name" FORCE)
ENDIF ()

# Add homekit and AWS stuff
if(DEFINED ENV{HOMEKIT_PATH})
    set(HOMEKIT_PATH $ENV{HOMEKIT_PATH})
else()
    set(HOMEKIT_PATH ${CMAKE_CURRENT_LIST_DIR}/components/esp-homekit-sdk)
endif(DEFINED ENV{HOMEKIT_PATH})
set(EXTRA_COMPONENT_DIRS
        ${EXTRA_COMPONENT_DIRS}
        ${HOMEKIT_PATH}/components
        ${HOMEKIT_PATH}/components/homekit
        components/esp-aws-iot/libraries
        components/esp-aws-iot/libraries/common/
)

# Pull codesign certificate
execute_process(
        COMMAND
        ${CMAKE_SOURCE_DIR}/bin/get-codesign-cert.py ${CODESIGN_URL} ${CMAKE_SOURCE_DIR}/certs/aws_codesign.crt
        COMMAND_ECHO STDOUT
        COMMAND_ERROR_IS_FATAL ANY
)

set(IDF_TARGET esp32)
set(PROJECT_NAME coffee-drivah-firmware)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(${PROJECT_NAME} VERSION ${PROJECT_VERSION})


# Rename target binary file with correct versioning
set(DEPLOY_BASE_DIR ${CMAKE_BINARY_DIR}/deploy)
set(FIRMWARE_TARGET_DIR ${DEPLOY_BASE_DIR}/firmware)

SET(CMAKE_CXX_STANDARD -std=c++14)
SET(CMAKE_C_STANDARD -std=c11)

# Set compile flags to give us something that will provide good perfomance
STRING(TOLOWER ${CMAKE_BUILD_TYPE} LOWERCASE_CMAKE_BUILD_TYPE)
IF (LOWERCASE_CMAKE_BUILD_TYPE MATCHES debug)
    SET(CMAKE_C_FLAGS "-O0 -Os -ffast-math ${CMAKE_C_FLAGS}")
    SET(CMAKE_CXX_FLAGS "-O0 -Os -ffast-math ${CMAKE_CXX_FLAGS}")
ELSEIF (LOWERCASE_CMAKE_BUILD_TYPE MATCHES release)
    SET(CMAKE_C_FLAGS "-O3 -ffast-math ${CMAKE_C_FLAGS}")
    SET(CMAKE_CXX_FLAGS "-O3 -ffast-math ${CMAKE_CXX_FLAGS}")
ELSE ()
    MESSAGE(FATAL_ERROR "Build type '${CMAKE_BUILD_TYPE}' is unknown")
ENDIF ()

MESSAGE(STATUS "Using CFLAGS ${CMAKE_C_FLAGS}")
MESSAGE(STATUS "Using CXXFLAGS ${CMAKE_CXX_FLAGS}")

# Rename target binary file with Hardware Revision, Firmware version and build type.
# SET(FINAL_FILE "${PROJECT_NAME}-r${HARDWARE_REVISION_MAJOR}-v${FIRMWARE_VERSION}-${GIT_SHORT_HASH}-${CMAKE_BUILD_TYPE}.bin")

# Package binaries into firmware directory
add_custom_command(TARGET app POST_BUILD

        # Grab the firmware and copy it to the deploy directory
        COMMAND ${CMAKE_COMMAND} -E rm -fr "${DEPLOY_BASE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_BASE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_BASE_DIR}/bin"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${FIRMWARE_TARGET_DIR}"

        # Drop in versio info
        COMMAND ${CMAKE_COMMAND} -E echo "${FIRMWARE_VERSION}" > "${FIRMWARE_TARGET_DIR}/firmware_version.txt"
        COMMAND ${CMAKE_COMMAND} -E echo "r${HARDWARE_REVISION_MAJOR}" > "${FIRMWARE_TARGET_DIR}/hardware_revision.txt"
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/ci/bin/* "${DEPLOY_BASE_DIR}/bin/"

        # Copy relevent firmware files over
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/*firmware.bin" "${FIRMWARE_TARGET_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/*firmware.elf" "${FIRMWARE_TARGET_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/*firmware.map" "${FIRMWARE_TARGET_DIR}"

        # Now copy buildspec across for deployment
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/ci/buildspec*.yaml" "${DEPLOY_BASE_DIR}"
        )

add_custom_command(TARGET bootloader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/partition_table/*.bin" "${FIRMWARE_TARGET_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/bootloader/*.bin" "${FIRMWARE_TARGET_DIR}"
)

# And finally the target that zips up the firmware as a single artefact
add_custom_target(deploy ALL
        DEPENDS app bootloader
        COMMAND zip -r deploy.zip deploy
)

target_add_binary_data(${PROJECT_NAME}.elf "certs/aws_codesign.crt" TEXT)