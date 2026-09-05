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
 *
 * TASK / THREAD OWNERSHIP CONTRACT:
 * ha_core is deliberately single-owner-task and is NOT thread-safe. One application
 * task owns all runtime access: Device/Entity/State mutations, getters/enumeration,
 * State-listener registration, service invocation, and ha_core_revision() sampling.
 * Producers running in other tasks must copy already-matched semantic results into
 * an application-owned queue and let the owner task call ha_core. They must not call
 * ha_core directly.
 *
 * Returned pointers are read-only borrowed views into fixed pools and must not be
 * cached across a successful mutating call. ha_core_revision() is only an owner-task
 * invalidation counter. It is not atomic, a lock, a memory barrier, a seqlock, or any
 * other cross-task synchronization primitive.
 */

#ifndef HA_MAX_DEVICES
#define HA_MAX_DEVICES 8
#endif
#ifndef HA_MAX_ENTITIES
#define HA_MAX_ENTITIES 32
#endif
#ifndef HA_MAX_STATES
#define HA_MAX_STATES HA_MAX_ENTITIES
#endif
#ifndef HA_MAX_ATTRIBUTES
#define HA_MAX_ATTRIBUTES 4
#endif
#ifndef HA_MAX_IDENTIFIERS
#define HA_MAX_IDENTIFIERS 2
#endif
#ifndef HA_MAX_CONNECTIONS
#define HA_MAX_CONNECTIONS 2
#endif

#define HA_ID_LEN 33
#define HA_ENTITY_ID_LEN 64
#define HA_UNIQUE_ID_LEN 64
#define HA_DOMAIN_LEN 20
#define HA_PLATFORM_LEN 32
#define HA_NAME_LEN 48
#define HA_STATE_LEN 40
#define HA_ATTRIBUTE_KEY_LEN 24
#define HA_ATTRIBUTE_VALUE_LEN 40
#define HA_DEVICE_CLASS_LEN 32
#define HA_UNIT_LEN 16
#define HA_ICON_LEN 32
#define HA_CONNECTION_TYPE_LEN 12
#define HA_IDENTIFIER_VALUE_LEN 40
#define HA_CONNECTION_VALUE_LEN 40
#define HA_SERVICE_LEN 16

#if HA_MAX_ATTRIBUTES > UINT8_MAX
#error "HA_MAX_ATTRIBUTES must fit in uint8_t"
#endif
#if HA_MAX_IDENTIFIERS > UINT8_MAX
#error "HA_MAX_IDENTIFIERS must fit in uint8_t"
#endif
#if HA_MAX_CONNECTIONS > UINT8_MAX
#error "HA_MAX_CONNECTIONS must fit in uint8_t"
#endif

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
    char key[HA_ATTRIBUTE_KEY_LEN];
    char value[HA_ATTRIBUTE_VALUE_LEN];
} ha_attribute_t;

typedef struct {
    char domain[HA_DOMAIN_LEN];
    char value[HA_IDENTIFIER_VALUE_LEN];
} ha_identifier_t;

typedef struct {
    char type[HA_CONNECTION_TYPE_LEN];
    char value[HA_CONNECTION_VALUE_LEN];
} ha_connection_t;

typedef struct {
    char id[HA_ID_LEN];
    ha_identifier_t identifiers[HA_MAX_IDENTIFIERS];
    uint8_t identifier_count;
    ha_connection_t connections[HA_MAX_CONNECTIONS];
    uint8_t connection_count;
    char manufacturer[HA_NAME_LEN];
    char model[HA_NAME_LEN];
    char model_id[HA_NAME_LEN];
    char name[HA_NAME_LEN];
} ha_device_t;

typedef bool (*ha_service_handler_t)(
    const char *entity_id,
    const char *service,
    const ha_attribute_t *data,
    size_t data_count,
    void *ctx);

typedef struct {
    char entity_id[HA_ENTITY_ID_LEN];
    char unique_id[HA_UNIQUE_ID_LEN];
    char platform[HA_PLATFORM_LEN];
    char domain[HA_DOMAIN_LEN];
    char device_id[HA_ID_LEN];
    char device_class[HA_DEVICE_CLASS_LEN];
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
    char entity_id[HA_ENTITY_ID_LEN];
    char state[HA_STATE_LEN];
    ha_attribute_t attributes[HA_MAX_ATTRIBUTES];
    uint8_t attribute_count;
} ha_state_t;

typedef struct {
    size_t device_struct_bytes;
    size_t entity_struct_bytes;
    size_t state_struct_bytes;
    size_t device_pool_bytes;
    size_t entity_pool_bytes;
    size_t state_pool_bytes;
    size_t total_static_bytes;
} ha_core_footprint_t;

typedef void (*ha_state_listener_t)(const ha_state_t *state, void *ctx);

void ha_core_reset(void);
/* Owner-task-only invalidation counter; never use it for cross-task synchronization. */
uint32_t ha_core_revision(void);
ha_core_footprint_t ha_core_footprint(void);

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
size_t ha_entity_count_for_device(const char *device_id);
const ha_entity_t *ha_entity_at_for_device(const char *device_id, size_t index);

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

#ifdef __cplusplus
}
#endif
