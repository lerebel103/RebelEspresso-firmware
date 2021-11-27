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
        src/utils
        src/sys
        src/hw/base
        src/hw/r${HARDWARE_REVISION}/src
        src/hw/r${HARDWARE_REVISION}/src/display
        src/homekit
        )

set(COMPONENT_REQUIRES
        mbedtls
        json
        nvs_flash

        app_update
        console
        esp-tls
        esp-google-iot
        esp_adc_cal
        esp_hap_core
        esp_hap_apple_profiles
        spiffs
        wifi_provisioning
        qrcode
        ESP32-MAX31865
        u8g2
        tft-driver
        )

set(COMPONENT_ADD_INCLUDEDIRS
        "."
        "./src"
        "./src/utils"
        "./_generated/"
        "./src/hw/base/"
        "./src/hw/r${HARDWARE_REVISION}/src"
        "../u8g2/csrc"
        )

idf_component_register(
        SRC_DIRS ${COMPONENT_SRCDIRS}
        REQUIRES ${COMPONENT_REQUIRES}
        INCLUDE_DIRS ${COMPONENT_ADD_INCLUDEDIRS}
)
