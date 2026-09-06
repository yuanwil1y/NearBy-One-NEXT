#include "native_handoff.h"

#include <stddef.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_ieee802154.h"
#include "freertos/queue.h"

#define WIFI_FCS_LEN 4

static nearby_wifi_rx_record_t s_wifi_slots[NEARBY_WIFI_RX_SLOT_COUNT];
static StaticQueue_t s_wifi_free_q_struct;
static StaticQueue_t s_wifi_ready_q_struct;
static uint8_t s_wifi_free_q_storage[NEARBY_WIFI_RX_SLOT_COUNT * sizeof(uint8_t)];
static uint8_t s_wifi_ready_q_storage[NEARBY_WIFI_RX_SLOT_COUNT * sizeof(uint8_t)];
static QueueHandle_t s_wifi_free_q;
static QueueHandle_t s_wifi_ready_q;
static volatile uint32_t s_wifi_drops;

static nearby_ble_disc_record_t s_ble_slots[NEARBY_BLE_RX_SLOT_COUNT];
static StaticQueue_t s_ble_free_q_struct;
static StaticQueue_t s_ble_ready_q_struct;
static uint8_t s_ble_free_q_storage[NEARBY_BLE_RX_SLOT_COUNT * sizeof(uint8_t)];
static uint8_t s_ble_ready_q_storage[NEARBY_BLE_RX_SLOT_COUNT * sizeof(uint8_t)];
static QueueHandle_t s_ble_free_q;
static QueueHandle_t s_ble_ready_q;
static volatile uint32_t s_ble_drops;

#if MYNEWT_VAL(BLE_EXT_ADV)
static nearby_ble_ext_disc_record_t s_ble_ext_slots[NEARBY_BLE_EXT_RX_SLOT_COUNT];
static StaticQueue_t s_ble_ext_free_q_struct;
static StaticQueue_t s_ble_ext_ready_q_struct;
static uint8_t s_ble_ext_free_q_storage[NEARBY_BLE_EXT_RX_SLOT_COUNT * sizeof(uint8_t)];
static uint8_t s_ble_ext_ready_q_storage[NEARBY_BLE_EXT_RX_SLOT_COUNT * sizeof(uint8_t)];
static QueueHandle_t s_ble_ext_free_q;
static QueueHandle_t s_ble_ext_ready_q;
static volatile uint32_t s_ble_ext_drops;
#endif

static nearby_i154_rx_record_t s_i154_slots[NEARBY_I154_RX_SLOT_COUNT];
static StaticQueue_t s_i154_free_q_struct;
static StaticQueue_t s_i154_ready_q_struct;
static uint8_t s_i154_free_q_storage[NEARBY_I154_RX_SLOT_COUNT * sizeof(uint8_t)];
static uint8_t s_i154_ready_q_storage[NEARBY_I154_RX_SLOT_COUNT * sizeof(uint8_t)];
static QueueHandle_t s_i154_free_q;
static QueueHandle_t s_i154_ready_q;
static volatile uint32_t s_i154_drops;
static bool s_i154_registered;

