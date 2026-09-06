#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEARBY_WIFI_SSID_MAX 32u
#define NEARBY_WIFI_MAX_RATES 16u
#define NEARBY_WIFI_MAX_VENDOR_IES 4u
#define NEARBY_WIFI_VENDOR_IE_DATA_MAX 24u

typedef enum {
    NEARBY_WIFI_PARSE_OK = 0,
    NEARBY_WIFI_PARSE_PARTIAL = 1,
    NEARBY_WIFI_PARSE_UNSUPPORTED = 2,
    NEARBY_WIFI_PARSE_ERR_ARG = -1,
    NEARBY_WIFI_PARSE_ERR_MALFORMED = -2,
} nearby_wifi_parse_result_t;

typedef enum {
    NEARBY_WIFI_MGMT_PROBE_REQ = 4,
    NEARBY_WIFI_MGMT_PROBE_RESP = 5,
    NEARBY_WIFI_MGMT_BEACON = 8,
} nearby_wifi_mgmt_subtype_t;

typedef struct {
    uint8_t oui[3];
    uint8_t data[NEARBY_WIFI_VENDOR_IE_DATA_MAX];
    uint8_t data_len;
    bool truncated;
} nearby_wifi_vendor_ie_t;

typedef struct {
    uint8_t subtype;
    uint8_t destination[6];
    uint8_t source[6];
    uint8_t bssid[6];

    bool has_ssid;
    uint8_t ssid[NEARBY_WIFI_SSID_MAX];
    uint8_t ssid_len;
    bool hidden_ssid;

    bool has_channel;
    uint8_t channel;
    bool has_beacon_interval;
    uint16_t beacon_interval_tu;
    bool has_capability;
    uint16_t capability;

    uint8_t supported_rates[NEARBY_WIFI_MAX_RATES];
    uint8_t supported_rate_count;
    bool supported_rate_overflow;

    bool has_rsn;
    bool has_ht;
    bool has_he;
    bool has_wps;

    nearby_wifi_vendor_ie_t vendor_ies[NEARBY_WIFI_MAX_VENDOR_IES];
    uint8_t vendor_ie_count;
    bool vendor_ie_overflow;
    bool partial_tail;
} nearby_wifi_mgmt_facts_t;

typedef struct {
    int8_t rssi;
    uint8_t channel;
    uint16_t original_len;
    uint16_t captured_len;
    bool source_truncated;
} nearby_wifi_passive_meta_t;

typedef struct {
    nearby_wifi_passive_meta_t meta;
    nearby_wifi_mgmt_facts_t facts;
} nearby_wifi_passive_normalized_t;

typedef struct {
    uint8_t bssid[6];
    uint8_t ssid[NEARBY_WIFI_SSID_MAX];
    uint8_t ssid_len;
    bool hidden_ssid;
    uint8_t primary_channel;
    int8_t rssi;
    int authmode;
} nearby_wifi_active_facts_t;

nearby_wifi_parse_result_t nearby_wifi_parse_management_frame(
    const uint8_t *frame,
    size_t frame_len,
    bool source_truncated,
    nearby_wifi_mgmt_facts_t *out);

#ifdef __cplusplus
}
#endif
