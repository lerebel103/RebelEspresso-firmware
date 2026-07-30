cmake_minimum_required(VERSION 3.16)

# Only include specific components needed for testing — avoid pulling in the
# full dependency tree (AWS IoT, HomeKit, WiFi, BLE, etc.)
set(COMPONENTS
    main
    freertos
    esp_event
    nvs_flash
    esp_system
    esp_common
    esp_rom
    esp_hw_support
    heap
    log
    soc
    hal
    esp_timer
    driver
    cjson
    unity
    partition_table
    spi_flash
    bootloader_support
    esptool_py
    bootloader
    esp_partition
    app_update
    esp_app_format
)

set(IDF_TARGET esp32)
set(PROJECT_NAME rebel-espresso-tests)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(${PROJECT_NAME})
