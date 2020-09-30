IF (PROJECT_SOURCE_DIR)
    CONFIGURE_FILE("${CMAKE_SOURCE_DIR}/main/version.h.in" "${CMAKE_CURRENT_SOURCE_DIR}/_generated/version.h")
ENDIF()

file(GLOB_RECURSE HARDWARE_SPECIFIC_SRCS
        hw/r${HARDWARE_REVISION}/*.h
        hw/r${HARDWARE_REVISION}/*.c
        hw/r${HARDWARE_REVISION}/*.cpp
        )


file(GLOB_RECURSE COMPONENT_SRCS
        events.h
        state.h
        state.cpp
        thing_info.h
        thing_info.cpp
        main.cpp

        control/controller.cpp
        control/controller.h
        control/process_loop.cpp
        control/process_loop.h
        control/vtb_pid.h
        control/vtb_pid.cpp
        control/actuate.cpp
        control/actuate.h

        hw/rtds.cpp
        hw/rtds.h
        hw/oled/display.cpp
        hw/oled/display.h
        hw/oled/u8g2_esp32_hal.c
        hw/oled/u8g2_esp32_hal.h

        sys/mqtt.cpp
        sys/mqtt.h
        sys/nvram_store.cpp
        sys/nvram_store.h
        sys/ota.cpp
        sys/ota.h
        sys/sntp.cpp
        sys/sntp.h
        sys/wifi_connect.cpp
        sys/wifi_connect.h
        sys/homekit.cpp
        sys/homekit.h

        ${HARDWARE_SPECIFIC_SRCS}
        )


set(COMPONENT_ADD_INCLUDEDIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/"
        "${CMAKE_CURRENT_SOURCE_DIR}/_generated/"
        "${CMAKE_CURRENT_SOURCE_DIR}/hw/r${HARDWARE_REVISION}"

        "${CMAKE_SOURCE_DIR}/components/u8g2/csrc"
       )

set(COMPONENT_REQUIRES
        json
        nvs_flash

        app_update
        esp-tls
        esp-google-iot
        esp32-homekit
        ESP32-MAX31865
        )

register_component()


add_definitions(
        -DARDUINO=102
)


