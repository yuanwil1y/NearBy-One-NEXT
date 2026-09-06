#include "nearby_ble_discovery.h"
#include "nearby_i154_discovery.h"
#include "nearby_lan_discovery.h"
#include "nearby_wifi_discovery.h"
#include "nearby_zigbee_discovery.h"

#include <stdint.h>
#include <stdio.h>

static uint32_t rng_state = 0x6e627931u;

static uint32_t rnd(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}

static void fill(uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        buf[i] = (uint8_t)(rnd() >> 24);
    }
}

int main(void)
{
    uint8_t buf[320];
    for (unsigned iteration = 0; iteration < 10000u; ++iteration) {
        const size_t len = rnd() % sizeof(buf);
        fill(buf, len);

        nearby_ble_discovery_facts_t ble;
        (void)nearby_ble_parse_advertisement(buf, len, (rnd() & 1u) != 0u, &ble);

        nearby_mdns_facts_t mdns;
        (void)nearby_mdns_parse(buf, len, &mdns);

        nearby_ssdp_facts_t ssdp;
        (void)nearby_ssdp_parse(buf, len, &ssdp);

        nearby_dhcp_facts_t dhcp;
        (void)nearby_dhcp_parse(buf, len, &dhcp);

        nearby_wifi_mgmt_facts_t wifi;
        (void)nearby_wifi_parse_management_frame(buf, len,
                                                 (rnd() & 1u) != 0u,
                                                 &wifi);

        const size_t i154_len = len > 127u ? 127u : len;
        nearby_i154_mac_facts_t mac;
        const nearby_i154_parse_result_t mac_result =
            nearby_i154_parse_mac(buf, i154_len, &mac);
        if (mac_result == NEARBY_I154_PARSE_OK) {
            nearby_zigbee_discovery_facts_t zigbee;
            (void)nearby_zigbee_parse_from_mac(buf, i154_len, &mac, &zigbee);
        }
    }
    puts("parser fuzz host tests: OK");
    return 0;
}
