cmake_minimum_required(VERSION 3.16)

IF (PROJECT_SOURCE_DIR)
    CONFIGURE_FILE("${CMAKE_CURRENT_SOURCE_DIR}/version.h.in" "${CMAKE_CURRENT_SOURCE_DIR}/_generated/version.h")
ENDIF()

set(COMPONENT_SRCDIRS
        src/
        src/utils
        src/sys
        src/hw/base
        src/hw/r2/src
        src/hw/r2/src/display
        src/hw/r2/src/webserver
        src/homekit
        )

set(COMPONENT_REQUIRES
        cjson
        nvs_flash
        connectivity

        app_update
        console
        esp-tls
        driver
        esp_adc
        esp_driver_i2c
        esp_hap_core
        esp_hap_apple_profiles
        esp_rom
        spiffs
        esp_http_server
        fatfs
        mbedtls
        esp_wifi
        ESP32-MAX31865
        tft-driver
        esp-ssr-controller
        )

set(COMPONENT_ADD_INCLUDEDIRS
        "."
        "./src"
        "./src/utils"
        "./_generated/"
        "./src/hw/base/"
        "./src/hw/r2/src"
        )

idf_component_register(
        SRC_DIRS ${COMPONENT_SRCDIRS}
        REQUIRES ${COMPONENT_REQUIRES}
        INCLUDE_DIRS ${COMPONENT_ADD_INCLUDEDIRS}
)
