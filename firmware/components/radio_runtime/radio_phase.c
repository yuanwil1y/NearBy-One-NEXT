#include "radio_runtime_internal.h"

#include "freertos/FreeRTOS.h"

static nearby_radio_phase_t s_phase;
static portMUX_TYPE s_phase_lock = portMUX_INITIALIZER_UNLOCKED;

esp_err_t nearby_radio_phase_claim(nearby_radio_phase_t phase)
{
    if (phase == NEARBY_RADIO_PHASE_NONE) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = ESP_OK;
    portENTER_CRITICAL(&s_phase_lock);
    if (s_phase == NEARBY_RADIO_PHASE_NONE) {
        s_phase = phase;
    } else if (s_phase != phase) {
        result = ESP_ERR_INVALID_STATE;
    }
    portEXIT_CRITICAL(&s_phase_lock);
    return result;
}

esp_err_t nearby_radio_phase_release(nearby_radio_phase_t phase)
{
    if (phase == NEARBY_RADIO_PHASE_NONE) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = ESP_OK;
    portENTER_CRITICAL(&s_phase_lock);
    if (s_phase != phase) {
        result = ESP_ERR_INVALID_STATE;
    } else {
        s_phase = NEARBY_RADIO_PHASE_NONE;
    }
    portEXIT_CRITICAL(&s_phase_lock);
    return result;
}

nearby_radio_phase_t nearby_radio_phase_current(void)
{
    nearby_radio_phase_t phase;
    portENTER_CRITICAL(&s_phase_lock);
    phase = s_phase;
    portEXIT_CRITICAL(&s_phase_lock);
    return phase;
}
