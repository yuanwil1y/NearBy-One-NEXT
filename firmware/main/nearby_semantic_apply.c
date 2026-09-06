#include "nearby_semantic_apply.h"

#include <stdio.h>
#include <string.h>

#include "ha_core.h"

static void copy_text(char *dst, size_t cap, const char *src)
{
    if (cap == 0) return;
    if (src == NULL) src = "";
    (void)snprintf(dst, cap, "%s", src);
}

bool nearby_semantic_apply_to_ha(const nearby_matched_semantic_t *matched)
{
    if (matched == NULL || matched->device_id[0] == '\0' || matched->identity[0] == '\0') {
        return false;
    }

    ha_device_t device = {0};
    copy_text(device.id, sizeof(device.id), matched->device_id);
    device.identifier_count = 1;
    copy_text(device.identifiers[0].domain, sizeof(device.identifiers[0].domain), "nearby");
    copy_text(device.identifiers[0].value, sizeof(device.identifiers[0].value), matched->identity);
    if (matched->connection_type[0] != '\0' && matched->connection_value[0] != '\0') {
        device.connection_count = 1;
        copy_text(device.connections[0].type, sizeof(device.connections[0].type), matched->connection_type);
        copy_text(device.connections[0].value, sizeof(device.connections[0].value), matched->connection_value);
    }
    copy_text(device.manufacturer, sizeof(device.manufacturer), matched->manufacturer);
    copy_text(device.model, sizeof(device.model), matched->model);
    copy_text(device.name, sizeof(device.name),
              matched->name[0] != '\0' ? matched->name : matched->device_id);
    if (!ha_device_upsert(&device)) {
        return false;
    }

    if (!matched->has_signal_strength) {
        return true;
    }

    ha_entity_t entity = {0};
    if (snprintf(entity.entity_id, sizeof(entity.entity_id),
                 "sensor.%s_rssi", matched->device_id) >= (int)sizeof(entity.entity_id) ||
        snprintf(entity.unique_id, sizeof(entity.unique_id),
                 "%s_rssi", matched->device_id) >= (int)sizeof(entity.unique_id)) {
        return false;
    }
    copy_text(entity.platform, sizeof(entity.platform),
              matched->platform[0] != '\0' ? matched->platform : "nearby");
    copy_text(entity.domain, sizeof(entity.domain), HA_DOMAIN_SENSOR);
    copy_text(entity.device_id, sizeof(entity.device_id), matched->device_id);
    copy_text(entity.device_class, sizeof(entity.device_class), "signal_strength");
    copy_text(entity.name, sizeof(entity.name), "Signal strength");
    copy_text(entity.unit_of_measurement, sizeof(entity.unit_of_measurement), "dBm");
    entity.has_entity_name = true;
    entity.enabled = true;
    entity.available = true;
    if (!ha_entity_upsert(&entity)) {
        return false;
    }

    char state[HA_STATE_LEN];
    (void)snprintf(state, sizeof(state), "%d", (int)matched->signal_strength_dbm);
    ha_attribute_t attributes[1] = {0};
    size_t attribute_count = 0;
    if (matched->transport[0] != '\0') {
        copy_text(attributes[0].key, sizeof(attributes[0].key), "transport");
        copy_text(attributes[0].value, sizeof(attributes[0].value), matched->transport);
        attribute_count = 1;
    }
    return ha_state_set(entity.entity_id, state, attributes, attribute_count);
}
