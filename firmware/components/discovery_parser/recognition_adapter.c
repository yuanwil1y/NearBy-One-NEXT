#include "nearby_recognition_adapter.h"

#include <stdio.h>
#include <string.h>

#define SSDP_MATCH_FIELD_MAX 12u

static nearby_db_text_t text_from_cstr(const char *s, size_t cap)
{
    nearby_db_text_t out = {0};
    if (s == NULL) {
        return out;
    }
    out.ptr = s;
    out.len = strnlen(s, cap);
    return out;
}

static bool valid_match_status(nearby_db_match_status_t status)
{
    return status == NEARBY_DB_MATCH_NOT_FOUND ||
           status == NEARBY_DB_MATCHED ||
           status == NEARBY_DB_MATCH_AMBIGUOUS;
}

static nearby_db_result_t validate_c_result(nearby_db_result_t rc,
                                            nearby_db_match_result_t *result)
{
    if (rc != NEARBY_DB_OK || result == NULL) {
        return rc;
    }
    if (!valid_match_status(result->status)) {
        memset(result, 0, sizeof(*result));
        return NEARBY_DB_ERR_FORMAT;
    }
    if (result->status != NEARBY_DB_MATCHED) {
        memset(&result->value, 0, sizeof(result->value));
    }
    return NEARBY_DB_OK;
}

nearby_db_result_t nearby_recognition_match_ble(
    nearby_db_t *db,
    const nearby_ble_normalized_t *observation,
    nearby_db_match_workspace_t *workspace,
    nearby_db_match_result_t *out_result)
{
    if (db == NULL || observation == NULL || workspace == NULL || out_result == NULL) {
        return NEARBY_DB_ERR_ARG;
    }

    char service_uuid_buf[NEARBY_BLE_MAX_SERVICE_UUIDS][NEARBY_BLE_UUID_STR_LEN];
    nearby_db_text_t service_uuids[NEARBY_BLE_MAX_SERVICE_UUIDS];
    char service_data_uuid_buf[NEARBY_BLE_MAX_SERVICE_DATA][NEARBY_BLE_UUID_STR_LEN];
    nearby_db_text_t service_data_uuids[NEARBY_BLE_MAX_SERVICE_DATA];
    nearby_db_ble_manufacturer_data_t manufacturer[NEARBY_BLE_MAX_MANUFACTURER_DATA];

    for (uint8_t i = 0; i < observation->facts.service_uuid_count; ++i) {
        nearby_ble_uuid_format(&observation->facts.service_uuids[i], service_uuid_buf[i]);
        service_uuids[i].ptr = service_uuid_buf[i];
        service_uuids[i].len = strlen(service_uuid_buf[i]);
    }
    for (uint8_t i = 0; i < observation->facts.service_data_count; ++i) {
        nearby_ble_uuid_format(&observation->facts.service_data[i].uuid,
                               service_data_uuid_buf[i]);
        service_data_uuids[i].ptr = service_data_uuid_buf[i];
        service_data_uuids[i].len = strlen(service_data_uuid_buf[i]);
    }
    for (uint8_t i = 0; i < observation->facts.manufacturer_data_count; ++i) {
        manufacturer[i].manufacturer_id = observation->facts.manufacturer_data[i].company_id;
        manufacturer[i].data = observation->facts.manufacturer_data[i].data;
        manufacturer[i].data_len = observation->facts.manufacturer_data[i].data_len;
    }

    nearby_db_ble_facts_t facts = {
        .connectable = observation->meta.connectable_known && observation->meta.connectable,
        .local_name = observation->facts.has_local_name
                          ? text_from_cstr(observation->facts.local_name,
                                           NEARBY_BLE_LOCAL_NAME_MAX)
                          : (nearby_db_text_t){0},
        .service_uuids = service_uuids,
        .service_uuid_count = observation->facts.service_uuid_count,
        .service_data_uuids = service_data_uuids,
        .service_data_uuid_count = observation->facts.service_data_count,
        .manufacturer_data = manufacturer,
        .manufacturer_data_count = observation->facts.manufacturer_data_count,
    };

    nearby_db_result_t rc = nearby_db_match_ble(db, &facts, workspace, out_result);
    return validate_c_result(rc, out_result);
}

