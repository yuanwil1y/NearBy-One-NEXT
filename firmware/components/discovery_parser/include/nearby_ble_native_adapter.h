#pragma once

#include "nearby_ble_discovery.h"
#include "native_handoff.h"

#ifdef __cplusplus
extern "C" {
#endif

nearby_ble_parse_result_t nearby_ble_normalize_legacy_record(
    const nearby_ble_disc_record_t *record,
    nearby_ble_normalized_t *out);

#if MYNEWT_VAL(BLE_EXT_ADV)
nearby_ble_parse_result_t nearby_ble_normalize_extended_record(
    const nearby_ble_ext_disc_record_t *record,
    nearby_ble_normalized_t *out);
#endif

#ifdef __cplusplus
}
#endif
