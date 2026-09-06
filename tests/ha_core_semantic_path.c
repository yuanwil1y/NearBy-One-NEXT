#include "ha_core.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int service_calls;

static bool semantic_service(const char *entity_id, const char *service,
                             const ha_attribute_t *data, size_t data_count, void *ctx) {
    (void)data;
    (void)data_count;
    (void)ctx;
    assert(strcmp(entity_id, "switch.semantic_power") == 0);
    assert(strcmp(service, HA_SERVICE_TURN_ON) == 0);
    ++service_calls;
    return true;
}

static void publish_already_matched_semantics(void) {
    ha_device_t device = {0};
    strcpy(device.id, "semantic-device-1");
    strcpy(device.name, "Test Sensor");
    strcpy(device.manufacturer, "Test Vendor");
    strcpy(device.model, "T100");
    device.identifier_count = 1;
    strcpy(device.identifiers[0].domain, "semantic_fixture");
    strcpy(device.identifiers[0].value, "device-1");
    assert(ha_device_upsert(&device));

    ha_entity_t temperature = {0};
    strcpy(temperature.entity_id, "sensor.semantic_temperature");
    strcpy(temperature.unique_id, "device-1-temperature");
    strcpy(temperature.platform, "semantic_fixture");
    strcpy(temperature.domain, HA_DOMAIN_SENSOR);
    strcpy(temperature.device_id, device.id);
    strcpy(temperature.device_class, "temperature");
    strcpy(temperature.name, "Temperature");
    strcpy(temperature.unit_of_measurement, "°C");
    temperature.has_entity_name = true;
    temperature.enabled = true;
    temperature.available = true;
    assert(ha_entity_upsert(&temperature));

    const ha_attribute_t temp_attrs[] = {
        {.key = "device_class", .value = "temperature"},
        {.key = "unit_of_measurement", .value = "°C"},
    };
    assert(ha_state_set(temperature.entity_id, "23.4", temp_attrs, 2));

    ha_entity_t power = {0};
    strcpy(power.entity_id, "switch.semantic_power");
    strcpy(power.unique_id, "device-1-power");
    strcpy(power.platform, "semantic_fixture");
    strcpy(power.domain, HA_DOMAIN_SWITCH);
    strcpy(power.device_id, device.id);
    strcpy(power.name, "Power");
    power.has_entity_name = true;
    power.enabled = true;
    power.available = true;
    power.service_handler = semantic_service;
    assert(ha_entity_upsert(&power));
    assert(ha_state_set(power.entity_id, HA_STATE_OFF, NULL, 0));
}

int main(void) {
    ha_core_reset();
    publish_already_matched_semantics();

    const ha_device_t *device = ha_device_get_by_identifier("semantic_fixture", "device-1");
    assert(device != NULL);
    assert(strcmp(device->name, "Test Sensor") == 0);
    assert(ha_entity_count_for_device(device->id) == 2);

    for (size_t i = 0; i < ha_entity_count_for_device(device->id); ++i) {
        const ha_entity_t *entity = ha_entity_at_for_device(device->id, i);
        assert(entity != NULL);
        assert(ha_state_get(entity->entity_id) != NULL);
    }

    const size_t device_count = ha_device_count();
    const size_t entity_count = ha_entity_count();
    publish_already_matched_semantics();
    assert(ha_device_count() == device_count);
    assert(ha_entity_count() == entity_count);
    assert(strcmp(ha_state_get("sensor.semantic_temperature")->state, "23.4") == 0);

    assert(ha_entity_call_service("switch.semantic_power", HA_SERVICE_TURN_ON, NULL, 0));
    assert(service_calls == 1);

    puts("ha_core_semantic_path: ok");
    return 0;
}
