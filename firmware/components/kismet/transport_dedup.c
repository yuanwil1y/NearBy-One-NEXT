#include "kismet_dedup.h"

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

static void hash_u8(hash_pair_t *h, uint8_t v) { hash_bytes(h, &v, sizeof(v)); }
static void hash_u16(hash_pair_t *h, uint16_t v)
{
    const uint8_t bytes[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    hash_bytes(h, bytes, sizeof(bytes));
}
static void hash_bool(hash_pair_t *h, bool v) { hash_u8(h, v ? 1u : 0u); }
static void hash_string(hash_pair_t *h, const char *s)
{
    if (s == NULL) { hash_u16(h, 0xffffu); return; }
    size_t len = strlen(s);
    if (len > 0xfffeu) len = 0xfffeu;
    hash_u16(h, (uint16_t)len);
    hash_bytes(h, s, len);
}

static bool table_should_emit(kismet_dedup_table_t *table, const hash_pair_t *h)
{
    for (uint8_t i = 0; i < KISMET_DEDUP_SLOT_COUNT; ++i) {
        const kismet_dedup_slot_t *slot = &table->slots[i];
        if (slot->used && slot->h1 == h->h1 && slot->h2 == h->h2) {
            ++table->duplicates;
            return false;
        }
    }
    for (uint8_t i = 0; i < KISMET_DEDUP_SLOT_COUNT; ++i) {
        kismet_dedup_slot_t *slot = &table->slots[i];
        if (!slot->used) {
            slot->used = true; slot->h1 = h->h1; slot->h2 = h->h2;
            return true;
        }
    }
    kismet_dedup_slot_t *slot = &table->slots[table->replace_cursor];
    slot->used = true; slot->h1 = h->h1; slot->h2 = h->h2;
    table->replace_cursor =
        (uint8_t)((table->replace_cursor + 1u) % KISMET_DEDUP_SLOT_COUNT);
    ++table->replacements;
    return true;
}

void kismet_dedup_init(kismet_dedup_t *state, uint32_t generation)
{
    if (state == NULL) return;
    memset(state, 0, sizeof(*state));
    state->generation = generation;
}
void kismet_dedup_reset(kismet_dedup_t *state, uint32_t generation)
{
    kismet_dedup_init(state, generation);
}

static void hash_ble_facts(hash_pair_t *h, const wireshark_ble_discovery_facts_t *facts)
{
    hash_bool(h, facts->has_flags); if (facts->has_flags) hash_u8(h, facts->flags);
    hash_bool(h, facts->has_local_name);
    if (facts->has_local_name) {
        hash_string(h, facts->local_name);
        hash_bool(h, facts->local_name_complete);
        hash_bool(h, facts->local_name_truncated);
    }
    hash_u8(h, facts->service_uuid_count);
    for (uint8_t i = 0; i < facts->service_uuid_count; ++i)
        hash_bytes(h, facts->service_uuids[i].bytes, sizeof(facts->service_uuids[i].bytes));
    hash_u8(h, facts->service_data_count);
    for (uint8_t i = 0; i < facts->service_data_count; ++i) {
        const wireshark_ble_service_data_t *entry = &facts->service_data[i];
        hash_bytes(h, entry->uuid.bytes, sizeof(entry->uuid.bytes));
        hash_u8(h, entry->data_len); hash_bytes(h, entry->data, entry->data_len);
        hash_bool(h, entry->data_truncated);
    }
    hash_u8(h, facts->manufacturer_data_count);
    for (uint8_t i = 0; i < facts->manufacturer_data_count; ++i) {
        const wireshark_ble_manufacturer_data_t *entry = &facts->manufacturer_data[i];
        hash_u16(h, entry->company_id); hash_u8(h, entry->data_len);
        hash_bytes(h, entry->data, entry->data_len); hash_bool(h, entry->data_truncated);
    }
    hash_bool(h, facts->has_tx_power); if (facts->has_tx_power) hash_u8(h, (uint8_t)facts->tx_power);
    hash_bool(h, facts->has_appearance); if (facts->has_appearance) hash_u16(h, facts->appearance);
    hash_bool(h, facts->source_incomplete); hash_bool(h, facts->partial_tail);
}

bool kismet_ble_dedup_should_emit(kismet_dedup_t *state, const wireshark_ble_observation_t *observation)
{
    if (state == NULL || observation == NULL) return false;
    hash_pair_t h; hash_init(&h);
    hash_bytes(&h, observation->meta.address, sizeof(observation->meta.address));
    hash_u8(&h, observation->meta.address_type); hash_bool(&h, observation->meta.legacy);
    hash_bool(&h, observation->meta.connectable_known);
    if (observation->meta.connectable_known) hash_bool(&h, observation->meta.connectable);
    hash_u8(&h, observation->meta.sid); hash_u8(&h, observation->meta.data_status);
    hash_ble_facts(&h, &observation->facts);
    return table_should_emit(&state->ble, &h);
}

bool kismet_mdns_dedup_should_emit(kismet_dedup_t *state, const wireshark_mdns_service_t *service)
{
    if (state == NULL || service == NULL) return false;
    hash_pair_t h; hash_init(&h);
    hash_string(&h, service->service_type); hash_string(&h, service->instance); hash_string(&h, service->host);
    hash_bool(&h, service->has_port); if (service->has_port) hash_u16(&h, service->port);
    hash_u8(&h, service->txt_count);
    for (uint8_t i = 0; i < service->txt_count; ++i) {
        hash_string(&h, service->txt[i].key); hash_bool(&h, service->txt[i].has_value);
        if (service->txt[i].has_value) hash_string(&h, service->txt[i].value);
        hash_bool(&h, service->txt[i].truncated);
    }
    hash_bool(&h, service->has_ipv4); if (service->has_ipv4) hash_bytes(&h, service->ipv4, sizeof(service->ipv4));
    hash_bool(&h, service->has_ipv6); if (service->has_ipv6) hash_bytes(&h, service->ipv6, sizeof(service->ipv6));
    return table_should_emit(&state->mdns, &h);
}

bool kismet_ssdp_dedup_should_emit(kismet_dedup_t *state, const wireshark_ssdp_facts_t *facts)
{
    if (state == NULL || facts == NULL) return false;
    hash_pair_t h; hash_init(&h);
    hash_string(&h, facts->usn); hash_string(&h, facts->st); hash_string(&h, facts->nt);
    hash_string(&h, facts->location); hash_string(&h, facts->device_type);
    hash_string(&h, facts->manufacturer); hash_string(&h, facts->manufacturer_url);
    hash_string(&h, facts->model_name); hash_string(&h, facts->model_number);
    return table_should_emit(&state->ssdp, &h);
}

bool kismet_dhcp_dedup_should_emit(kismet_dedup_t *state, const wireshark_dhcp_facts_t *facts)
{
    if (state == NULL || facts == NULL) return false;
    hash_pair_t h; hash_init(&h);
    hash_u8(&h, facts->chaddr_len); hash_bytes(&h, facts->chaddr, facts->chaddr_len);
    hash_string(&h, facts->hostname); hash_string(&h, facts->vendor_class);
    hash_u8(&h, facts->client_identifier_len); hash_bytes(&h, facts->client_identifier, facts->client_identifier_len);
    return table_should_emit(&state->dhcp, &h);
}

bool kismet_wifi_active_dedup_should_emit(kismet_dedup_t *state, const kismet_wifi_active_facts_t *facts)
{
    if (state == NULL || facts == NULL) return false;
    hash_pair_t h; hash_init(&h);
    hash_bytes(&h, facts->bssid, sizeof(facts->bssid)); hash_u8(&h, facts->ssid_len);
    hash_bytes(&h, facts->ssid, facts->ssid_len); hash_bool(&h, facts->hidden_ssid);
    hash_u8(&h, facts->primary_channel); hash_u16(&h, (uint16_t)facts->authmode);
    return table_should_emit(&state->wifi_active, &h);
}

static void hash_wifi_mgmt(hash_pair_t *h, const wireshark_80211_mgmt_facts_t *facts)
{
    hash_u8(h, facts->subtype); hash_bytes(h, facts->source, sizeof(facts->source));
    hash_bytes(h, facts->bssid, sizeof(facts->bssid)); hash_bool(h, facts->has_ssid);
    if (facts->has_ssid) { hash_u8(h, facts->ssid_len); hash_bytes(h, facts->ssid, facts->ssid_len); hash_bool(h, facts->hidden_ssid); }
    hash_bool(h, facts->has_channel); if (facts->has_channel) hash_u8(h, facts->channel);
    hash_bool(h, facts->has_capability); if (facts->has_capability) hash_u16(h, facts->capability);
    hash_u8(h, facts->supported_rate_count); hash_bytes(h, facts->supported_rates, facts->supported_rate_count);
    hash_bool(h, facts->has_rsn); hash_bool(h, facts->has_ht); hash_bool(h, facts->has_he); hash_bool(h, facts->has_wps);
    hash_u8(h, facts->vendor_ie_count);
    for (uint8_t i = 0; i < facts->vendor_ie_count; ++i) {
        hash_bytes(h, facts->vendor_ies[i].oui, sizeof(facts->vendor_ies[i].oui));
        hash_u8(h, facts->vendor_ies[i].data_len); hash_bytes(h, facts->vendor_ies[i].data, facts->vendor_ies[i].data_len);
        hash_bool(h, facts->vendor_ies[i].truncated);
    }
}

bool kismet_wifi_passive_dedup_should_emit(kismet_dedup_t *state, const wireshark_80211_passive_observation_t *observation)
{
    if (state == NULL || observation == NULL) return false;
    hash_pair_t h; hash_init(&h); hash_wifi_mgmt(&h, &observation->facts);
    return table_should_emit(&state->wifi_passive, &h);
}
