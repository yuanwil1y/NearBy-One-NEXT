#include "nearby_wifi_native_adapter.h"

#include <string.h>

void nearby_wifi_normalize_active_record(const wifi_ap_record_t *record,
                                         nearby_wifi_active_facts_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (record == NULL) {
        return;
    }
    memcpy(out->bssid, record->bssid, sizeof(out->bssid));
    size_t ssid_len = 0;
    while (ssid_len < NEARBY_WIFI_SSID_MAX && record->ssid[ssid_len] != 0u) {
        ++ssid_len;
    }
    if (ssid_len > 0u) {
        memcpy(out->ssid, record->ssid, ssid_len);
    }
    out->ssid_len = (uint8_t)ssid_len;
    out->hidden_ssid = ssid_len == 0u;
    out->primary_channel = record->primary;
    out->rssi = record->rssi;
    out->authmode = (int)record->authmode;
}

nearby_wifi_parse_result_t nearby_wifi_normalize_passive_record(
    const nearby_wifi_rx_record_t *record,
    nearby_wifi_passive_normalized_t *out)
{
    if (record == NULL || out == NULL) {
        return NEARBY_WIFI_PARSE_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->meta.rssi = record->rx_ctrl.rssi;
    out->meta.channel = record->rx_ctrl.channel;
    out->meta.original_len = record->original_len;
    out->meta.captured_len = record->captured_len;
    out->meta.source_truncated = record->truncated;
    if (record->type != WIFI_PKT_MGMT) {
        return NEARBY_WIFI_PARSE_UNSUPPORTED;
    }
    return nearby_wifi_parse_management_frame(record->payload,
                                              record->captured_len,
                                              record->truncated,
                                              &out->facts);
}
