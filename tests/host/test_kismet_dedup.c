#include "kismet_dedup.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static wireshark_ble_observation_t sample_ble(void)
{
    wireshark_ble_observation_t obs;
    memset(&obs, 0, sizeof(obs));
    const uint8_t addr[6] = {1, 2, 3, 4, 5, 6};
    memcpy(obs.meta.address, addr, sizeof(addr));
    obs.meta.address_type = 1;
    obs.meta.rssi = -60;
    obs.meta.legacy = true;
    obs.meta.connectable_known = true;
    obs.meta.sid = 0xff;
    obs.facts.has_local_name = true;
    strcpy(obs.facts.local_name, "sensor");
    obs.facts.local_name_complete = true;
    obs.facts.manufacturer_data_count = 1;
    obs.facts.manufacturer_data[0].company_id = 0x004c;
    obs.facts.manufacturer_data[0].data_len = 2;
    obs.facts.manufacturer_data[0].data[0] = 0x02;
    obs.facts.manufacturer_data[0].data[1] = 0x15;
    return obs;
}

int main(void)
{
    kismet_dedup_t state;
    kismet_dedup_init(&state, 10);
    assert(state.generation == 10);
    wireshark_ble_observation_t obs = sample_ble();
    assert(kismet_ble_dedup_should_emit(&state, &obs));
    obs.meta.rssi = -90;
    assert(!kismet_ble_dedup_should_emit(&state, &obs));
    assert(state.ble.duplicates == 1u);
    kismet_dedup_reset(&state, 11);
    assert(state.generation == 11 && state.ble.duplicates == 0u);

    for (unsigned i = 0; i < KISMET_DEDUP_SLOT_COUNT + 3u; ++i) {
        wireshark_dhcp_facts_t facts;
        memset(&facts, 0, sizeof(facts));
        facts.chaddr_len = 6;
        facts.chaddr[0] = (uint8_t)i;
        facts.chaddr[5] = 1;
        assert(kismet_dhcp_dedup_should_emit(&state, &facts));
    }
    assert(state.dhcp.replacements == 3u);
    assert(state.dhcp.replace_cursor == 3u);

    kismet_dedup_reset(&state, 12);
    kismet_wifi_active_facts_t wifi;
    memset(&wifi, 0, sizeof(wifi));
    memcpy(wifi.bssid, "\x01\x02\x03\x04\x05\x06", 6);
    memcpy(wifi.ssid, "lab", 3);
    wifi.ssid_len = 3;
    wifi.primary_channel = 6;
    wifi.authmode = 3;
    wifi.rssi = -30;
    assert(kismet_wifi_active_dedup_should_emit(&state, &wifi));
    wifi.rssi = -80;
    assert(!kismet_wifi_active_dedup_should_emit(&state, &wifi));

    puts("Kismet dedup host tests: OK");
    return 0;
}
