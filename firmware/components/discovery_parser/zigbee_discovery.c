#include "nearby_zigbee_discovery.h"

#include <string.h>

#define ZIGBEE_NWK_MIN_HEADER 8u
#define ZDO_DEVICE_ANNOUNCE_CLUSTER 0x0013u
#define ZDO_NODE_DESC_RSP_CLUSTER 0x8002u
#define ZDO_SIMPLE_DESC_RSP_CLUSTER 0x8004u
#define ZDO_ACTIVE_EP_RSP_CLUSTER 0x8005u
#define ZCL_BASIC_CLUSTER 0x0000u
#define ZCL_CMD_READ_ATTR_RSP 0x01u
#define ZCL_CMD_REPORT_ATTR 0x0au
#define ZCL_ATTR_MANUFACTURER_NAME 0x0004u
#define ZCL_ATTR_MODEL_IDENTIFIER 0x0005u
#define ZCL_TYPE_OCTET_STRING 0x41u
#define ZCL_TYPE_CHAR_STRING 0x42u

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static bool advance(size_t *pos, size_t count, size_t len)
{
    if (count > len - *pos) {
        return false;
    }
    *pos += count;
    return true;
}

static bool copy_cluster_list(const uint8_t *data, size_t len, size_t *pos,
                              uint8_t declared_count, uint16_t *out,
                              uint8_t *out_count, bool *overflow)
{
    *out_count = 0;
    for (uint8_t i = 0; i < declared_count; ++i) {
        if (len - *pos < 2u) {
            return false;
        }
        const uint16_t cluster = le16(data + *pos);
        *pos += 2u;
        if (*out_count < NEARBY_ZIGBEE_MAX_CLUSTERS) {
            out[(*out_count)++] = cluster;
        } else {
            *overflow = true;
        }
    }
    return true;
}

static nearby_zigbee_parse_result_t parse_device_announce(
    const uint8_t *payload, size_t len, nearby_zigbee_discovery_facts_t *out)
{
    if (len < 12u) {
        return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
    }
    out->has_device_announce = true;
    out->announced_nwk = le16(payload + 1u);
    memcpy(out->announced_ieee, payload + 3u, 8u);
    out->announced_capability = payload[11u];
    return NEARBY_ZIGBEE_PARSE_OK;
}

static nearby_zigbee_parse_result_t parse_node_descriptor_response(
    const uint8_t *payload, size_t len, nearby_zigbee_discovery_facts_t *out)
{
    /* transaction seq + status + nwk addr + 13-byte node descriptor */
    if (len < 17u) {
        return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
    }
    if (payload[1] != 0u) {
        return NEARBY_ZIGBEE_PARSE_OK;
    }
    const size_t desc = 4u;
    out->has_node_descriptor = true;
    out->manufacturer_code = le16(payload + desc + 3u);
    return NEARBY_ZIGBEE_PARSE_OK;
}

static nearby_zigbee_parse_result_t parse_simple_descriptor_response(
    const uint8_t *payload, size_t len, nearby_zigbee_discovery_facts_t *out)
{
    if (len < 5u) {
        return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
    }
    if (payload[1] != 0u) {
        return NEARBY_ZIGBEE_PARSE_OK;
    }
    const uint8_t descriptor_len = payload[4u];
    if ((size_t)descriptor_len > len - 5u || descriptor_len < 8u) {
        return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
    }
    const uint8_t *desc = payload + 5u;
    const size_t desc_len = descriptor_len;
    size_t pos = 0u;
    out->has_simple_descriptor = true;
    out->descriptor_endpoint = desc[pos++];
    if (desc_len - pos < 5u) {
        return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
    }
    out->descriptor_profile_id = le16(desc + pos);
    pos += 2u;
    out->descriptor_device_id = le16(desc + pos);
    pos += 2u;
    out->descriptor_device_version = (uint8_t)(desc[pos++] & 0x0fu);
    if (pos >= desc_len) {
        return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
    }
    const uint8_t input_count = desc[pos++];
    if (!copy_cluster_list(desc, desc_len, &pos, input_count,
                           out->input_clusters, &out->input_cluster_count,
                           &out->input_cluster_overflow)) {
        return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
    }
    if (pos >= desc_len) {
        return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
    }
    const uint8_t output_count = desc[pos++];
    if (!copy_cluster_list(desc, desc_len, &pos, output_count,
                           out->output_clusters, &out->output_cluster_count,
                           &out->output_cluster_overflow)) {
        return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
    }
    return (out->input_cluster_overflow || out->output_cluster_overflow)
               ? NEARBY_ZIGBEE_PARSE_PARTIAL : NEARBY_ZIGBEE_PARSE_OK;
}

