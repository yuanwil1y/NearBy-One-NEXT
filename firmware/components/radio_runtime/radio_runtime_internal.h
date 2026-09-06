#pragma once

#include "esp_err.h"

typedef enum {
    NEARBY_RADIO_PHASE_NONE = 0,
    NEARBY_RADIO_PHASE_WIFI,
    NEARBY_RADIO_PHASE_NIMBLE,
    NEARBY_RADIO_PHASE_I154,
} nearby_radio_phase_t;

esp_err_t nearby_radio_phase_claim(nearby_radio_phase_t phase);
esp_err_t nearby_radio_phase_release(nearby_radio_phase_t phase);
nearby_radio_phase_t nearby_radio_phase_current(void);
