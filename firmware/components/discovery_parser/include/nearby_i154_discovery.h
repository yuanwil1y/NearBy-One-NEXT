#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEARBY_I154_ADDR_MAX 8u

typedef enum {
    NEARBY_I154_PARSE_OK = 0,
    NEARBY_I154_PARSE_SECURED = 1,
    NEARBY_I154_PARSE_UNSUPPORTED = 2,
    NEARBY_I154_PARSE_PARTIAL = 3,
    NEARBY_I154_PARSE_ERR_ARG = -1,
    NEARBY_I154_PARSE_ERR_MALFORMED = -2,
} nearby_i154_parse_result_t;

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
    uint8_t destination_addr[NEARBY_I154_ADDR_MAX];
    uint8_t destination_addr_len;

    uint8_t source_addr_mode;
    bool has_source_pan;
    uint16_t source_pan;
    uint8_t source_addr[NEARBY_I154_ADDR_MAX];
    uint8_t source_addr_len;

    uint16_t payload_offset;
    uint16_t payload_len;
} nearby_i154_mac_facts_t;

typedef struct {
    uint8_t channel;
    int8_t rssi;
    uint8_t lqi;
    uint64_t timestamp;
    uint8_t phy_length;
} nearby_i154_native_meta_t;

typedef struct {
    nearby_i154_native_meta_t meta;
    nearby_i154_mac_facts_t mac;
} nearby_i154_normalized_t;

nearby_i154_parse_result_t nearby_i154_parse_mac(const uint8_t *psdu,
                                                 size_t psdu_len,
                                                 nearby_i154_mac_facts_t *out);

#ifdef __cplusplus
}
#endif
