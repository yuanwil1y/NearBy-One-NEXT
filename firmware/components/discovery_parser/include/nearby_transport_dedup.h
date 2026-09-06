#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "nearby_ble_discovery.h"
#include "nearby_lan_discovery.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NEARBY_DEDUP_SLOT_COUNT 24u

typedef struct {
    uint64_t h1;
    uint64_t h2;
    bool used;
} nearby_dedup_slot_t;

typedef struct {
    nearby_dedup_slot_t slots[NEARBY_DEDUP_SLOT_COUNT];
    uint8_t replace_cursor;
    uint32_t duplicates;
    uint32_t replacements;
} nearby_dedup_table_t;

typedef struct {
    uint32_t generation;
    nearby_dedup_table_t ble;
    nearby_dedup_table_t mdns;
    nearby_dedup_table_t ssdp;
    nearby_dedup_table_t dhcp;
} nearby_scan_dedup_t;

void nearby_scan_dedup_reset(nearby_scan_dedup_t *state, uint32_t generation);

/* Return true when this observation should be emitted; false means duplicate. */
bool nearby_ble_dedup_should_emit(nearby_scan_dedup_t *state,
                                  const nearby_ble_normalized_t *observation);
bool nearby_mdns_dedup_should_emit(nearby_scan_dedup_t *state,
                                   const nearby_mdns_service_t *service);
bool nearby_ssdp_dedup_should_emit(nearby_scan_dedup_t *state,
                                   const nearby_ssdp_facts_t *facts);
bool nearby_dhcp_dedup_should_emit(nearby_scan_dedup_t *state,
                                   const nearby_dhcp_facts_t *facts);

#ifdef __cplusplus
}
#endif
