#include "nearby_lan_discovery.h"

nearby_lan_parse_result_t nearby_mdns_parse(const uint8_t *message,
                                            size_t message_len,
                                            nearby_mdns_facts_t *out)
{
    return wireshark_mdns_parse(message, message_len, out);
}
