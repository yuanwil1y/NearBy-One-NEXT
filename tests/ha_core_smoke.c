#include "ha_core.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int listener_calls;
static int service_calls;

static void on_state(const ha_state_t *state, void *ctx) {
    (void)ctx;
    assert(state != NULL);
    ++listener_calls;
}

static bool on_service(const char *entity_id, const char *service,
                       const ha_attribute_t *data, size_t data_count, void *ctx) {
    (void)data;
    (void)data_count;
    (void)ctx;
    assert(strcmp(entity_id, "switch.test") == 0);
    assert(strcmp(service, HA_SERVICE_TURN_ON) == 0);
    ++service_calls;
    return true;
}

int main(void) {
    ha_core_reset();
    assert(ha_device_count() == 0);
    assert(ha_entity_count() == 0);
    assert(ha_state_count() == 0);

    ha_device_t device = {0};
    strcpy(device.id, "device-1");
    strcpy(device.name, "Test device");
    strcpy(device.config_entry_id, "test-entry");
    device.identifier_count = 1;
    strcpy(device.identifiers[0].domain, "test");
    strcpy(device.identifiers[0].value, "123");
    assert(ha_device_upsert(&device));
    assert(ha_device_get_by_identifier("test", "123") != NULL);

    ha_entity_t entity = {0};
    strcpy(entity.entity_id, "switch.test");
    strcpy(entity.unique_id, "switch-test-123");
    strcpy(entity.platform, "test");
    strcpy(entity.domain, HA_DOMAIN_SWITCH);
    strcpy(entity.device_id, device.id);
    entity.enabled = true;
    entity.available = true;
    entity.service_handler = on_service;
    assert(ha_entity_upsert(&entity));
    assert(ha_entity_get_by_unique_id(HA_DOMAIN_SWITCH, "test", "switch-test-123") != NULL);

    ha_state_set_listener(on_state, NULL);
    const ha_attribute_t attrs[] = {{.key = "friendly_name", .value = "Test switch"}};
    assert(ha_state_set(entity.entity_id, HA_STATE_OFF, attrs, 1));
    assert(listener_calls == 1);
    assert(strcmp(ha_state_get(entity.entity_id)->state, HA_STATE_OFF) == 0);
    assert(ha_entity_supports_service(entity.entity_id, HA_SERVICE_TURN_ON));
    assert(!ha_entity_supports_service(entity.entity_id, HA_SERVICE_PRESS));
    assert(ha_entity_call_service(entity.entity_id, HA_SERVICE_TURN_ON, NULL, 0));
    assert(service_calls == 1);

    assert(ha_device_count() == 1);
    assert(ha_entity_count() == 1);
    assert(ha_state_count() == 1);
    assert(ha_device_at(0) != NULL);
    assert(ha_entity_at(0) != NULL);
    assert(ha_state_at(0) != NULL);

    assert(ha_device_remove(device.id));
    assert(ha_device_count() == 0);
    assert(ha_entity_count() == 0);
    assert(ha_state_count() == 0);

    ha_mock_fixtures_load();
    assert(ha_device_count() == 2);
    assert(ha_entity_count() == 5);
    assert(ha_state_count() == 5);
    assert(ha_entity_get("sensor.living_room_temperature") != NULL);
    assert(ha_entity_get("binary_sensor.living_room_motion") != NULL);
    assert(ha_entity_get("switch.desk_plug") != NULL);
    assert(ha_entity_get("light.desk_lamp") != NULL);
    assert(ha_entity_get("button.desk_identify") != NULL);
    assert(ha_entity_call_service("button.desk_identify", HA_SERVICE_PRESS, NULL, 0));

    ha_core_reset();
    assert(ha_device_count() == 0);
    assert(ha_entity_count() == 0);
    assert(ha_state_count() == 0);

    puts("ha_core_smoke: ok");
    return 0;
}
