idf_component_register(
    SRCS
        "test_main.cpp"
        "test_pid.cpp"
        "test_json_config.cpp"
        "test_nvs_persistence.cpp"
        "test_event_loop.cpp"
        "test_config_roundtrip.cpp"
        "test_dns_server.cpp"
        "test_auth_bypass.cpp"
        "test_wifi_credentials.cpp"
        "test_wifi_state_machine.cpp"
        "test_ap_fallback_timer.cpp"
        # Source files under test (pulled from the main project)
        "${CMAKE_CURRENT_LIST_DIR}/../../components/rebel-espresso/src/utils/pid.cpp"
        # nvram_store.cpp has a dependency on HomeKit config for init — provide a
        # stripped version for tests that only includes the get/set functions.
        "nvram_store_test_shim.cpp"
    INCLUDE_DIRS
        "."
        "${CMAKE_CURRENT_LIST_DIR}/../../components/rebel-espresso/src"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/rebel-espresso/src/utils"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/rebel-espresso/src/sys"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/rebel-espresso/src/hw/base"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/rebel-espresso/src/hw/r2/src"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/rebel-espresso"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/esp-connectivity/src"
    REQUIRES
        unity nvs_flash esp_event cjson driver esp_timer esp_driver_spi esp_adc
    WHOLE_ARCHIVE
)
