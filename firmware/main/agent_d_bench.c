#include "agent_d_bench.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "radio_runtime.h"
#include "scan_session.h"
#include "web_mgmt.h"

static const char *TAG = "agent-d-bench";

#define BENCH_SD_BYTES       (1024U * 1024U)
#define BENCH_SD_CHUNK       4096U
#define BENCH_LCD_ROWS       8U
#define BENCH_STRESS_CYCLES  100U
#define BENCH_WEB_CYCLES     20U

#if defined(CONFIG_NEARBY_AGENT_D_SD_IO_BENCH) || defined(CONFIG_NEARBY_AGENT_D_SPI_CONTENTION_BENCH)
static uint8_t s_io_buffer[BENCH_SD_CHUNK] __attribute__((aligned(4)));
static void fill_io_pattern(void)
{
    for (size_t i = 0; i < sizeof(s_io_buffer); ++i) {
        s_io_buffer[i] = (uint8_t)((i * 31U + 7U) & 0xffU);
    }
}
#endif

#ifdef CONFIG_NEARBY_AGENT_D_SD_IO_BENCH
static uint8_t s_read_buffer[BENCH_SD_CHUNK] __attribute__((aligned(4)));
#endif

#if defined(CONFIG_NEARBY_AGENT_D_LCD_VISUAL_BENCH) || defined(CONFIG_NEARBY_AGENT_D_SPI_CONTENTION_BENCH)
static uint16_t s_lcd_buffer[NEARBY_BOARD_LCD_H_RES * BENCH_LCD_ROWS];

static uint16_t lcd_rgb565_wire_word(uint16_t color)
{
    return (uint16_t)((color << 8) | (color >> 8));
}

static esp_err_t lcd_draw_rect(esp_lcd_panel_handle_t panel,
                               int x0,
                               int y0,
                               int x1,
                               int y1,
                               uint16_t color)
{
    if (panel == NULL || x0 < 0 || y0 < 0 || x1 <= x0 || y1 <= y0 ||
        x1 > NEARBY_BOARD_LCD_H_RES || y1 > NEARBY_BOARD_LCD_V_RES) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t width = (size_t)(x1 - x0);
    for (int y = y0; y < y1; ) {
        const int rows = (y1 - y) > (int)BENCH_LCD_ROWS ? (int)BENCH_LCD_ROWS : (y1 - y);
        const size_t pixels = width * (size_t)rows;
        if (pixels > sizeof(s_lcd_buffer) / sizeof(s_lcd_buffer[0])) {
            return ESP_ERR_INVALID_SIZE;
        }
        for (size_t i = 0; i < pixels; ++i) s_lcd_buffer[i] = lcd_rgb565_wire_word(color);
        esp_err_t err = esp_lcd_panel_draw_bitmap(panel, x0, y, x1, y + rows, s_lcd_buffer);
        if (err != ESP_OK) return err;
        y += rows;
    }
    return ESP_OK;
}
#endif

