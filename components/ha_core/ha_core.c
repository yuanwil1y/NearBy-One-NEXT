#include "ha_core.h"

#include <string.h>

typedef struct {
    bool in_use;
    ha_device_t value;
} device_slot_t;

typedef struct {
    bool in_use;
    ha_entity_t value;
} entity_slot_t;

typedef struct {
    bool in_use;
    ha_state_t value;
} state_slot_t;

static device_slot_t s_devices[HA_MAX_DEVICES];
static entity_slot_t s_entities[HA_MAX_ENTITIES];
static state_slot_t s_states[HA_MAX_STATES];
static ha_state_listener_t s_state_listener;
static void *s_state_listener_ctx;
static uint32_t s_revision;

static bool cstr_fits(const char *value, size_t capacity, bool required) {
    if (value == NULL || capacity == 0) {
        return false;
    }
    for (size_t i = 0; i < capacity; ++i) {
        if (value[i] == '\0') {
            return !required || i != 0;
        }
    }
    return false;
}

static bool fixed_string_valid(const char *value, size_t capacity, bool required) {
    return cstr_fits(value, capacity, required);
}

static bool domain_supported(const char *domain) {
    return domain != NULL &&
           (strcmp(domain, HA_DOMAIN_SENSOR) == 0 ||
            strcmp(domain, HA_DOMAIN_BINARY_SENSOR) == 0 ||
            strcmp(domain, HA_DOMAIN_SWITCH) == 0 ||
            strcmp(domain, HA_DOMAIN_LIGHT) == 0 ||
            strcmp(domain, HA_DOMAIN_BUTTON) == 0);
}

static bool slug_valid(const char *value) {
    if (value == NULL || value[0] == '\0') {
        return false;
    }
    for (const char *p = value; *p != '\0'; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) {
            return false;
        }
    }
    return true;
}

static bool entity_id_matches_domain(const char *entity_id, const char *domain) {
    if (!cstr_fits(entity_id, HA_ENTITY_ID_LEN, true) ||
        !cstr_fits(domain, HA_DOMAIN_LEN, true)) {
        return false;
    }
    const size_t domain_len = strlen(domain);
    if (strncmp(entity_id, domain, domain_len) != 0 || entity_id[domain_len] != '.') {
        return false;
    }
    return slug_valid(domain) && slug_valid(entity_id + domain_len + 1);
}

static void revision_bump(void) {
    ++s_revision;
    if (s_revision == 0) {
        ++s_revision;
    }
}

static device_slot_t *device_find_slot(const char *device_id) {
    if (!cstr_fits(device_id, HA_ID_LEN, true)) {
        return NULL;
    }
    for (size_t i = 0; i < HA_MAX_DEVICES; ++i) {
        if (s_devices[i].in_use && strcmp(s_devices[i].value.id, device_id) == 0) {
            return &s_devices[i];
        }
    }
    return NULL;
}

static entity_slot_t *entity_find_slot(const char *entity_id) {
    if (!cstr_fits(entity_id, HA_ENTITY_ID_LEN, true)) {
        return NULL;
    }
    for (size_t i = 0; i < HA_MAX_ENTITIES; ++i) {
        if (s_entities[i].in_use && strcmp(s_entities[i].value.entity_id, entity_id) == 0) {
            return &s_entities[i];
        }
    }
    return NULL;
}

static state_slot_t *state_find_slot(const char *entity_id) {
    if (!cstr_fits(entity_id, HA_ENTITY_ID_LEN, true)) {
        return NULL;
    }
    for (size_t i = 0; i < HA_MAX_STATES; ++i) {
        if (s_states[i].in_use && strcmp(s_states[i].value.entity_id, entity_id) == 0) {
            return &s_states[i];
        }
    }
    return NULL;
}

static bool attributes_valid(const ha_attribute_t *attributes, size_t attribute_count) {
    if (attribute_count > HA_MAX_ATTRIBUTES ||
        (attribute_count > 0 && attributes == NULL)) {
        return false;
    }
    for (size_t i = 0; i < attribute_count; ++i) {
        if (!fixed_string_valid(attributes[i].key, sizeof(attributes[i].key), true) ||
            !fixed_string_valid(attributes[i].value, sizeof(attributes[i].value), false)) {
            return false;
        }
    }
    return true;
}

