#include "nearby_i154_discovery.h"

nearby_i154_parse_result_t nearby_i154_parse_mac(const uint8_t *psdu,
                                                 size_t psdu_len,
                                                 nearby_i154_mac_facts_t *out)
{
    return wireshark_i154_parse_mac(psdu, psdu_len, out);
}
