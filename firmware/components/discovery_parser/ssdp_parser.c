#include "nearby_lan_discovery.h"

#include <ctype.h>
#include <string.h>

#define SSDP_MAX_HEADERS 32

static size_t trim_right(const uint8_t *s, size_t len)
{
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
                       s[len - 1] == '\r' || s[len - 1] == '\n')) {
        --len;
    }
    return len;
}

static void copy_text(char *dst, size_t cap, const uint8_t *src, size_t len,
                      bool *truncated)
{
    while (len > 0 && (*src == ' ' || *src == '\t')) {
        ++src;
        --len;
    }
    len = trim_right(src, len);
    const size_t n = len >= cap ? cap - 1 : len;
    if (n > 0) {
        memcpy(dst, src, n);
    }
    dst[n] = '\0';
    if (n != len) {
        *truncated = true;
    }
}

static bool name_eq_ci(const uint8_t *name, size_t len, const char *lit)
{
    const size_t lit_len = strlen(lit);
    if (len != lit_len) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        if (tolower((unsigned char)name[i]) !=
            tolower((unsigned char)lit[i])) {
            return false;
        }
    }
    return true;
}

nearby_lan_parse_result_t nearby_ssdp_parse(const uint8_t *message,
                                            size_t message_len,
                                            nearby_ssdp_facts_t *out)
{
    if (out == NULL || (message == NULL && message_len != 0)) {
        return NEARBY_LAN_PARSE_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (message_len == 0) {
        return NEARBY_LAN_PARSE_ERR_MALFORMED;
    }

    bool partial = false;
    size_t pos = 0;
    size_t line_no = 0;
    while (pos < message_len) {
        size_t end = pos;
        while (end < message_len && message[end] != '\n') {
            ++end;
        }
        size_t len = end - pos;
        if (len > 0 && message[pos + len - 1] == '\r') {
            --len;
        }

        if (line_no == 0) {
            copy_text(out->start_line, sizeof(out->start_line),
                      message + pos, len, &out->value_truncated);
            if (out->start_line[0] == '\0') {
                return NEARBY_LAN_PARSE_ERR_MALFORMED;
            }
        } else {
            if (len == 0) {
                return partial || out->value_truncated
                           ? NEARBY_LAN_PARSE_PARTIAL
                           : NEARBY_LAN_PARSE_OK;
            }
            if (out->header_count >= SSDP_MAX_HEADERS) {
                out->header_limit_reached = true;
                return NEARBY_LAN_PARSE_PARTIAL;
            }
            ++out->header_count;

            size_t colon = 0;
            while (colon < len && message[pos + colon] != ':') {
                ++colon;
            }
            if (colon == 0 || colon == len) {
                return NEARBY_LAN_PARSE_ERR_MALFORMED;
            }
            const uint8_t *value = message + pos + colon + 1;
            const size_t value_len = len - colon - 1;
            const uint8_t *name = message + pos;

#define SET_HEADER(field, literal) \
            if (name_eq_ci(name, colon, literal)) { \
                copy_text(out->field, sizeof(out->field), value, value_len, \
                          &out->value_truncated); \
            } else

            SET_HEADER(st, "st")
            SET_HEADER(nt, "nt")
            SET_HEADER(usn, "usn")
            SET_HEADER(server, "server")
            SET_HEADER(location, "location")
            SET_HEADER(man, "man")
            SET_HEADER(mx, "mx")
            SET_HEADER(device_type, "devicetype")
            SET_HEADER(manufacturer, "manufacturer")
            SET_HEADER(manufacturer_url, "manufacturerurl")
            SET_HEADER(model_name, "modelname")
            SET_HEADER(model_number, "modelnumber")
            {
                /* Unknown SSDP/HTTPU headers are intentionally ignored. */
            }
#undef SET_HEADER
        }

        ++line_no;
        if (end == message_len) {
            pos = end;
            break;
        }
        pos = end + 1;
    }

    /* UDP payloads occasionally omit the final empty CRLF line; accept them. */
    partial = out->value_truncated || out->header_limit_reached;
    return partial ? NEARBY_LAN_PARSE_PARTIAL : NEARBY_LAN_PARSE_OK;
}
