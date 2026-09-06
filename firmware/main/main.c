#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "db_storage.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "ha_core.h"
#include "lvgl.h"
#include "native_handoff.h"
#include "nearby_board.h"
#include "nearby_lvgl_port.h"
#include "nearby_recognition_db.h"
#include "nearby_scan_pipeline.h"
#include "nearby_semantic_apply.h"
#include "nearby_ui.h"
#include "nearby_web_portal.h"
#include "radio_runtime.h"
#include "scan_session.h"
#include "web_mgmt.h"

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

#define OWNER_QUEUE_LEN 8u
#define OWNER_TASK_STACK 8192u
#define OWNER_TASK_PRIORITY 4u
#define PRODUCT_SCAN_MODULES 7u

static const char *TAG = "nearby-product";
static nearby_board_lcd_handles_t s_lcd;
static QueueHandle_t s_owner_queue;

typedef enum {
    OWNER_CMD_SCAN = 0,
    OWNER_CMD_PORTAL_START,
    OWNER_CMD_PORTAL_STOP,
    OWNER_CMD_SERVICE,
} owner_command_type_t;

typedef struct {
    owner_command_type_t type;
    char entity_id[HA_ENTITY_ID_LEN];
    char service[HA_SERVICE_LEN];
} owner_command_t;

static void copy_text(char *dst, size_t cap, const char *src)
{
    if (cap == 0) return;
    (void)snprintf(dst, cap, "%s", src ? src : "");
}

static void log_runtime_mark(const char *label, int64_t started_us)
{
    const int64_t now_us = esp_timer_get_time();
    ESP_LOGI(TAG,
             "%s: elapsed=%" PRId64 " us free_heap=%" PRIu32 " stack_hwm=%u words",
             label, now_us - started_us, esp_get_free_heap_size(),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
}

static esp_err_t db_validate_finish(const char *part_path, size_t total_bytes, void *ctx)
{
    (void)ctx;
    nearby_db_file_info_t info;
    const nearby_db_result_t result = nearby_db_validate_release_file(part_path, &info);
    if (result != NEARBY_DB_OK) {
        ESP_LOGE(TAG, "DB finalize validation failed: %d", (int)result);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (info.file_size != total_bytes) return ESP_ERR_INVALID_SIZE;
    ESP_LOGI(TAG, "DB upload verified: version=%" PRIu32 " bytes=%" PRIu64,
             info.db_version, info.file_size);
    return ESP_OK;
}

static void update_db_status(void)
{
    char status[48];
    esp_err_t mount_err = nearby_board_sd_mount(false);
    if (mount_err != ESP_OK) {
        copy_text(status, sizeof(status), "Missing");
    } else {
        struct stat st;
        if (stat(NEARBY_DB_FINAL_PATH, &st) != 0) {
            copy_text(status, sizeof(status), "Missing");
        } else {
            nearby_db_file_info_t info;
            nearby_db_result_t result = nearby_db_validate_release_file(NEARBY_DB_FINAL_PATH, &info);
            if (result == NEARBY_DB_OK) {
                (void)snprintf(status, sizeof(status), "Ready · v%" PRIu32, info.db_version);
            } else {
                copy_text(status, sizeof(status), "Invalid");
            }
        }
    }

    const esp_app_desc_t *app = esp_app_get_description();
    nearby_ui_set_versions(status, app != NULL ? app->version : "unknown");
}

static void update_sta_status(void)
{
    nearby_web_mgmt_status_t status;
    if (nearby_web_mgmt_get_status(&status) != ESP_OK) {
        nearby_ui_set_sta_status(NEARBY_UI_STA_DISCONNECTED, NULL);
        return;
    }

    nearby_ui_sta_state_t ui_state = NEARBY_UI_STA_DISCONNECTED;
    switch (status.sta_state) {
    case NEARBY_WEB_STA_CONNECTING:
        ui_state = NEARBY_UI_STA_CONNECTING;
        break;
    case NEARBY_WEB_STA_CONNECTED:
        ui_state = NEARBY_UI_STA_CONNECTED;
        break;
    case NEARBY_WEB_STA_FAILED:
        ui_state = NEARBY_UI_STA_FAILED;
        break;
    case NEARBY_WEB_STA_DISCONNECTED:
    default:
        break;
    }
    nearby_ui_set_sta_status(ui_state, status.sta_ipv4);
}

static bool queue_owner_command(owner_command_type_t type,
                                const char *entity_id,
                                const char *service)
{
    if (s_owner_queue == NULL) return false;
    owner_command_t command = {.type = type};
    copy_text(command.entity_id, sizeof(command.entity_id), entity_id);
    copy_text(command.service, sizeof(command.service), service);
    return xQueueSend(s_owner_queue, &command, 0) == pdTRUE;
}

static void ui_scan_requested(void *ctx)
{
    (void)ctx;
    (void)queue_owner_command(OWNER_CMD_SCAN, NULL, NULL);
}

static void ui_portal_start_requested(void *ctx)
{
    (void)ctx;
    (void)queue_owner_command(OWNER_CMD_PORTAL_START, NULL, NULL);
}

static void ui_portal_stop_requested(void *ctx)
{
    (void)ctx;
    (void)queue_owner_command(OWNER_CMD_PORTAL_STOP, NULL, NULL);
}

static void ui_service_requested(const char *entity_id, const char *service, void *ctx)
{
    (void)ctx;
    (void)queue_owner_command(OWNER_CMD_SERVICE, entity_id, service);
}

static void owner_start_scan(void)
{
    if (nearby_ui_scan_state() == NEARBY_UI_SCAN_SCANNING ||
        scan_session_is_active() || scan_session_competing_op_is_active()) {
        ESP_LOGW(TAG, "Scan rejected: product gate busy");
        return;
    }

    /* Fresh nearby generation: runtime HA data never survives a new scan. */
    ha_core_reset();
    nearby_ui_refresh_from_ha();
    nearby_ui_scan_begin(PRODUCT_SCAN_MODULES);

    esp_err_t err = nearby_scan_pipeline_request();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Scan request failed: %s", esp_err_to_name(err));
        nearby_ui_scan_end();
    }
}

static void owner_start_portal(void)
{
    if (nearby_ui_scan_state() == NEARBY_UI_SCAN_SCANNING || scan_session_is_active()) {
        ESP_LOGW(TAG, "Web Management rejected while scanning");
        nearby_ui_set_portal_info(false, NULL, NULL, NULL);
        return;
    }

    esp_err_t err = nearby_web_mgmt_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Web Management start failed: %s", esp_err_to_name(err));
        nearby_ui_set_portal_info(false, NULL, NULL, NULL);
        return;
    }

    err = nearby_web_portal_register(nearby_web_mgmt_httpd());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Static Web Portal registration failed: %s", esp_err_to_name(err));
        (void)nearby_web_mgmt_stop();
        nearby_ui_set_portal_info(false, NULL, NULL, NULL);
        return;
    }

    nearby_web_mgmt_status_t status;
    if (nearby_web_mgmt_get_status(&status) == ESP_OK) {
        nearby_ui_set_portal_info(true,
                                  status.ssid,
                                  status.ap_ipv4[0] ? status.ap_ipv4 : "192.168.4.1",
                                  status.password);
    }
    update_sta_status();
}

