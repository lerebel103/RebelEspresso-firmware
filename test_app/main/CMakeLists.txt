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
        "test_safety_interlocks.cpp"
        "test_io_scan.cpp"
        "test_process_image.cpp"
        "test_descale.cpp"
        "test_water_probe.cpp"
        "test_mqtt_config.cpp"
        # Source files under test (pulled from the main project)
        "${CMAKE_CURRENT_LIST_DIR}/../../components/rebel-espresso/src/utils/pid.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/rebel-espresso/src/machine/boiler_temp.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/rebel-espresso/src/machine/boiler_refill_states.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/rebel-espresso/src/machine/setpoint_selector.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/rebel-espresso/src/machine/water_probe.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/rebel-espresso/src/comms/mqtt/mqtt_config.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/rebel-espresso/src/runtime/process_image.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/rebel-espresso/src/runtime/io_scan_safety.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/rebel-espresso/src/runtime/io_scan_modes.cpp"
        # nvram_store.cpp has a dependency on HomeKit config for init — provide a
        # stripped version for tests that only includes the get/set functions.
        "nvram_store_test_shim.cpp"
        # Safety test shims (mock SSR, out_signals, app_metrics, brew_temp, esp_restart)
        "ssr_ctrl_test_shim.cpp"
        "safety_test_shims.cpp"
    INCLUDE_DIRS
        "."
        "${CMAKE_CURRENT_LIST_DIR}/../../components/rebel-espresso/src"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/rebel-espresso/src/utils"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/rebel-espresso/src/runtime"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/rebel-espresso/src/machine"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/rebel-espresso/src/comms"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/rebel-espresso/src/device"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/rebel-espresso"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/esp-connectivity/src"
        "${CMAKE_CURRENT_LIST_DIR}/../../components/esp-ssr-controller/src"
    REQUIRES
        unity nvs_flash esp_event cjson driver esp_timer esp_driver_spi esp_adc
    WHOLE_ARCHIVE
)

# Wrap esp_restart so safety tests can intercept restart calls without
# affecting normal panic handler behavior. The wrapper only intercepts
# when test code explicitly arms it.
target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_restart")
