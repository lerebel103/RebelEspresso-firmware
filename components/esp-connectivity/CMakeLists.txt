cmake_minimum_required(VERSION 3.16)

set(COMPONENT_SRCDIRS
        src/
        src/common
        src/wifi
        src/sntp
        )

set(COMPONENT_ADD_INCLUDEDIRS
        src/
        )

set(COMPONENT_REQUIRES
        freertos
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        esp_app_format
        lwip
        esp_timer
        )

idf_component_register(
        SRC_DIRS ${COMPONENT_SRCDIRS}
        REQUIRES ${COMPONENT_REQUIRES}
        INCLUDE_DIRS ${COMPONENT_ADD_INCLUDEDIRS}
)

target_compile_options(${COMPONENT_LIB} PRIVATE "-include" "freertos/FreeRTOS.h")
