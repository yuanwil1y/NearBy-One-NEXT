#include "nearby_ble_native_adapter.h"

#include <string.h>

static void copy_addr(const ble_addr_t *addr, nearby_ble_native_meta_t *meta)
{
    memcpy(meta->address, addr->val, sizeof(meta->address));
    meta->address_type = addr->type;
}

static void legacy_connectable(uint8_t event_type,
                               bool *known,
                               bool *connectable)
{
    *known = true;
    switch (event_type) {
    case BLE_HCI_ADV_RPT_EVTYPE_ADV_IND:
    case BLE_HCI_ADV_RPT_EVTYPE_DIR_IND:
        *connectable = true;
        break;
    case BLE_HCI_ADV_RPT_EVTYPE_SCAN_IND:
    case BLE_HCI_ADV_RPT_EVTYPE_NONCONN_IND:
        *connectable = false;
        break;
    case BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP:
    default:
        *known = false;
        *connectable = false;
        break;
    }
}

nearby_ble_parse_result_t nearby_ble_normalize_legacy_record(
    const nearby_ble_disc_record_t *record,
    nearby_ble_normalized_t *out)
{
    if (record == NULL || out == NULL) {
        return NEARBY_BLE_PARSE_ERR_ARG;
    }

    memset(out, 0, sizeof(*out));
    copy_addr(&record->desc.addr, &out->meta);
    out->meta.rssi = record->desc.rssi;
    out->meta.legacy = true;
    out->meta.source_truncated = record->truncated;
    out->meta.data_status = 0;
    out->meta.sid = 0xff;
    out->meta.controller_tx_power = 127;
    legacy_connectable(record->desc.event_type,
                       &out->meta.connectable_known,
                       &out->meta.connectable);

    return nearby_ble_parse_advertisement(record->desc.data,
                                          record->desc.length_data,
                                          record->truncated,
                                          &out->facts);
}

#if MYNEWT_VAL(BLE_EXT_ADV)
nearby_ble_parse_result_t nearby_ble_normalize_extended_record(
    const nearby_ble_ext_disc_record_t *record,
    nearby_ble_normalized_t *out)
{
    if (record == NULL || out == NULL) {
        return NEARBY_BLE_PARSE_ERR_ARG;
    }

    memset(out, 0, sizeof(*out));
    copy_addr(&record->desc.addr, &out->meta);
    out->meta.rssi = record->desc.rssi;
    out->meta.legacy = (record->desc.props & BLE_HCI_ADV_LEGACY_MASK) != 0;
    out->meta.connectable_known = true;
    out->meta.connectable = (record->desc.props & BLE_HCI_ADV_CONN_MASK) != 0;
    out->meta.source_truncated = record->truncated;
    out->meta.data_status = record->desc.data_status;
    out->meta.sid = record->desc.sid;
    out->meta.primary_phy = record->desc.prim_phy;
    out->meta.secondary_phy = record->desc.sec_phy;
    out->meta.controller_tx_power = record->desc.tx_power;
    out->meta.periodic_adv_interval = record->desc.periodic_adv_itvl;

    const bool incomplete =
        record->truncated ||
        record->desc.data_status != BLE_GAP_EXT_ADV_DATA_STATUS_COMPLETE;

    return nearby_ble_parse_advertisement(record->desc.data,
                                          record->desc.length_data,
                                          incomplete,
                                          &out->facts);
}
#endif
