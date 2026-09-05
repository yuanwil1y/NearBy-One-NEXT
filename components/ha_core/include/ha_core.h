#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Minimal RAM-only subset of Home Assistant Device / Entity / State semantics.
 * Field names intentionally follow HA Core where practical.
 *
 * No persistence is performed. ha_core_reset() returns the runtime to boot-empty.
 */

#ifndef HA_MAX_DEVICES
#define HA_MAX_DEVICES 24
#endif
#ifndef HA_MAX_ENTITIES
#define HA_MAX_ENTITIES 96
#endif
#ifndef HA_MAX_STATES
#define HA_MAX_STATES HA_MAX_ENTITIES
#endif
#ifndef HA_MAX_ATTRIBUTES
#define HA_MAX_ATTRIBUTES 8
#endif
#ifndef HA_MAX_IDENTIFIERS
#define HA_MAX_IDENTIFIERS 4
#endif
#ifndef HA_MAX_CONNECTIONS
#define HA_MAX_CONNECTIONS 4
#endif

#define HA_ID_LEN 40
#define HA_ENTITY_ID_LEN 80
#define HA_UNIQUE_ID_LEN 80
#define HA_DOMAIN_LEN 24
#define HA_PLATFORM_LEN 40
#define HA_NAME_LEN 64
#define HA_VALUE_LEN 80
#define HA_KEY_LEN 40
#define HA_URL_LEN 96
#define HA_VERSION_LEN 32
#define HA_DEVICE_CLASS_LEN 40
#define HA_ENTITY_CATEGORY_LEN 24
#define HA_UNIT_LEN 24
#define HA_ICON_LEN 48
#define HA_CONNECTION_TYPE_LEN 16

#define HA_DOMAIN_SENSOR "sensor"
#define HA_DOMAIN_BINARY_SENSOR "binary_sensor"
#define HA_DOMAIN_SWITCH "switch"
#define HA_DOMAIN_LIGHT "light"
#define HA_DOMAIN_BUTTON "button"

#define HA_SERVICE_TURN_ON "turn_on"
#define HA_SERVICE_TURN_OFF "turn_off"
#define HA_SERVICE_PRESS "press"

#define HA_STATE_ON "on"
#define HA_STATE_OFF "off"
#define HA_STATE_UNAVAILABLE "unavailable"
#define HA_STATE_UNKNOWN "unknown"

typedef struct {
    char key[HA_KEY_LEN];
    char value[HA_VALUE_LEN];
} ha_attribute_t;

typedef struct {
    char domain[HA_DOMAIN_LEN];
    char value[HA_VALUE_LEN];
} ha_identifier_t;

typedef struct {
    char type[HA_CONNECTION_TYPE_LEN];
    char value[HA_VALUE_LEN];
} ha_connection_t;

typedef struct {
    bool in_use;
    char id[HA_ID_LEN];
    char config_entry_id[HA_ID_LEN];
    char config_subentry_id[HA_ID_LEN];
    ha_identifier_t identifiers[HA_MAX_IDENTIFIERS];
    size_t identifier_count;
    ha_connection_t connections[HA_MAX_CONNECTIONS];
    size_t connection_count;
    char manufacturer[HA_NAME_LEN];
    char model[HA_NAME_LEN];
    char model_id[HA_NAME_LEN];
    char name[HA_NAME_LEN];
    char serial_number[HA_NAME_LEN];
    char sw_version[HA_VERSION_LEN];
    char hw_version[HA_VERSION_LEN];
    char configuration_url[HA_URL_LEN];
    char via_device_id[HA_ID_LEN];
} ha_device_t;

typedef bool (*ha_service_handler_t)(
    const char *entity_id,
    const char *service,
    const ha_attribute_t *data,
    size_t data_count,
    void *ctx);

typedef struct {
    bool in_use;
    char entity_id[HA_ENTITY_ID_LEN];
    char unique_id[HA_UNIQUE_ID_LEN];
    char platform[HA_PLATFORM_LEN];
    char domain[HA_DOMAIN_LEN];
    char device_id[HA_ID_LEN];
    char config_entry_id[HA_ID_LEN];
    char config_subentry_id[HA_ID_LEN];
    char device_class[HA_DEVICE_CLASS_LEN];
    char entity_category[HA_ENTITY_CATEGORY_LEN];
    char name[HA_NAME_LEN];
    char icon[HA_ICON_LEN];
    char unit_of_measurement[HA_UNIT_LEN];
    uint32_t supported_features;
    bool has_entity_name;
    bool enabled;
    bool available;
    ha_service_handler_t service_handler;
    void *service_context;
} ha_entity_t;

typedef struct {
    bool in_use;
    char entity_id[HA_ENTITY_ID_LEN];
    char state[HA_VALUE_LEN];
    ha_attribute_t attributes[HA_MAX_ATTRIBUTES];
    size_t attribute_count;
} ha_state_t;

typedef void (*ha_state_listener_t)(const ha_state_t *state, void *ctx);

void ha_core_reset(void);

bool ha_device_upsert(const ha_device_t *device);
bool ha_device_remove(const char *device_id);
const ha_device_t *ha_device_get(const char *device_id);
const ha_device_t *ha_device_get_by_identifier(const char *domain, const char *value);
const ha_device_t *ha_device_get_by_connection(const char *type, const char *value);
size_t ha_device_count(void);
const ha_device_t *ha_device_at(size_t index);

bool ha_entity_upsert(const ha_entity_t *entity);
bool ha_entity_remove(const char *entity_id);
const ha_entity_t *ha_entity_get(const char *entity_id);
const ha_entity_t *ha_entity_get_by_unique_id(
    const char *domain, const char *platform, const char *unique_id);
size_t ha_entity_count(void);
const ha_entity_t *ha_entity_at(size_t index);

bool ha_state_set(
    const char *entity_id,
    const char *state,
    const ha_attribute_t *attributes,
    size_t attribute_count);
bool ha_state_remove(const char *entity_id);
const ha_state_t *ha_state_get(const char *entity_id);
size_t ha_state_count(void);
const ha_state_t *ha_state_at(size_t index);
void ha_state_set_listener(ha_state_listener_t listener, void *ctx);

bool ha_entity_supports_service(const char *entity_id, const char *service);
bool ha_entity_call_service(
    const char *entity_id,
    const char *service,
    const ha_attribute_t *data,
    size_t data_count);

/* Populate deterministic UI-only demo data. Existing runtime data is cleared. */
void ha_mock_fixtures_load(void);

#ifdef __cplusplus
}
#endif
