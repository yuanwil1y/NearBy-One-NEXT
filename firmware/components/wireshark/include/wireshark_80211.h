#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIRESHARK_80211_SSID_MAX 32u
#define WIRESHARK_80211_MAX_RATES 16u
#define WIRESHARK_80211_MAX_VENDOR_IES 4u
#define WIRESHARK_80211_VENDOR_IE_DATA_MAX 24u

typedef enum {
    WIRESHARK_80211_PARSE_OK = 0,
    WIRESHARK_80211_PARSE_PARTIAL = 1,
    WIRESHARK_80211_PARSE_UNSUPPORTED = 2,
    WIRESHARK_80211_PARSE_ERR_ARG = -1,
    WIRESHARK_80211_PARSE_ERR_MALFORMED = -2,
} wireshark_80211_parse_result_t;

typedef enum {
    WIRESHARK_80211_MGMT_PROBE_REQ = 4,
    WIRESHARK_80211_MGMT_PROBE_RESP = 5,
    WIRESHARK_80211_MGMT_BEACON = 8,
} wireshark_80211_mgmt_subtype_t;

typedef struct {
    uint8_t oui[3];
    uint8_t data[WIRESHARK_80211_VENDOR_IE_DATA_MAX];
    uint8_t data_len;
    bool truncated;
} wireshark_80211_vendor_ie_t;

typedef struct {
    uint8_t subtype;
    uint8_t destination[6];
    uint8_t source[6];
    uint8_t bssid[6];

    bool has_ssid;
    uint8_t ssid[WIRESHARK_80211_SSID_MAX];
    uint8_t ssid_len;
    bool hidden_ssid;

    bool has_channel;
    uint8_t channel;
    bool has_beacon_interval;
    uint16_t beacon_interval_tu;
    bool has_capability;
    uint16_t capability;

    uint8_t supported_rates[WIRESHARK_80211_MAX_RATES];
    uint8_t supported_rate_count;
    bool supported_rate_overflow;

    bool has_rsn;
    bool has_ht;
    bool has_he;
    bool has_wps;

    wireshark_80211_vendor_ie_t vendor_ies[WIRESHARK_80211_MAX_VENDOR_IES];
    uint8_t vendor_ie_count;
    bool vendor_ie_overflow;
    bool partial_tail;
} wireshark_80211_mgmt_facts_t;

typedef struct {
    int8_t rssi;
    uint8_t channel;
    uint16_t original_len;
    uint16_t captured_len;
    bool source_truncated;
} wireshark_80211_passive_meta_t;

typedef struct {
    wireshark_80211_passive_meta_t meta;
    wireshark_80211_mgmt_facts_t facts;
} wireshark_80211_passive_observation_t;

/*
 * Phase 1 public contract only. The parser implementation remains in the
 * legacy discovery_parser component until Phase 2.
 */
wireshark_80211_parse_result_t wireshark_80211_parse_mgmt(
    const uint8_t *frame,
    size_t frame_len,
    bool source_truncated,
    wireshark_80211_mgmt_facts_t *out);

#ifdef __cplusplus
}
#endif
