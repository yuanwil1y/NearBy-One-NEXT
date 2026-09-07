#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIRESHARK_I154_ADDR_MAX 8u

typedef enum {
    WIRESHARK_I154_PARSE_OK = 0,
    WIRESHARK_I154_PARSE_SECURED = 1,
    WIRESHARK_I154_PARSE_UNSUPPORTED = 2,
    WIRESHARK_I154_PARSE_PARTIAL = 3,
    WIRESHARK_I154_PARSE_ERR_ARG = -1,
    WIRESHARK_I154_PARSE_ERR_MALFORMED = -2,
} wireshark_i154_parse_result_t;

typedef struct {
    uint8_t frame_type;
    uint8_t frame_version;
    bool security_enabled;
    bool frame_pending;
    bool ack_request;
    bool pan_id_compression;
    bool sequence_suppressed;
    bool ie_present;

    bool has_sequence;
    uint8_t sequence;

    uint8_t destination_addr_mode;
    bool has_destination_pan;
    uint16_t destination_pan;
    uint8_t destination_addr[WIRESHARK_I154_ADDR_MAX];
    uint8_t destination_addr_len;

    uint8_t source_addr_mode;
    bool has_source_pan;
    uint16_t source_pan;
    uint8_t source_addr[WIRESHARK_I154_ADDR_MAX];
    uint8_t source_addr_len;

    uint16_t payload_offset;
    uint16_t payload_len;
} wireshark_i154_mac_facts_t;

typedef struct {
    uint8_t channel;
    int8_t rssi;
    uint8_t lqi;
    uint64_t timestamp;
    uint8_t phy_length;
} wireshark_i154_native_meta_t;

typedef struct {
    wireshark_i154_native_meta_t meta;
    wireshark_i154_mac_facts_t mac;
} wireshark_i154_observation_t;

/* Phase 1 contract; implementation remains under nearby_i154_parse_mac(). */
wireshark_i154_parse_result_t wireshark_i154_parse_mac(
    const uint8_t *psdu,
    size_t psdu_len,
    wireshark_i154_mac_facts_t *out);

#ifdef __cplusplus
}
#endif
