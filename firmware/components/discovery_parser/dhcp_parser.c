#include "nearby_lan_discovery.h"

nearby_lan_parse_result_t nearby_dhcp_parse(const uint8_t *message,
                                            size_t message_len,
                                            nearby_dhcp_facts_t *out)
{
    return wireshark_dhcp_parse(message, message_len, out);
}
