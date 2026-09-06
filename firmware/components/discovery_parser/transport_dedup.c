#include "nearby_transport_dedup.h"

#include <string.h>

#define FNV1A64_OFFSET 1469598103934665603ULL
#define FNV1A64_PRIME 1099511628211ULL
#define FNV_ALT_OFFSET 1099511628211ULL

typedef struct {
    uint64_t h1;
    uint64_t h2;
} hash_pair_t;

static void hash_init(hash_pair_t *h)
{
    h->h1 = FNV1A64_OFFSET;
    h->h2 = FNV_ALT_OFFSET;
}

static void hash_bytes(hash_pair_t *h, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; ++i) {
        h->h1 ^= p[i];
        h->h1 *= FNV1A64_PRIME;
        h->h2 ^= (uint8_t)(p[i] + (uint8_t)i);
        h->h2 *= FNV1A64_PRIME;
        h->h2 ^= h->h2 >> 32;
    }
}

static void hash_u8(hash_pair_t *h, uint8_t v)
{
    hash_bytes(h, &v, sizeof(v));
}

static void hash_u16(hash_pair_t *h, uint16_t v)
{
    const uint8_t bytes[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    hash_bytes(h, bytes, sizeof(bytes));
}

static void hash_bool(hash_pair_t *h, bool v)
{
    hash_u8(h, v ? 1u : 0u);
}

static void hash_string(hash_pair_t *h, const char *s)
{
    if (s == NULL) {
        hash_u16(h, 0xffffu);
        return;
    }
    size_t len = strlen(s);
    if (len > 0xfffeu) {
        len = 0xfffeu;
    }
    hash_u16(h, (uint16_t)len);
    hash_bytes(h, s, len);
}

static bool table_should_emit(nearby_dedup_table_t *table, const hash_pair_t *h)
{
    for (uint8_t i = 0; i < NEARBY_DEDUP_SLOT_COUNT; ++i) {
        const nearby_dedup_slot_t *slot = &table->slots[i];
        if (slot->used && slot->h1 == h->h1 && slot->h2 == h->h2) {
            ++table->duplicates;
            return false;
        }
    }

    for (uint8_t i = 0; i < NEARBY_DEDUP_SLOT_COUNT; ++i) {
        nearby_dedup_slot_t *slot = &table->slots[i];
        if (!slot->used) {
            slot->used = true;
            slot->h1 = h->h1;
            slot->h2 = h->h2;
            return true;
        }
    }

    nearby_dedup_slot_t *slot = &table->slots[table->replace_cursor];
    slot->used = true;
    slot->h1 = h->h1;
    slot->h2 = h->h2;
    table->replace_cursor = (uint8_t)((table->replace_cursor + 1u) % NEARBY_DEDUP_SLOT_COUNT);
    ++table->replacements;
    return true;
}

void nearby_scan_dedup_reset(nearby_scan_dedup_t *state, uint32_t generation)
{
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->generation = generation;
}

static void hash_ble_facts(hash_pair_t *h, const nearby_ble_discovery_facts_t *facts)
{
    hash_bool(h, facts->has_flags);
    if (facts->has_flags) {
        hash_u8(h, facts->flags);
    }
    hash_bool(h, facts->has_local_name);
    if (facts->has_local_name) {
        hash_string(h, facts->local_name);
        hash_bool(h, facts->local_name_complete);
        hash_bool(h, facts->local_name_truncated);
    }
    hash_u8(h, facts->service_uuid_count);
    for (uint8_t i = 0; i < facts->service_uuid_count; ++i) {
        hash_bytes(h, facts->service_uuids[i].bytes,
                   sizeof(facts->service_uuids[i].bytes));
    }
    hash_u8(h, facts->service_data_count);
    for (uint8_t i = 0; i < facts->service_data_count; ++i) {
        const nearby_ble_service_data_t *entry = &facts->service_data[i];
        hash_bytes(h, entry->uuid.bytes, sizeof(entry->uuid.bytes));
        hash_u8(h, entry->data_len);
        hash_bytes(h, entry->data, entry->data_len);
        hash_bool(h, entry->data_truncated);
    }
    hash_u8(h, facts->manufacturer_data_count);
    for (uint8_t i = 0; i < facts->manufacturer_data_count; ++i) {
        const nearby_ble_manufacturer_data_t *entry = &facts->manufacturer_data[i];
        hash_u16(h, entry->company_id);
        hash_u8(h, entry->data_len);
        hash_bytes(h, entry->data, entry->data_len);
        hash_bool(h, entry->data_truncated);
    }
    hash_bool(h, facts->has_tx_power);
    if (facts->has_tx_power) {
        hash_u8(h, (uint8_t)facts->tx_power);
    }
    hash_bool(h, facts->has_appearance);
    if (facts->has_appearance) {
        hash_u16(h, facts->appearance);
    }
    hash_bool(h, facts->source_incomplete);
    hash_bool(h, facts->partial_tail);
}

bool nearby_ble_dedup_should_emit(nearby_scan_dedup_t *state,
                                  const nearby_ble_normalized_t *observation)
{
    if (state == NULL || observation == NULL) {
        return false;
    }
    hash_pair_t h;
    hash_init(&h);
    hash_bytes(&h, observation->meta.address, sizeof(observation->meta.address));
    hash_u8(&h, observation->meta.address_type);
    hash_bool(&h, observation->meta.legacy);
    hash_bool(&h, observation->meta.connectable_known);
    if (observation->meta.connectable_known) {
        hash_bool(&h, observation->meta.connectable);
    }
    hash_u8(&h, observation->meta.sid);
    hash_u8(&h, observation->meta.data_status);
    hash_ble_facts(&h, &observation->facts);
    return table_should_emit(&state->ble, &h);
}

bool nearby_mdns_dedup_should_emit(nearby_scan_dedup_t *state,
                                   const nearby_mdns_service_t *service)
{
    if (state == NULL || service == NULL) {
        return false;
    }
    hash_pair_t h;
    hash_init(&h);
    hash_string(&h, service->service_type);
    hash_string(&h, service->instance);
    hash_string(&h, service->host);
    hash_bool(&h, service->has_port);
    if (service->has_port) {
        hash_u16(&h, service->port);
    }
    hash_u8(&h, service->txt_count);
    for (uint8_t i = 0; i < service->txt_count; ++i) {
        hash_string(&h, service->txt[i].key);
        hash_bool(&h, service->txt[i].has_value);
        if (service->txt[i].has_value) {
            hash_string(&h, service->txt[i].value);
        }
        hash_bool(&h, service->txt[i].truncated);
    }
    hash_bool(&h, service->has_ipv4);
    if (service->has_ipv4) {
        hash_bytes(&h, service->ipv4, sizeof(service->ipv4));
    }
    hash_bool(&h, service->has_ipv6);
    if (service->has_ipv6) {
        hash_bytes(&h, service->ipv6, sizeof(service->ipv6));
    }
    return table_should_emit(&state->mdns, &h);
}

bool nearby_ssdp_dedup_should_emit(nearby_scan_dedup_t *state,
                                   const nearby_ssdp_facts_t *facts)
{
    if (state == NULL || facts == NULL) {
        return false;
    }
    hash_pair_t h;
    hash_init(&h);
    hash_string(&h, facts->usn);
    hash_string(&h, facts->st);
    hash_string(&h, facts->nt);
    hash_string(&h, facts->location);
    hash_string(&h, facts->device_type);
    hash_string(&h, facts->manufacturer);
    hash_string(&h, facts->manufacturer_url);
    hash_string(&h, facts->model_name);
    hash_string(&h, facts->model_number);
    return table_should_emit(&state->ssdp, &h);
}

bool nearby_dhcp_dedup_should_emit(nearby_scan_dedup_t *state,
                                   const nearby_dhcp_facts_t *facts)
{
    if (state == NULL || facts == NULL) {
        return false;
    }
    hash_pair_t h;
    hash_init(&h);
    hash_u8(&h, facts->chaddr_len);
    hash_bytes(&h, facts->chaddr, facts->chaddr_len);
    hash_string(&h, facts->hostname);
    hash_string(&h, facts->vendor_class);
    hash_u8(&h, facts->client_identifier_len);
    hash_bytes(&h, facts->client_identifier, facts->client_identifier_len);
    return table_should_emit(&state->dhcp, &h);
}