nearby_db_result_t nearby_recognition_match_zeroconf(
    nearby_db_t *db,
    const nearby_mdns_service_t *service,
    nearby_db_match_workspace_t *workspace,
    nearby_db_match_result_t *out_result)
{
    if (db == NULL || service == NULL || workspace == NULL || out_result == NULL) {
        return NEARBY_DB_ERR_ARG;
    }
    nearby_db_kv_t properties[NEARBY_MDNS_MAX_TXT];
    for (uint8_t i = 0; i < service->txt_count; ++i) {
        properties[i].key = text_from_cstr(service->txt[i].key, NEARBY_MDNS_TXT_KEY_MAX);
        properties[i].value = service->txt[i].has_value
                                  ? text_from_cstr(service->txt[i].value,
                                                   NEARBY_MDNS_TXT_VALUE_MAX)
                                  : (nearby_db_text_t){"", 0};
    }
    nearby_db_zeroconf_facts_t facts = {
        .service_type = text_from_cstr(service->service_type, NEARBY_MDNS_NAME_MAX),
        .name = text_from_cstr(service->instance, NEARBY_MDNS_NAME_MAX),
        .properties = properties,
        .property_count = service->txt_count,
    };
    nearby_db_result_t rc = nearby_db_match_zeroconf(db, &facts, workspace, out_result);
    return validate_c_result(rc, out_result);
}

static void add_ssdp_field(nearby_db_kv_t *fields, size_t *count,
                           const char *key, const char *value, size_t cap)
{
    if (*count >= SSDP_MATCH_FIELD_MAX || value == NULL || value[0] == '\0') {
        return;
    }
    fields[*count].key.ptr = key;
    fields[*count].key.len = strlen(key);
    fields[*count].value = text_from_cstr(value, cap);
    ++(*count);
}

nearby_db_result_t nearby_recognition_match_ssdp(
    nearby_db_t *db,
    const nearby_ssdp_facts_t *facts,
    nearby_db_match_workspace_t *workspace,
    nearby_db_match_result_t *out_result)
{
    if (db == NULL || facts == NULL || workspace == NULL || out_result == NULL) {
        return NEARBY_DB_ERR_ARG;
    }
    nearby_db_kv_t fields[SSDP_MATCH_FIELD_MAX];
    size_t count = 0;
    add_ssdp_field(fields, &count, "ST", facts->st, sizeof(facts->st) - 1u);
    add_ssdp_field(fields, &count, "NT", facts->nt, sizeof(facts->nt) - 1u);
    add_ssdp_field(fields, &count, "USN", facts->usn, sizeof(facts->usn) - 1u);
    add_ssdp_field(fields, &count, "SERVER", facts->server, sizeof(facts->server) - 1u);
    add_ssdp_field(fields, &count, "LOCATION", facts->location, sizeof(facts->location) - 1u);
    add_ssdp_field(fields, &count, "MAN", facts->man, sizeof(facts->man) - 1u);
    add_ssdp_field(fields, &count, "MX", facts->mx, sizeof(facts->mx) - 1u);
    add_ssdp_field(fields, &count, "deviceType", facts->device_type, sizeof(facts->device_type) - 1u);
    add_ssdp_field(fields, &count, "manufacturer", facts->manufacturer, sizeof(facts->manufacturer) - 1u);
    add_ssdp_field(fields, &count, "manufacturerURL", facts->manufacturer_url, sizeof(facts->manufacturer_url) - 1u);
    add_ssdp_field(fields, &count, "modelName", facts->model_name, sizeof(facts->model_name) - 1u);
    add_ssdp_field(fields, &count, "modelNumber", facts->model_number, sizeof(facts->model_number) - 1u);

    nearby_db_ssdp_facts_t query = {
        .fields = fields,
        .field_count = count,
    };
    nearby_db_result_t rc = nearby_db_match_ssdp(db, &query, workspace, out_result);
    return validate_c_result(rc, out_result);
}

nearby_db_result_t nearby_recognition_match_dhcp(
    nearby_db_t *db,
    const nearby_dhcp_facts_t *facts,
    nearby_db_match_workspace_t *workspace,
    nearby_db_match_result_t *out_result)
{
    if (db == NULL || facts == NULL || workspace == NULL || out_result == NULL) {
        return NEARBY_DB_ERR_ARG;
    }
    char mac[33] = {0};
    const size_t mac_len = facts->chaddr_len > 16u ? 16u : facts->chaddr_len;
    for (size_t i = 0; i < mac_len; ++i) {
        (void)snprintf(mac + i * 2u, sizeof(mac) - i * 2u, "%02X", facts->chaddr[i]);
    }
    nearby_db_dhcp_facts_t query = {
        .hostname = text_from_cstr(facts->hostname, NEARBY_DHCP_HOSTNAME_MAX),
        .macaddress = {mac, mac_len * 2u},
        .registered_device = false,
    };
    nearby_db_result_t rc = nearby_db_match_dhcp(db, &query, workspace, out_result);
    return validate_c_result(rc, out_result);
}

bool nearby_recognition_result_is_usable(const nearby_db_match_result_t *result)
{
    return result != NULL && result->status == NEARBY_DB_MATCHED &&
           result->value.value_size != 0u;
}
