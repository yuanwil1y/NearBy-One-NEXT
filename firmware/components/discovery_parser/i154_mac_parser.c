#include "nearby_i154_discovery.h"

#include <string.h>

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static bool addr_mode_supported(uint8_t mode)
{
    return mode == 0u || mode == 2u || mode == 3u;
}

static uint8_t addr_len(uint8_t mode)
{
    return mode == 2u ? 2u : (mode == 3u ? 8u : 0u);
}

static bool take_bytes(const uint8_t *psdu, size_t psdu_len, size_t *pos,
                       uint8_t *out, size_t count)
{
    if (count > psdu_len - *pos) {
        return false;
    }
    if (out != NULL && count > 0u) {
        memcpy(out, psdu + *pos, count);
    }
    *pos += count;
    return true;
}

nearby_i154_parse_result_t nearby_i154_parse_mac(const uint8_t *psdu,
                                                 size_t psdu_len,
                                                 nearby_i154_mac_facts_t *out)
{
    if (out == NULL || (psdu == NULL && psdu_len != 0u)) {
        return NEARBY_I154_PARSE_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (psdu_len < 2u) {
        return NEARBY_I154_PARSE_ERR_MALFORMED;
    }

    const uint16_t fcf = le16(psdu);
    out->frame_type = (uint8_t)(fcf & 0x07u);
    out->security_enabled = (fcf & (1u << 3)) != 0u;
    out->frame_pending = (fcf & (1u << 4)) != 0u;
    out->ack_request = (fcf & (1u << 5)) != 0u;
    out->pan_id_compression = (fcf & (1u << 6)) != 0u;
    out->sequence_suppressed = (fcf & (1u << 8)) != 0u;
    out->ie_present = (fcf & (1u << 9)) != 0u;
    out->destination_addr_mode = (uint8_t)((fcf >> 10) & 0x03u);
    out->frame_version = (uint8_t)((fcf >> 12) & 0x03u);
    out->source_addr_mode = (uint8_t)((fcf >> 14) & 0x03u);

    if (!addr_mode_supported(out->destination_addr_mode) ||
        !addr_mode_supported(out->source_addr_mode)) {
        return NEARBY_I154_PARSE_ERR_MALFORMED;
    }

    /*
     * Zigbee-class traffic uses 802.15.4-2003/2006 MAC headers. 2015 PAN-ID
     * elision has a larger addressing matrix; fail closed instead of guessing.
     */
    if (out->frame_version >= 2u) {
        return NEARBY_I154_PARSE_UNSUPPORTED;
    }
    if (out->sequence_suppressed || out->ie_present) {
        return NEARBY_I154_PARSE_UNSUPPORTED;
    }

    size_t pos = 2u;
    if (!take_bytes(psdu, psdu_len, &pos, &out->sequence, 1u)) {
        return NEARBY_I154_PARSE_ERR_MALFORMED;
    }
    out->has_sequence = true;

    if (out->destination_addr_mode != 0u) {
        if (psdu_len - pos < 2u) {
            return NEARBY_I154_PARSE_ERR_MALFORMED;
        }
        out->destination_pan = le16(psdu + pos);
        out->has_destination_pan = true;
        pos += 2u;
        out->destination_addr_len = addr_len(out->destination_addr_mode);
        if (!take_bytes(psdu, psdu_len, &pos, out->destination_addr,
                        out->destination_addr_len)) {
            return NEARBY_I154_PARSE_ERR_MALFORMED;
        }
    }

    if (out->source_addr_mode != 0u) {
        if (out->pan_id_compression && out->has_destination_pan) {
            out->source_pan = out->destination_pan;
            out->has_source_pan = true;
        } else {
            if (psdu_len - pos < 2u) {
                return NEARBY_I154_PARSE_ERR_MALFORMED;
            }
            out->source_pan = le16(psdu + pos);
            out->has_source_pan = true;
            pos += 2u;
        }
        out->source_addr_len = addr_len(out->source_addr_mode);
        if (!take_bytes(psdu, psdu_len, &pos, out->source_addr,
                        out->source_addr_len)) {
            return NEARBY_I154_PARSE_ERR_MALFORMED;
        }
    }

    if (out->security_enabled) {
        if (pos >= psdu_len) {
            return NEARBY_I154_PARSE_ERR_MALFORMED;
        }
        const uint8_t security_control = psdu[pos++];
        const uint8_t key_id_mode = (uint8_t)((security_control >> 3) & 0x03u);
        const bool frame_counter_suppressed = (security_control & (1u << 5)) != 0u;
        if (!frame_counter_suppressed) {
            if (!take_bytes(psdu, psdu_len, &pos, NULL, 4u)) {
                return NEARBY_I154_PARSE_ERR_MALFORMED;
            }
        }
        size_t key_bytes = 0u;
        if (key_id_mode == 1u) {
            key_bytes = 1u;
        } else if (key_id_mode == 2u) {
            key_bytes = 5u;
        } else if (key_id_mode == 3u) {
            key_bytes = 9u;
        }
        if (!take_bytes(psdu, psdu_len, &pos, NULL, key_bytes)) {
            return NEARBY_I154_PARSE_ERR_MALFORMED;
        }
    }

    if (pos > UINT16_MAX || psdu_len - pos > UINT16_MAX) {
        return NEARBY_I154_PARSE_ERR_MALFORMED;
    }
    out->payload_offset = (uint16_t)pos;
    out->payload_len = (uint16_t)(psdu_len - pos);
    return out->security_enabled ? NEARBY_I154_PARSE_SECURED
                                 : NEARBY_I154_PARSE_OK;
}
