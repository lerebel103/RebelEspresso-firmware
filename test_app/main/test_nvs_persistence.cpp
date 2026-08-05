/**
 * NVS persistence tests.
 *
 * Tests the nvram_store abstraction layer and verifies that values survive
 * save/load cycles. This exercises the NVS flash subsystem which is fully
 * emulated in QEMU.
 *
 * Critical for migration: NVS API behavior must remain consistent.
 */
#include <unity.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "utils/nvram_store.h"

#define TEST_NVS_NAMESPACE "test_nvs"

static void _erase_test_namespace() {
    nvs_handle_t handle;
    if (nvs_open(TEST_NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

TEST_CASE("NVS: u8 store and retrieve", "[nvs]") {
    _erase_test_namespace();

    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open(TEST_NVS_NAMESPACE, NVS_READWRITE, &handle));

    uint8_t default_val = 42;
    uint8_t value = 0;

    // First read — should return default
    nvram_store_get_u8(handle, "test_u8", &value, &default_val);
    TEST_ASSERT_EQUAL(42, value);

    // Write a new value
    uint8_t new_val = 99;
    nvram_store_set_u8(handle, "test_u8", &new_val);

    // Read back
    value = 0;
    nvram_store_get_u8(handle, "test_u8", &value, &default_val);
    TEST_ASSERT_EQUAL(99, value);

    nvs_close(handle);
}

TEST_CASE("NVS: u16 store and retrieve", "[nvs]") {
    _erase_test_namespace();

    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open(TEST_NVS_NAMESPACE, NVS_READWRITE, &handle));

    uint16_t default_val = 1000;
    uint16_t value = 0;

    nvram_store_get_u16(handle, "test_u16", &value, &default_val);
    TEST_ASSERT_EQUAL(1000, value);

    uint16_t new_val = 54321;
    nvram_store_set_u16(handle, "test_u16", &new_val);

    value = 0;
    nvram_store_get_u16(handle, "test_u16", &value, &default_val);
    TEST_ASSERT_EQUAL(54321, value);

    nvs_close(handle);
}

TEST_CASE("NVS: u32 store and retrieve", "[nvs]") {
    _erase_test_namespace();

    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open(TEST_NVS_NAMESPACE, NVS_READWRITE, &handle));

    uint32_t default_val = 100000;
    uint32_t value = 0;

    nvram_store_get_u32(handle, "test_u32", &value, &default_val);
    TEST_ASSERT_EQUAL(100000, value);

    uint32_t new_val = 3141592;
    nvram_store_set_u32(handle, "test_u32", &new_val);

    value = 0;
    nvram_store_get_u32(handle, "test_u32", &value, &default_val);
    TEST_ASSERT_EQUAL(3141592, value);

    nvs_close(handle);
}

TEST_CASE("NVS: u64 store and retrieve (used for doubles)", "[nvs]") {
    _erase_test_namespace();

    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open(TEST_NVS_NAMESPACE, NVS_READWRITE, &handle));

    // Store a double as u64 (this is how PID parameters are persisted)
    double original = 3.14159265;
    double default_val = 0.0;
    double restored = 0.0;

    nvram_store_set_u64(handle, "test_dbl", (uint64_t *)&original);

    nvram_store_get_u64(handle, "test_dbl", (uint64_t *)&restored, &default_val);
    TEST_ASSERT_EQUAL_DOUBLE(original, restored);

    nvs_close(handle);
}

TEST_CASE("NVS: missing key returns default value", "[nvs]") {
    _erase_test_namespace();

    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open(TEST_NVS_NAMESPACE, NVS_READWRITE, &handle));

    uint32_t default_val = 777;
    uint32_t value = 0;

    esp_err_t err = nvram_store_get_u32(handle, "nonexistent", &value, &default_val);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(777, value);

    nvs_close(handle);
}

TEST_CASE("NVS: string store and retrieve", "[nvs]") {
    _erase_test_namespace();

    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open(TEST_NVS_NAMESPACE, NVS_READWRITE, &handle));

    // Schedules stores JSON as a string in NVS
    const char *json_str = "{\"en\":true,\"mon\":[{\"en\":true,\"start\":\"06:30\",\"stop\":\"22:00\"}]}";
    ESP_ERROR_CHECK(nvs_set_str(handle, "sched_json", json_str));
    ESP_ERROR_CHECK(nvs_commit(handle));

    // Read back
    size_t len = 0;
    ESP_ERROR_CHECK(nvs_get_str(handle, "sched_json", NULL, &len));
    TEST_ASSERT_GREATER_THAN(0, len);

    char *buffer = (char *)malloc(len);
    ESP_ERROR_CHECK(nvs_get_str(handle, "sched_json", buffer, &len));
    TEST_ASSERT_EQUAL_STRING(json_str, buffer);

    free(buffer);
    nvs_close(handle);
}

TEST_CASE("NVS: erase and verify key is gone", "[nvs]") {
    _erase_test_namespace();

    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open(TEST_NVS_NAMESPACE, NVS_READWRITE, &handle));

    uint8_t val = 55;
    nvram_store_set_u8(handle, "ephemeral", &val);

    // Verify it's there
    uint8_t read_val = 0;
    uint8_t default_val = 0;
    nvram_store_get_u8(handle, "ephemeral", &read_val, &default_val);
    TEST_ASSERT_EQUAL(55, read_val);

    // Erase all
    nvs_erase_all(handle);
    nvs_commit(handle);

    // Should now return default
    read_val = 0;
    nvram_store_get_u8(handle, "ephemeral", &read_val, &default_val);
    TEST_ASSERT_EQUAL(0, read_val);

    nvs_close(handle);
}