static esp_err_t init_index_queues(QueueHandle_t *free_q,
                                   QueueHandle_t *ready_q,
                                   StaticQueue_t *free_q_struct,
                                   StaticQueue_t *ready_q_struct,
                                   uint8_t *free_storage,
                                   uint8_t *ready_storage,
                                   UBaseType_t slot_count)
{
    if (*free_q != NULL && *ready_q != NULL) {
        return ESP_OK;
    }

    *free_q = xQueueCreateStatic(slot_count,
                                sizeof(uint8_t),
                                free_storage,
                                free_q_struct);
    *ready_q = xQueueCreateStatic(slot_count,
                                 sizeof(uint8_t),
                                 ready_storage,
                                 ready_q_struct);
    if (*free_q == NULL || *ready_q == NULL) {
        return ESP_ERR_NO_MEM;
    }

    for (uint8_t i = 0; i < slot_count; ++i) {
        if (xQueueSend(*free_q, &i, 0) != pdTRUE) {
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

esp_err_t nearby_wifi_handoff_init(void)
{
    return init_index_queues(&s_wifi_free_q,
                             &s_wifi_ready_q,
                             &s_wifi_free_q_struct,
                             &s_wifi_ready_q_struct,
                             s_wifi_free_q_storage,
                             s_wifi_ready_q_storage,
                             NEARBY_WIFI_RX_SLOT_COUNT);
}

void nearby_wifi_promiscuous_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (buf == NULL || s_wifi_free_q == NULL || s_wifi_ready_q == NULL) {
        ++s_wifi_drops;
        return;
    }

    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;

    /* Follow Espressif simple_sniffer: do not pass driver-marked RX errors. */
    if (type != WIFI_PKT_MISC && pkt->rx_ctrl.rx_state != 0) {
        ++s_wifi_drops;
        return;
    }

    uint8_t slot_index;
    if (xQueueReceive(s_wifi_free_q, &slot_index, 0) != pdTRUE) {
        ++s_wifi_drops;
        return;
    }

    nearby_wifi_rx_record_t *slot = &s_wifi_slots[slot_index];
    slot->type = type;
    slot->rx_ctrl = pkt->rx_ctrl;
    slot->original_len = 0;
    slot->captured_len = 0;
    slot->truncated = false;

    if (type != WIFI_PKT_MISC) {
        uint16_t frame_len = pkt->rx_ctrl.sig_len;
        /* ESP-IDF simple_sniffer excludes the trailing 802.11 FCS in PCAP data. */
        if (frame_len >= WIFI_FCS_LEN) {
            frame_len -= WIFI_FCS_LEN;
        }

        slot->original_len = frame_len;
        slot->captured_len = frame_len > NEARBY_WIFI_CAPTURE_MAX
                                 ? NEARBY_WIFI_CAPTURE_MAX
                                 : frame_len;
        slot->truncated = slot->captured_len != slot->original_len;
        memcpy(slot->payload, pkt->payload, slot->captured_len);
    }

    if (xQueueSend(s_wifi_ready_q, &slot_index, 0) != pdTRUE) {
        (void)xQueueSend(s_wifi_free_q, &slot_index, 0);
        ++s_wifi_drops;
    }
}

esp_err_t nearby_wifi_rx_take(nearby_wifi_rx_record_t **out, TickType_t timeout_ticks)
{
    if (out == NULL || s_wifi_ready_q == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t slot_index;
    if (xQueueReceive(s_wifi_ready_q, &slot_index, timeout_ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    *out = &s_wifi_slots[slot_index];
    return ESP_OK;
}

esp_err_t nearby_wifi_rx_release(nearby_wifi_rx_record_t *record)
{
    if (record == NULL || s_wifi_free_q == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (uint8_t i = 0; i < NEARBY_WIFI_RX_SLOT_COUNT; ++i) {
        if (record == &s_wifi_slots[i]) {
            return xQueueSend(s_wifi_free_q, &i, 0) == pdTRUE
                       ? ESP_OK
                       : ESP_ERR_INVALID_STATE;
        }
    }
    return ESP_ERR_INVALID_ARG;
}

uint32_t nearby_wifi_rx_drop_count(void)
{
    return s_wifi_drops;
}

esp_err_t nearby_ble_handoff_init(void)
{
    esp_err_t err = init_index_queues(&s_ble_free_q,
                                      &s_ble_ready_q,
                                      &s_ble_free_q_struct,
                                      &s_ble_ready_q_struct,
                                      s_ble_free_q_storage,
                                      s_ble_ready_q_storage,
                                      NEARBY_BLE_RX_SLOT_COUNT);
    if (err != ESP_OK) {
        return err;
    }

#if MYNEWT_VAL(BLE_EXT_ADV)
    err = init_index_queues(&s_ble_ext_free_q,
                            &s_ble_ext_ready_q,
                            &s_ble_ext_free_q_struct,
                            &s_ble_ext_ready_q_struct,
                            s_ble_ext_free_q_storage,
                            s_ble_ext_ready_q_storage,
                            NEARBY_BLE_EXT_RX_SLOT_COUNT);
#endif
    return err;
}

static void queue_legacy_disc(const struct ble_gap_disc_desc *desc)
{
    if (desc == NULL || s_ble_free_q == NULL || s_ble_ready_q == NULL) {
        ++s_ble_drops;
        return;
    }

    uint8_t slot_index;
    if (xQueueReceive(s_ble_free_q, &slot_index, 0) != pdTRUE) {
        ++s_ble_drops;
        return;
    }

    nearby_ble_disc_record_t *slot = &s_ble_slots[slot_index];
    slot->desc = *desc;
    slot->original_length_data = desc->length_data;
    const uint16_t copied_len = desc->length_data > NEARBY_BLE_LEGACY_DATA_MAX
                                    ? NEARBY_BLE_LEGACY_DATA_MAX
                                    : desc->length_data;
    slot->truncated = copied_len != desc->length_data;

    if (copied_len > 0 && desc->data != NULL) {
        memcpy(slot->data, desc->data, copied_len);
    }

    /* Keep B on the native descriptor while making its pointer durable. */
    slot->desc.data = slot->data;
    slot->desc.length_data = (uint8_t)copied_len;

    if (xQueueSend(s_ble_ready_q, &slot_index, 0) != pdTRUE) {
        (void)xQueueSend(s_ble_free_q, &slot_index, 0);
        ++s_ble_drops;
    }
}

#if MYNEWT_VAL(BLE_EXT_ADV)
static void queue_extended_disc(const struct ble_gap_ext_disc_desc *desc)
{
    if (desc == NULL || s_ble_ext_free_q == NULL || s_ble_ext_ready_q == NULL) {
        ++s_ble_ext_drops;
        return;
    }

    uint8_t slot_index;
    if (xQueueReceive(s_ble_ext_free_q, &slot_index, 0) != pdTRUE) {
        ++s_ble_ext_drops;
        return;
    }

    nearby_ble_ext_disc_record_t *slot = &s_ble_ext_slots[slot_index];
    slot->desc = *desc;
    slot->original_length_data = desc->length_data;
    const uint8_t copied_len = desc->length_data;
    slot->truncated = false;

    if (copied_len > 0 && desc->data != NULL) {
        memcpy(slot->data, desc->data, copied_len);
    }

    /*
     * Preserve props/data_status/SID/PHY exactly. Incomplete controller reports
     * remain separate records; D deliberately does not perform reassembly.
     */
    slot->desc.data = slot->data;
    slot->desc.length_data = copied_len;

    if (xQueueSend(s_ble_ext_ready_q, &slot_index, 0) != pdTRUE) {
        (void)xQueueSend(s_ble_ext_free_q, &slot_index, 0);
        ++s_ble_ext_drops;
    }
}
#endif

int nearby_nimble_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    if (event == NULL) {
        return 0;
    }

    if (event->type == BLE_GAP_EVENT_DISC) {
        queue_legacy_disc(&event->disc);
        return 0;
    }

#if MYNEWT_VAL(BLE_EXT_ADV)
    if (event->type == BLE_GAP_EVENT_EXT_DISC) {
        queue_extended_disc(&event->ext_disc);
    }
#endif
    return 0;
}

esp_err_t nearby_ble_disc_take(nearby_ble_disc_record_t **out, TickType_t timeout_ticks)
{
    if (out == NULL || s_ble_ready_q == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t slot_index;
    if (xQueueReceive(s_ble_ready_q, &slot_index, timeout_ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    *out = &s_ble_slots[slot_index];
    return ESP_OK;
}

esp_err_t nearby_ble_disc_release(nearby_ble_disc_record_t *record)
{
    if (record == NULL || s_ble_free_q == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (uint8_t i = 0; i < NEARBY_BLE_RX_SLOT_COUNT; ++i) {
        if (record == &s_ble_slots[i]) {
            return xQueueSend(s_ble_free_q, &i, 0) == pdTRUE
                       ? ESP_OK
                       : ESP_ERR_INVALID_STATE;
        }
    }
    return ESP_ERR_INVALID_ARG;
}

uint32_t nearby_ble_rx_drop_count(void)
{
    return s_ble_drops;
}

#if MYNEWT_VAL(BLE_EXT_ADV)
esp_err_t nearby_ble_ext_disc_take(nearby_ble_ext_disc_record_t **out, TickType_t timeout_ticks)
{
    if (out == NULL || s_ble_ext_ready_q == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t slot_index;
    if (xQueueReceive(s_ble_ext_ready_q, &slot_index, timeout_ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    *out = &s_ble_ext_slots[slot_index];
    return ESP_OK;
}

esp_err_t nearby_ble_ext_disc_release(nearby_ble_ext_disc_record_t *record)
{
    if (record == NULL || s_ble_ext_free_q == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (uint8_t i = 0; i < NEARBY_BLE_EXT_RX_SLOT_COUNT; ++i) {
        if (record == &s_ble_ext_slots[i]) {
            return xQueueSend(s_ble_ext_free_q, &i, 0) == pdTRUE
                       ? ESP_OK
                       : ESP_ERR_INVALID_STATE;
        }
    }
    return ESP_ERR_INVALID_ARG;
}

uint32_t nearby_ble_ext_rx_drop_count(void)
{
    return s_ble_ext_drops;
}
#endif

static void IRAM_ATTR nearby_i154_rx_done_isr(uint8_t *frame,
                                               esp_ieee802154_frame_info_t *frame_info)
{
    BaseType_t task_woken = pdFALSE;

    if (frame == NULL) {
        ++s_i154_drops;
        return;
    }

    uint8_t slot_index;
    bool queued = false;

    if (frame_info != NULL && s_i154_free_q != NULL && s_i154_ready_q != NULL &&
        xQueueReceiveFromISR(s_i154_free_q, &slot_index, &task_woken) == pdTRUE) {
        nearby_i154_rx_record_t *slot = &s_i154_slots[slot_index];
        const uint8_t phy_len = frame[0];
        const uint16_t total_len = (uint16_t)phy_len + 1U;

        if (total_len <= NEARBY_I154_FRAME_STORAGE) {
            slot->frame_info = *frame_info;
            for (uint16_t i = 0; i < total_len; ++i) {
                slot->frame[i] = frame[i];
            }
            queued = true;
        } else {
            (void)xQueueSendFromISR(s_i154_free_q, &slot_index, &task_woken);
            ++s_i154_drops;
        }
    } else {
        ++s_i154_drops;
    }

    /* Driver buffer ownership ends here, before any task-level parsing. */
    (void)esp_ieee802154_receive_handle_done(frame);

    if (queued) {
        if (xQueueSendFromISR(s_i154_ready_q, &slot_index, &task_woken) != pdTRUE) {
            (void)xQueueSendFromISR(s_i154_free_q, &slot_index, &task_woken);
            ++s_i154_drops;
        }
    }

    if (task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

esp_err_t nearby_i154_handoff_init(void)
{
    esp_err_t err = init_index_queues(&s_i154_free_q,
                                      &s_i154_ready_q,
                                      &s_i154_free_q_struct,
                                      &s_i154_ready_q_struct,
                                      s_i154_free_q_storage,
                                      s_i154_ready_q_storage,
                                      NEARBY_I154_RX_SLOT_COUNT);
    if (err != ESP_OK) {
        return err;
    }

    if (s_i154_registered) {
        return ESP_OK;
    }

    const esp_ieee802154_event_cb_list_t callbacks = {
        .rx_done_cb = nearby_i154_rx_done_isr,
    };

    err = esp_ieee802154_event_callback_list_register(callbacks);
    if (err == ESP_OK) {
        s_i154_registered = true;
    }
    return err;
}

esp_err_t nearby_i154_rx_take(nearby_i154_rx_record_t **out, TickType_t timeout_ticks)
{
    if (out == NULL || s_i154_ready_q == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t slot_index;
    if (xQueueReceive(s_i154_ready_q, &slot_index, timeout_ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    *out = &s_i154_slots[slot_index];
    return ESP_OK;
}

esp_err_t nearby_i154_rx_release(nearby_i154_rx_record_t *record)
{
    if (record == NULL || s_i154_free_q == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (uint8_t i = 0; i < NEARBY_I154_RX_SLOT_COUNT; ++i) {
        if (record == &s_i154_slots[i]) {
            return xQueueSend(s_i154_free_q, &i, 0) == pdTRUE
                       ? ESP_OK
                       : ESP_ERR_INVALID_STATE;
        }
    }
    return ESP_ERR_INVALID_ARG;
}

uint32_t nearby_i154_rx_drop_count(void)
{
    return s_i154_drops;
}
