#include "scan_session.h"

#include "freertos/semphr.h"
#include "freertos/task.h"

static StaticSemaphore_t s_gate_storage;
static SemaphoreHandle_t s_gate;
static TaskHandle_t s_scan_owner;
static TaskHandle_t s_competing_owner;
static uint32_t s_generation;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;

static TaskHandle_t current_task(void)
{
    return xTaskGetCurrentTaskHandle();
}

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
    s_scan_owner = current_task();
    s_competing_owner = NULL;
    ++s_generation;
    portEXIT_CRITICAL(&s_state_lock);
    return ESP_OK;
}

esp_err_t scan_session_require_scan_owner(void)
{
    TaskHandle_t owner;
    portENTER_CRITICAL(&s_state_lock);
    owner = s_scan_owner;
    portEXIT_CRITICAL(&s_state_lock);
    return owner != NULL && owner == current_task() ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t scan_session_end(void)
{
    if (s_gate == NULL || scan_session_require_scan_owner() != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    portENTER_CRITICAL(&s_state_lock);
    s_scan_owner = NULL;
    portEXIT_CRITICAL(&s_state_lock);
    return xSemaphoreGive(s_gate) == pdTRUE ? ESP_OK : ESP_ERR_INVALID_STATE;
}

bool scan_session_is_active(void)
{
    bool active;
    portENTER_CRITICAL(&s_state_lock);
    active = s_scan_owner != NULL;
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
    if (xSemaphoreTake(s_gate, 0) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    portENTER_CRITICAL(&s_state_lock);
    s_competing_owner = current_task();
    s_scan_owner = NULL;
    portEXIT_CRITICAL(&s_state_lock);
    return ESP_OK;
}

bool scan_session_competing_op_is_active(void)
{
    bool active;
    portENTER_CRITICAL(&s_state_lock);
    active = s_competing_owner != NULL;
    portEXIT_CRITICAL(&s_state_lock);
    return active;
}

esp_err_t scan_session_require_competing_active(void)
{
    return scan_session_competing_op_is_active() ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t scan_session_require_competing_owner(void)
{
    TaskHandle_t owner;
    portENTER_CRITICAL(&s_state_lock);
    owner = s_competing_owner;
    portEXIT_CRITICAL(&s_state_lock);
    return owner != NULL && owner == current_task() ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t scan_session_competing_op_end(void)
{
    if (s_gate == NULL || scan_session_require_competing_owner() != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    portENTER_CRITICAL(&s_state_lock);
    s_competing_owner = NULL;
    portEXIT_CRITICAL(&s_state_lock);
    return xSemaphoreGive(s_gate) == pdTRUE ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t scan_session_cleanup_owned(void)
{
    if (scan_session_require_scan_owner() == ESP_OK) {
        return scan_session_end();
    }
    if (scan_session_require_competing_owner() == ESP_OK) {
        return scan_session_competing_op_end();
    }
    return ESP_ERR_INVALID_STATE;
}
