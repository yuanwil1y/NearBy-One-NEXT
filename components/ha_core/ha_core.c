#include "ha_core.h"

#include <string.h>

static ha_device_t s_devices[HA_MAX_DEVICES];
static ha_entity_t s_entities[HA_MAX_ENTITIES];
static ha_state_t s_states[HA_MAX_STATES];
static ha_state_listener_t s_state_listener;
static void *s_state_listener_ctx;

static bool str_present(const char *value) {
    return value != NULL && value[0] != '\0';
}

static bool domain_supported(const char *domain) {
    return domain != NULL &&
           (strcmp(domain, HA_DOMAIN_SENSOR) == 0 ||
            strcmp(domain, HA_DOMAIN_BINARY_SENSOR) == 0 ||
            strcmp(domain, HA_DOMAIN_SWITCH) == 0 ||
            strcmp(domain, HA_DOMAIN_LIGHT) == 0 ||
            strcmp(domain, HA_DOMAIN_BUTTON) == 0);
}

static bool entity_id_matches_domain(const char *entity_id, const char *domain) {
    if (!str_present(entity_id) || !str_present(domain)) {
        return false;
    }
    const size_t domain_len = strlen(domain);
    return strncmp(entity_id, domain, domain_len) == 0 && entity_id[domain_len] == '.';
}

static ha_device_t *device_find_mut(const char *device_id) {
    for (size_t i = 0; i < HA_MAX_DEVICES; ++i) {
        if (s_devices[i].in_use && strcmp(s_devices[i].id, device_id) == 0) {
            return &s_devices[i];
        }
    }
    return NULL;
}

static ha_entity_t *entity_find_mut(const char *entity_id) {
    for (size_t i = 0; i < HA_MAX_ENTITIES; ++i) {
        if (s_entities[i].in_use && strcmp(s_entities[i].entity_id, entity_id) == 0) {
            return &s_entities[i];
        }
    }
    return NULL;
}

static ha_state_t *state_find_mut(const char *entity_id) {
    for (size_t i = 0; i < HA_MAX_STATES; ++i) {
        if (s_states[i].in_use && strcmp(s_states[i].entity_id, entity_id) == 0) {
            return &s_states[i];
        }
    }
    return NULL;
}

static void entity_remove_slot(ha_entity_t *entity) {
    if (entity == NULL || !entity->in_use) {
        return;
    }
    (void)ha_state_remove(entity->entity_id);
    memset(entity, 0, sizeof(*entity));
}

void ha_core_reset(void) {
    memset(s_devices, 0, sizeof(s_devices));
    memset(s_entities, 0, sizeof(s_entities));
    memset(s_states, 0, sizeof(s_states));
    s_state_listener = NULL;
    s_state_listener_ctx = NULL;
}

bool ha_device_upsert(const ha_device_t *device) {
    if (device == NULL || !str_present(device->id) ||
        device->identifier_count > HA_MAX_IDENTIFIERS ||
        device->connection_count > HA_MAX_CONNECTIONS) {
        return false;
    }

    ha_device_t *slot = device_find_mut(device->id);
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

    *slot = *device;
    slot->in_use = true;
    return true;
}

bool ha_device_remove(const char *device_id) {
    ha_device_t *device = device_find_mut(device_id);
    if (device == NULL) {
        return false;
    }

    for (size_t i = 0; i < HA_MAX_ENTITIES; ++i) {
        if (s_entities[i].in_use && strcmp(s_entities[i].device_id, device_id) == 0) {
            entity_remove_slot(&s_entities[i]);
        }
    }
    memset(device, 0, sizeof(*device));
    return true;
}

const ha_device_t *ha_device_get(const char *device_id) {
    return device_find_mut(device_id);
}

const ha_device_t *ha_device_get_by_identifier(const char *domain, const char *value) {
    if (!str_present(domain) || !str_present(value)) {
        return NULL;
    }
    for (size_t i = 0; i < HA_MAX_DEVICES; ++i) {
        if (!s_devices[i].in_use) {
            continue;
        }
        for (size_t j = 0; j < s_devices[i].identifier_count; ++j) {
            if (strcmp(s_devices[i].identifiers[j].domain, domain) == 0 &&
                strcmp(s_devices[i].identifiers[j].value, value) == 0) {
                return &s_devices[i];
            }
        }
    }
    return NULL;
}

