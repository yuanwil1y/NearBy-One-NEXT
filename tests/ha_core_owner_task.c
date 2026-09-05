#include "ha_core.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/*
 * Intentionally single-threaded: this models the production ownership contract.
 * A worker task would copy this semantic value into an application-owned queue;
 * only the owner task dequeues it and calls ha_core.
 */
typedef struct {
    ha_device_t device;
    ha_entity_t entity;
    char state[HA_STATE_LEN];
    ha_attribute_t attributes[1];
    size_t attribute_count;
} queued_semantic_result_t;

static queued_semantic_result_t semantic_result(const char *state) {
    queued_semantic_result_t msg = {0};

    strcpy(msg.device.id, "owner-device");
    strcpy(msg.device.name, "Owner Task Sensor");
    msg.device.identifier_count = 1;
    strcpy(msg.device.identifiers[0].domain, "semantic_fixture");
    strcpy(msg.device.identifiers[0].value, "owner-device-1");

    strcpy(msg.entity.entity_id, "sensor.owner_temperature");
    strcpy(msg.entity.unique_id, "owner-temperature-1");
    strcpy(msg.entity.platform, "semantic_fixture");
    strcpy(msg.entity.domain, HA_DOMAIN_SENSOR);
    strcpy(msg.entity.device_id, msg.device.id);
    strcpy(msg.entity.device_class, "temperature");
    strcpy(msg.entity.name, "Temperature");
    strcpy(msg.entity.unit_of_measurement, "C");
    msg.entity.has_entity_name = true;
    msg.entity.enabled = true;
    msg.entity.available = true;

    strcpy(msg.state, state);
    strcpy(msg.attributes[0].key, "unit_of_measurement");
    strcpy(msg.attributes[0].value, "C");
    msg.attribute_count = 1;
    return msg;
}

static void owner_apply_semantic_result(const queued_semantic_result_t *msg) {
    assert(msg != NULL);
    assert(ha_device_upsert(&msg->device));
    assert(ha_entity_upsert(&msg->entity));
    assert(ha_state_set(msg->entity.entity_id, msg->state,
                        msg->attributes, msg->attribute_count));
}

static void owner_enumerate_ui(void) {
    const uint32_t before = ha_core_revision();

    assert(ha_device_count() == 1);
    const ha_device_t *device = ha_device_at(0);
    assert(device != NULL);
    assert(strcmp(device->id, "owner-device") == 0);

    assert(ha_entity_count_for_device(device->id) == 1);
    const ha_entity_t *entity = ha_entity_at_for_device(device->id, 0);
    assert(entity != NULL);
    assert(strcmp(entity->entity_id, "sensor.owner_temperature") == 0);

    const ha_state_t *state = ha_state_get(entity->entity_id);
    assert(state != NULL);
    assert(state->state[0] != '\0');

    /* Read-only owner-task enumeration does not invalidate borrowed views. */
    assert(ha_core_revision() == before);
}

int main(void) {
    ha_core_reset();

    queued_semantic_result_t first = semantic_result("21.5");
    owner_apply_semantic_result(&first);
    owner_enumerate_ui();

    const ha_entity_t *borrowed = ha_entity_get("sensor.owner_temperature");
    assert(borrowed != NULL);
    char durable_entity_id[HA_ENTITY_ID_LEN];
    strcpy(durable_entity_id, borrowed->entity_id);

    const uint32_t before_update = ha_core_revision();
    queued_semantic_result_t second = semantic_result("22.0");
    owner_apply_semantic_result(&second);
    assert(ha_core_revision() != before_update);

    /*
     * revision only tells the owner task that prior borrowed views may be stale.
     * Re-resolve by a copied key; do not use revision as a concurrent-reader lock.
     */
    const ha_state_t *updated = ha_state_get(durable_entity_id);
    assert(updated != NULL);
    assert(strcmp(updated->state, "22.0") == 0);
    owner_enumerate_ui();

    puts("ha_core_owner_task: ok");
    return 0;
}