static bool device_valid(const ha_device_t *device) {
    if (device == NULL ||
        !fixed_string_valid(device->id, sizeof(device->id), true) ||
        device->identifier_count > HA_MAX_IDENTIFIERS ||
        device->connection_count > HA_MAX_CONNECTIONS ||
        (device->identifier_count == 0 && device->connection_count == 0) ||
        !fixed_string_valid(device->manufacturer, sizeof(device->manufacturer), false) ||
        !fixed_string_valid(device->model, sizeof(device->model), false) ||
        !fixed_string_valid(device->model_id, sizeof(device->model_id), false) ||
        !fixed_string_valid(device->name, sizeof(device->name), false)) {
        return false;
    }
    for (size_t i = 0; i < device->identifier_count; ++i) {
        if (!fixed_string_valid(device->identifiers[i].domain,
                                sizeof(device->identifiers[i].domain), true) ||
            !fixed_string_valid(device->identifiers[i].value,
                                sizeof(device->identifiers[i].value), true)) {
            return false;
        }
    }
    for (size_t i = 0; i < device->connection_count; ++i) {
        if (!fixed_string_valid(device->connections[i].type,
                                sizeof(device->connections[i].type), true) ||
            !fixed_string_valid(device->connections[i].value,
                                sizeof(device->connections[i].value), true)) {
            return false;
        }
    }
    return true;
}

static bool entity_valid(const ha_entity_t *entity) {
    return entity != NULL &&
           fixed_string_valid(entity->entity_id, sizeof(entity->entity_id), true) &&
           fixed_string_valid(entity->unique_id, sizeof(entity->unique_id), true) &&
           fixed_string_valid(entity->platform, sizeof(entity->platform), true) &&
           fixed_string_valid(entity->domain, sizeof(entity->domain), true) &&
           fixed_string_valid(entity->device_id, sizeof(entity->device_id), false) &&
           fixed_string_valid(entity->device_class, sizeof(entity->device_class), false) &&
           fixed_string_valid(entity->name, sizeof(entity->name), false) &&
           fixed_string_valid(entity->icon, sizeof(entity->icon), false) &&
           fixed_string_valid(entity->unit_of_measurement,
                              sizeof(entity->unit_of_measurement), false) &&
           domain_supported(entity->domain) &&
           entity_id_matches_domain(entity->entity_id, entity->domain);
}

static bool device_identity_conflicts(const ha_device_t *device, const device_slot_t *self) {
    for (size_t i = 0; i < HA_MAX_DEVICES; ++i) {
        const device_slot_t *slot = &s_devices[i];
        if (!slot->in_use || slot == self) {
            continue;
        }
        for (size_t incoming = 0; incoming < device->identifier_count; ++incoming) {
            for (size_t existing = 0; existing < slot->value.identifier_count; ++existing) {
                if (strcmp(device->identifiers[incoming].domain,
                           slot->value.identifiers[existing].domain) == 0 &&
                    strcmp(device->identifiers[incoming].value,
                           slot->value.identifiers[existing].value) == 0) {
                    return true;
                }
            }
        }
        for (size_t incoming = 0; incoming < device->connection_count; ++incoming) {
            for (size_t existing = 0; existing < slot->value.connection_count; ++existing) {
                if (strcmp(device->connections[incoming].type,
                           slot->value.connections[existing].type) == 0 &&
                    strcmp(device->connections[incoming].value,
                           slot->value.connections[existing].value) == 0) {
                    return true;
                }
            }
        }
    }
    return false;
}

static void state_remove_slot(state_slot_t *slot) {
    if (slot == NULL || !slot->in_use) {
        return;
    }
    memset(slot, 0, sizeof(*slot));
}

static void entity_remove_slot(entity_slot_t *slot) {
    if (slot == NULL || !slot->in_use) {
        return;
    }
    state_slot_t *state = state_find_slot(slot->value.entity_id);
    state_remove_slot(state);
    memset(slot, 0, sizeof(*slot));
}

