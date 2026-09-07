#include "nearby_zigbee_discovery.h"

nearby_zigbee_parse_result_t nearby_zigbee_parse_from_mac(
    const uint8_t *psdu,
    size_t psdu_len,
    const nearby_i154_mac_facts_t *mac,
    nearby_zigbee_discovery_facts_t *out)
{
    return wireshark_zigbee_parse(psdu, psdu_len, mac, out);
}
