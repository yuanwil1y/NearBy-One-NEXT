#include "ha_core.h"

#include <string.h>

static bool mock_service_handler(
    const char *entity_id,
    const char *service,
    const ha_attribute_t *data,
    size_t data_count,
    void *ctx) {
    (void)entity_id;
    (void)service;
    (void)data;
    (void)data_count;
    (void)ctx;
    return true;
}

static ha_device_t device(const char *id, const char *name, const char *manufacturer,
                          const char *model, const char *platform_id) {
    ha_device_t out = {0};
    strncpy(out.id, id, sizeof(out.id) - 1);
    strncpy(out.name, name, sizeof(out.name) - 1);
    strncpy(out.manufacturer, manufacturer, sizeof(out.manufacturer) - 1);
    strncpy(out.model, model, sizeof(out.model) - 1);
    strncpy(out.config_entry_id, "mock-entry", sizeof(out.config_entry_id) - 1);
    out.identifier_count = 1;
    strncpy(out.identifiers[0].domain, "mock", sizeof(out.identifiers[0].domain) - 1);
    strncpy(out.identifiers[0].value, platform_id, sizeof(out.identifiers[0].value) - 1);
    return out;
}

static ha_entity_t entity(const char *entity_id, const char *unique_id, const char *domain,
                          const char *device_id, const char *name) {
    ha_entity_t out = {0};
    strncpy(out.entity_id, entity_id, sizeof(out.entity_id) - 1);
    strncpy(out.unique_id, unique_id, sizeof(out.unique_id) - 1);
    strncpy(out.platform, "mock", sizeof(out.platform) - 1);
    strncpy(out.domain, domain, sizeof(out.domain) - 1);
    strncpy(out.device_id, device_id, sizeof(out.device_id) - 1);
    strncpy(out.config_entry_id, "mock-entry", sizeof(out.config_entry_id) - 1);
    strncpy(out.name, name, sizeof(out.name) - 1);
    out.has_entity_name = true;
    out.enabled = true;
    out.available = true;
    return out;
}

void ha_mock_fixtures_load(void) {
    ha_core_reset();

    ha_device_t room = device("dev-env-1", "Living Room Sensor", "Xiaomi", "Mock BLE Env", "env-1");
    room.connections[0] = (ha_connection_t){0};
    strncpy(room.connections[0].type, "bluetooth", sizeof(room.connections[0].type) - 1);
    strncpy(room.connections[0].value, "AA:BB:CC:DD:EE:01", sizeof(room.connections[0].value) - 1);
    room.connection_count = 1;
    (void)ha_device_upsert(&room);

    ha_device_t desk = device("dev-desk-1", "Desk Accessories", "NearBy Mock", "UI Fixture", "desk-1");
    (void)ha_device_upsert(&desk);

    ha_entity_t temperature = entity("sensor.living_room_temperature", "env-1-temp",
                                     HA_DOMAIN_SENSOR, room.id, "Temperature");
    strncpy(temperature.device_class, "temperature", sizeof(temperature.device_class) - 1);
    strncpy(temperature.unit_of_measurement, "°C", sizeof(temperature.unit_of_measurement) - 1);
    (void)ha_entity_upsert(&temperature);
    const ha_attribute_t temp_attrs[] = {
        {.key = "device_class", .value = "temperature"},
        {.key = "unit_of_measurement", .value = "°C"},
        {.key = "state_class", .value = "measurement"},
    };
    (void)ha_state_set(temperature.entity_id, "23.6", temp_attrs, 3);

    ha_entity_t motion = entity("binary_sensor.living_room_motion", "env-1-motion",
                                HA_DOMAIN_BINARY_SENSOR, room.id, "Motion");
    strncpy(motion.device_class, "motion", sizeof(motion.device_class) - 1);
    (void)ha_entity_upsert(&motion);
    const ha_attribute_t motion_attrs[] = {{.key = "device_class", .value = "motion"}};
    (void)ha_state_set(motion.entity_id, HA_STATE_OFF, motion_attrs, 1);

    ha_entity_t plug = entity("switch.desk_plug", "desk-1-plug", HA_DOMAIN_SWITCH,
                              desk.id, "Desk Plug");
    plug.service_handler = mock_service_handler;
    (void)ha_entity_upsert(&plug);
    (void)ha_state_set(plug.entity_id, HA_STATE_ON, NULL, 0);

    ha_entity_t light = entity("light.desk_lamp", "desk-1-light", HA_DOMAIN_LIGHT,
                               desk.id, "Desk Lamp");
    light.service_handler = mock_service_handler;
    (void)ha_entity_upsert(&light);
    const ha_attribute_t light_attrs[] = {
        {.key = "brightness", .value = "180"},
        {.key = "color_mode", .value = "brightness"},
    };
    (void)ha_state_set(light.entity_id, HA_STATE_ON, light_attrs, 2);

    ha_entity_t identify = entity("button.desk_identify", "desk-1-identify", HA_DOMAIN_BUTTON,
                                  desk.id, "Identify");
    identify.service_handler = mock_service_handler;
    (void)ha_entity_upsert(&identify);
    (void)ha_state_set(identify.entity_id, HA_STATE_UNKNOWN, NULL, 0);
}