void ha_core_reset(void) {
    memset(s_devices, 0, sizeof(s_devices));
    memset(s_entities, 0, sizeof(s_entities));
    memset(s_states, 0, sizeof(s_states));
    revision_bump();
}

uint32_t ha_core_revision(void) {
    return s_revision;
}

ha_core_footprint_t ha_core_footprint(void) {
    const ha_core_footprint_t result = {
        .device_struct_bytes = sizeof(ha_device_t),
        .entity_struct_bytes = sizeof(ha_entity_t),
        .state_struct_bytes = sizeof(ha_state_t),
        .device_pool_bytes = sizeof(s_devices),
        .entity_pool_bytes = sizeof(s_entities),
        .state_pool_bytes = sizeof(s_states),
        .total_static_bytes = sizeof(s_devices) + sizeof(s_entities) + sizeof(s_states) +
                              sizeof(s_state_listener) + sizeof(s_state_listener_ctx) +
                              sizeof(s_revision),
    };
    return result;
}

bool ha_device_upsert(const ha_device_t *device) {
    if (!device_valid(device)) {
        return false;
    }

    device_slot_t *slot = device_find_slot(device->id);
    if (device_identity_conflicts(device, slot)) {
        return false;
    }
    if (slot == NULL) {
        for (size_t i = 0; i < HA_MAX_DEVICES; ++i) {
            if (!s_devices[i].in_use) {
                slot = &s_devices[i];
                break;
            }
        }
    }
    if (slot == NULL) {
        return false;
    }

    slot->value = *device;
    slot->in_use = true;
    revision_bump();
    return true;
}

bool ha_device_remove(const char *device_id) {
    device_slot_t *device = device_find_slot(device_id);
    if (device == NULL) {
        return false;
    }

    for (size_t i = 0; i < HA_MAX_ENTITIES; ++i) {
        if (s_entities[i].in_use &&
            strcmp(s_entities[i].value.device_id, device_id) == 0) {
            entity_remove_slot(&s_entities[i]);
        }
    }
    memset(device, 0, sizeof(*device));
    revision_bump();
    return true;
}

const ha_device_t *ha_device_get(const char *device_id) {
    const device_slot_t *slot = device_find_slot(device_id);
    return slot == NULL ? NULL : &slot->value;
}

const ha_device_t *ha_device_get_by_identifier(const char *domain, const char *value) {
    if (!cstr_fits(domain, HA_DOMAIN_LEN, true) ||
        !cstr_fits(value, HA_IDENTIFIER_VALUE_LEN, true)) {
        return NULL;
    }
    for (size_t i = 0; i < HA_MAX_DEVICES; ++i) {
        if (!s_devices[i].in_use) {
            continue;
        }
        for (size_t j = 0; j < s_devices[i].value.identifier_count; ++j) {
            if (strcmp(s_devices[i].value.identifiers[j].domain, domain) == 0 &&
                strcmp(s_devices[i].value.identifiers[j].value, value) == 0) {
                return &s_devices[i].value;
            }
        }
    }
    return NULL;
}

const ha_device_t *ha_device_get_by_connection(const char *type, const char *value) {
    if (!cstr_fits(type, HA_CONNECTION_TYPE_LEN, true) ||
        !cstr_fits(value, HA_CONNECTION_VALUE_LEN, true)) {
        return NULL;
    }
    for (size_t i = 0; i < HA_MAX_DEVICES; ++i) {
        if (!s_devices[i].in_use) {
            continue;
        }
        for (size_t j = 0; j < s_devices[i].value.connection_count; ++j) {
            if (strcmp(s_devices[i].value.connections[j].type, type) == 0 &&
                strcmp(s_devices[i].value.connections[j].value, value) == 0) {
                return &s_devices[i].value;
            }
        }
    }
    return NULL;
}

size_t ha_device_count(void) {
    size_t count = 0;
    for (size_t i = 0; i < HA_MAX_DEVICES; ++i) {
        count += s_devices[i].in_use ? 1u : 0u;
    }
    return count;
}

