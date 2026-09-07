#include "nearby_matter_discovery.h"

#include <ctype.h>
#include <string.h>

static const uint8_t k_matter_service_uuid[16] = {
    0x00, 0x00, 0xff, 0xf6, 0x00, 0x00, 0x10, 0x00,
    0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb,
};

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static bool ascii_eq_ci(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return false;
    }
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static bool uuid_is_matter(const nearby_ble_uuid_t *uuid)
{
    return uuid != NULL &&
           memcmp(uuid->bytes, k_matter_service_uuid, sizeof(k_matter_service_uuid)) == 0;
}

nearby_matter_parse_result_t nearby_matter_from_ble(
    const nearby_ble_normalized_t *ble,
    nearby_matter_discovery_facts_t *out)
{
    if (ble == NULL || out == NULL) {
        return NEARBY_MATTER_PARSE_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->source = NEARBY_MATTER_SOURCE_BLE;

    const nearby_ble_service_data_t *matter = NULL;
    for (uint8_t i = 0; i < ble->facts.service_data_count; ++i) {
        if (uuid_is_matter(&ble->facts.service_data[i].uuid)) {
            matter = &ble->facts.service_data[i];
            break;
        }
    }
    if (matter == NULL) {
        return NEARBY_MATTER_PARSE_NOT_MATTER;
    }
    if (matter->data_len < 8u) {
        return matter->data_truncated || ble->facts.source_incomplete
                   ? NEARBY_MATTER_PARSE_PARTIAL
                   : NEARBY_MATTER_PARSE_ERR_MALFORMED;
    }

    out->has_opcode = true;
    out->opcode = matter->data[0];
    out->has_discriminator = true;
    out->discriminator = (uint16_t)(le16(matter->data + 1u) & 0x0fffu);
    out->has_advertisement_version = true;
    out->advertisement_version = (uint8_t)((matter->data[2] >> 4) & 0x0fu);
    out->has_vendor_product = true;
    out->vendor_id = le16(matter->data + 3u);
    out->product_id = le16(matter->data + 5u);
    out->has_additional_data_flags = true;
    out->additional_data_flags = matter->data[7u];

    return (matter->data_truncated || ble->facts.source_incomplete)
               ? NEARBY_MATTER_PARSE_PARTIAL : NEARBY_MATTER_PARSE_OK;
}

static const nearby_mdns_txt_t *find_txt_ci(const nearby_mdns_service_t *service,
                                            const char *key)
{
    for (uint8_t i = 0; i < service->txt_count; ++i) {
        if (ascii_eq_ci(service->txt[i].key, key)) {
            return &service->txt[i];
        }
    }
    return NULL;
}

static bool parse_decimal_u32(const char *s, uint32_t max_value, uint32_t *out)
{
    if (s == NULL || out == NULL || *s == '\0') {
        return false;
    }
    uint32_t value = 0u;
    for (const char *p = s; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        const uint32_t digit = (uint32_t)(*p - '0');
        if (value > (max_value - digit) / 10u) {
            return false;
        }
        value = value * 10u + digit;
    }
    *out = value;
    return true;
}

static bool parse_vp(const char *s, uint16_t *vendor, uint16_t *product)
{
    if (s == NULL || vendor == NULL || product == NULL) {
        return false;
    }
    const char *plus = strchr(s, '+');
    if (plus == NULL || plus == s || plus[1] == '\0' || strchr(plus + 1, '+') != NULL) {
        return false;
    }
    char vendor_text[6];
    char product_text[6];
    const size_t vendor_len = (size_t)(plus - s);
    const size_t product_len = strlen(plus + 1);
    if (vendor_len >= sizeof(vendor_text) || product_len >= sizeof(product_text)) {
        return false;
    }
    memcpy(vendor_text, s, vendor_len);
    vendor_text[vendor_len] = '\0';
    memcpy(product_text, plus + 1, product_len + 1u);
    uint32_t v = 0u;
    uint32_t p = 0u;
    if (!parse_decimal_u32(vendor_text, UINT16_MAX, &v) ||
        !parse_decimal_u32(product_text, UINT16_MAX, &p)) {
        return false;
    }
    *vendor = (uint16_t)v;
    *product = (uint16_t)p;
    return true;
}

nearby_matter_parse_result_t nearby_matter_from_mdns(
    const nearby_mdns_service_t *service,
    nearby_matter_discovery_facts_t *out)
{
    if (service == NULL || out == NULL) {
        return NEARBY_MATTER_PARSE_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->source = NEARBY_MATTER_SOURCE_MDNS;

    if (!ascii_eq_ci(service->service_type, "_matterc._udp.local.")) {
        return NEARBY_MATTER_PARSE_NOT_MATTER;
    }

    bool partial = service->txt_overflow;
    const nearby_mdns_txt_t *d = find_txt_ci(service, "D");
    if (d != NULL) {
        if (!d->has_value || d->truncated) {
            partial = true;
        } else {
            uint32_t value = 0u;
            if (!parse_decimal_u32(d->value, 0x0fffu, &value)) {
                return NEARBY_MATTER_PARSE_ERR_MALFORMED;
            }
            out->has_discriminator = true;
            out->discriminator = (uint16_t)value;
        }
    }

    const nearby_mdns_txt_t *vp = find_txt_ci(service, "VP");
    if (vp != NULL) {
        if (!vp->has_value || vp->truncated) {
            partial = true;
        } else if (!parse_vp(vp->value, &out->vendor_id, &out->product_id)) {
            return NEARBY_MATTER_PARSE_ERR_MALFORMED;
        } else {
            out->has_vendor_product = true;
        }
    }

    const nearby_mdns_txt_t *cm = find_txt_ci(service, "CM");
    if (cm != NULL) {
        if (!cm->has_value || cm->truncated) {
            partial = true;
        } else {
            uint32_t value = 0u;
            if (!parse_decimal_u32(cm->value, UINT8_MAX, &value)) {
                return NEARBY_MATTER_PARSE_ERR_MALFORMED;
            }
            out->has_commissioning_mode = true;
            out->commissioning_mode = (uint8_t)value;
        }
    }

    const nearby_mdns_txt_t *dt = find_txt_ci(service, "DT");
    if (dt != NULL) {
        if (!dt->has_value || dt->truncated) {
            partial = true;
        } else {
            uint32_t value = 0u;
            if (!parse_decimal_u32(dt->value, UINT32_MAX, &value)) {
                return NEARBY_MATTER_PARSE_ERR_MALFORMED;
            }
            out->has_device_type = true;
            out->device_type = value;
        }
    }

    return partial ? NEARBY_MATTER_PARSE_PARTIAL : NEARBY_MATTER_PARSE_OK;
}
