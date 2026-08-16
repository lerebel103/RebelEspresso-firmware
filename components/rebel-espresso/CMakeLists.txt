cmake_minimum_required(VERSION 3.16)

IF (PROJECT_SOURCE_DIR)
    CONFIGURE_FILE("${CMAKE_CURRENT_SOURCE_DIR}/version.h.in" "${CMAKE_CURRENT_SOURCE_DIR}/_generated/version.h")
ENDIF()

set(COMPONENT_SRCDIRS
        src/
        src/utils
        src/runtime
        src/machine
        src/comms
        src/comms/webserver
        src/comms/homekit
        src/comms/mqtt
        src/device
        )

set(COMPONENT_REQUIRES
        cjson
        nvs_flash
        esp-connectivity

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
        mdns
        mbedtls
        esp_wifi
        tft-driver
        esp-ssr-controller
        espressif__mqtt
        )

set(COMPONENT_ADD_INCLUDEDIRS
        "."
        "./src"
        "./src/utils"
        "./_generated/"
        "./src/runtime"
        "./src/machine"
        "./src/comms"
        "./src/comms/webserver"
        "./src/comms/homekit"
        "./src/comms/mqtt"
        "./src/device"
        )

# The web UI is gzipped into the build directory and embedded in the firmware binary.
set(WEBAPP_HTML "${CMAKE_SOURCE_DIR}/webapp/index.html")
set(WEBAPP_GZ "${CMAKE_BINARY_DIR}/generated/webapp/index.html.gz")

idf_component_register(
        SRC_DIRS ${COMPONENT_SRCDIRS}
        REQUIRES ${COMPONENT_REQUIRES}
        INCLUDE_DIRS ${COMPONENT_ADD_INCLUDEDIRS}
        EMBED_FILES ${WEBAPP_GZ}
)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    add_custom_command(
            OUTPUT "${WEBAPP_GZ}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/generated/webapp"
            COMMAND ${CMAKE_COMMAND}
                    -DINPUT_FILE=${WEBAPP_HTML}
                    -DOUTPUT_FILE=${WEBAPP_GZ}
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/gzip_webapp.cmake"
            DEPENDS "${WEBAPP_HTML}"
            COMMENT "Compressing web UI for firmware embedding"
            VERBATIM
    )

    add_custom_target(webapp_gzip DEPENDS "${WEBAPP_GZ}")
    add_dependencies(${COMPONENT_LIB} webapp_gzip)
endif()