const ha_device_t *ha_device_get_by_connection(const char *type, const char *value) {
    if (!str_present(type) || !str_present(value)) {
        return NULL;
    }
    for (size_t i = 0; i < HA_MAX_DEVICES; ++i) {
        if (!s_devices[i].in_use) {
            continue;
        }
        for (size_t j = 0; j < s_devices[i].connection_count; ++j) {
            if (strcmp(s_devices[i].connections[j].type, type) == 0 &&
                strcmp(s_devices[i].connections[j].value, value) == 0) {
                return &s_devices[i];
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
            return &s_devices[i];
        }
    }
    return NULL;
}

bool ha_entity_upsert(const ha_entity_t *entity) {
    if (entity == NULL || !str_present(entity->entity_id) ||
        !str_present(entity->unique_id) || !str_present(entity->platform) ||
        !domain_supported(entity->domain) ||
        !entity_id_matches_domain(entity->entity_id, entity->domain)) {
        return false;
    }
    if (str_present(entity->device_id) && ha_device_get(entity->device_id) == NULL) {
        return false;
    }
    const ha_entity_t *same_unique = ha_entity_get_by_unique_id(
        entity->domain, entity->platform, entity->unique_id);
    if (same_unique != NULL && strcmp(same_unique->entity_id, entity->entity_id) != 0) {
        return false;
    }

    ha_entity_t *slot = entity_find_mut(entity->entity_id);
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

    *slot = *entity;
    slot->in_use = true;
    return true;
}

bool ha_entity_remove(const char *entity_id) {
    ha_entity_t *entity = entity_find_mut(entity_id);
    if (entity == NULL) {
        return false;
    }
    entity_remove_slot(entity);
    return true;
}

const ha_entity_t *ha_entity_get(const char *entity_id) {
    return entity_find_mut(entity_id);
}

const ha_entity_t *ha_entity_get_by_unique_id(
    const char *domain, const char *platform, const char *unique_id) {
    if (!str_present(domain) || !str_present(platform) || !str_present(unique_id)) {
        return NULL;
    }
    for (size_t i = 0; i < HA_MAX_ENTITIES; ++i) {
        if (s_entities[i].in_use && strcmp(s_entities[i].domain, domain) == 0 &&
            strcmp(s_entities[i].platform, platform) == 0 &&
            strcmp(s_entities[i].unique_id, unique_id) == 0) {
            return &s_entities[i];
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
            return &s_entities[i];
        }
    }
    return NULL;
}

bool ha_state_set(
    const char *entity_id,
    const char *state,
    const ha_attribute_t *attributes,
    size_t attribute_count) {
    if (!str_present(entity_id) || state == NULL ||
        attribute_count > HA_MAX_ATTRIBUTES ||
        (attribute_count > 0 && attributes == NULL)) {
        return false;
    }
    const ha_entity_t *entity = ha_entity_get(entity_id);
    if (entity == NULL) {
        return false;
    }

    ha_state_t *slot = state_find_mut(entity_id);
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

    memset(slot, 0, sizeof(*slot));
    slot->in_use = true;
    strncpy(slot->entity_id, entity_id, sizeof(slot->entity_id) - 1);
    strncpy(slot->state, state, sizeof(slot->state) - 1);
    if (attribute_count > 0) {
        memcpy(slot->attributes, attributes, attribute_count * sizeof(attributes[0]));
    }
    slot->attribute_count = attribute_count;

    if (s_state_listener != NULL) {
        s_state_listener(slot, s_state_listener_ctx);
    }
    return true;
}

bool ha_state_remove(const char *entity_id) {
    ha_state_t *state = state_find_mut(entity_id);
    if (state == NULL) {
        return false;
    }
    memset(state, 0, sizeof(*state));
    return true;
}

const ha_state_t *ha_state_get(const char *entity_id) {
    return state_find_mut(entity_id);
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
            return &s_states[i];
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
    return entity != NULL && service != NULL && entity->enabled && entity->available &&
           entity->service_handler != NULL && domain_service_supported(entity->domain, service);
}

bool ha_entity_call_service(
    const char *entity_id,
    const char *service,
    const ha_attribute_t *data,
    size_t data_count) {
    const ha_entity_t *entity = ha_entity_get(entity_id);
    if (!ha_entity_supports_service(entity_id, service) ||
        data_count > HA_MAX_ATTRIBUTES || (data_count > 0 && data == NULL)) {
        return false;
    }
    return entity->service_handler(entity->entity_id, service, data, data_count,
                                   entity->service_context);
}
