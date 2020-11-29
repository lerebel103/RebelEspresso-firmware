cmake_minimum_required(VERSION 3.5)

IF (PROJECT_SOURCE_DIR)
    CONFIGURE_FILE("${CMAKE_CURRENT_SOURCE_DIR}/version.h.in" "${CMAKE_CURRENT_SOURCE_DIR}/_generated/version.h")
ENDIF()

file(GLOB_RECURSE HARDWARE_SPECIFIC_SRCS
        hw/r${HARDWARE_REVISION}/*.h
        hw/r${HARDWARE_REVISION}/*.c
        hw/r${HARDWARE_REVISION}/*.cpp
        )


set(COMPONENT_SRCDIRS
        src/
        src/control
        src/sys
        src/hw
        src/hw/oled
        src/homekit
        )

set(COMPONENT_REQUIRES
        mbedtls
        json
        nvs_flash

        app_update
        esp-tls
        esp-google-iot
        esp_adc_cal
        ESP32-MAX31865
        u8g2
        port
        )

set(COMPONENT_ADD_INCLUDEDIRS
        "."
        "./src"
        "./_generated/"
        "./src/hw/r${HARDWARE_REVISION}"
        "../u8g2/csrc"
        )

set(COMPONENT_SRCEXCLUDE
        )

register_component()

add_definitions(
        -DARDUINO=102
)
