#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIRESHARK_LAN_PARSE_OK = 0,
    WIRESHARK_LAN_PARSE_PARTIAL = 1,
    WIRESHARK_LAN_PARSE_ERR_ARG = -1,
    WIRESHARK_LAN_PARSE_ERR_MALFORMED = -2,
} wireshark_lan_parse_result_t;

#ifdef __cplusplus
}
#endif
