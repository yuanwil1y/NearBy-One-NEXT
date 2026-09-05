#include "ha_core.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static bool service_ok(const char *entity_id, const char *service,
                       const ha_attribute_t *data, size_t data_count, void *ctx) {
    (void)entity_id;
    (void)service;
    (void)data;
    (void)data_count;
    (void)ctx;
    return true;
}

static void set_device(ha_device_t *device, unsigned index) {
    memset(device, 0, sizeof(*device));
    snprintf(device->id, sizeof(device->id), "device-%u", index);
    strcpy(device->identifiers[0].domain, "test");
    snprintf(device->identifiers[0].value, sizeof(device->identifiers[0].value),
             "device-%u", index);
    device->identifier_count = 1;
}

static void set_entity(ha_entity_t *entity, unsigned index, const char *device_id) {
    memset(entity, 0, sizeof(*entity));
    snprintf(entity->entity_id, sizeof(entity->entity_id), "sensor.test_%u", index);
    snprintf(entity->unique_id, sizeof(entity->unique_id), "unique-%u", index);
    strcpy(entity->platform, "test");
    strcpy(entity->domain, HA_DOMAIN_SENSOR);
    if (device_id != NULL) {
        strncpy(entity->device_id, device_id, sizeof(entity->device_id) - 1);
    }
    entity->enabled = true;
    entity->available = true;
}

int main(void) {
    ha_core_reset();

    for (unsigned i = 0; i < HA_MAX_DEVICES; ++i) {
        ha_device_t device;
        set_device(&device, i);
        assert(ha_device_upsert(&device));
    }
    ha_device_t overflow_device;
    set_device(&overflow_device, HA_MAX_DEVICES);
    assert(!ha_device_upsert(&overflow_device));
    assert(ha_device_count() == HA_MAX_DEVICES);

    ha_core_reset();
    ha_device_t no_identity = {0};
    strcpy(no_identity.id, "no-identity");
    assert(!ha_device_upsert(&no_identity));

    ha_device_t owner;
    set_device(&owner, 0);
    strcpy(owner.identifiers[0].value, "same-id");
    assert(ha_device_upsert(&owner));

    ha_device_t collision;
    set_device(&collision, 1);
    strcpy(collision.identifiers[0].value, "same-id");
    assert(!ha_device_upsert(&collision));
    assert(ha_device_count() == 1);

    ha_device_t connection_owner;
    set_device(&connection_owner, 2);
    connection_owner.connection_count = 1;
    strcpy(connection_owner.connections[0].type, "mac");
    strcpy(connection_owner.connections[0].value, "00:11:22:33:44:55");
    assert(ha_device_upsert(&connection_owner));

    ha_device_t connection_collision;
    set_device(&connection_collision, 3);
    connection_collision.connection_count = 1;
    strcpy(connection_collision.connections[0].type, "mac");
    strcpy(connection_collision.connections[0].value, "00:11:22:33:44:55");
    assert(!ha_device_upsert(&connection_collision));

    ha_entity_t missing_owner;
    set_entity(&missing_owner, 0, "missing");
    assert(!ha_entity_upsert(&missing_owner));

    ha_entity_t invalid_entity_id;
    set_entity(&invalid_entity_id, 0, owner.id);
    strcpy(invalid_entity_id.entity_id, "sensor.Bad-Name");
    assert(!ha_entity_upsert(&invalid_entity_id));

    ha_entity_t first;
    set_entity(&first, 0, owner.id);
    assert(ha_entity_upsert(&first));

    ha_entity_t same_unique;
    set_entity(&same_unique, 1, owner.id);
    strcpy(same_unique.unique_id, first.unique_id);
    assert(!ha_entity_upsert(&same_unique));

    ha_entity_t retarget = first;
    strcpy(retarget.unique_id, "changed-identity");
    assert(!ha_entity_upsert(&retarget));

    ha_attribute_t too_many[HA_MAX_ATTRIBUTES + 1];
    memset(too_many, 0, sizeof(too_many));
    for (size_t i = 0; i < HA_MAX_ATTRIBUTES + 1; ++i) {
        strcpy(too_many[i].key, "k");
    }
    assert(!ha_state_set(first.entity_id, "1", too_many, HA_MAX_ATTRIBUTES + 1));

    ha_entity_t control = {0};
    strcpy(control.entity_id, "switch.control");
    strcpy(control.unique_id, "switch-control");
    strcpy(control.platform, "test");
    strcpy(control.domain, HA_DOMAIN_SWITCH);
    strcpy(control.device_id, owner.id);
    control.enabled = false;
    control.available = true;
    control.service_handler = service_ok;
    assert(ha_entity_upsert(&control));
    assert(!ha_entity_supports_service(control.entity_id, HA_SERVICE_TURN_ON));

    control.enabled = true;
    control.available = false;
    assert(ha_entity_upsert(&control));
    assert(!ha_entity_supports_service(control.entity_id, HA_SERVICE_TURN_ON));

    control.available = true;
    assert(ha_entity_upsert(&control));
    assert(ha_entity_supports_service(control.entity_id, HA_SERVICE_TURN_ON));

    ha_core_reset();
    set_device(&owner, 0);
    assert(ha_device_upsert(&owner));
    for (unsigned i = 0; i < HA_MAX_ENTITIES; ++i) {
        ha_entity_t entity;
        set_entity(&entity, i, owner.id);
        assert(ha_entity_upsert(&entity));
        if (i < HA_MAX_STATES) {
            assert(ha_state_set(entity.entity_id, "ok", NULL, 0));
        } else {
            assert(!ha_state_set(entity.entity_id, "full", NULL, 0));
        }
    }
    ha_entity_t overflow_entity;
    set_entity(&overflow_entity, HA_MAX_ENTITIES, owner.id);
    assert(!ha_entity_upsert(&overflow_entity));
    assert(ha_entity_count() == HA_MAX_ENTITIES);
    assert(ha_state_count() == (HA_MAX_STATES < HA_MAX_ENTITIES ? HA_MAX_STATES : HA_MAX_ENTITIES));

    char unterminated[HA_ENTITY_ID_LEN];
    memset(unterminated, 'x', sizeof(unterminated));
    assert(ha_entity_get(unterminated) == NULL);
    assert(!ha_state_set(unterminated, "x", NULL, 0));

    ha_core_reset();
    assert(ha_device_count() == 0);
    assert(ha_entity_count() == 0);
    assert(ha_state_count() == 0);

    puts("ha_core_boundaries: ok");
    return 0;
}