static void __attribute__((unused)) bench_log_resources(const char *label)
{
    ESP_LOGI(TAG,
             "%s free_heap=%" PRIu32 " min_free_heap=%" PRIu32 " stack_hwm=%u words",
             label,
             esp_get_free_heap_size(),
             esp_get_minimum_free_heap_size(),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
}

#ifdef CONFIG_NEARBY_AGENT_D_LCD_VISUAL_BENCH
static esp_err_t lcd_visual_bench(const nearby_board_lcd_handles_t *lcd)
{
    if (lcd == NULL || lcd->panel == NULL) return ESP_ERR_INVALID_ARG;
    ESP_LOGW(TAG, "BENCH_REQUIRED LCD: verify portrait 170x320, all four corners, color order and inversion");

    static const uint16_t colors[] = {
        0xf800, /* red */
        0x07e0, /* green */
        0x001f, /* blue */
        0xffff, /* white */
        0x0000, /* black */
        0x8410, /* approximately 50% gray */
        0x055e, /* NB_BLUE #03A9F4 converted to RGB565 */
    };
    ESP_LOGW(TAG, "LCD bands top->bottom: RED GREEN BLUE WHITE BLACK GRAY NB_BLUE(#03A9F4)");
    const int band_height = NEARBY_BOARD_LCD_V_RES / (int)(sizeof(colors) / sizeof(colors[0]));
    for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); ++i) {
        const int y0 = (int)i * band_height;
        const int y1 = i + 1 == sizeof(colors) / sizeof(colors[0])
                           ? NEARBY_BOARD_LCD_V_RES
                           : (int)(i + 1) * band_height;
        esp_err_t err = lcd_draw_rect(lcd->panel, 0, y0, NEARBY_BOARD_LCD_H_RES, y1, colors[i]);
        if (err != ESP_OK) return err;
    }

    const int marker = 8;
    esp_err_t err = lcd_draw_rect(lcd->panel, 0, 0, marker, marker, 0xffff);
    if (err == ESP_OK) err = lcd_draw_rect(lcd->panel, NEARBY_BOARD_LCD_H_RES - marker, 0,
                                            NEARBY_BOARD_LCD_H_RES, marker, 0xffff);
    if (err == ESP_OK) err = lcd_draw_rect(lcd->panel, 0, NEARBY_BOARD_LCD_V_RES - marker,
                                            marker, NEARBY_BOARD_LCD_V_RES, 0xffff);
    if (err == ESP_OK) err = lcd_draw_rect(lcd->panel,
                                            NEARBY_BOARD_LCD_H_RES - marker,
                                            NEARBY_BOARD_LCD_V_RES - marker,
                                            NEARBY_BOARD_LCD_H_RES,
                                            NEARBY_BOARD_LCD_V_RES,
                                            0xffff);
    bench_log_resources("LCD visual pattern queued");
    return err;
}
#endif

