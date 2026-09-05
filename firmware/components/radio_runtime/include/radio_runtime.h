#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_netif.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Wi-Fi native driver lifecycle. No parser/recognition semantics live here. */
esp_err_t nearby_wifi_driver_init(void);
esp_err_t nearby_wifi_active_scan(wifi_ap_record_t *records,
                                  uint16_t *inout_count,
                                  bool show_hidden);
esp_err_t nearby_wifi_promiscuous_start(uint8_t channel, uint32_t filter_mask);
esp_err_t nearby_wifi_promiscuous_stop(void);
esp_err_t nearby_wifi_driver_deinit(void);
bool nearby_wifi_driver_is_started(void);
esp_netif_t *nearby_wifi_sta_netif(void);

/* Session-oriented configuration helpers used by temporary Web Management. */
esp_err_t nearby_wifi_set_mode(wifi_mode_t mode);
esp_err_t nearby_wifi_set_ap_config(const wifi_config_t *config);
esp_err_t nearby_wifi_set_sta_config(const wifi_config_t *config);
esp_err_t nearby_wifi_sta_connect(void);
esp_err_t nearby_wifi_sta_disconnect(void);

/* ESP-NimBLE host/controller lifecycle and GAP discovery. */
esp_err_t nearby_nimble_driver_init(TickType_t sync_timeout_ticks);
esp_err_t nearby_nimble_scan_start(uint32_t duration_ms, bool passive);
esp_err_t nearby_nimble_scan_stop(void);
esp_err_t nearby_nimble_driver_deinit(void);
bool nearby_nimble_driver_is_ready(void);

/* Raw ESP32-C6 IEEE 802.15.4 lifecycle. */
esp_err_t nearby_i154_receive_start(uint8_t channel);
esp_err_t nearby_i154_receive_stop(void);
bool nearby_i154_receive_is_active(void);

/**
 * Error-path cleanup while the current task still owns the complete scan gate.
 * Stops 802.15.4, tears down NimBLE, then tears down Wi-Fi; attempts every
 * cleanup step and returns the first error encountered.
 */
esp_err_t nearby_radio_scan_cleanup_all(void);

#ifdef __cplusplus
}
#endif
