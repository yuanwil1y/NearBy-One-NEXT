#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kismet_types.h"
#include "wireshark_80211.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Phase 1 compatibility aliases. Wireshark owns parsed 802.11 facts. */
#define NEARBY_WIFI_SSID_MAX WIRESHARK_80211_SSID_MAX
#define NEARBY_WIFI_MAX_RATES WIRESHARK_80211_MAX_RATES
#define NEARBY_WIFI_MAX_VENDOR_IES WIRESHARK_80211_MAX_VENDOR_IES
#define NEARBY_WIFI_VENDOR_IE_DATA_MAX WIRESHARK_80211_VENDOR_IE_DATA_MAX

typedef wireshark_80211_parse_result_t nearby_wifi_parse_result_t;
#define NEARBY_WIFI_PARSE_OK WIRESHARK_80211_PARSE_OK
#define NEARBY_WIFI_PARSE_PARTIAL WIRESHARK_80211_PARSE_PARTIAL
#define NEARBY_WIFI_PARSE_UNSUPPORTED WIRESHARK_80211_PARSE_UNSUPPORTED
#define NEARBY_WIFI_PARSE_ERR_ARG WIRESHARK_80211_PARSE_ERR_ARG
#define NEARBY_WIFI_PARSE_ERR_MALFORMED WIRESHARK_80211_PARSE_ERR_MALFORMED

typedef wireshark_80211_mgmt_subtype_t nearby_wifi_mgmt_subtype_t;
#define NEARBY_WIFI_MGMT_PROBE_REQ WIRESHARK_80211_MGMT_PROBE_REQ
#define NEARBY_WIFI_MGMT_PROBE_RESP WIRESHARK_80211_MGMT_PROBE_RESP
#define NEARBY_WIFI_MGMT_BEACON WIRESHARK_80211_MGMT_BEACON

typedef wireshark_80211_vendor_ie_t nearby_wifi_vendor_ie_t;
typedef wireshark_80211_mgmt_facts_t nearby_wifi_mgmt_facts_t;
typedef wireshark_80211_passive_meta_t nearby_wifi_passive_meta_t;
typedef wireshark_80211_passive_observation_t nearby_wifi_passive_normalized_t;

/* Active scan records are acquisition observations and belong to Kismet. */
typedef kismet_wifi_active_facts_t nearby_wifi_active_facts_t;

/* Legacy symbol retained until the Phase 2 implementation migration. */
nearby_wifi_parse_result_t nearby_wifi_parse_management_frame(
    const uint8_t *frame,
    size_t frame_len,
    bool source_truncated,
    nearby_wifi_mgmt_facts_t *out);

#ifdef __cplusplus
}
#endif
