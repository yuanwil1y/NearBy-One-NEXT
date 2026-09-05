#include <inttypes.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "native_handoff.h"
#include "nearby_board.h"
#include "scan_session.h"

static const char *TAG = "nearby-hw-audit";

static void log_runtime_mark(const char *label, int64_t started_us)
{
    const int64_t now_us = esp_timer_get_time();
    ESP_LOGI(TAG,
             "%s: elapsed=%" PRId64 " us free_heap=%" PRIu32
             " stack_hwm=%u words",
             label,
             now_us - started_us,
             esp_get_free_heap_size(),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
}

static esp_err_t scan_session_smoke_test(void)
{
    esp_err_t err = scan_session_begin(0);
    if (err != ESP_OK) {
        return err;
    }

    if (!scan_session_is_active()) {
        (void)scan_session_end();
        return ESP_FAIL;
    }

    /* A device/UI RF operation must be rejected for the whole scan session. */
    err = scan_session_competing_op_try_begin();
    if (err != ESP_ERR_INVALID_STATE) {
        if (err == ESP_OK) {
            (void)scan_session_competing_op_end();
        }
        (void)scan_session_end();
        return ESP_FAIL;
    }

    err = scan_session_end();
    if (err != ESP_OK) {
        return err;
    }

    if (scan_session_is_active()) {
        return ESP_FAIL;
    }

    /* Once the complete scan ends, a standalone/device operation can proceed. */
    err = scan_session_competing_op_try_begin();
    if (err != ESP_OK) {
        return err;
    }

    return scan_session_competing_op_end();
}

void app_main(void)
{
    int64_t started_us = esp_timer_get_time();
    ESP_LOGI(TAG, "NearBy One NEXT Agent-D hardware first-pass bring-up");
    ESP_LOGI(TAG, "boot free_heap=%" PRIu32, esp_get_free_heap_size());

    ESP_ERROR_CHECK(scan_session_init());
    ESP_ERROR_CHECK(scan_session_smoke_test());
    log_runtime_mark("scan-session gate smoke test", started_us);

    started_us = esp_timer_get_time();
    ESP_ERROR_CHECK(nearby_wifi_handoff_init());
    log_runtime_mark("Wi-Fi handoff queues", started_us);

    started_us = esp_timer_get_time();
    ESP_ERROR_CHECK(nearby_ble_handoff_init());
    log_runtime_mark("NimBLE handoff queues", started_us);

    /* Must be registered before esp_ieee802154_enable(). */
    started_us = esp_timer_get_time();
    ESP_ERROR_CHECK(nearby_i154_handoff_init());
    log_runtime_mark("802.15.4 ISR handoff queues/callback", started_us);

    nearby_board_lcd_handles_t lcd = {0};
    started_us = esp_timer_get_time();
    ESP_ERROR_CHECK(nearby_board_lcd_init(&lcd));
    log_runtime_mark("ST7789V2 LCD init", started_us);

    ESP_LOGI(TAG,
             "first-pass primitives ready; drops wifi=%" PRIu32
             " ble=%" PRIu32 " i154=%" PRIu32,
             nearby_wifi_rx_drop_count(),
             nearby_ble_rx_drop_count(),
             nearby_i154_rx_drop_count());
}
