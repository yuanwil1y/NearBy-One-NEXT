#include "nearby_scan_pipeline.h"

#include <stdio.h>
#include <string.h>

#include "db_storage.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nearby_ble_scan.h"
#include "nearby_i154_scan.h"
#include "nearby_lan_scan.h"
#include "nearby_matcher_keys.h"
#include "nearby_matter_discovery.h"
#include "nearby_recognition_adapter.h"
#include "nearby_recognition_db.h"
#include "nearby_scan_coordinator.h"
#include "nearby_scan_default_runners.h"
#include "nearby_transport_dedup.h"
#include "nearby_wifi_scan.h"

#define PIPELINE_EVENT_QUEUE_LEN 12u
#define PIPELINE_TASK_STACK      8192u
#define PIPELINE_TASK_PRIORITY   5u
#define VALUE_SCRATCH_BYTES      1024u

typedef struct {
    QueueHandle_t event_queue;
    QueueHandle_t command_queue;
    nearby_db_t db;
    bool db_open;
    uint8_t workspace_bytes[NEARBY_DB_MATCH_WORKSPACE_RECOMMENDED];
    char value_scratch[VALUE_SCRATCH_BYTES + 1u];
    nearby_db_match_workspace_t workspace;
    uint8_t published_terminal_count;
} pipeline_ctx_t;

typedef struct {
    char domain[32];
    char manufacturer[48];
    char model[48];
} record_metadata_t;

static pipeline_ctx_t s;

static void copy_text(char *dst, size_t cap, const char *src)
{
    if (cap == 0) return;
    (void)snprintf(dst, cap, "%s", src ? src : "");
}