static nearby_zigbee_parse_result_t parse_active_endpoint_response(
    const uint8_t *payload, size_t len, nearby_zigbee_discovery_facts_t *out)
{
    if (len < 5u) {
        return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
    }
    if (payload[1] != 0u) {
        return NEARBY_ZIGBEE_PARSE_OK;
    }
    const uint8_t count = payload[4u];
    if ((size_t)count > len - 5u) {
        return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
    }
    for (uint8_t i = 0; i < count; ++i) {
        if (out->active_endpoint_count < NEARBY_ZIGBEE_MAX_ENDPOINTS) {
            out->active_endpoints[out->active_endpoint_count++] = payload[5u + i];
        } else {
            out->active_endpoint_overflow = true;
        }
    }
    return out->active_endpoint_overflow ? NEARBY_ZIGBEE_PARSE_PARTIAL
                                         : NEARBY_ZIGBEE_PARSE_OK;
}

static size_t fixed_zcl_type_size(uint8_t type)
{
    if (type >= 0x08u && type <= 0x0fu) {
        return (size_t)(type - 0x08u + 1u);
    }
    if (type == 0x10u) return 1u;
    if (type >= 0x18u && type <= 0x1fu) {
        return (size_t)(type - 0x18u + 1u);
    }
    if (type >= 0x20u && type <= 0x27u) {
        return (size_t)(type - 0x20u + 1u);
    }
    if (type >= 0x28u && type <= 0x2fu) {
        return (size_t)(type - 0x28u + 1u);
    }
    if (type == 0x30u) return 1u;
    if (type == 0x31u) return 2u;
    if (type == 0x38u) return 2u;
    if (type == 0x39u) return 4u;
    if (type == 0x3au) return 8u;
    if (type == 0xe0u || type == 0xe1u || type == 0xe2u) return 4u;
    if (type == 0xe8u || type == 0xe9u) return 2u;
    if (type == 0xeau) return 4u;
    return 0u;
}

static bool parse_zcl_string(const uint8_t *data, size_t len, size_t *pos,
                             char *out, size_t out_cap, bool *truncated)
{
    if (*pos >= len) {
        return false;
    }
    const uint8_t declared = data[(*pos)++];
    if (declared == 0xffu) {
        out[0] = '\0';
        *truncated = false;
        return true;
    }
    if ((size_t)declared > len - *pos) {
        return false;
    }
    const size_t copy_len = declared >= out_cap ? out_cap - 1u : declared;
    if (copy_len > 0u) {
        memcpy(out, data + *pos, copy_len);
    }
    out[copy_len] = '\0';
    *truncated = copy_len != declared;
    *pos += declared;
    return true;
}

