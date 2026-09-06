#include <inttypes.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "native_handoff.h"
#include "nearby_board.h"
#include "radio_runtime.h"
#include "scan_session.h"

#if defined(CONFIG_NEARBY_AGENT_D_LCD_VISUAL_BENCH) || \
    defined(CONFIG_NEARBY_AGENT_D_TOUCH_BENCH) || \
    defined(CONFIG_NEARBY_AGENT_D_SD_IO_BENCH) || \
    defined(CONFIG_NEARBY_AGENT_D_SD_DESTRUCTIVE_BENCH) || \
    defined(CONFIG_NEARBY_AGENT_D_WEB_BENCH) || \
    defined(CONFIG_NEARBY_AGENT_D_SPI_CONTENTION_BENCH) || \
    defined(CONFIG_NEARBY_AGENT_D_STRESS_BENCH)
#include "agent_d_bench.h"
#define NEARBY_AGENT_D_HAS_EXTRA_BENCH 1
#endif

static const char *TAG = "nearby-hw-audit";

static void log_runtime_mark(const char *label, int64_t started_us)
{
    const int64_t now_us = esp_timer_get_time();
    ESP_LOGI(TAG,
             "%s: elapsed=%" PRId64 " us free_heap=%" PRIu32 " stack_hwm=%u words",
             label, now_us - started_us, esp_get_free_heap_size(),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
}

static esp_err_t scan_session_smoke_test(void)
{
    esp_err_t err = scan_session_begin(0);
    if (err != ESP_OK) return err;
    if (!scan_session_is_active()) return ESP_FAIL;

    if (scan_session_competing_op_try_begin() != ESP_ERR_INVALID_STATE) return ESP_FAIL;

    /* Fatal cleanup is a hard gate: end must fail until recovery clears it. */
    err = scan_session_mark_fatal(ESP_FAIL);
    if (err != ESP_OK || scan_session_fatal_error() != ESP_FAIL) return ESP_FAIL;
    if (scan_session_end() != ESP_ERR_INVALID_STATE || !scan_session_is_active()) return ESP_FAIL;
    err = scan_session_recovery_clear_fatal();
    if (err != ESP_OK) return err;

    /* Exercise owner-aware cleanup on the recovered happy path. */
    err = scan_session_cleanup_owned();
    if (err != ESP_OK || scan_session_is_active()) return ESP_FAIL;

    err = scan_session_competing_op_try_begin();
    if (err != ESP_OK || !scan_session_competing_op_is_active()) return ESP_FAIL;
    if (scan_session_begin(0) != ESP_ERR_INVALID_STATE) return ESP_FAIL;

    err = scan_session_cleanup_owned();
    if (err != ESP_OK || scan_session_competing_op_is_active()) return ESP_FAIL;
    return ESP_OK;
}

#if CONFIG_NEARBY_AGENT_D_RADIO_SMOKE
static esp_err_t radio_bench_smoke(void)
{
    ESP_LOGW(TAG, "BENCH_REQUIRED: executing real shared-RF lifecycle smoke");
    esp_err_t err = scan_session_begin(pdMS_TO_TICKS(1000));
    if (err != ESP_OK) return err;

    wifi_ap_record_t aps[8];
    uint16_t ap_count = 8;
    int64_t started_us = esp_timer_get_time();

    err = nearby_wifi_active_scan(aps, &ap_count, true);
    if (err != ESP_OK) goto cleanup;
    ESP_LOGI(TAG, "Wi-Fi active scan returned %u AP records", ap_count);
    log_runtime_mark("Wi-Fi active scan", started_us);

    started_us = esp_timer_get_time();
    err = nearby_wifi_promiscuous_start(1, 0);
    if (err != ESP_OK) goto cleanup;
    vTaskDelay(pdMS_TO_TICKS(250));
    err = nearby_wifi_promiscuous_stop();
    if (err != ESP_OK) goto cleanup;
    err = nearby_wifi_driver_deinit();
    if (err != ESP_OK) goto cleanup;
    log_runtime_mark("Wi-Fi promiscuous start/stop/deinit", started_us);

    started_us = esp_timer_get_time();
    err = nearby_nimble_driver_init(pdMS_TO_TICKS(5000));
    if (err != ESP_OK) goto cleanup;
    err = nearby_nimble_scan_start(1500, true);
    if (err != ESP_OK) goto cleanup;
    vTaskDelay(pdMS_TO_TICKS(1750));
    err = nearby_nimble_scan_stop();
    if (err != ESP_OK) goto cleanup;
    err = nearby_nimble_driver_deinit();
    if (err != ESP_OK) goto cleanup;
    log_runtime_mark("NimBLE GAP scan/start/stop/deinit", started_us);

    started_us = esp_timer_get_time();
    err = nearby_i154_receive_start(11);
    if (err != ESP_OK) goto cleanup;
    vTaskDelay(pdMS_TO_TICKS(500));
    err = nearby_i154_receive_stop();
    if (err != ESP_OK) goto cleanup;
    log_runtime_mark("802.15.4 promiscuous receive start/stop", started_us);

cleanup: {
        esp_err_t cleanup_err = nearby_radio_scan_cleanup_all();
        if (cleanup_err != ESP_OK) {
            ESP_LOGE(TAG, "radio cleanup failed; scan gate intentionally remains held: %s",
                     esp_err_to_name(cleanup_err));
            return cleanup_err;
        }
        esp_err_t end_err = scan_session_end();
        return err != ESP_OK ? err : end_err;
    }
}
#endif

#if CONFIG_NEARBY_AGENT_D_BOARD_IO_SMOKE
static esp_err_t board_io_bench_smoke(void)
{
    ESP_LOGW(TAG, "BENCH_REQUIRED: probing CST816 and shared-SPI SD without formatting");

    esp_err_t err = nearby_board_cst816_init();
    if (err != ESP_OK) return err;
    nearby_board_touch_sample_t sample;
    err = nearby_board_cst816_read(&sample);
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG, "CST816 raw pressed=%d x=%u y=%u", sample.pressed, sample.x, sample.y);

    err = nearby_board_sd_mount(false);
    if (err != ESP_OK) return err;
    sdmmc_card_t *card = nearby_board_sd_card();
    if (card != NULL) {
        ESP_LOGI(TAG, "SD mounted sector_size=%u capacity_sectors=%u",
                 card->csd.sector_size, card->csd.capacity);
    }
    return ESP_OK;
}
#endif

