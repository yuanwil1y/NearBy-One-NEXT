#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ha_core.h"
#include "nearby_semantic_apply.h"

int main(void)
{
    ha_core_reset();

    nearby_matched_semantic_t ble = {0};
    strcpy(ble.device_id, "ble_aabbccddeeff");
    strcpy(ble.identity, "ble:aa:bb:cc:dd:ee:ff");
    strcpy(ble.connection_type, "bluetooth");
    strcpy(ble.connection_value, "AA:BB:CC:DD:EE:FF");
    strcpy(ble.manufacturer, "Acme");
    strcpy(ble.model, "Thermo One");
    strcpy(ble.name, "Hall Sensor");
    strcpy(ble.platform, "acme_ble");
    strcpy(ble.transport, "ble");
    ble.has_signal_strength = true;
    ble.signal_strength_dbm = -61;

    assert(nearby_semantic_apply_to_ha(&ble));
    assert(ha_device_count() == 1);
    assert(ha_entity_count() == 1);
    assert(ha_state_count() == 1);
    const ha_device_t *device = ha_device_get("ble_aabbccddeeff");
    assert(device != NULL && strcmp(device->name, "Hall Sensor") == 0);
    const ha_state_t *state = ha_state_get("sensor.ble_aabbccddeeff_rssi");
    assert(state != NULL && strcmp(state->state, "-61") == 0);
    assert(state->attribute_count == 1 && strcmp(state->attributes[0].value, "ble") == 0);

    ble.signal_strength_dbm = -55;
    assert(nearby_semantic_apply_to_ha(&ble));
    assert(ha_device_count() == 1 && ha_entity_count() == 1 && ha_state_count() == 1);
    state = ha_state_get("sensor.ble_aabbccddeeff_rssi");
    assert(state != NULL && strcmp(state->state, "-55") == 0);

    nearby_matched_semantic_t lan = {0};
    strcpy(lan.device_id, "lan_0123456789abcdef");
    strcpy(lan.identity, "mdns:0123456789abcdef");
    strcpy(lan.name, "Living Room Bridge");
    strcpy(lan.platform, "homekit_controller");
    strcpy(lan.transport, "zeroconf");
    assert(nearby_semantic_apply_to_ha(&lan));
    assert(ha_device_count() == 2);
    assert(ha_entity_count() == 1);

    nearby_matched_semantic_t invalid = {0};
    strcpy(invalid.device_id, "bad");
    assert(!nearby_semantic_apply_to_ha(&invalid));

    puts("semantic apply integration: ok");
    return 0;
}
