#include "nearby_wifi_discovery.h"

#include <string.h>

#define IEEE80211_MGMT_HDR_LEN 24u
#define IEEE80211_BEACON_FIXED_LEN 12u
#define IE_SSID 0u
#define IE_SUPPORTED_RATES 1u
#define IE_DS_PARAMETER 3u
#define IE_RSN 48u
#define IE_EXT_SUPPORTED_RATES 50u
#define IE_HT_CAPABILITIES 45u
#define IE_HT_OPERATION 61u
#define IE_VENDOR_SPECIFIC 221u
#define IE_EXTENSION 255u
#define IE_EXT_HE_CAPABILITIES 35u
#define IE_EXT_HE_OPERATION 36u

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static bool copy_rates(nearby_wifi_mgmt_facts_t *out,
                       const uint8_t *data,
                       size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        if (out->supported_rate_count >= NEARBY_WIFI_MAX_RATES) {
            out->supported_rate_overflow = true;
            return true;
        }
        out->supported_rates[out->supported_rate_count++] = data[i];
    }
    return true;
}

static void add_vendor_ie(nearby_wifi_mgmt_facts_t *out,
                          const uint8_t *data,
                          size_t len)
{
    if (len < 3u) {
        return;
    }
    if (out->vendor_ie_count >= NEARBY_WIFI_MAX_VENDOR_IES) {
        out->vendor_ie_overflow = true;
        return;
    }
    nearby_wifi_vendor_ie_t *slot = &out->vendor_ies[out->vendor_ie_count++];
    memset(slot, 0, sizeof(*slot));
    memcpy(slot->oui, data, 3);
    const size_t payload_len = len - 3u;
    const size_t copy_len = payload_len > NEARBY_WIFI_VENDOR_IE_DATA_MAX
                                ? NEARBY_WIFI_VENDOR_IE_DATA_MAX
                                : payload_len;
    if (copy_len > 0u) {
        memcpy(slot->data, data + 3u, copy_len);
    }
    slot->data_len = (uint8_t)copy_len;
    slot->truncated = copy_len != payload_len;

    if (len >= 4u && data[0] == 0x00u && data[1] == 0x50u &&
        data[2] == 0xf2u && data[3] == 0x04u) {
        out->has_wps = true;
    }
}

nearby_wifi_parse_result_t nearby_wifi_parse_management_frame(
    const uint8_t *frame,
    size_t frame_len,
    bool source_truncated,
    nearby_wifi_mgmt_facts_t *out)
{
    if (out == NULL || (frame == NULL && frame_len != 0u)) {
        return NEARBY_WIFI_PARSE_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (frame_len < IEEE80211_MGMT_HDR_LEN) {
        return source_truncated ? NEARBY_WIFI_PARSE_PARTIAL
                                : NEARBY_WIFI_PARSE_ERR_MALFORMED;
    }

    const uint16_t frame_control = le16(frame);
    const uint8_t type = (uint8_t)((frame_control >> 2) & 0x03u);
    const uint8_t subtype = (uint8_t)((frame_control >> 4) & 0x0fu);
    if (type != 0u) {
        return NEARBY_WIFI_PARSE_UNSUPPORTED;
    }
    if (subtype != NEARBY_WIFI_MGMT_BEACON &&
        subtype != NEARBY_WIFI_MGMT_PROBE_REQ &&
        subtype != NEARBY_WIFI_MGMT_PROBE_RESP) {
        return NEARBY_WIFI_PARSE_UNSUPPORTED;
    }

    out->subtype = subtype;
    memcpy(out->destination, frame + 4u, 6u);
    memcpy(out->source, frame + 10u, 6u);
    memcpy(out->bssid, frame + 16u, 6u);

    size_t pos = IEEE80211_MGMT_HDR_LEN;
    if (subtype == NEARBY_WIFI_MGMT_BEACON ||
        subtype == NEARBY_WIFI_MGMT_PROBE_RESP) {
        if (frame_len - pos < IEEE80211_BEACON_FIXED_LEN) {
            return source_truncated ? NEARBY_WIFI_PARSE_PARTIAL
                                    : NEARBY_WIFI_PARSE_ERR_MALFORMED;
        }
        out->has_beacon_interval = true;
        out->beacon_interval_tu = le16(frame + pos + 8u);
        out->has_capability = true;
        out->capability = le16(frame + pos + 10u);
        pos += IEEE80211_BEACON_FIXED_LEN;
    }

    bool partial = source_truncated;
    while (pos < frame_len) {
        if (frame_len - pos < 2u) {
            out->partial_tail = true;
            return source_truncated ? NEARBY_WIFI_PARSE_PARTIAL
                                    : NEARBY_WIFI_PARSE_ERR_MALFORMED;
        }
        const uint8_t id = frame[pos++];
        const uint8_t len = frame[pos++];
        if ((size_t)len > frame_len - pos) {
            out->partial_tail = true;
            return source_truncated ? NEARBY_WIFI_PARSE_PARTIAL
                                    : NEARBY_WIFI_PARSE_ERR_MALFORMED;
        }
        const uint8_t *data = frame + pos;

        switch (id) {
        case IE_SSID:
            if (len > NEARBY_WIFI_SSID_MAX) {
                return NEARBY_WIFI_PARSE_ERR_MALFORMED;
            }
            if (!out->has_ssid) {
                out->has_ssid = true;
                out->ssid_len = len;
                out->hidden_ssid = len == 0u;
                if (len > 0u) {
                    memcpy(out->ssid, data, len);
                }
            }
            break;
        case IE_SUPPORTED_RATES:
        case IE_EXT_SUPPORTED_RATES:
            (void)copy_rates(out, data, len);
            break;
        case IE_DS_PARAMETER:
            if (len < 1u) {
                return NEARBY_WIFI_PARSE_ERR_MALFORMED;
            }
            out->has_channel = true;
            out->channel = data[0];
            break;
        case IE_RSN:
            out->has_rsn = true;
            break;
        case IE_HT_CAPABILITIES:
            out->has_ht = true;
            break;
        case IE_HT_OPERATION:
            out->has_ht = true;
            if (len >= 1u && !out->has_channel) {
                out->has_channel = true;
                out->channel = data[0];
            }
            break;
        case IE_VENDOR_SPECIFIC:
            add_vendor_ie(out, data, len);
            break;
        case IE_EXTENSION:
            if (len >= 1u && (data[0] == IE_EXT_HE_CAPABILITIES ||
                              data[0] == IE_EXT_HE_OPERATION)) {
                out->has_he = true;
            }
            break;
        default:
            break;
        }
        pos += len;
    }

    partial = partial || out->supported_rate_overflow || out->vendor_ie_overflow;
    for (uint8_t i = 0; i < out->vendor_ie_count; ++i) {
        partial = partial || out->vendor_ies[i].truncated;
    }
    return partial ? NEARBY_WIFI_PARSE_PARTIAL : NEARBY_WIFI_PARSE_OK;
}