static void owner_stop_portal(void)
{
    if (scan_session_competing_op_is_active()) {
        esp_err_t err = nearby_web_mgmt_stop();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Web Management stop failed: %s", esp_err_to_name(err));
        }
    }
    nearby_ui_set_portal_info(false, NULL, NULL, NULL);
    update_sta_status();
    update_db_status();
}

static void owner_call_service(const owner_command_t *command)
{
    if (command == NULL || command->entity_id[0] == '\0' || command->service[0] == '\0' ||
        nearby_ui_scan_state() == NEARBY_UI_SCAN_SCANNING) {
        return;
    }
    if (!ha_entity_call_service(command->entity_id, command->service, NULL, 0)) {
        ESP_LOGW(TAG, "HA service rejected: %s %s", command->entity_id, command->service);
    }
    nearby_ui_refresh_from_ha();
}

static void owner_handle_command(const owner_command_t *command)
{
    if (command == NULL) return;
    switch (command->type) {
    case OWNER_CMD_SCAN:
        owner_start_scan();
        break;
    case OWNER_CMD_PORTAL_START:
        owner_start_portal();
        break;
    case OWNER_CMD_PORTAL_STOP:
        owner_stop_portal();
        break;
    case OWNER_CMD_SERVICE:
        owner_call_service(command);
        break;
    default:
        break;
    }
}

