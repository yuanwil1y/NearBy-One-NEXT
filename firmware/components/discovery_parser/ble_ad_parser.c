#include "nearby_ble_discovery.h"

#include <stdio.h>
#include <string.h>

#define BLE_AD_FLAGS 0x01u
#define BLE_AD_UUID16_INCOMPLETE 0x02u
#define BLE_AD_UUID16_COMPLETE 0x03u
#define BLE_AD_UUID32_INCOMPLETE 0x04u
#define BLE_AD_UUID32_COMPLETE 0x05u
#define BLE_AD_UUID128_INCOMPLETE 0x06u
#define BLE_AD_UUID128_COMPLETE 0x07u
#define BLE_AD_NAME_SHORT 0x08u
#define BLE_AD_NAME_COMPLETE 0x09u
#define BLE_AD_TX_POWER 0x0Au
#define BLE_AD_SERVICE_DATA16 0x16u
#define BLE_AD_APPEARANCE 0x19u
#define BLE_AD_SERVICE_DATA32 0x20u
#define BLE_AD_SERVICE_DATA128 0x21u
#define BLE_AD_MANUFACTURER 0xFFu

static const uint8_t k_bt_base_uuid[16] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x00,
    0x80,0x00,0x00,0x80,0x5f,0x9b,0x34,0xfb,
};

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void uuid16_wire(const uint8_t *p, nearby_ble_uuid_t *out)
{
    memcpy(out->bytes, k_bt_base_uuid, sizeof(out->bytes));
    const uint16_t v = le16(p);
    out->bytes[2] = (uint8_t)(v >> 8);
    out->bytes[3] = (uint8_t)v;
}

static void uuid32_wire(const uint8_t *p, nearby_ble_uuid_t *out)
{
    memcpy(out->bytes, k_bt_base_uuid, sizeof(out->bytes));
    const uint32_t v = le32(p);
    out->bytes[0] = (uint8_t)(v >> 24);
    out->bytes[1] = (uint8_t)(v >> 16);
    out->bytes[2] = (uint8_t)(v >> 8);
    out->bytes[3] = (uint8_t)v;
}

static void uuid128_wire(const uint8_t *p, nearby_ble_uuid_t *out)
{
    for (size_t i = 0; i < 16; ++i) {
        out->bytes[i] = p[15 - i];
    }
}

static bool uuid_eq(const nearby_ble_uuid_t *a, const nearby_ble_uuid_t *b)
{
    return memcmp(a->bytes, b->bytes, sizeof(a->bytes)) == 0;
}

bool nearby_ble_has_service_uuid(const nearby_ble_discovery_facts_t *facts,
                                 const nearby_ble_uuid_t *uuid)
{
    if (facts == NULL || uuid == NULL) {
        return false;
    }
    for (uint8_t i = 0; i < facts->service_uuid_count; ++i) {
        if (uuid_eq(&facts->service_uuids[i], uuid)) {
            return true;
        }
    }
    return false;
}

static void add_uuid(nearby_ble_discovery_facts_t *out,
                     const nearby_ble_uuid_t *uuid)
{
    if (nearby_ble_has_service_uuid(out, uuid)) {
        return;
    }
    if (out->service_uuid_count >= NEARBY_BLE_MAX_SERVICE_UUIDS) {
        out->service_uuid_overflow = true;
        return;
    }
    out->service_uuids[out->service_uuid_count++] = *uuid;
}

static void bounded_copy(uint8_t *dst, uint8_t *out_len, bool *out_trunc,
                         const uint8_t *src, size_t len)
{
    const size_t n = len > NEARBY_BLE_MATCH_BYTES_MAX
                         ? NEARBY_BLE_MATCH_BYTES_MAX
                         : len;
    if (n != 0) {
        memcpy(dst, src, n);
    }
    *out_len = (uint8_t)n;
    *out_trunc = n != len;
}

