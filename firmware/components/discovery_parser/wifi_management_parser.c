#include "nearby_wifi_discovery.h"

nearby_wifi_parse_result_t nearby_wifi_parse_management_frame(
    const uint8_t *frame,
    size_t frame_len,
    bool source_truncated,
    nearby_wifi_mgmt_facts_t *out)
{
    return wireshark_80211_parse_mgmt(frame, frame_len, source_truncated, out);
}