static void owner_handle_pipeline_event(const nearby_pipeline_event_t *event)
{
    if (event == NULL) return;
    switch (event->type) {
    case NEARBY_PIPELINE_EVENT_MATCHED:
        if (nearby_semantic_apply_to_ha(&event->matched)) {
            nearby_ui_refresh_from_ha();
        } else {
            ESP_LOGW(TAG, "Matched semantic could not fit HA runtime pools");
        }
        break;
    case NEARBY_PIPELINE_EVENT_MODULE_TERMINAL:
        nearby_ui_scan_module_complete();
        break;
    case NEARBY_PIPELINE_EVENT_SCAN_DONE:
        if (!scan_session_is_active()) {
            nearby_ui_scan_end();
            nearby_ui_refresh_from_ha();
        } else {
            ESP_LOGE(TAG, "SCAN_DONE arrived before D released the scan gate; touch remains locked");
        }
        if (event->error != ESP_OK) {
            ESP_LOGW(TAG, "Scan completed with module error: %s", esp_err_to_name(event->error));
        }
        break;
    case NEARBY_PIPELINE_EVENT_FATAL:
        ESP_LOGE(TAG, "Scan fatal cleanup error: %s", esp_err_to_name(event->error));
        if (!scan_session_is_active()) {
            nearby_ui_scan_end();
            nearby_ui_refresh_from_ha();
        } else {
            /* Frozen D rule: an unrecovered fatal gate must not be hidden by UX unlock. */
            ESP_LOGE(TAG, "D scan gate remains held after fatal recovery; touch stays locked");
        }
        break;
    default:
        break;
    }
}

static void product_owner_task(void *arg)
{
    (void)arg;
    ha_core_reset();

    ESP_ERROR_CHECK(nearby_lvgl_port_init(&s_lcd));
    const nearby_ui_callbacks_t callbacks = {
        .scan_requested = ui_scan_requested,
        .portal_start_requested = ui_portal_start_requested,
        .portal_stop_requested = ui_portal_stop_requested,
        .service_requested = ui_service_requested,
        .ctx = NULL,
    };
    nearby_ui_init(&callbacks);
    ESP_ERROR_CHECK(nearby_scan_pipeline_init());

    /* Non-destructive only: missing/invalid SD/DB never prevents the product UI boot. */
    update_db_status();
    update_sta_status();
    ESP_LOGI(TAG, "Product owner ready: boot is empty + idle; waiting for explicit Scan");

    int64_t next_sta_refresh_us = 0;
    for (;;) {
        owner_command_t command;
        while (xQueueReceive(s_owner_queue, &command, 0) == pdTRUE) {
            owner_handle_command(&command);
        }

        nearby_pipeline_event_t event;
        while (nearby_scan_pipeline_receive(&event, 0)) {
            owner_handle_pipeline_event(&event);
        }

        const int64_t now_us = esp_timer_get_time();
        if (now_us >= next_sta_refresh_us) {
            update_sta_status();
            next_sta_refresh_us = now_us + 500000;
        }

        /* LVGL callbacks execute here, on the same task that owns all HA access. */
        (void)lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
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
            ESP_LOGE(TAG, "BENCH_REQUIRED radio cleanup failed: %s", esp_err_to_name(cleanup_err));
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
    const int64_t started_us = esp_timer_get_time();
    ESP_LOGI(TAG, "NearBy One NEXT production boot");
    ESP_LOGI(TAG, "boot free_heap=%" PRIu32, esp_get_free_heap_size());

    ESP_ERROR_CHECK(scan_session_init());
    ESP_ERROR_CHECK(nearby_wifi_handoff_init());
    ESP_ERROR_CHECK(nearby_ble_handoff_init());
    ESP_ERROR_CHECK(nearby_i154_handoff_init());

    const nearby_db_validation_hooks_t db_hooks = {
        .on_finish = db_validate_finish,
        .ctx = NULL,
    };
    ESP_ERROR_CHECK(nearby_web_mgmt_set_db_validation_hooks(&db_hooks));

    memset(&s_lcd, 0, sizeof(s_lcd));
    ESP_ERROR_CHECK(nearby_board_lcd_init(&s_lcd));
    log_runtime_mark("ST7789V2 LCD init", started_us);

#if CONFIG_NEARBY_AGENT_D_BOARD_IO_SMOKE
    ESP_ERROR_CHECK(board_io_bench_smoke());
#endif

#if CONFIG_NEARBY_AGENT_D_RADIO_SMOKE
    ESP_ERROR_CHECK(radio_bench_smoke());
#endif

#ifdef NEARBY_AGENT_D_HAS_EXTRA_BENCH
    ESP_ERROR_CHECK(agent_d_bench_run(&s_lcd));
#endif

    s_owner_queue = xQueueCreate(OWNER_QUEUE_LEN, sizeof(owner_command_t));
    ESP_ERROR_CHECK(s_owner_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    BaseType_t created = xTaskCreate(product_owner_task,
                                     "nearby_owner",
                                     OWNER_TASK_STACK,
                                     NULL,
                                     OWNER_TASK_PRIORITY,
                                     NULL);
    ESP_ERROR_CHECK(created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
