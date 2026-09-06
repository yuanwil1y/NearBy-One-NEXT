#include "nearby_matcher_keys.h"

#include <stdio.h>
#include <string.h>

static nearby_matcher_key_result_t format_key(char *out,
                                              size_t out_size,
                                              const char *fmt,
                                              const char *a,
                                              const char *b)
{
    if (out == NULL || out_size == 0 || fmt == NULL || a == NULL) {
        return NEARBY_MATCHER_KEY_ERR_ARG;
    }
    int n;
    if (b != NULL) {
        n = snprintf(out, out_size, fmt, a, b);
    } else {
        n = snprintf(out, out_size, fmt, a);
    }
    if (n < 0 || (size_t)n >= out_size || (size_t)n >= NEARBY_MATCHER_KEY_MAX) {
        if (out_size > 0) {
            out[0] = '\0';
        }
        return NEARBY_MATCHER_KEY_ERR_RANGE;
    }
    return NEARBY_MATCHER_KEY_OK;
}

nearby_matcher_key_result_t nearby_match_key_zha_manufacturer_model(
    const char *manufacturer,
    const char *model,
    char *out,
    size_t out_size)
{
    if (manufacturer == NULL || model == NULL || manufacturer[0] == '\0' ||
        model[0] == '\0') {
        return NEARBY_MATCHER_KEY_ERR_ARG;
    }
    return format_key(out, out_size, "zigbee:%s\x1f%s", manufacturer, model);
}

nearby_matcher_key_result_t nearby_match_key_z2m_model(
    const char *model,
    char *out,
    size_t out_size)
{
    if (model == NULL || model[0] == '\0') {
        return NEARBY_MATCHER_KEY_ERR_ARG;
    }
    return format_key(out, out_size, "zigbee:model:%s", model, NULL);
}

nearby_matcher_key_result_t nearby_match_key_z2m_fingerprint(
    const char *manufacturer,
    const char *model,
    char *out,
    size_t out_size)
{
    if (manufacturer == NULL || model == NULL || manufacturer[0] == '\0' ||
        model[0] == '\0') {
        return NEARBY_MATCHER_KEY_ERR_ARG;
    }
    return format_key(out, out_size, "zigbee:fingerprint:%s\x1f%s",
                      manufacturer, model);
}

nearby_matcher_key_result_t nearby_match_key_matter_vidpid(
    uint16_t vendor_id,
    uint16_t product_id,
    char *out,
    size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return NEARBY_MATCHER_KEY_ERR_ARG;
    }
    const int n = snprintf(out, out_size, "matter:vidpid:%04x:%04x",
                           vendor_id, product_id);
    if (n < 0 || (size_t)n >= out_size || (size_t)n >= NEARBY_MATCHER_KEY_MAX) {
        out[0] = '\0';
        return NEARBY_MATCHER_KEY_ERR_RANGE;
    }
    return NEARBY_MATCHER_KEY_OK;
}

nearby_matcher_key_result_t nearby_match_key_oui24(
    const uint8_t mac[6],
    char *out,
    size_t out_size)
{
    if (mac == NULL || out == NULL || out_size == 0) {
        return NEARBY_MATCHER_KEY_ERR_ARG;
    }
    const int n = snprintf(out, out_size, "oui:%02X%02X%02X",
                           mac[0], mac[1], mac[2]);
    if (n < 0 || (size_t)n >= out_size || (size_t)n >= NEARBY_MATCHER_KEY_MAX) {
        out[0] = '\0';
        return NEARBY_MATCHER_KEY_ERR_RANGE;
    }
    return NEARBY_MATCHER_KEY_OK;
}
