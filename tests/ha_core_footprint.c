#include "ha_core.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    const ha_core_footprint_t footprint = ha_core_footprint();
    printf("ha_device_t=%zu bytes\n", footprint.device_struct_bytes);
    printf("ha_entity_t=%zu bytes\n", footprint.entity_struct_bytes);
    printf("ha_state_t=%zu bytes\n", footprint.state_struct_bytes);
    printf("device_pool=%zu bytes (%d slots)\n", footprint.device_pool_bytes, HA_MAX_DEVICES);
    printf("entity_pool=%zu bytes (%d slots)\n", footprint.entity_pool_bytes, HA_MAX_ENTITIES);
    printf("state_pool=%zu bytes (%d slots)\n", footprint.state_pool_bytes, HA_MAX_STATES);
    printf("ha_core_static=%zu bytes\n", footprint.total_static_bytes);

#if HA_MAX_DEVICES == 8 && HA_MAX_ENTITIES == 32 && HA_MAX_STATES == 32 && \
    HA_MAX_ATTRIBUTES == 4 && HA_MAX_IDENTIFIERS == 2 && HA_MAX_CONNECTIONS == 2
    /* Host builds use 64-bit pointers; ESP32-C6 is expected to be no larger. */
    assert(footprint.total_static_bytes < 32u * 1024u);
#endif

    puts("ha_core_footprint: ok");
    return 0;
}
