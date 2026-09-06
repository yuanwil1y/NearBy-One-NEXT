#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
esp_err_t scan_session_begin(TickType_t timeout_ticks);
esp_err_t scan_session_end(void);
esp_err_t scan_session_require_scan_owner(void);
uint32_t scan_session_generation(void);
esp_err_t scan_session_fatal_error(void);
