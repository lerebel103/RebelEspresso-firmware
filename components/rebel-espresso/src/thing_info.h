#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

struct thing_info_ext_t {

    /**
     * Thing type
     */
    char thing_type[32];

    /**
     * Major Hardware revision
     */
    uint8_t hardware_version_major;

    /**
     * Minor Hardware revision
     */
    uint8_t hardware_version_minor;

    /**
     * Serial number
     */
    uint64_t serial;

    /**
     * Manufacturer ID.
     * Maps to a manufacturer
     */
    uint16_t manufacturer_id;

    /**
     * Build/Assembly datetime as Unix Timestamp (epoch seconds)
     */
    uint64_t build_epoch_s;

};


void thing_info_init();

/**
 * Unique identifier for this device.
 * @return
 */
const char *thing_info_id();

const char* thing_info_hardware_revision();

/**
 * Extended device information
 */
const thing_info_ext_t* thing_info_ext();

#ifdef __cplusplus
}
#endif
