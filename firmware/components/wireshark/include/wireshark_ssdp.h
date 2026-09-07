#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wireshark_lan.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIRESHARK_SSDP_START_LINE_MAX 80
#define WIRESHARK_SSDP_VALUE_MAX 160

typedef struct {
    char start_line[WIRESHARK_SSDP_START_LINE_MAX + 1];
    char st[WIRESHARK_SSDP_VALUE_MAX + 1];
    char nt[WIRESHARK_SSDP_VALUE_MAX + 1];
    char usn[WIRESHARK_SSDP_VALUE_MAX + 1];
    char server[WIRESHARK_SSDP_VALUE_MAX + 1];
    char location[WIRESHARK_SSDP_VALUE_MAX + 1];
    char man[WIRESHARK_SSDP_VALUE_MAX + 1];
    char mx[16];
    char device_type[WIRESHARK_SSDP_VALUE_MAX + 1];
    char manufacturer[WIRESHARK_SSDP_VALUE_MAX + 1];
    char manufacturer_url[WIRESHARK_SSDP_VALUE_MAX + 1];
    char model_name[WIRESHARK_SSDP_VALUE_MAX + 1];
    char model_number[64];
    uint16_t header_count;
    bool value_truncated;
    bool header_limit_reached;
} wireshark_ssdp_facts_t;

/* Phase 1 contract; implementation remains under nearby_ssdp_parse(). */
wireshark_lan_parse_result_t wireshark_ssdp_parse(
    const uint8_t *message,
    size_t message_len,
    wireshark_ssdp_facts_t *out);

#ifdef __cplusplus
}
#endif