static nearby_zigbee_parse_result_t parse_basic_zcl(
    const uint8_t *payload, size_t len, nearby_zigbee_discovery_facts_t *out)
{
    if (len < 3u) {
        return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
    }
    size_t pos = 0u;
    const uint8_t frame_control = payload[pos++];
    if ((frame_control & 0x04u) != 0u) {
        if (!advance(&pos, 2u, len)) {
            return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
        }
    }
    if (!advance(&pos, 1u, len) || pos >= len) {
        return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
    }
    const uint8_t command = payload[pos++];
    if (command != ZCL_CMD_READ_ATTR_RSP && command != ZCL_CMD_REPORT_ATTR) {
        return NEARBY_ZIGBEE_PARSE_UNSUPPORTED;
    }

    bool partial = false;
    while (pos < len) {
        if (len - pos < 2u) {
            return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
        }
        const uint16_t attr = le16(payload + pos);
        pos += 2u;
        if (command == ZCL_CMD_READ_ATTR_RSP) {
            if (pos >= len) return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
            const uint8_t status = payload[pos++];
            if (status != 0u) {
                continue;
            }
        }
        if (pos >= len) return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
        const uint8_t type = payload[pos++];
        if ((attr == ZCL_ATTR_MANUFACTURER_NAME || attr == ZCL_ATTR_MODEL_IDENTIFIER) &&
            (type == ZCL_TYPE_CHAR_STRING || type == ZCL_TYPE_OCTET_STRING)) {
            if (attr == ZCL_ATTR_MANUFACTURER_NAME) {
                if (!parse_zcl_string(payload, len, &pos, out->manufacturer,
                                      sizeof(out->manufacturer),
                                      &out->manufacturer_truncated)) {
                    return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
                }
                out->has_manufacturer = out->manufacturer[0] != '\0';
                partial = partial || out->manufacturer_truncated;
            } else {
                if (!parse_zcl_string(payload, len, &pos, out->model,
                                      sizeof(out->model),
                                      &out->model_truncated)) {
                    return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
                }
                out->has_model = out->model[0] != '\0';
                partial = partial || out->model_truncated;
            }
            continue;
        }
        if (type == ZCL_TYPE_CHAR_STRING || type == ZCL_TYPE_OCTET_STRING) {
            if (pos >= len) return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
            const uint8_t str_len = payload[pos++];
            if (str_len != 0xffu && !advance(&pos, str_len, len)) {
                return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
            }
            continue;
        }
        const size_t fixed = fixed_zcl_type_size(type);
        if (fixed == 0u) {
            return partial ? NEARBY_ZIGBEE_PARSE_PARTIAL
                           : NEARBY_ZIGBEE_PARSE_UNSUPPORTED;
        }
        if (!advance(&pos, fixed, len)) {
            return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
        }
    }
    return partial ? NEARBY_ZIGBEE_PARSE_PARTIAL : NEARBY_ZIGBEE_PARSE_OK;
}