const ha_device_t *ha_device_at(size_t index) {
    size_t seen = 0;
    for (size_t i = 0; i < HA_MAX_DEVICES; ++i) {
        if (!s_devices[i].in_use) {
            continue;
        }
        if (seen++ == index) {
            return &s_devices[i].value;
        }
    }
    return NULL;
}

bool ha_entity_upsert(const ha_entity_t *entity) {
    if (!entity_valid(entity)) {
        return false;
    }
    if (entity->device_id[0] != '\0' && ha_device_get(entity->device_id) == NULL) {
        return false;
    }

    entity_slot_t *slot = entity_find_slot(entity->entity_id);
    const ha_entity_t *same_unique = ha_entity_get_by_unique_id(
        entity->domain, entity->platform, entity->unique_id);
    if (same_unique != NULL && same_unique != (slot == NULL ? NULL : &slot->value)) {
        return false;
    }
    if (slot != NULL &&
        (strcmp(slot->value.domain, entity->domain) != 0 ||
         strcmp(slot->value.platform, entity->platform) != 0 ||
         strcmp(slot->value.unique_id, entity->unique_id) != 0)) {
        return false;
    }

    if (slot == NULL) {
        for (size_t i = 0; i < HA_MAX_ENTITIES; ++i) {
            if (!s_entities[i].in_use) {
                slot = &s_entities[i];
                break;
            }
        }
    }
    if (slot == NULL) {
        return false;
    }

    slot->value = *entity;
    slot->in_use = true;
    revision_bump();
    return true;
}

bool ha_entity_remove(const char *entity_id) {
    entity_slot_t *slot = entity_find_slot(entity_id);
    if (slot == NULL) {
        return false;
    }
    entity_remove_slot(slot);
    revision_bump();
    return true;
}

const ha_entity_t *ha_entity_get(const char *entity_id) {
    const entity_slot_t *slot = entity_find_slot(entity_id);
    return slot == NULL ? NULL : &slot->value;
}

const ha_entity_t *ha_entity_get_by_unique_id(
    const char *domain, const char *platform, const char *unique_id) {
    if (!cstr_fits(domain, HA_DOMAIN_LEN, true) ||
        !cstr_fits(platform, HA_PLATFORM_LEN, true) ||
        !cstr_fits(unique_id, HA_UNIQUE_ID_LEN, true)) {
        return NULL;
    }
    for (size_t i = 0; i < HA_MAX_ENTITIES; ++i) {
        if (s_entities[i].in_use &&
            strcmp(s_entities[i].value.domain, domain) == 0 &&
            strcmp(s_entities[i].value.platform, platform) == 0 &&
            strcmp(s_entities[i].value.unique_id, unique_id) == 0) {
            return &s_entities[i].value;
        }
    }
    return NULL;
}

size_t ha_entity_count(void) {
    size_t count = 0;
    for (size_t i = 0; i < HA_MAX_ENTITIES; ++i) {
        count += s_entities[i].in_use ? 1u : 0u;
    }
    return count;
}

const ha_entity_t *ha_entity_at(size_t index) {
    size_t seen = 0;
    for (size_t i = 0; i < HA_MAX_ENTITIES; ++i) {
        if (!s_entities[i].in_use) {
            continue;
        }
        if (seen++ == index) {
            return &s_entities[i].value;
        }
    }
    return NULL;
}

size_t ha_entity_count_for_device(const char *device_id) {
    if (!cstr_fits(device_id, HA_ID_LEN, true)) {
        return 0;
    }
    size_t count = 0;
    for (size_t i = 0; i < HA_MAX_ENTITIES; ++i) {
        if (s_entities[i].in_use &&
            strcmp(s_entities[i].value.device_id, device_id) == 0) {
            ++count;
        }
    }
    return count;
}

const ha_entity_t *ha_entity_at_for_device(const char *device_id, size_t index) {
    if (!cstr_fits(device_id, HA_ID_LEN, true)) {
        return NULL;
    }
    size_t seen = 0;
    for (size_t i = 0; i < HA_MAX_ENTITIES; ++i) {
        if (!s_entities[i].in_use ||
            strcmp(s_entities[i].value.device_id, device_id) != 0) {
            continue;
        }
        if (seen++ == index) {
            return &s_entities[i].value;
        }
    }
    return NULL;
}

