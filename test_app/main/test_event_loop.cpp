/**
 * ESP Event Loop tests.
 *
 * Tests the esp_event system behavior that the project relies on heavily.
 * All components register handlers on MACHINE_EVENTS and use the default
 * event loop. This verifies handler registration, posting, and delivery.
 *
 * Critical for migration: esp_event API must behave identically post-upgrade.
 */
#include <unity.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_event.h>

// Define a test event base
ESP_EVENT_DEFINE_BASE(TEST_EVENTS);

enum test_event_id_t {
    TEST_EVENT_A = 0,
    TEST_EVENT_B,
    TEST_EVENT_C,
};

// Counters for handler invocations
static int s_event_a_count = 0;
static int s_event_b_count = 0;
static int s_event_data_received = 0;

static void handler_a(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (id == TEST_EVENT_A) {
        s_event_a_count++;
    }
}

static void handler_b(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (id == TEST_EVENT_B) {
        s_event_b_count++;
    }
}

static void handler_with_data(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (id == TEST_EVENT_C && data != NULL) {
        s_event_data_received = *(int *)data;
    }
}

TEST_CASE("Event: handler receives posted event", "[event]") {
    s_event_a_count = 0;

    ESP_ERROR_CHECK(esp_event_handler_register(TEST_EVENTS, TEST_EVENT_A, handler_a, NULL));

    ESP_ERROR_CHECK(esp_event_post(TEST_EVENTS, TEST_EVENT_A, NULL, 0, portMAX_DELAY));
    vTaskDelay(pdMS_TO_TICKS(50));

    TEST_ASSERT_EQUAL(1, s_event_a_count);

    ESP_ERROR_CHECK(esp_event_handler_unregister(TEST_EVENTS, TEST_EVENT_A, handler_a));
}

TEST_CASE("Event: multiple handlers for different events", "[event]") {
    s_event_a_count = 0;
    s_event_b_count = 0;

    ESP_ERROR_CHECK(esp_event_handler_register(TEST_EVENTS, TEST_EVENT_A, handler_a, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(TEST_EVENTS, TEST_EVENT_B, handler_b, NULL));

    ESP_ERROR_CHECK(esp_event_post(TEST_EVENTS, TEST_EVENT_A, NULL, 0, portMAX_DELAY));
    ESP_ERROR_CHECK(esp_event_post(TEST_EVENTS, TEST_EVENT_B, NULL, 0, portMAX_DELAY));
    ESP_ERROR_CHECK(esp_event_post(TEST_EVENTS, TEST_EVENT_A, NULL, 0, portMAX_DELAY));
    vTaskDelay(pdMS_TO_TICKS(50));

    TEST_ASSERT_EQUAL(2, s_event_a_count);
    TEST_ASSERT_EQUAL(1, s_event_b_count);

    ESP_ERROR_CHECK(esp_event_handler_unregister(TEST_EVENTS, TEST_EVENT_A, handler_a));
    ESP_ERROR_CHECK(esp_event_handler_unregister(TEST_EVENTS, TEST_EVENT_B, handler_b));
}

TEST_CASE("Event: event data is passed correctly", "[event]") {
    s_event_data_received = 0;

    ESP_ERROR_CHECK(esp_event_handler_register(TEST_EVENTS, TEST_EVENT_C, handler_with_data, NULL));

    int payload = 12345;
    ESP_ERROR_CHECK(esp_event_post(TEST_EVENTS, TEST_EVENT_C, &payload, sizeof(payload), portMAX_DELAY));
    vTaskDelay(pdMS_TO_TICKS(50));

    TEST_ASSERT_EQUAL(12345, s_event_data_received);

    ESP_ERROR_CHECK(esp_event_handler_unregister(TEST_EVENTS, TEST_EVENT_C, handler_with_data));
}

TEST_CASE("Event: unregistered handler does not fire", "[event]") {
    s_event_a_count = 0;

    ESP_ERROR_CHECK(esp_event_handler_register(TEST_EVENTS, TEST_EVENT_A, handler_a, NULL));
    ESP_ERROR_CHECK(esp_event_handler_unregister(TEST_EVENTS, TEST_EVENT_A, handler_a));

    ESP_ERROR_CHECK(esp_event_post(TEST_EVENTS, TEST_EVENT_A, NULL, 0, portMAX_DELAY));
    vTaskDelay(pdMS_TO_TICKS(50));

    TEST_ASSERT_EQUAL(0, s_event_a_count);
}

TEST_CASE("Event: event group bits manipulation", "[event]") {
    // Tests FreeRTOS event group — used extensively by project components
    EventGroupHandle_t eg = xEventGroupCreate();
    TEST_ASSERT_NOT_NULL(eg);

    const EventBits_t BIT_0 = (1 << 0);
    const EventBits_t BIT_1 = (1 << 1);
    const EventBits_t BIT_2 = (1 << 2);

    // Initially all clear
    TEST_ASSERT_EQUAL(0, xEventGroupGetBits(eg));

    // Set bits
    xEventGroupSetBits(eg, BIT_0 | BIT_2);
    EventBits_t bits = xEventGroupGetBits(eg);
    TEST_ASSERT_TRUE(bits & BIT_0);
    TEST_ASSERT_FALSE(bits & BIT_1);
    TEST_ASSERT_TRUE(bits & BIT_2);

    // Clear a bit
    xEventGroupClearBits(eg, BIT_0);
    bits = xEventGroupGetBits(eg);
    TEST_ASSERT_FALSE(bits & BIT_0);
    TEST_ASSERT_TRUE(bits & BIT_2);

    vEventGroupDelete(eg);
}

TEST_CASE("Event: FreeRTOS task delay timing", "[event]") {
    // Verify basic FreeRTOS timing works (critical after PicolibC migration)
    TickType_t start = xTaskGetTickCount();
    vTaskDelay(pdMS_TO_TICKS(100));
    TickType_t elapsed = xTaskGetTickCount() - start;

    // Should be approximately 10 ticks (at 100Hz), allow ±2 tolerance
    TEST_ASSERT_INT_WITHIN(2, 10, (int)elapsed);
}
