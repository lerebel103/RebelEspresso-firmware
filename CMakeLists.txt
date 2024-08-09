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

# If the environment does not specify a hardware revision to build for...
IF (NOT HARDWARE_REVISION)
    set(HARDWARE_REVISION "2.0"
            CACHE INTERNAL "Set hardware revision to build for."
            FORCE)
ENDIF ()
# If the environment does not specify a hardware revision to build for...
IF (NOT THING_TYPE)
    set(THING_TYPE "rebel-espresso"
            CACHE INTERNAL "Set hardware thing type to build for."
            FORCE)
ENDIF ()
MESSAGE(STATUS "Building for Thing Type ${THING_TYPE} r${HARDWARE_REVISION}.")

# Git
find_package(Git REQUIRED)
if (GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    # Update submodules as needed
    option(GIT_SUBMODULE "Check submodules during build" ON)
    if (GIT_SUBMODULE)
        message(STATUS "Submodule update")
        execute_process(
                COMMAND ${GIT_EXECUTABLE} submodule sync
                COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                RESULT_VARIABLE GIT_SUBMOD_RESULT)
        if (NOT GIT_SUBMOD_RESULT EQUAL "0")
            message(FATAL_ERROR "git submodule update --init failed with ${GIT_SUBMOD_RESULT}, please checkout submodules")
        endif ()
    endif ()
endif ()

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
        ${GIT_EXECUTABLE} for-each-ref refs/tags --sort=-creatordate --count=1 --format=%\(refname:short\)
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

# Not ussing this
set(PROJECT_VERSION_TWEAK "0" CACHE INTERNAL "" FORCE)

SET(PROJECT_VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH} CACHE INTERNAL "" FORCE)
SET(FIRMWARE_VERSION ${PROJECT_VERSION} CACHE INTERNAL "" FORCE)
MESSAGE(STATUS "Firmware version is ${PROJECT_VERSION}")

SET(BUILD_TYPE ${CMAKE_BUILD_TYPE} CACHE INTERNAL "" FORCE)


IF (NOT NVRAM_STORAGE_PARTITION)
    SET(NVRAM_STORAGE_PARTITION "storage" CACHE INTERNAL "NVS partition name"  FORCE)
ENDIF()


# Set hardware revision dependent files
file(COPY components/rebel-espresso/src/hw/r${HARDWARE_REVISION}/sdkconfig DESTINATION ${CMAKE_SOURCE_DIR}/)

if(DEFINED ENV{HOMEKIT_PATH})
    set(HOMEKIT_PATH $ENV{HOMEKIT_PATH})
else()
    set(HOMEKIT_PATH ${CMAKE_CURRENT_LIST_DIR}/components/esp-homekit-sdk)
endif(DEFINED ENV{HOMEKIT_PATH})

set(EXTRA_COMPONENT_DIRS ${HOMEKIT_PATH}/components ${HOMEKIT_PATH}/components/homekit ${CMAKE_CURRENT_LIST_DIR}/../common)

set(PROJECT_NAME rebel-espresso-firmware)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(${PROJECT_NAME} VERSION ${PROJECT_VERSION})


# Rename target binary file with correct versioning
set(FIRMWARE_TARGET_DIR ${CMAKE_BINARY_DIR}/firmware/${THING_TYPE}/r${HARDWARE_REVISION}/v${FIRMWARE_VERSION})
file(MAKE_DIRECTORY ${FIRMWARE_TARGET_DIR})

SET(CMAKE_CXX_STANDARD -std=c++14)
SET(CMAKE_C_STANDARD -std=c11)

# Set compile flags to give us something that will provide good perfomance
STRING(TOLOWER ${CMAKE_BUILD_TYPE} LOWERCASE_CMAKE_BUILD_TYPE)
IF (LOWERCASE_CMAKE_BUILD_TYPE MATCHES debug)
    SET(CMAKE_C_FLAGS   "-O0 -ffast-math ${CMAKE_C_FLAGS}")
    SET(CMAKE_CXX_FLAGS "-O0 -ffast-math ${CMAKE_CXX_FLAGS}")
ELSEIF (LOWERCASE_CMAKE_BUILD_TYPE MATCHES release)
    SET(CMAKE_C_FLAGS   "-O3 -ffast-math ${CMAKE_C_FLAGS}")
    SET(CMAKE_CXX_FLAGS "-O3 -ffast-math ${CMAKE_CXX_FLAGS}")
ELSE ()
    MESSAGE(FATAL_ERROR "Build type '${CMAKE_BUILD_TYPE}' is unknown")
ENDIF ()

MESSAGE(STATUS "Using CFLAGS ${CMAKE_C_FLAGS}")
MESSAGE(STATUS "Using CXXFLAGS ${CMAKE_CXX_FLAGS}")

# Rename target binary file with Hardware Revision, Firmware version and build type.
SET(FINAL_FILE "${PROJECT_NAME}-r${HARDWARE_REVISION}-v${FIRMWARE_VERSION}-${GIT_SHORT_HASH}-${CMAKE_BUILD_TYPE}.bin")
add_custom_command(OUTPUT
        ${CMAKE_BINARY_DIR}/${FINAL_FILE}
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin" "${CMAKE_BINARY_DIR}/${FINAL_FILE}"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/*firmware.bin" "${FIRMWARE_TARGET_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/ota*.bin" "${FIRMWARE_TARGET_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/partition_table/*.bin" "${FIRMWARE_TARGET_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/bootloader/*.bin" "${FIRMWARE_TARGET_DIR}"
        DEPENDS app bootloader 
        )

# Create target which consume the command via DEPENDS.
add_custom_target(firmware ALL DEPENDS ${CMAKE_BINARY_DIR}/${FINAL_FILE})

