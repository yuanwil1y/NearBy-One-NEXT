#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "kismet_types.h"
#include "wireshark_dhcp.h"
#include "wireshark_mdns.h"
#include "wireshark_ssdp.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KISMET_LAN_RX_MAX 1536u
#define KISMET_MDNS_DISCOVERED_TYPE_MAX 12u

typedef esp_err_t (*kismet_mdns_observation_cb_t)(
    const wireshark_mdns_service_t *service,
    wireshark_lan_parse_result_t parse_result,
    void *ctx);

typedef esp_err_t (*kismet_ssdp_observation_cb_t)(
    const wireshark_ssdp_facts_t *facts,
    wireshark_lan_parse_result_t parse_result,
    void *ctx);

typedef esp_err_t (*kismet_dhcp_observation_cb_t)(
    const wireshark_dhcp_facts_t *facts,
    wireshark_lan_parse_result_t parse_result,
    void *ctx);

typedef struct {
    uint32_t duration_ms;
    kismet_dedup_t *dedup;
    kismet_mdns_observation_cb_t observation_cb;
    void *observation_ctx;
} kismet_mdns_scan_config_t;

typedef struct {
    uint32_t datagrams;
    uint32_t malformed_datagrams;
    uint32_t partial_datagrams;
    uint32_t services_seen;
    uint32_t duplicate_services;
    uint32_t emitted_services;
    uint32_t discovered_service_types;
} kismet_mdns_scan_stats_t;

typedef struct {
    uint32_t duration_ms;
    kismet_dedup_t *dedup;
    kismet_ssdp_observation_cb_t observation_cb;
    void *observation_ctx;
} kismet_ssdp_scan_config_t;

typedef struct {
    uint32_t datagrams;
    uint32_t malformed_datagrams;
    uint32_t partial_datagrams;
    uint32_t duplicate_datagrams;
    uint32_t emitted_datagrams;
} kismet_ssdp_scan_stats_t;

typedef struct {
    uint32_t duration_ms;
    kismet_dedup_t *dedup;
    kismet_dhcp_observation_cb_t observation_cb;
    void *observation_ctx;
} kismet_dhcp_scan_config_t;

typedef struct {
    uint32_t datagrams;
    uint32_t malformed_datagrams;
    uint32_t partial_datagrams;
    uint32_t duplicate_datagrams;
    uint32_t emitted_datagrams;
} kismet_dhcp_scan_stats_t;

/* Phase 1 contracts; implementations remain under nearby_* until Phase 3. */
bool kismet_lan_scan_prerequisite_ready(void);

esp_err_t kismet_mdns_scan(const kismet_mdns_scan_config_t *config,
                           kismet_mdns_scan_stats_t *stats);
esp_err_t kismet_ssdp_scan(const kismet_ssdp_scan_config_t *config,
                           kismet_ssdp_scan_stats_t *stats);
esp_err_t kismet_dhcp_scan(const kismet_dhcp_scan_config_t *config,
                           kismet_dhcp_scan_stats_t *stats);

#ifdef __cplusplus
}
#endif