void app_main(void)
{
    int64_t started_us = esp_timer_get_time();
    ESP_LOGI(TAG, "NearBy One NEXT Agent-D hardware bring-up");
    ESP_LOGI(TAG, "boot free_heap=%" PRIu32, esp_get_free_heap_size());

    ESP_ERROR_CHECK(scan_session_init());
    ESP_ERROR_CHECK(scan_session_smoke_test());
    log_runtime_mark("scan-session owner/fatal-recovery smoke", started_us);

    started_us = esp_timer_get_time();
    ESP_ERROR_CHECK(nearby_wifi_handoff_init());
    ESP_ERROR_CHECK(nearby_ble_handoff_init());
    ESP_ERROR_CHECK(nearby_i154_handoff_init());
    log_runtime_mark("native handoff queues/callback registration", started_us);

    nearby_board_lcd_handles_t lcd = {0};
    started_us = esp_timer_get_time();
    ESP_ERROR_CHECK(nearby_board_lcd_init(&lcd));
    log_runtime_mark("ST7789V2 LCD init", started_us);

#if CONFIG_NEARBY_AGENT_D_BOARD_IO_SMOKE
    ESP_ERROR_CHECK(board_io_bench_smoke());
#endif

#if CONFIG_NEARBY_AGENT_D_RADIO_SMOKE
    ESP_ERROR_CHECK(radio_bench_smoke());
#endif

#ifdef NEARBY_AGENT_D_HAS_EXTRA_BENCH
    ESP_ERROR_CHECK(agent_d_bench_run(&lcd));
#endif

#if MYNEWT_VAL(BLE_EXT_ADV)
    ESP_LOGI(TAG,
             "primitives ready; drops wifi=%" PRIu32 " ble_legacy=%" PRIu32
             " ble_ext=%" PRIu32 " i154=%" PRIu32,
             nearby_wifi_rx_drop_count(), nearby_ble_rx_drop_count(),
             nearby_ble_ext_rx_drop_count(), nearby_i154_rx_drop_count());
#else
    ESP_LOGI(TAG,
             "primitives ready; drops wifi=%" PRIu32 " ble=%" PRIu32 " i154=%" PRIu32,
             nearby_wifi_rx_drop_count(), nearby_ble_rx_drop_count(), nearby_i154_rx_drop_count());
#endif
}
