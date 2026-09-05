#include "scan_session.h"

#include "freertos/semphr.h"
#include "freertos/task.h"

static StaticSemaphore_t s_gate_storage;
static SemaphoreHandle_t s_gate;
static TaskHandle_t s_scan_owner;
static uint32_t s_generation;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;

esp_err_t scan_session_init(void)
{
    if (s_gate != NULL) {
        return ESP_OK;
    }

    s_gate = xSemaphoreCreateMutexStatic(&s_gate_storage);
    return s_gate != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t scan_session_begin(TickType_t timeout_ticks)
{
    esp_err_t err = scan_session_init();
    if (err != ESP_OK) {
        return err;
    }

    if (xSemaphoreTake(s_gate, timeout_ticks) != pdTRUE) {
        return timeout_ticks == 0 ? ESP_ERR_INVALID_STATE : ESP_ERR_TIMEOUT;
    }

    portENTER_CRITICAL(&s_state_lock);
    s_scan_owner = xTaskGetCurrentTaskHandle();
    ++s_generation;
    portEXIT_CRITICAL(&s_state_lock);

    return ESP_OK;
}

esp_err_t scan_session_end(void)
{
    if (s_gate == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    TaskHandle_t owner;
    portENTER_CRITICAL(&s_state_lock);
    owner = s_scan_owner;
    portEXIT_CRITICAL(&s_state_lock);

    if (owner == NULL || owner != xTaskGetCurrentTaskHandle()) {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&s_state_lock);
    s_scan_owner = NULL;
    portEXIT_CRITICAL(&s_state_lock);

    if (xSemaphoreGive(s_gate) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

bool scan_session_is_active(void)
{
    bool active;
    portENTER_CRITICAL(&s_state_lock);
    active = (s_scan_owner != NULL);
    portEXIT_CRITICAL(&s_state_lock);
    return active;
}

uint32_t scan_session_generation(void)
{
    uint32_t generation;
    portENTER_CRITICAL(&s_state_lock);
    generation = s_generation;
    portEXIT_CRITICAL(&s_state_lock);
    return generation;
}

esp_err_t scan_session_competing_op_try_begin(void)
{
    esp_err_t err = scan_session_init();
    if (err != ESP_OK) {
        return err;
    }

    /* Non-blocking by contract: active scan means BUSY immediately. */
    if (xSemaphoreTake(s_gate, 0) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

esp_err_t scan_session_competing_op_end(void)
{
    if (s_gate == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return xSemaphoreGive(s_gate) == pdTRUE ? ESP_OK : ESP_ERR_INVALID_STATE;
}
