#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NEARBY_LAN_PARSE_OK = 0,
    NEARBY_LAN_PARSE_PARTIAL = 1,
    NEARBY_LAN_PARSE_ERR_ARG = -1,
    NEARBY_LAN_PARSE_ERR_MALFORMED = -2,
} nearby_lan_parse_result_t;

#define NEARBY_MDNS_NAME_MAX 80
#define NEARBY_MDNS_MAX_SERVICES 6
#define NEARBY_MDNS_MAX_TXT 8
#define NEARBY_MDNS_TXT_KEY_MAX 24
#define NEARBY_MDNS_TXT_VALUE_MAX 48

typedef struct {
    char key[NEARBY_MDNS_TXT_KEY_MAX + 1];
    char value[NEARBY_MDNS_TXT_VALUE_MAX + 1];
    bool has_value;
    bool truncated;
} nearby_mdns_txt_t;

typedef struct {
    char service_type[NEARBY_MDNS_NAME_MAX + 1];
    char instance[NEARBY_MDNS_NAME_MAX + 1];
    char host[NEARBY_MDNS_NAME_MAX + 1];
    uint16_t port;
    bool has_port;
    nearby_mdns_txt_t txt[NEARBY_MDNS_MAX_TXT];
    uint8_t txt_count;
    bool txt_overflow;
    bool has_ipv4;
    uint8_t ipv4[4];
    bool has_ipv6;
    uint8_t ipv6[16];
} nearby_mdns_service_t;

typedef struct {
    nearby_mdns_service_t services[NEARBY_MDNS_MAX_SERVICES];
    uint8_t service_count;
    bool service_overflow;
    bool name_overflow;
    bool rr_walk_limited;
} nearby_mdns_facts_t;

nearby_lan_parse_result_t nearby_mdns_parse(const uint8_t *message,
                                            size_t message_len,
                                            nearby_mdns_facts_t *out);

#define NEARBY_SSDP_START_LINE_MAX 80
#define NEARBY_SSDP_VALUE_MAX 160

typedef struct {
    char start_line[NEARBY_SSDP_START_LINE_MAX + 1];
    char st[NEARBY_SSDP_VALUE_MAX + 1];
    char nt[NEARBY_SSDP_VALUE_MAX + 1];
    char usn[NEARBY_SSDP_VALUE_MAX + 1];
    char server[NEARBY_SSDP_VALUE_MAX + 1];
    char location[NEARBY_SSDP_VALUE_MAX + 1];
    char man[NEARBY_SSDP_VALUE_MAX + 1];
    char mx[16];
    char device_type[NEARBY_SSDP_VALUE_MAX + 1];
    char manufacturer[NEARBY_SSDP_VALUE_MAX + 1];
    char manufacturer_url[NEARBY_SSDP_VALUE_MAX + 1];
    char model_name[NEARBY_SSDP_VALUE_MAX + 1];
    char model_number[64];
    uint16_t header_count;
    bool value_truncated;
    bool header_limit_reached;
} nearby_ssdp_facts_t;

nearby_lan_parse_result_t nearby_ssdp_parse(const uint8_t *message,
                                            size_t message_len,
                                            nearby_ssdp_facts_t *out);

#define NEARBY_DHCP_HOSTNAME_MAX 64
#define NEARBY_DHCP_VENDOR_MAX 64
#define NEARBY_DHCP_CLIENT_ID_MAX 32

typedef struct {
    uint8_t op;
    uint8_t htype;
    uint8_t hlen;
    uint32_t xid;
    uint8_t chaddr[16];
    uint8_t chaddr_len;
    bool has_message_type;
    uint8_t message_type;
    char hostname[NEARBY_DHCP_HOSTNAME_MAX + 1];
    bool hostname_truncated;
    char vendor_class[NEARBY_DHCP_VENDOR_MAX + 1];
    bool vendor_class_truncated;
    uint8_t client_identifier[NEARBY_DHCP_CLIENT_ID_MAX];
    uint8_t client_identifier_len;
    bool client_identifier_truncated;
    bool options_ended;
} nearby_dhcp_facts_t;

nearby_lan_parse_result_t nearby_dhcp_parse(const uint8_t *message,
                                            size_t message_len,
                                            nearby_dhcp_facts_t *out);

#ifdef __cplusplus
}
#endif