nearby_zigbee_parse_result_t nearby_zigbee_parse_from_mac(
    const uint8_t *psdu,
    size_t psdu_len,
    const nearby_i154_mac_facts_t *mac,
    nearby_zigbee_discovery_facts_t *out)
{
    if (psdu == NULL || mac == NULL || out == NULL) {
        return NEARBY_ZIGBEE_PARSE_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (mac->payload_offset > psdu_len || mac->payload_len > psdu_len - mac->payload_offset) {
        return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
    }
    if (mac->security_enabled) {
        return NEARBY_ZIGBEE_PARSE_SECURED;
    }
    if (mac->frame_type != 1u) {
        return NEARBY_ZIGBEE_PARSE_UNSUPPORTED;
    }

    const uint8_t *nwk = psdu + mac->payload_offset;
    const size_t nwk_len = mac->payload_len;
    if (nwk_len < ZIGBEE_NWK_MIN_HEADER) {
        return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
    }
    const uint16_t nwk_fcf = le16(nwk);
    out->nwk_frame_type = (uint8_t)(nwk_fcf & 0x03u);
    out->nwk_protocol_version = (uint8_t)((nwk_fcf >> 2) & 0x0fu);
    const bool multicast = (nwk_fcf & (1u << 8)) != 0u;
    out->nwk_security = (nwk_fcf & (1u << 9)) != 0u;
    const bool source_route = (nwk_fcf & (1u << 10)) != 0u;
    const bool destination_ieee = (nwk_fcf & (1u << 11)) != 0u;
    const bool source_ieee = (nwk_fcf & (1u << 12)) != 0u;
    out->destination_nwk = le16(nwk + 2u);
    out->source_nwk = le16(nwk + 4u);
    out->radius = nwk[6u];
    out->nwk_sequence = nwk[7u];

    if (out->nwk_protocol_version != 2u || out->nwk_frame_type != 0u) {
        return NEARBY_ZIGBEE_PARSE_UNSUPPORTED;
    }
    size_t pos = 8u;
    if (destination_ieee) {
        if (!advance(&pos, 8u, nwk_len)) return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
        memcpy(out->destination_ieee, nwk + pos - 8u, 8u);
        out->has_destination_ieee = true;
    }
    if (source_ieee) {
        if (!advance(&pos, 8u, nwk_len)) return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
        memcpy(out->source_ieee, nwk + pos - 8u, 8u);
        out->has_source_ieee = true;
    }
    if (multicast && !advance(&pos, 1u, nwk_len)) {
        return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
    }
    if (source_route) {
        if (nwk_len - pos < 2u) return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
        const uint8_t relay_count = nwk[pos];
        pos += 2u;
        if (!advance(&pos, (size_t)relay_count * 2u, nwk_len)) {
            return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
        }
    }
    if (out->nwk_security) {
        return NEARBY_ZIGBEE_PARSE_SECURED;
    }
    if (nwk_len - pos < 8u) {
        return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
    }

    const uint8_t aps_fc = nwk[pos++];
    const uint8_t aps_frame_type = (uint8_t)(aps_fc & 0x03u);
    const uint8_t delivery_mode = (uint8_t)((aps_fc >> 2) & 0x03u);
    const bool aps_security = (aps_fc & (1u << 5)) != 0u;
    const bool aps_extended = (aps_fc & (1u << 7)) != 0u;
    if (aps_frame_type != 0u || delivery_mode == 3u || aps_extended) {
        return NEARBY_ZIGBEE_PARSE_UNSUPPORTED;
    }
    if (aps_security) {
        return NEARBY_ZIGBEE_PARSE_SECURED;
    }
    if (nwk_len - pos < 7u) {
        return NEARBY_ZIGBEE_PARSE_ERR_MALFORMED;
    }
    out->destination_endpoint = nwk[pos++];
    out->cluster_id = le16(nwk + pos);
    pos += 2u;
    out->profile_id = le16(nwk + pos);
    pos += 2u;
    out->source_endpoint = nwk[pos++];
    out->aps_counter = nwk[pos++];
    out->has_aps = true;

    const uint8_t *app = nwk + pos;
    const size_t app_len = nwk_len - pos;
    if (out->profile_id == 0x0000u) {
        switch (out->cluster_id) {
        case ZDO_DEVICE_ANNOUNCE_CLUSTER:
            return parse_device_announce(app, app_len, out);
        case ZDO_NODE_DESC_RSP_CLUSTER:
            return parse_node_descriptor_response(app, app_len, out);
        case ZDO_SIMPLE_DESC_RSP_CLUSTER:
            return parse_simple_descriptor_response(app, app_len, out);
        case ZDO_ACTIVE_EP_RSP_CLUSTER:
            return parse_active_endpoint_response(app, app_len, out);
        default:
            return NEARBY_ZIGBEE_PARSE_OK;
        }
    }
    if (out->cluster_id == ZCL_BASIC_CLUSTER) {
        const nearby_zigbee_parse_result_t zcl = parse_basic_zcl(app, app_len, out);
        return zcl == NEARBY_ZIGBEE_PARSE_UNSUPPORTED ? NEARBY_ZIGBEE_PARSE_OK : zcl;
    }
    return NEARBY_ZIGBEE_PARSE_OK;
}