static void add_service_data(nearby_ble_discovery_facts_t *out,
                             const nearby_ble_uuid_t *uuid,
                             const uint8_t *data, size_t len)
{
    for (uint8_t i = 0; i < out->service_data_count; ++i) {
        if (uuid_eq(&out->service_data[i].uuid, uuid)) {
            bounded_copy(out->service_data[i].data,
                         &out->service_data[i].data_len,
                         &out->service_data[i].data_truncated,
                         data, len);
            return;
        }
    }
    if (out->service_data_count >= NEARBY_BLE_MAX_SERVICE_DATA) {
        out->service_data_overflow = true;
        return;
    }
    nearby_ble_service_data_t *slot = &out->service_data[out->service_data_count++];
    slot->uuid = *uuid;
    bounded_copy(slot->data, &slot->data_len, &slot->data_truncated, data, len);
}

static void add_manufacturer_data(nearby_ble_discovery_facts_t *out,
                                  uint16_t company_id,
                                  const uint8_t *data, size_t len)
{
    for (uint8_t i = 0; i < out->manufacturer_data_count; ++i) {
        if (out->manufacturer_data[i].company_id == company_id) {
            bounded_copy(out->manufacturer_data[i].data,
                         &out->manufacturer_data[i].data_len,
                         &out->manufacturer_data[i].data_truncated,
                         data, len);
            return;
        }
    }
    if (out->manufacturer_data_count >= NEARBY_BLE_MAX_MANUFACTURER_DATA) {
        out->manufacturer_data_overflow = true;
        return;
    }
    nearby_ble_manufacturer_data_t *slot =
        &out->manufacturer_data[out->manufacturer_data_count++];
    slot->company_id = company_id;
    bounded_copy(slot->data, &slot->data_len, &slot->data_truncated, data, len);
}

const nearby_ble_manufacturer_data_t *nearby_ble_find_manufacturer_data(
    const nearby_ble_discovery_facts_t *facts, uint16_t company_id)
{
    if (facts == NULL) {
        return NULL;
    }
    for (uint8_t i = 0; i < facts->manufacturer_data_count; ++i) {
        if (facts->manufacturer_data[i].company_id == company_id) {
            return &facts->manufacturer_data[i];
        }
    }
    return NULL;
}

const nearby_ble_service_data_t *nearby_ble_find_service_data(
    const nearby_ble_discovery_facts_t *facts, const nearby_ble_uuid_t *uuid)
{
    if (facts == NULL || uuid == NULL) {
        return NULL;
    }
    for (uint8_t i = 0; i < facts->service_data_count; ++i) {
        if (uuid_eq(&facts->service_data[i].uuid, uuid)) {
            return &facts->service_data[i];
        }
    }
    return NULL;
}

static void parse_name(nearby_ble_discovery_facts_t *out, bool complete,
                       const uint8_t *data, size_t len)
{
    if (out->has_local_name && out->local_name_complete && !complete) {
        return;
    }
    const size_t n = len > NEARBY_BLE_LOCAL_NAME_MAX
                         ? NEARBY_BLE_LOCAL_NAME_MAX
                         : len;
    if (n != 0) {
        memcpy(out->local_name, data, n);
    }
    out->local_name[n] = '\0';
    out->has_local_name = true;
    out->local_name_complete = complete;
    out->local_name_truncated = n != len;
}

static bool parse_uuid_list(nearby_ble_discovery_facts_t *out,
                            const uint8_t *data, size_t len, size_t width)
{
    if (len % width != 0) {
        return false;
    }
    for (size_t off = 0; off < len; off += width) {
        nearby_ble_uuid_t uuid;
        if (width == 2) {
            uuid16_wire(data + off, &uuid);
        } else if (width == 4) {
            uuid32_wire(data + off, &uuid);
        } else {
            uuid128_wire(data + off, &uuid);
        }
        add_uuid(out, &uuid);
    }
    return true;
}

static bool parse_service_data(nearby_ble_discovery_facts_t *out,
                               const uint8_t *data, size_t len, size_t width)
{
    if (len < width) {
        return false;
    }
    nearby_ble_uuid_t uuid;
    if (width == 2) {
        uuid16_wire(data, &uuid);
    } else if (width == 4) {
        uuid32_wire(data, &uuid);
    } else {
        uuid128_wire(data, &uuid);
    }
    add_service_data(out, &uuid, data + width, len - width);
    add_uuid(out, &uuid);
    return true;
}