bool ha_state_set(
    const char *entity_id,
    const char *state,
    const ha_attribute_t *attributes,
    size_t attribute_count) {
    if (!cstr_fits(entity_id, HA_ENTITY_ID_LEN, true) ||
        !cstr_fits(state, HA_STATE_LEN, false) ||
        !attributes_valid(attributes, attribute_count) ||
        ha_entity_get(entity_id) == NULL) {
        return false;
    }

    state_slot_t *slot = state_find_slot(entity_id);
    if (slot == NULL) {
        for (size_t i = 0; i < HA_MAX_STATES; ++i) {
            if (!s_states[i].in_use) {
                slot = &s_states[i];
                break;
            }
        }
    }
    if (slot == NULL) {
        return false;
    }

    memset(&slot->value, 0, sizeof(slot->value));
    memcpy(slot->value.entity_id, entity_id, strlen(entity_id) + 1);
    memcpy(slot->value.state, state, strlen(state) + 1);
    if (attribute_count > 0) {
        memcpy(slot->value.attributes, attributes, attribute_count * sizeof(attributes[0]));
    }
    slot->value.attribute_count = (uint8_t)attribute_count;
    slot->in_use = true;
    revision_bump();

    if (s_state_listener != NULL) {
        s_state_listener(&slot->value, s_state_listener_ctx);
    }
    return true;
}

bool ha_state_remove(const char *entity_id) {
    state_slot_t *slot = state_find_slot(entity_id);
    if (slot == NULL) {
        return false;
    }
    state_remove_slot(slot);
    revision_bump();
    return true;
}

const ha_state_t *ha_state_get(const char *entity_id) {
    const state_slot_t *slot = state_find_slot(entity_id);
    return slot == NULL ? NULL : &slot->value;
}

size_t ha_state_count(void) {
    size_t count = 0;
    for (size_t i = 0; i < HA_MAX_STATES; ++i) {
        count += s_states[i].in_use ? 1u : 0u;
    }
    return count;
}

const ha_state_t *ha_state_at(size_t index) {
    size_t seen = 0;
    for (size_t i = 0; i < HA_MAX_STATES; ++i) {
        if (!s_states[i].in_use) {
            continue;
        }
        if (seen++ == index) {
            return &s_states[i].value;
        }
    }
    return NULL;
}

void ha_state_set_listener(ha_state_listener_t listener, void *ctx) {
    s_state_listener = listener;
    s_state_listener_ctx = ctx;
}

static bool domain_service_supported(const char *domain, const char *service) {
    if (strcmp(domain, HA_DOMAIN_SWITCH) == 0 || strcmp(domain, HA_DOMAIN_LIGHT) == 0) {
        return strcmp(service, HA_SERVICE_TURN_ON) == 0 ||
               strcmp(service, HA_SERVICE_TURN_OFF) == 0;
    }
    if (strcmp(domain, HA_DOMAIN_BUTTON) == 0) {
        return strcmp(service, HA_SERVICE_PRESS) == 0;
    }
    return false;
}

bool ha_entity_supports_service(const char *entity_id, const char *service) {
    const ha_entity_t *entity = ha_entity_get(entity_id);
    return entity != NULL && cstr_fits(service, HA_SERVICE_LEN, true) &&
           entity->enabled && entity->available && entity->service_handler != NULL &&
           domain_service_supported(entity->domain, service);
}

bool ha_entity_call_service(
    const char *entity_id,
    const char *service,
    const ha_attribute_t *data,
    size_t data_count) {
    const ha_entity_t *entity = ha_entity_get(entity_id);
    if (entity == NULL || !ha_entity_supports_service(entity_id, service) ||
        !attributes_valid(data, data_count)) {
        return false;
    }
    return entity->service_handler(entity->entity_id, service, data, data_count,
                                   entity->service_context);
}
