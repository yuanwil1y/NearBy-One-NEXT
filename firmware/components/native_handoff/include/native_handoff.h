#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_wifi_types.h"
#include "esp_ieee802154_types.h"
#include "freertos/FreeRTOS.h"
#include "host/ble_gap.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Transport-specific bounded buffers. These are not a cross-radio packet ABI. */
#define NEARBY_WIFI_RX_SLOT_COUNT       4
#define NEARBY_WIFI_CAPTURE_MAX         2360
#define NEARBY_BLE_RX_SLOT_COUNT        8
#define NEARBY_BLE_LEGACY_DATA_MAX      64
#if MYNEWT_VAL(BLE_EXT_ADV)
#define NEARBY_BLE_EXT_RX_SLOT_COUNT    4
#define NEARBY_BLE_EXT_DATA_MAX         255
#endif
#define NEARBY_I154_RX_SLOT_COUNT       8
#define NEARBY_I154_FRAME_STORAGE       128 /* length byte + max 127-byte PHY frame */

typedef struct {
    wifi_promiscuous_pkt_type_t type;
    wifi_pkt_rx_ctrl_t rx_ctrl;
    uint16_t original_len;
    uint16_t captured_len;
    bool truncated;
    uint8_t payload[NEARBY_WIFI_CAPTURE_MAX];
} nearby_wifi_rx_record_t;

typedef struct {
    /* Native NimBLE descriptor copied by value. desc.data is repointed below. */
    struct ble_gap_disc_desc desc;
    uint16_t original_length_data;
    bool truncated;
    uint8_t data[NEARBY_BLE_LEGACY_DATA_MAX];
} nearby_ble_disc_record_t;

#if MYNEWT_VAL(BLE_EXT_ADV)
typedef struct {
    /*
     * Native extended descriptor copied by value. desc.data is repointed to
     * data[] so callback-owned storage never crosses the D->B boundary.
     * desc.data_status remains exactly what NimBLE reported; D does not
     * reassemble incomplete chained reports.
     */
    struct ble_gap_ext_disc_desc desc;
    uint16_t original_length_data;
    bool truncated;
    uint8_t data[NEARBY_BLE_EXT_DATA_MAX];
} nearby_ble_ext_disc_record_t;
#endif

typedef struct {
    esp_ieee802154_frame_info_t frame_info;
    /* Preserves ESP-IDF's native receive-buffer shape: frame[0] is length. */
    uint8_t frame[NEARBY_I154_FRAME_STORAGE];
} nearby_i154_rx_record_t;

/* Wi-Fi promiscuous callback handoff. */
esp_err_t nearby_wifi_handoff_init(void);
void nearby_wifi_promiscuous_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type);
esp_err_t nearby_wifi_rx_take(nearby_wifi_rx_record_t **out, TickType_t timeout_ticks);
esp_err_t nearby_wifi_rx_release(nearby_wifi_rx_record_t *record);
uint32_t nearby_wifi_rx_drop_count(void);

/* NimBLE GAP discovery handoff. Legacy and extended reports remain separate. */
esp_err_t nearby_ble_handoff_init(void);
int nearby_nimble_gap_event_cb(struct ble_gap_event *event, void *arg);
esp_err_t nearby_ble_disc_take(nearby_ble_disc_record_t **out, TickType_t timeout_ticks);
esp_err_t nearby_ble_disc_release(nearby_ble_disc_record_t *record);
uint32_t nearby_ble_rx_drop_count(void);
#if MYNEWT_VAL(BLE_EXT_ADV)
esp_err_t nearby_ble_ext_disc_take(nearby_ble_ext_disc_record_t **out, TickType_t timeout_ticks);
esp_err_t nearby_ble_ext_disc_release(nearby_ble_ext_disc_record_t *record);
uint32_t nearby_ble_ext_rx_drop_count(void);
#endif

/*
 * Raw IEEE 802.15.4 handoff. This registers rx_done_cb through the public
 * event-callback-list API and therefore must run while the 802.15.4 subsystem
 * is disabled, before esp_ieee802154_enable().
 */
esp_err_t nearby_i154_handoff_init(void);
esp_err_t nearby_i154_rx_take(nearby_i154_rx_record_t **out, TickType_t timeout_ticks);
esp_err_t nearby_i154_rx_release(nearby_i154_rx_record_t *record);
uint32_t nearby_i154_rx_drop_count(void);

#ifdef __cplusplus
}
#endif