#ifdef CONFIG_NEARBY_AGENT_D_TOUCH_BENCH
static esp_err_t touch_bench(void)
{
    ESP_LOGW(TAG, "BENCH_REQUIRED touch: tap all four corners during the next 30 seconds");
    esp_err_t err = nearby_board_cst816_init();
    if (err != ESP_OK) return err;

    uint16_t min_x = UINT16_MAX, min_y = UINT16_MAX, max_x = 0, max_y = 0;
    uint32_t samples = 0;
    const int64_t deadline = esp_timer_get_time() + 30LL * 1000LL * 1000LL;
    while (esp_timer_get_time() < deadline) {
        nearby_board_touch_sample_t sample;
        err = nearby_board_cst816_read(&sample);
        if (err != ESP_OK) return err;
        if (sample.pressed) {
            if (sample.x < min_x) min_x = sample.x;
            if (sample.x > max_x) max_x = sample.x;
            if (sample.y < min_y) min_y = sample.y;
            if (sample.y > max_y) max_y = sample.y;
            ++samples;
            ESP_LOGI(TAG, "touch raw x=%u y=%u", sample.x, sample.y);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_LOGI(TAG, "touch summary samples=%" PRIu32 " x=[%u,%u] y=[%u,%u]",
             samples,
             samples ? min_x : 0,
             samples ? max_x : 0,
             samples ? min_y : 0,
             samples ? max_y : 0);
    bench_log_resources("touch bench complete");
    return samples > 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}
#endif

#ifdef CONFIG_NEARBY_AGENT_D_SD_IO_BENCH
static esp_err_t sd_write_read_once(const char *path, size_t total_bytes, bool remove_after)
{
    fill_io_pattern();
    FILE *file = fopen(path, "wb");
    if (file == NULL) return ESP_FAIL;

    const int64_t write_start = esp_timer_get_time();
    size_t written = 0;
    while (written < total_bytes) {
        const size_t remaining = total_bytes - written;
        const size_t chunk = remaining < sizeof(s_io_buffer) ? remaining : sizeof(s_io_buffer);
        if (fwrite(s_io_buffer, 1, chunk, file) != chunk) {
            (void)fclose(file);
            return ESP_FAIL;
        }
        written += chunk;
    }
    if (fflush(file) != 0 || fsync(fileno(file)) != 0 || fclose(file) != 0) return ESP_FAIL;
    const int64_t write_us = esp_timer_get_time() - write_start;

    file = fopen(path, "rb");
    if (file == NULL) return ESP_FAIL;
    const int64_t read_start = esp_timer_get_time();
    size_t read_total = 0;
    while (read_total < total_bytes) {
        const size_t remaining = total_bytes - read_total;
        const size_t chunk = remaining < sizeof(s_read_buffer) ? remaining : sizeof(s_read_buffer);
        if (fread(s_read_buffer, 1, chunk, file) != chunk ||
            memcmp(s_read_buffer, s_io_buffer, chunk) != 0) {
            (void)fclose(file);
            return ESP_FAIL;
        }
        read_total += chunk;
    }
    if (fclose(file) != 0) return ESP_FAIL;
    const int64_t read_us = esp_timer_get_time() - read_start;

    ESP_LOGI(TAG,
             "SD IO bytes=%u write_us=%" PRId64 " write_KiB_s=%" PRIu64
             " read_us=%" PRId64 " read_KiB_s=%" PRIu64,
             (unsigned)total_bytes,
             write_us,
             write_us > 0 ? ((uint64_t)total_bytes * 1000000ULL / (uint64_t)write_us) / 1024ULL : 0,
             read_us,
             read_us > 0 ? ((uint64_t)total_bytes * 1000000ULL / (uint64_t)read_us) / 1024ULL : 0);

    if (remove_after) (void)unlink(path);
    return ESP_OK;
}

static esp_err_t sd_io_bench(void)
{
    ESP_LOGW(TAG, "BENCH_REQUIRED SD: running bounded non-destructive write/read verification");
    const bool was_mounted = nearby_board_sd_is_mounted();
    esp_err_t err = nearby_board_sd_mount(false);
    if (err != ESP_OK) return err;
    err = sd_write_read_once(NEARBY_BOARD_SD_MOUNT_POINT "/nearby-agent-d-io.bin", BENCH_SD_BYTES, true);
    if (!was_mounted) {
        const esp_err_t unmount_err = nearby_board_sd_unmount();
        if (err == ESP_OK) err = unmount_err;
    }
    bench_log_resources("SD IO bench complete");
    return err;
}
#endif

#ifdef CONFIG_NEARBY_AGENT_D_SD_DESTRUCTIVE_BENCH
static esp_err_t sd_destructive_bench(void)
{
    ESP_LOGW(TAG, "BENCH_REQUIRED DESTRUCTIVE: whole SD card will be reformatted now");
    const int64_t started = esp_timer_get_time();
    const esp_err_t err = nearby_board_sd_format_whole(true);
    ESP_LOGI(TAG, "whole-SD format result=%s elapsed_us=%" PRId64,
             esp_err_to_name(err), esp_timer_get_time() - started);
    bench_log_resources("whole-SD format complete");
    return err;
}
#endif

#ifdef CONFIG_NEARBY_AGENT_D_WEB_BENCH
static esp_err_t web_bench(void)
{
    ESP_LOGW(TAG, "BENCH_REQUIRED SoftAP/HTTP: phone join and API test window is 120 seconds");
    const int64_t started = esp_timer_get_time();
    esp_err_t err = nearby_web_mgmt_start();
    if (err != ESP_OK) return err;

    nearby_web_mgmt_status_t status;
    err = nearby_web_mgmt_get_status(&status);
    if (err != ESP_OK) {
        (void)nearby_web_mgmt_stop();
        return err;
    }
    ESP_LOGI(TAG, "SoftAP ssid=%s password=*** ip=%s", status.ssid, status.ap_ipv4);
    bench_log_resources("SoftAP/HTTP started");
    vTaskDelay(pdMS_TO_TICKS(120000));
    err = nearby_web_mgmt_stop();
    ESP_LOGI(TAG, "SoftAP/HTTP stop result=%s elapsed_us=%" PRId64,
             esp_err_to_name(err), esp_timer_get_time() - started);
    bench_log_resources("SoftAP/HTTP stopped");
    return err;
}
#endif

#ifdef CONFIG_NEARBY_AGENT_D_SPI_CONTENTION_BENCH
typedef struct {
    SemaphoreHandle_t done;
    esp_err_t result;
    int64_t elapsed_us;
    UBaseType_t stack_hwm;
} sd_worker_result_t;

static sd_worker_result_t s_sd_worker;
static StaticSemaphore_t s_sd_done_storage;

static void sd_contention_worker(void *arg)
{
    sd_worker_result_t *result = (sd_worker_result_t *)arg;
    fill_io_pattern();
    FILE *file = fopen(NEARBY_BOARD_SD_MOUNT_POINT "/nearby-agent-d-contention.bin", "wb");
    if (file == NULL) {
        result->result = ESP_FAIL;
        result->stack_hwm = uxTaskGetStackHighWaterMark(NULL);
        xSemaphoreGive(result->done);
        vTaskDelete(NULL);
        return;
    }

    const int64_t started = esp_timer_get_time();
    size_t written = 0;
    result->result = ESP_OK;
    while (written < BENCH_SD_BYTES) {
        size_t chunk = BENCH_SD_BYTES - written;
        if (chunk > sizeof(s_io_buffer)) chunk = sizeof(s_io_buffer);
        if (fwrite(s_io_buffer, 1, chunk, file) != chunk) {
            result->result = ESP_FAIL;
            break;
        }
        written += chunk;
    }
    if (result->result == ESP_OK &&
        (fflush(file) != 0 || fsync(fileno(file)) != 0)) {
        result->result = ESP_FAIL;
    }
    (void)fclose(file);
    result->elapsed_us = esp_timer_get_time() - started;
    result->stack_hwm = uxTaskGetStackHighWaterMark(NULL);
    xSemaphoreGive(result->done);
    vTaskDelete(NULL);
}

static esp_err_t spi_contention_bench(const nearby_board_lcd_handles_t *lcd)
{
    if (lcd == NULL || lcd->panel == NULL) return ESP_ERR_INVALID_ARG;
    ESP_LOGW(TAG, "BENCH_REQUIRED SPI2: simultaneous LCD flush calls and SD write");

    const bool was_mounted = nearby_board_sd_is_mounted();
    esp_err_t err = nearby_board_sd_mount(false);
    if (err != ESP_OK) return err;

    memset(&s_sd_worker, 0, sizeof(s_sd_worker));
    s_sd_worker.done = xSemaphoreCreateBinaryStatic(&s_sd_done_storage);
    if (s_sd_worker.done == NULL) {
        if (!was_mounted) (void)nearby_board_sd_unmount();
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(sd_contention_worker, "sd-contention", 4096, &s_sd_worker, 4, NULL) != pdPASS) {
        if (!was_mounted) (void)nearby_board_sd_unmount();
        return ESP_ERR_NO_MEM;
    }

    int64_t max_draw_call_us = 0;
    uint32_t draw_calls = 0;
    uint16_t color = 0x001f;
    bool worker_done = false;
    while (!worker_done) {
        worker_done = xSemaphoreTake(s_sd_worker.done, 0) == pdTRUE;
        if (worker_done) break;

        const int y = (int)((draw_calls * BENCH_LCD_ROWS) % NEARBY_BOARD_LCD_V_RES);
        const int y1 = y + (int)BENCH_LCD_ROWS <= NEARBY_BOARD_LCD_V_RES
                           ? y + (int)BENCH_LCD_ROWS
                           : NEARBY_BOARD_LCD_V_RES;
        const int64_t started = esp_timer_get_time();
        err = lcd_draw_rect(lcd->panel, 0, y, NEARBY_BOARD_LCD_H_RES, y1, color);
        const int64_t elapsed = esp_timer_get_time() - started;
        if (elapsed > max_draw_call_us) max_draw_call_us = elapsed;
        if (err != ESP_OK) break;
        color ^= 0xffff;
        ++draw_calls;
        vTaskDelay(1);
    }

    if (!worker_done) {
        (void)xSemaphoreTake(s_sd_worker.done, portMAX_DELAY);
    }
    (void)unlink(NEARBY_BOARD_SD_MOUNT_POINT "/nearby-agent-d-contention.bin");

    ESP_LOGI(TAG,
             "SPI contention lcd_draw_calls=%" PRIu32 " max_draw_call_us=%" PRId64
             " sd_result=%s sd_elapsed_us=%" PRId64 " sd_stack_hwm=%u words",
             draw_calls, max_draw_call_us, esp_err_to_name(s_sd_worker.result),
             s_sd_worker.elapsed_us, (unsigned)s_sd_worker.stack_hwm);
    bench_log_resources("SPI contention complete");

    if (!was_mounted) {
        const esp_err_t unmount_err = nearby_board_sd_unmount();
        if (err == ESP_OK) err = unmount_err;
    }
    if (err != ESP_OK) return err;
    return s_sd_worker.result;
}
#endif

#ifdef CONFIG_NEARBY_AGENT_D_STRESS_BENCH
static esp_err_t stress_bench(void)
{
    ESP_LOGW(TAG, "BENCH_REQUIRED stress: %u radio/scan/SD cycles + %u Web cycles",
             BENCH_STRESS_CYCLES, BENCH_WEB_CYCLES);
    bench_log_resources("stress begin");

    for (uint32_t i = 0; i < BENCH_STRESS_CYCLES; ++i) {
        esp_err_t err = scan_session_begin(pdMS_TO_TICKS(1000));
        if (err != ESP_OK) return err;

        err = nearby_wifi_driver_init();
        if (err == ESP_OK) err = nearby_wifi_driver_deinit();
        if (err == ESP_OK) err = nearby_nimble_driver_init(pdMS_TO_TICKS(5000));
        if (err == ESP_OK) err = nearby_nimble_driver_deinit();
        if (err == ESP_OK) err = nearby_i154_receive_start(11);
        if (err == ESP_OK) err = nearby_i154_receive_stop();

        const esp_err_t cleanup_err = nearby_radio_scan_cleanup_all();
        if (cleanup_err != ESP_OK) return cleanup_err;
        const esp_err_t end_err = scan_session_end();
        if (err != ESP_OK) return err;
        if (end_err != ESP_OK) return end_err;

        err = nearby_board_sd_mount(false);
        if (err != ESP_OK) return err;
        err = nearby_board_sd_unmount();
        if (err != ESP_OK) return err;

        if ((i + 1U) % 10U == 0U) {
            ESP_LOGI(TAG, "stress progress cycle=%" PRIu32 "/%u", i + 1U, BENCH_STRESS_CYCLES);
            bench_log_resources("stress checkpoint");
        }
    }

    for (uint32_t i = 0; i < BENCH_WEB_CYCLES; ++i) {
        esp_err_t err = nearby_web_mgmt_start();
        if (err != ESP_OK) return err;
        err = nearby_web_mgmt_stop();
        if (err != ESP_OK) return err;
    }

    bench_log_resources("stress complete");
    return ESP_OK;
}
#endif

esp_err_t agent_d_bench_run(const nearby_board_lcd_handles_t *lcd)
{
#ifdef CONFIG_NEARBY_AGENT_D_LCD_VISUAL_BENCH
    esp_err_t err = lcd_visual_bench(lcd);
    if (err != ESP_OK) return err;
#endif
#ifdef CONFIG_NEARBY_AGENT_D_TOUCH_BENCH
    esp_err_t touch_err = touch_bench();
    if (touch_err != ESP_OK) return touch_err;
#endif
#ifdef CONFIG_NEARBY_AGENT_D_SD_IO_BENCH
    esp_err_t sd_err = sd_io_bench();
    if (sd_err != ESP_OK) return sd_err;
#endif
#ifdef CONFIG_NEARBY_AGENT_D_SD_DESTRUCTIVE_BENCH
    esp_err_t format_err = sd_destructive_bench();
    if (format_err != ESP_OK) return format_err;
#endif
#ifdef CONFIG_NEARBY_AGENT_D_SPI_CONTENTION_BENCH
    esp_err_t contention_err = spi_contention_bench(lcd);
    if (contention_err != ESP_OK) return contention_err;
#endif
#ifdef CONFIG_NEARBY_AGENT_D_WEB_BENCH
    esp_err_t web_err = web_bench();
    if (web_err != ESP_OK) return web_err;
#endif
#ifdef CONFIG_NEARBY_AGENT_D_STRESS_BENCH
    esp_err_t stress_err = stress_bench();
    if (stress_err != ESP_OK) return stress_err;
#endif

    (void)lcd;
    return ESP_OK;
}
