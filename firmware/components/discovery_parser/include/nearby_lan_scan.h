#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "nearby_lan_discovery.h"
#include "nearby_transport_dedup.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NEARBY_LAN_RX_MAX 1536u
#define NEARBY_MDNS_DISCOVERED_TYPE_MAX 12u

typedef esp_err_t (*nearby_mdns_observation_cb_t)(
    const nearby_mdns_service_t *service,
    nearby_lan_parse_result_t parse_result,
    void *ctx);

typedef esp_err_t (*nearby_ssdp_observation_cb_t)(
    const nearby_ssdp_facts_t *facts,
    nearby_lan_parse_result_t parse_result,
    void *ctx);

typedef esp_err_t (*nearby_dhcp_observation_cb_t)(
    const nearby_dhcp_facts_t *facts,
    nearby_lan_parse_result_t parse_result,
    void *ctx);

typedef struct {
    uint32_t duration_ms;
    nearby_scan_dedup_t *dedup;
    nearby_mdns_observation_cb_t observation_cb;
    void *observation_ctx;
} nearby_zeroconf_scan_config_t;

typedef struct {
    uint32_t datagrams;
    uint32_t malformed_datagrams;
    uint32_t partial_datagrams;
    uint32_t services_seen;
    uint32_t duplicate_services;
    uint32_t emitted_services;
    uint32_t discovered_service_types;
} nearby_zeroconf_scan_stats_t;

typedef struct {
    uint32_t duration_ms;
    nearby_scan_dedup_t *dedup;
    nearby_ssdp_observation_cb_t observation_cb;
    void *observation_ctx;
} nearby_ssdp_scan_config_t;

typedef struct {
    uint32_t datagrams;
    uint32_t malformed_datagrams;
    uint32_t partial_datagrams;
    uint32_t duplicate_datagrams;
    uint32_t emitted_datagrams;
} nearby_ssdp_scan_stats_t;

typedef struct {
    uint32_t duration_ms;
    nearby_scan_dedup_t *dedup;
    nearby_dhcp_observation_cb_t observation_cb;
    void *observation_ctx;
} nearby_dhcp_scan_config_t;

typedef struct {
    uint32_t datagrams;
    uint32_t malformed_datagrams;
    uint32_t partial_datagrams;
    uint32_t duplicate_datagrams;
    uint32_t emitted_datagrams;
} nearby_dhcp_scan_stats_t;

/* True only when D's current STA netif has an IPv4 address. No provisioning is done here. */
bool nearby_lan_scan_prerequisite_ready(void);

esp_err_t nearby_zeroconf_scan_run(const nearby_zeroconf_scan_config_t *config,
                                   nearby_zeroconf_scan_stats_t *stats);
esp_err_t nearby_ssdp_scan_run(const nearby_ssdp_scan_config_t *config,
                               nearby_ssdp_scan_stats_t *stats);
esp_err_t nearby_dhcp_scan_run(const nearby_dhcp_scan_config_t *config,
                               nearby_dhcp_scan_stats_t *stats);

#ifdef __cplusplus
}
#endif