nearby_ble_parse_result_t nearby_ble_parse_advertisement(
    const uint8_t *data, size_t data_len, bool source_incomplete,
    nearby_ble_discovery_facts_t *out)
{
    if (out == NULL || (data == NULL && data_len != 0)) {
        return NEARBY_BLE_PARSE_ERR_ARG;
    }

    memset(out, 0, sizeof(*out));
    out->source_incomplete = source_incomplete;

    size_t off = 0;
    while (off < data_len) {
        const uint8_t ad_len = data[off++];
        if (ad_len == 0) {
            break;
        }
        if ((size_t)ad_len > data_len - off) {
            out->partial_tail = true;
            return source_incomplete ? NEARBY_BLE_PARSE_PARTIAL
                                     : NEARBY_BLE_PARSE_ERR_MALFORMED;
        }

        const uint8_t type = data[off];
        const uint8_t *payload = data + off + 1;
        const size_t payload_len = (size_t)ad_len - 1;
        bool field_ok = true;

        switch (type) {
        case BLE_AD_FLAGS:
            if (payload_len >= 1) {
                out->has_flags = true;
                out->flags = payload[0];
            } else {
                field_ok = false;
            }
            break;
        case BLE_AD_NAME_SHORT:
        case BLE_AD_NAME_COMPLETE:
            parse_name(out, type == BLE_AD_NAME_COMPLETE, payload, payload_len);
            break;
        case BLE_AD_UUID16_INCOMPLETE:
        case BLE_AD_UUID16_COMPLETE:
            field_ok = parse_uuid_list(out, payload, payload_len, 2);
            break;
        case BLE_AD_UUID32_INCOMPLETE:
        case BLE_AD_UUID32_COMPLETE:
            field_ok = parse_uuid_list(out, payload, payload_len, 4);
            break;
        case BLE_AD_UUID128_INCOMPLETE:
        case BLE_AD_UUID128_COMPLETE:
            field_ok = parse_uuid_list(out, payload, payload_len, 16);
            break;
        case BLE_AD_SERVICE_DATA16:
            field_ok = parse_service_data(out, payload, payload_len, 2);
            break;
        case BLE_AD_SERVICE_DATA32:
            field_ok = parse_service_data(out, payload, payload_len, 4);
            break;
        case BLE_AD_SERVICE_DATA128:
            field_ok = parse_service_data(out, payload, payload_len, 16);
            break;
        case BLE_AD_MANUFACTURER:
            if (payload_len >= 2) {
                add_manufacturer_data(out, le16(payload),
                                      payload + 2, payload_len - 2);
            } else {
                field_ok = false;
            }
            break;
        case BLE_AD_TX_POWER:
            if (payload_len >= 1) {
                out->has_tx_power = true;
                out->tx_power = (int8_t)payload[0];
            } else {
                field_ok = false;
            }
            break;
        case BLE_AD_APPEARANCE:
            if (payload_len >= 2) {
                out->has_appearance = true;
                out->appearance = le16(payload);
            } else {
                field_ok = false;
            }
            break;
        default:
            break;
        }

        if (!field_ok) {
            return NEARBY_BLE_PARSE_ERR_MALFORMED;
        }
        off += ad_len;
    }

    return source_incomplete ? NEARBY_BLE_PARSE_PARTIAL : NEARBY_BLE_PARSE_OK;
}

void nearby_ble_uuid_format(const nearby_ble_uuid_t *uuid,
                            char out[NEARBY_BLE_UUID_STR_LEN])
{
    if (out == NULL) {
        return;
    }
    if (uuid == NULL) {
        out[0] = '\0';
        return;
    }
    (void)snprintf(out, NEARBY_BLE_UUID_STR_LEN,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        uuid->bytes[0], uuid->bytes[1], uuid->bytes[2], uuid->bytes[3],
        uuid->bytes[4], uuid->bytes[5], uuid->bytes[6], uuid->bytes[7],
        uuid->bytes[8], uuid->bytes[9], uuid->bytes[10], uuid->bytes[11],
        uuid->bytes[12], uuid->bytes[13], uuid->bytes[14], uuid->bytes[15]);
}
