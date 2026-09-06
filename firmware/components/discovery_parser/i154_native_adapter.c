#include "nearby_i154_native_adapter.h"

#include <string.h>

nearby_i154_parse_result_t nearby_i154_normalize_record(
    const nearby_i154_rx_record_t *record,
    nearby_i154_normalized_t *out)
{
    if (record == NULL || out == NULL) {
        return NEARBY_I154_PARSE_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    const uint8_t phy_len = record->frame[0];
    if (phy_len == 0u || (uint16_t)phy_len + 1u > NEARBY_I154_FRAME_STORAGE) {
        return NEARBY_I154_PARSE_ERR_MALFORMED;
    }
    out->meta.channel = record->frame_info.channel;
    out->meta.rssi = record->frame_info.rssi;
    out->meta.lqi = record->frame_info.lqi;
    out->meta.timestamp = record->frame_info.timestamp;
    out->meta.phy_length = phy_len;
    return nearby_i154_parse_mac(record->frame + 1u, phy_len, &out->mac);
}