static uint64_t fnv1a64_update(uint64_t hash, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; ++i) {
        hash ^= p[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t hash_texts(const char *a, const char *b, const char *c)
{
    uint64_t h = UINT64_C(1469598103934665603);
    if (a) h = fnv1a64_update(h, a, strlen(a));
    h = fnv1a64_update(h, "\0", 1);
    if (b) h = fnv1a64_update(h, b, strlen(b));
    h = fnv1a64_update(h, "\0", 1);
    if (c) h = fnv1a64_update(h, c, strlen(c));
    return h;
}

static void format_mac(const uint8_t mac[6], char compact[13], char colon[18])
{
    (void)snprintf(compact, 13, "%02x%02x%02x%02x%02x%02x",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    (void)snprintf(colon, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static bool queue_event(const nearby_pipeline_event_t *event)
{
    return s.event_queue != NULL && xQueueSend(s.event_queue, event, portMAX_DELAY) == pdTRUE;
}

static bool json_string_from_buffer(const char *json, const char *key,
                                    char *out, size_t out_size)
{
    if (!json || !key || !out || out_size == 0) return false;
    char needle[64];
    if (snprintf(needle, sizeof(needle), "\"%s\":", key) >= (int)sizeof(needle)) {
        return false;
    }
    const char *p = strstr(json, needle);
    if (!p) return false;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (*p++ != '"') return false;

    size_t n = 0;
    while (*p && *p != '"') {
        char ch = *p++;
        if (ch == '\\') {
            char escaped = *p++;
            if (escaped == '\0') return false;
            switch (escaped) {
            case 'n': ch = '\n'; break;
            case 'r': ch = '\r'; break;
            case 't': ch = '\t'; break;
            case '"': ch = '"'; break;
            case '\\': ch = '\\'; break;
            default: ch = '?'; break;
            }
        }
        if (n + 1 < out_size) out[n++] = ch;
    }
    if (*p != '"') return false;
    out[n] = '\0';
    return n > 0;
}

static bool metadata_from_ref(const nearby_db_value_ref_t *ref, record_metadata_t *out)
{
    if (!s.db_open || !ref || !out) return false;
    memset(out, 0, sizeof(*out));
    size_t want = ref->value_size < VALUE_SCRATCH_BYTES
                    ? ref->value_size : VALUE_SCRATCH_BYTES;
    size_t got = 0;
    if (nearby_db_read_value(&s.db, ref, 0, s.value_scratch, want, &got) != NEARBY_DB_OK) {
        return false;
    }
    s.value_scratch[got] = '\0';
    (void)json_string_from_buffer(s.value_scratch, "domain", out->domain, sizeof(out->domain));
    if (!json_string_from_buffer(s.value_scratch, "manufacturer",
                                 out->manufacturer, sizeof(out->manufacturer))) {
        (void)json_string_from_buffer(s.value_scratch, "organization",
                                      out->manufacturer, sizeof(out->manufacturer));
    }
    if (!json_string_from_buffer(s.value_scratch, "model", out->model, sizeof(out->model))) {
        if (!json_string_from_buffer(s.value_scratch, "model_name", out->model, sizeof(out->model))) {
            (void)json_string_from_buffer(s.value_scratch, "product_name", out->model, sizeof(out->model));
        }
    }
    return true;
}

static bool exact_ref(const char *key, nearby_db_value_ref_t *ref)
{
    if (!s.db_open || !key || !ref) return false;
    return nearby_db_find(&s.db, key, strlen(key), ref) == NEARBY_DB_OK;
}

static void fill_common(nearby_matched_semantic_t *out,
                        const char *device_id, const char *identity,
                        const char *platform, const char *transport,
                        const record_metadata_t *metadata,
                        const char *name)
{
    memset(out, 0, sizeof(*out));
    copy_text(out->device_id, sizeof(out->device_id), device_id);
    copy_text(out->identity, sizeof(out->identity), identity);
    copy_text(out->platform, sizeof(out->platform),
              platform && platform[0] ? platform : "nearby");
    copy_text(out->transport, sizeof(out->transport), transport);
    if (metadata) {
        copy_text(out->manufacturer, sizeof(out->manufacturer), metadata->manufacturer);
        copy_text(out->model, sizeof(out->model), metadata->model);
    }
    copy_text(out->name, sizeof(out->name),
              name && name[0] ? name : (metadata && metadata->model[0] ? metadata->model : device_id));
}

static esp_err_t emit_mac_semantic(const uint8_t mac[6], const char *prefix,
                                   const char *platform, const char *transport,
                                   const record_metadata_t *metadata,
                                   const char *name, int8_t rssi)
{
    nearby_pipeline_event_t event = {.type = NEARBY_PIPELINE_EVENT_MATCHED};
    char compact[13], colon[18], device_id[33], identity[40];
    format_mac(mac, compact, colon);
    (void)snprintf(device_id, sizeof(device_id), "%s_%s", prefix, compact);
    (void)snprintf(identity, sizeof(identity), "%s:%s", transport, colon);
    fill_common(&event.matched, device_id, identity, platform, transport, metadata, name);
    copy_text(event.matched.connection_type, sizeof(event.matched.connection_type),
              strcmp(transport, "ble") == 0 ? "bluetooth" : "mac");
    copy_text(event.matched.connection_value, sizeof(event.matched.connection_value), colon);
    event.matched.has_signal_strength = true;
    event.matched.signal_strength_dbm = rssi;
    return queue_event(&event) ? ESP_OK : ESP_FAIL;
}

static esp_err_t wifi_active_observation(const nearby_wifi_active_facts_t *facts, void *ctx)
{
    (void)ctx;
    if (!s.db_open || !facts) return ESP_OK;
    char key[NEARBY_MATCHER_KEY_MAX];
    if (nearby_match_key_oui24(facts->bssid, key, sizeof(key)) != NEARBY_MATCHER_KEY_OK) return ESP_OK;
    nearby_db_value_ref_t ref;
    if (!exact_ref(key, &ref)) return ESP_OK;
    record_metadata_t metadata;
    (void)metadata_from_ref(&ref, &metadata);
    char ssid[NEARBY_WIFI_SSID_MAX + 1u];
    size_t n = facts->ssid_len < NEARBY_WIFI_SSID_MAX ? facts->ssid_len : NEARBY_WIFI_SSID_MAX;
    memcpy(ssid, facts->ssid, n);
    ssid[n] = '\0';
    return emit_mac_semantic(facts->bssid, "wifi", "ieee_oui", "wifi",
                             &metadata, ssid[0] ? ssid : NULL, facts->rssi);
}

static esp_err_t wifi_passive_observation(const nearby_wifi_passive_normalized_t *obs,
                                          nearby_wifi_parse_result_t parsed, void *ctx)
{
    (void)ctx;
    if (!s.db_open || !obs || parsed < 0) return ESP_OK;
    char key[NEARBY_MATCHER_KEY_MAX];
    if (nearby_match_key_oui24(obs->facts.source, key, sizeof(key)) != NEARBY_MATCHER_KEY_OK) return ESP_OK;
    nearby_db_value_ref_t ref;
    if (!exact_ref(key, &ref)) return ESP_OK;
    record_metadata_t metadata;
    (void)metadata_from_ref(&ref, &metadata);
    char ssid[NEARBY_WIFI_SSID_MAX + 1u] = {0};
    if (obs->facts.has_ssid) {
        size_t n = obs->facts.ssid_len < NEARBY_WIFI_SSID_MAX ? obs->facts.ssid_len : NEARBY_WIFI_SSID_MAX;
        memcpy(ssid, obs->facts.ssid, n);
    }
    return emit_mac_semantic(obs->facts.source, "wifi", "ieee_oui", "wifi",
                             &metadata, ssid[0] ? ssid : NULL, obs->meta.rssi);
}

static bool ble_match_ref(const nearby_ble_normalized_t *obs, nearby_db_value_ref_t *ref,
                          record_metadata_t *metadata, const char **platform)
{
    nearby_db_match_result_t result;
    if (s.db_open && nearby_recognition_match_ble(&s.db, obs, &s.workspace, &result) == NEARBY_DB_OK &&
        nearby_recognition_result_is_usable(&result)) {
        *ref = result.value;
        (void)metadata_from_ref(ref, metadata);
        *platform = metadata->domain[0] ? metadata->domain : "ha_bluetooth";
        return true;
    }

    nearby_matter_discovery_facts_t matter;
    if (nearby_matter_from_ble(obs, &matter) == NEARBY_MATTER_PARSE_OK && matter.has_vendor_product) {
        char key[NEARBY_MATCHER_KEY_MAX];
        if (nearby_match_key_matter_vidpid(matter.vendor_id, matter.product_id,
                                           key, sizeof(key)) == NEARBY_MATCHER_KEY_OK &&
            exact_ref(key, ref)) {
            (void)metadata_from_ref(ref, metadata);
            *platform = "matter";
            return true;
        }
    }
    return false;
}

static esp_err_t ble_observation(const nearby_ble_normalized_t *obs,
                                 nearby_ble_parse_result_t parsed, void *ctx)
{
    (void)ctx;
    if (!s.db_open || !obs || parsed < 0) return ESP_OK;
    nearby_db_value_ref_t ref;
    record_metadata_t metadata;
    const char *platform = NULL;
    if (!ble_match_ref(obs, &ref, &metadata, &platform)) return ESP_OK;
    const char *name = obs->facts.has_local_name ? obs->facts.local_name : NULL;
    return emit_mac_semantic(obs->meta.address, "ble", platform, "ble",
                             &metadata, name, obs->meta.rssi);
}

static esp_err_t mdns_observation(const nearby_mdns_service_t *service,
                                  nearby_lan_parse_result_t parsed, void *ctx)
{
    (void)ctx;
    if (!s.db_open || !service || parsed < 0) return ESP_OK;
    nearby_db_match_result_t result;
    nearby_db_value_ref_t ref;
    record_metadata_t metadata;
    const char *platform = NULL;
    bool matched = false;
    if (nearby_recognition_match_zeroconf(&s.db, service, &s.workspace, &result) == NEARBY_DB_OK &&
        nearby_recognition_result_is_usable(&result)) {
        ref = result.value;
        (void)metadata_from_ref(&ref, &metadata);
        platform = metadata.domain[0] ? metadata.domain : "zeroconf";
        matched = true;
    } else {
        nearby_matter_discovery_facts_t matter;
        if (nearby_matter_from_mdns(service, &matter) == NEARBY_MATTER_PARSE_OK && matter.has_vendor_product) {
            char key[NEARBY_MATCHER_KEY_MAX];
            if (nearby_match_key_matter_vidpid(matter.vendor_id, matter.product_id,
                                               key, sizeof(key)) == NEARBY_MATCHER_KEY_OK &&
                exact_ref(key, &ref)) {
                (void)metadata_from_ref(&ref, &metadata);
                platform = "matter";
                matched = true;
            }
        }
    }
    if (!matched) return ESP_OK;

    uint64_t h = hash_texts(service->service_type, service->instance, service->host);
    nearby_pipeline_event_t event = {.type = NEARBY_PIPELINE_EVENT_MATCHED};
    char device_id[33], identity[40];
    (void)snprintf(device_id, sizeof(device_id), "lan_%016llx", (unsigned long long)h);
    (void)snprintf(identity, sizeof(identity), "mdns:%016llx", (unsigned long long)h);
    fill_common(&event.matched, device_id, identity, platform, "zeroconf", &metadata,
                service->instance[0] ? service->instance : service->host);
    return queue_event(&event) ? ESP_OK : ESP_FAIL;
}

static esp_err_t ssdp_observation(const nearby_ssdp_facts_t *facts,
                                  nearby_lan_parse_result_t parsed, void *ctx)
{
    (void)ctx;
    if (!s.db_open || !facts || parsed < 0) return ESP_OK;
    nearby_db_match_result_t result;
    if (nearby_recognition_match_ssdp(&s.db, facts, &s.workspace, &result) != NEARBY_DB_OK ||
        !nearby_recognition_result_is_usable(&result)) return ESP_OK;
    record_metadata_t metadata;
    (void)metadata_from_ref(&result.value, &metadata);
    if (!metadata.manufacturer[0]) copy_text(metadata.manufacturer, sizeof(metadata.manufacturer), facts->manufacturer);
    if (!metadata.model[0]) copy_text(metadata.model, sizeof(metadata.model),
                                      facts->model_name[0] ? facts->model_name : facts->model_number);
    uint64_t h = hash_texts(facts->usn, facts->location, facts->server);
    nearby_pipeline_event_t event = {.type = NEARBY_PIPELINE_EVENT_MATCHED};
    char device_id[33], identity[40];
    (void)snprintf(device_id, sizeof(device_id), "lan_%016llx", (unsigned long long)h);
    (void)snprintf(identity, sizeof(identity), "ssdp:%016llx", (unsigned long long)h);
    fill_common(&event.matched, device_id, identity,
                metadata.domain[0] ? metadata.domain : "ssdp", "ssdp", &metadata,
                facts->model_name[0] ? facts->model_name : facts->server);
    return queue_event(&event) ? ESP_OK : ESP_FAIL;
}

static esp_err_t dhcp_observation(const nearby_dhcp_facts_t *facts,
                                  nearby_lan_parse_result_t parsed, void *ctx)
{
    (void)ctx;
    if (!s.db_open || !facts || parsed < 0) return ESP_OK;
    nearby_db_match_result_t result;
    if (nearby_recognition_match_dhcp(&s.db, facts, &s.workspace, &result) != NEARBY_DB_OK ||
        !nearby_recognition_result_is_usable(&result)) return ESP_OK;
    record_metadata_t metadata;
    (void)metadata_from_ref(&result.value, &metadata);

    if (facts->chaddr_len >= 6) {
        nearby_pipeline_event_t event = {.type = NEARBY_PIPELINE_EVENT_MATCHED};
        char compact[13], colon[18], device_id[33], identity[40];
        format_mac(facts->chaddr, compact, colon);
        (void)snprintf(device_id, sizeof(device_id), "lan_%s", compact);
        (void)snprintf(identity, sizeof(identity), "dhcp:%s", colon);
        fill_common(&event.matched, device_id, identity,
                    metadata.domain[0] ? metadata.domain : "dhcp", "dhcp", &metadata,
                    facts->hostname);
        copy_text(event.matched.connection_type, sizeof(event.matched.connection_type), "mac");
        copy_text(event.matched.connection_value, sizeof(event.matched.connection_value), colon);
        return queue_event(&event) ? ESP_OK : ESP_FAIL;
    }

    uint64_t h = hash_texts(facts->hostname, facts->vendor_class, NULL);
    nearby_pipeline_event_t event = {.type = NEARBY_PIPELINE_EVENT_MATCHED};
    char device_id[33], identity[40];
    (void)snprintf(device_id, sizeof(device_id), "lan_%016llx", (unsigned long long)h);
    (void)snprintf(identity, sizeof(identity), "dhcp:%016llx", (unsigned long long)h);
    fill_common(&event.matched, device_id, identity,
                metadata.domain[0] ? metadata.domain : "dhcp", "dhcp", &metadata,
                facts->hostname);
    return queue_event(&event) ? ESP_OK : ESP_FAIL;
}

static esp_err_t i154_observation(const nearby_i154_normalized_t *obs,
                                  nearby_i154_parse_result_t mac_result,
                                  const nearby_zigbee_discovery_facts_t *zigbee,
                                  nearby_zigbee_parse_result_t zigbee_result,
                                  void *ctx)
{
    (void)ctx;
    if (!s.db_open || !obs || !zigbee || mac_result < 0 || zigbee_result < 0 ||
        !zigbee->has_manufacturer || !zigbee->has_model) return ESP_OK;
    char key[NEARBY_MATCHER_KEY_MAX];
    if (nearby_match_key_zha_manufacturer_model(zigbee->manufacturer, zigbee->model,
                                                 key, sizeof(key)) != NEARBY_MATCHER_KEY_OK) return ESP_OK;
    nearby_db_value_ref_t ref;
    if (!exact_ref(key, &ref)) return ESP_OK;
    record_metadata_t metadata;
    (void)metadata_from_ref(&ref, &metadata);
    if (!metadata.manufacturer[0]) copy_text(metadata.manufacturer, sizeof(metadata.manufacturer), zigbee->manufacturer);
    if (!metadata.model[0]) copy_text(metadata.model, sizeof(metadata.model), zigbee->model);

    uint8_t ieee[8] = {0};
    bool has_ieee = false;
    if (zigbee->has_source_ieee) {
        memcpy(ieee, zigbee->source_ieee, sizeof(ieee));
        has_ieee = true;
    } else if (zigbee->has_device_announce) {
        memcpy(ieee, zigbee->announced_ieee, sizeof(ieee));
        has_ieee = true;
    } else if (obs->mac.source_addr_len == 8) {
        memcpy(ieee, obs->mac.source_addr, sizeof(ieee));
        has_ieee = true;
    }

    nearby_pipeline_event_t event = {.type = NEARBY_PIPELINE_EVENT_MATCHED};
    char device_id[33], identity[40], connection[24];
    if (has_ieee) {
        (void)snprintf(connection, sizeof(connection),
                       "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
                       ieee[0], ieee[1], ieee[2], ieee[3], ieee[4], ieee[5], ieee[6], ieee[7]);
        (void)snprintf(device_id, sizeof(device_id),
                       "zigbee_%02x%02x%02x%02x%02x%02x%02x%02x",
                       ieee[0], ieee[1], ieee[2], ieee[3], ieee[4], ieee[5], ieee[6], ieee[7]);
        (void)snprintf(identity, sizeof(identity), "zigbee:%s", connection);
    } else {
        uint64_t h = hash_texts(zigbee->manufacturer, zigbee->model, NULL);
        h = fnv1a64_update(h, &zigbee->source_nwk, sizeof(zigbee->source_nwk));
        (void)snprintf(device_id, sizeof(device_id), "zigbee_%016llx", (unsigned long long)h);
        (void)snprintf(identity, sizeof(identity), "zigbee:%016llx", (unsigned long long)h);
        connection[0] = '\0';
    }
    fill_common(&event.matched, device_id, identity, "zha", "zigbee", &metadata, zigbee->model);
    if (connection[0]) {
        copy_text(event.matched.connection_type, sizeof(event.matched.connection_type), "zigbee");
        copy_text(event.matched.connection_value, sizeof(event.matched.connection_value), connection);
    }
    event.matched.has_signal_strength = true;
    event.matched.signal_strength_dbm = obs->meta.rssi;
    return queue_event(&event) ? ESP_OK : ESP_FAIL;
}

static void coordinator_event(const nearby_scan_event_t *event, void *ctx)
{
    (void)ctx;
    if (!event) return;
    while (s.published_terminal_count < event->terminal_count) {
        ++s.published_terminal_count;
        nearby_pipeline_event_t out = {
            .type = NEARBY_PIPELINE_EVENT_MODULE_TERMINAL,
            .terminal_count = s.published_terminal_count,
            .error = event->error,
        };
        (void)queue_event(&out);
    }
}

static void configure_runners(nearby_scan_default_context_t *context,
                              nearby_scan_runners_t *runners,
                              nearby_scan_dedup_t *dedup,
                              nearby_wifi_active_scan_stats_t *wifi_active_stats,
                              nearby_wifi_passive_scan_stats_t *wifi_passive_stats,
                              nearby_zeroconf_scan_stats_t *zeroconf_stats,
                              nearby_ssdp_scan_stats_t *ssdp_stats,
                              nearby_dhcp_scan_stats_t *dhcp_stats,
                              nearby_ble_scan_stats_t *ble_stats,
                              nearby_i154_scan_stats_t *i154_stats)
{
    static nearby_wifi_active_scan_config_t wifi_active;
    static nearby_wifi_passive_scan_config_t wifi_passive;
    static nearby_zeroconf_scan_config_t zeroconf;
    static nearby_ssdp_scan_config_t ssdp;
    static nearby_dhcp_scan_config_t dhcp;
    static nearby_ble_scan_config_t ble;
    static nearby_i154_scan_config_t i154;

    wifi_active = (nearby_wifi_active_scan_config_t){
        .show_hidden = true, .dedup = dedup,
        .observation_cb = wifi_active_observation,
    };
    wifi_passive = (nearby_wifi_passive_scan_config_t){
        .first_channel = 1, .last_channel = 13, .dwell_ms = 100,
        .idle_poll_ticks = pdMS_TO_TICKS(10), .dedup = dedup,
        .observation_cb = wifi_passive_observation,
    };
    zeroconf = (nearby_zeroconf_scan_config_t){
        .duration_ms = 1200, .dedup = dedup, .observation_cb = mdns_observation,
    };
    ssdp = (nearby_ssdp_scan_config_t){
        .duration_ms = 1200, .dedup = dedup, .observation_cb = ssdp_observation,
    };
    dhcp = (nearby_dhcp_scan_config_t){
        .duration_ms = 1200, .dedup = dedup, .observation_cb = dhcp_observation,
    };
    ble = (nearby_ble_scan_config_t){
        .duration_ms = 2500, .passive = true,
        .sync_timeout_ticks = pdMS_TO_TICKS(3000),
        .idle_poll_ticks = pdMS_TO_TICKS(10), .dedup = dedup,
        .observation_cb = ble_observation,
    };
    i154 = (nearby_i154_scan_config_t){
        .first_channel = 11, .last_channel = 26, .dwell_ms = 120,
        .idle_poll_ticks = pdMS_TO_TICKS(10), .observation_cb = i154_observation,
    };

    *context = (nearby_scan_default_context_t){
        .wifi_active_config = &wifi_active, .wifi_active_stats = wifi_active_stats,
        .wifi_management_config = &wifi_passive, .wifi_management_stats = wifi_passive_stats,
        .zeroconf_config = &zeroconf, .zeroconf_stats = zeroconf_stats,
        .ssdp_config = &ssdp, .ssdp_stats = ssdp_stats,
        .dhcp_config = &dhcp, .dhcp_stats = dhcp_stats,
        .ble_config = &ble, .ble_stats = ble_stats,
        .i154_config = &i154, .i154_stats = i154_stats,
    };
    nearby_scan_default_runners_init(context, runners);
}

static void scan_worker(void *arg)
{
    (void)arg;
    for (;;) {
        uint8_t command = 0;
        if (xQueueReceive(s.command_queue, &command, portMAX_DELAY) != pdTRUE) continue;
        s.published_terminal_count = 0;
        s.workspace.data = s.workspace_bytes;
        s.workspace.size = sizeof(s.workspace_bytes);
        s.db_open = nearby_db_open(&s.db, NEARBY_DB_FINAL_PATH) == NEARBY_DB_OK;

        nearby_scan_dedup_t dedup;
        nearby_scan_dedup_reset(&dedup, 0);
        nearby_wifi_active_scan_stats_t wifi_active_stats;
        nearby_wifi_passive_scan_stats_t wifi_passive_stats;
        nearby_zeroconf_scan_stats_t zeroconf_stats;
        nearby_ssdp_scan_stats_t ssdp_stats;
        nearby_dhcp_scan_stats_t dhcp_stats;
        nearby_ble_scan_stats_t ble_stats;
        nearby_i154_scan_stats_t i154_stats;
        nearby_scan_default_context_t default_context;
        nearby_scan_runners_t runners;
        configure_runners(&default_context, &runners, &dedup,
                          &wifi_active_stats, &wifi_passive_stats,
                          &zeroconf_stats, &ssdp_stats, &dhcp_stats,
                          &ble_stats, &i154_stats);

        nearby_scan_coordinator_t coordinator;
        nearby_scan_coordinator_init(&coordinator, coordinator_event, NULL);
        esp_err_t run_err = nearby_scan_coordinator_run(&coordinator,
                                                        NEARBY_SCAN_ALL_MODULES_MASK,
                                                        pdMS_TO_TICKS(100),
                                                        &runners);
        if (coordinator.state == NEARBY_SCAN_STATE_FATAL &&
            nearby_scan_coordinator_gate_held(&coordinator)) {
            esp_err_t recover_err = nearby_scan_coordinator_recover(&coordinator);
            if (recover_err != ESP_OK) {
                nearby_pipeline_event_t fatal = {
                    .type = NEARBY_PIPELINE_EVENT_FATAL,
                    .error = recover_err,
                    .terminal_count = coordinator.terminal_count,
                };
                (void)queue_event(&fatal);
                if (s.db_open) nearby_db_close(&s.db);
                s.db_open = false;
                continue;
            }
        }

        nearby_pipeline_event_t done = {
            .type = NEARBY_PIPELINE_EVENT_SCAN_DONE,
            .error = nearby_scan_coordinator_first_module_error(&coordinator),
            .terminal_count = coordinator.terminal_count,
        };
        if (done.error == ESP_OK && run_err != ESP_OK) done.error = run_err;
        (void)queue_event(&done);
        if (s.db_open) nearby_db_close(&s.db);
        s.db_open = false;
    }
}

esp_err_t nearby_scan_pipeline_init(void)
{
    if (s.event_queue || s.command_queue) return ESP_ERR_INVALID_STATE;
    s.event_queue = xQueueCreate(PIPELINE_EVENT_QUEUE_LEN, sizeof(nearby_pipeline_event_t));
    s.command_queue = xQueueCreate(1, sizeof(uint8_t));
    if (!s.event_queue || !s.command_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(scan_worker, "nearby_scan", PIPELINE_TASK_STACK, NULL,
                    PIPELINE_TASK_PRIORITY, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t nearby_scan_pipeline_request(void)
{
    if (!s.command_queue) return ESP_ERR_INVALID_STATE;
    const uint8_t command = 1;
    return xQueueSend(s.command_queue, &command, 0) == pdTRUE ? ESP_OK : ESP_ERR_INVALID_STATE;
}

bool nearby_scan_pipeline_receive(nearby_pipeline_event_t *event, TickType_t timeout_ticks)
{
    return event && s.event_queue && xQueueReceive(s.event_queue, event, timeout_ticks) == pdTRUE;
}
