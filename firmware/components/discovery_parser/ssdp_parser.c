#include "nearby_lan_discovery.h"

nearby_lan_parse_result_t nearby_ssdp_parse(const uint8_t *message,
                                            size_t message_len,
                                            nearby_ssdp_facts_t *out)
{
    return wireshark_ssdp_parse(message, message_len, out);
}
