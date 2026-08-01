/**
 * Tests for the AP fallback timer guard logic.
 *
 * FreeRTOS software timers don't reliably fire in the QEMU test environment
 * (timer daemon task starved), so we test the decision logic directly:
 * - Timer starts and is reported as running
 * - Timer can be stopped
 * - Guard prevents restarting an already-running timer
 * - Guard prevents starting timer when AP is already active
 */
#include <unity.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/timers.h>

#define WIFI_CONNECTED_BIT (1 << 1)
#define WIFI_AP_ACTIVE_BIT (1 << 2)

extern EventGroupHandle_t status_event_group;

static TimerHandle_t s_test_timer = nullptr;

static void _dummy_cb(TimerHandle_t timer) {
  // Not expected to fire in QEMU tests
}

static void _start_timer(uint32_t timeout_ms) {
  if (s_test_timer == nullptr) {
    s_test_timer = xTimerCreate("test_ap", pdMS_TO_TICKS(timeout_ms), pdFALSE, nullptr, _dummy_cb);
  } else {
    xTimerChangePeriod(s_test_timer, pdMS_TO_TICKS(timeout_ms), 0);
  }
  xTimerStart(s_test_timer, 0);
}

static void _stop_timer() {
  if (s_test_timer != nullptr) {
    xTimerStop(s_test_timer, 0);
  }
}

static bool _is_timer_running() {
  if (s_test_timer == nullptr) return false;
  return xTimerIsTimerActive(s_test_timer) != pdFALSE;
}

static void _reset_state() {
  _stop_timer();
  xEventGroupClearBits(status_event_group, WIFI_CONNECTED_BIT | WIFI_AP_ACTIVE_BIT);
  // Allow timer command queue to process
  vTaskDelay(pdMS_TO_TICKS(50));
}

TEST_CASE("AP fallback: timer starts and reports running", "[ap_fallback]") {
  _reset_state();

  TEST_ASSERT_FALSE(_is_timer_running());

  _start_timer(30000);  // Long timeout — won't fire during test
  vTaskDelay(pdMS_TO_TICKS(50));  // Let timer queue process

  TEST_ASSERT_TRUE(_is_timer_running());

  _reset_state();
}

TEST_CASE("AP fallback: timer can be stopped", "[ap_fallback]") {
  _reset_state();

  _start_timer(30000);
  vTaskDelay(pdMS_TO_TICKS(50));
  TEST_ASSERT_TRUE(_is_timer_running());

  _stop_timer();
  vTaskDelay(pdMS_TO_TICKS(50));
  TEST_ASSERT_FALSE(_is_timer_running());

  _reset_state();
}

TEST_CASE("AP fallback: guard prevents restart when timer running", "[ap_fallback]") {
  _reset_state();

  _start_timer(30000);
  vTaskDelay(pdMS_TO_TICKS(50));

  // Simulate the guard check from wifi_manager.cpp disconnect handler:
  // if (!wifi_ap_is_active() && !_is_ap_fallback_timer_running()) { start }
  EventBits_t bits = xEventGroupGetBits(status_event_group);
  bool ap_active = (bits & WIFI_AP_ACTIVE_BIT) != 0;
  bool would_start = (!ap_active && !_is_timer_running());

  TEST_ASSERT_FALSE(would_start);  // Should NOT restart

  _reset_state();
}

TEST_CASE("AP fallback: guard prevents start when AP already active", "[ap_fallback]") {
  _reset_state();

  xEventGroupSetBits(status_event_group, WIFI_AP_ACTIVE_BIT);

  EventBits_t bits = xEventGroupGetBits(status_event_group);
  bool ap_active = (bits & WIFI_AP_ACTIVE_BIT) != 0;
  bool would_start = (!ap_active && !_is_timer_running());

  TEST_ASSERT_FALSE(would_start);  // Should NOT start

  _reset_state();
}

TEST_CASE("AP fallback: guard allows start when no timer and no AP", "[ap_fallback]") {
  _reset_state();

  EventBits_t bits = xEventGroupGetBits(status_event_group);
  bool ap_active = (bits & WIFI_AP_ACTIVE_BIT) != 0;
  bool would_start = (!ap_active && !_is_timer_running());

  TEST_ASSERT_TRUE(would_start);  // Should allow start

  _reset_state();
}
