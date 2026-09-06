#include "nearby_lan_scan.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "radio_runtime.h"
#include "scan_session.h"

#define MDNS_PORT 5353u
#define SSDP_PORT 1900u
#define DHCP_SERVER_PORT 67u
#define LAN_SOCKET_POLL_MS 100u
#define DNS_QTYPE_PTR 12u
#define DNS_QCLASS_IN 1u

static uint8_t s_lan_rx[NEARBY_LAN_RX_MAX];
static char s_mdns_types[NEARBY_MDNS_DISCOVERED_TYPE_MAX][NEARBY_MDNS_NAME_MAX + 1];

static TickType_t ms_to_nonzero_ticks(uint32_t ms)
{
    TickType_t ticks = pdMS_TO_TICKS(ms);
    return ticks == 0 ? 1 : ticks;
}

static bool elapsed(TickType_t start, TickType_t duration)
{
    return (TickType_t)(xTaskGetTickCount() - start) >= duration;
}

static esp_err_t require_lan_ipv4(esp_ip4_addr_t *out_ip)
{
    if (scan_session_require_scan_owner() != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_netif_t *netif = nearby_wifi_sta_netif();
    if (netif == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(netif, &info) != ESP_OK || info.ip.addr == 0u) {
        return ESP_ERR_INVALID_STATE;
    }
    if (out_ip != NULL) {
        *out_ip = info.ip;
    }
    return ESP_OK;
}

bool nearby_lan_scan_prerequisite_ready(void)
{
    return require_lan_ipv4(NULL) == ESP_OK;
}

static int udp_socket_with_timeout(void)
{
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        return -1;
    }
    struct timeval timeout = {
        .tv_sec = 0,
        .tv_usec = LAN_SOCKET_POLL_MS * 1000,
    };
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        close(fd);
        return -1;
    }
    int one = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    return fd;
}

static esp_err_t bind_udp(int fd, uint16_t port)
{
    struct sockaddr_in local = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    return bind(fd, (const struct sockaddr *)&local, sizeof(local)) == 0
               ? ESP_OK : ESP_FAIL;
}

static int recv_datagram(int fd)
{
    int rc = recvfrom(fd, s_lan_rx, sizeof(s_lan_rx), 0, NULL, NULL);
    if (rc >= 0) {
        return rc;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        return 0;
    }
    return -1;
}

static bool mdns_add_type(const char *type, uint8_t *count)
{
    if (type == NULL || type[0] != '_' || count == NULL) {
        return false;
    }
    for (uint8_t i = 0; i < *count; ++i) {
        if (strcasecmp(s_mdns_types[i], type) == 0) {
            return false;
        }
    }
    if (*count >= NEARBY_MDNS_DISCOVERED_TYPE_MAX) {
        return false;
    }
    size_t n = strnlen(type, NEARBY_MDNS_NAME_MAX + 1u);
    if (n == 0 || n > NEARBY_MDNS_NAME_MAX) {
        return false;
    }
    memcpy(s_mdns_types[*count], type, n + 1u);
    ++(*count);
    return true;
}

static bool dns_encode_name(uint8_t *buf, size_t cap, size_t *off, const char *name)
{
    if (buf == NULL || off == NULL || name == NULL) {
        return false;
    }
    const char *p = name;
    while (*p != '\0') {
        const char *dot = strchr(p, '.');
        size_t len = dot != NULL ? (size_t)(dot - p) : strlen(p);
        if (len == 0) {
            if (dot != NULL && dot[1] == '\0') {
                p = dot + 1;
                break;
            }
            return false;
        }
        if (len > 63u || *off + 1u + len > cap) {
            return false;
        }
        buf[(*off)++] = (uint8_t)len;
        memcpy(buf + *off, p, len);
        *off += len;
        if (dot == NULL) {
            p += len;
            break;
        }
        p = dot + 1;
    }
    if (*off >= cap) {
        return false;
    }
    buf[(*off)++] = 0;
    return true;
}

static esp_err_t mdns_send_ptr_query(int fd, const char *name)
{
    uint8_t query[128] = {0};
    query[5] = 1; /* QDCOUNT */
    size_t off = 12;
    if (!dns_encode_name(query, sizeof(query), &off, name) || off + 4u > sizeof(query)) {
        return ESP_ERR_INVALID_ARG;
    }
    query[off++] = 0;
    query[off++] = DNS_QTYPE_PTR;
    query[off++] = 0;
    query[off++] = DNS_QCLASS_IN;

    struct sockaddr_in dest = {
        .sin_family = AF_INET,
        .sin_port = htons(MDNS_PORT),
        .sin_addr.s_addr = inet_addr("224.0.0.251"),
    };
    return sendto(fd, query, off, 0, (const struct sockaddr *)&dest, sizeof(dest)) == (ssize_t)off
               ? ESP_OK : ESP_FAIL;
}

static esp_err_t mdns_process_datagram(
    const nearby_zeroconf_scan_config_t *config,
    nearby_zeroconf_scan_stats_t *stats,
    uint8_t *type_count)
{
    nearby_mdns_facts_t facts;
    const nearby_lan_parse_result_t parsed =
        nearby_mdns_parse(s_lan_rx, stats == NULL ? 0u : 0u, &facts);
    (void)config;
    (void)stats;
    (void)type_count;
    (void)parsed;
    return ESP_ERR_INVALID_STATE;
}

static esp_err_t mdns_handle_bytes(
    const nearby_zeroconf_scan_config_t *config,
    nearby_zeroconf_scan_stats_t *stats,
    const uint8_t *data,
    size_t len,
    uint8_t *type_count)
{
    nearby_mdns_facts_t facts;
    const nearby_lan_parse_result_t parsed = nearby_mdns_parse(data, len, &facts);
    ++stats->datagrams;
    if (parsed == NEARBY_LAN_PARSE_ERR_MALFORMED || parsed == NEARBY_LAN_PARSE_ERR_ARG) {
        ++stats->malformed_datagrams;
        return ESP_OK;
    }
    if (parsed == NEARBY_LAN_PARSE_PARTIAL) {
        ++stats->partial_datagrams;
    }

    for (uint8_t i = 0; i < facts.service_count; ++i) {
        const nearby_mdns_service_t *service = &facts.services[i];
        ++stats->services_seen;
        if (strcasecmp(service->service_type, "_services._dns-sd._udp.local.") == 0) {
            if (mdns_add_type(service->instance, type_count)) {
                ++stats->discovered_service_types;
            }
            continue;
        }
        if (config->dedup != NULL &&
            !nearby_mdns_dedup_should_emit(config->dedup, service)) {
            ++stats->duplicate_services;
            continue;
        }
        if (config->observation_cb != NULL) {
            esp_err_t err = config->observation_cb(service, parsed, config->observation_ctx);
            if (err != ESP_OK) {
                return err;
            }
            ++stats->emitted_services;
        }
    }
    return ESP_OK;
}

static esp_err_t mdns_receive_until(
    int fd,
    TickType_t deadline_start,
    TickType_t deadline_ticks,
    const nearby_zeroconf_scan_config_t *config,
    nearby_zeroconf_scan_stats_t *stats,
    uint8_t *type_count)
{
    while (!elapsed(deadline_start, deadline_ticks)) {
        int rc = recv_datagram(fd);
        if (rc < 0) {
            return ESP_FAIL;
        }
        if (rc == 0) {
            continue;
        }
        esp_err_t err = mdns_handle_bytes(config, stats, s_lan_rx, (size_t)rc, type_count);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t nearby_zeroconf_scan_run(const nearby_zeroconf_scan_config_t *config,
                                   nearby_zeroconf_scan_stats_t *stats)
{
    if (config == NULL || stats == NULL || config->duration_ms == 0u) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_ip4_addr_t ip;
    esp_err_t err = require_lan_ipv4(&ip);
    if (err != ESP_OK) {
        return err;
    }
    memset(stats, 0, sizeof(*stats));
    memset(s_mdns_types, 0, sizeof(s_mdns_types));

    int fd = udp_socket_with_timeout();
    if (fd < 0) {
        return ESP_FAIL;
    }
    if (bind_udp(fd, MDNS_PORT) != ESP_OK) {
        close(fd);
        return ESP_FAIL;
    }

    struct ip_mreq membership = {
        .imr_multiaddr.s_addr = inet_addr("224.0.0.251"),
        .imr_interface.s_addr = ip.addr,
    };
    if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership, sizeof(membership)) != 0) {
        close(fd);
        return ESP_FAIL;
    }
    struct in_addr iface = {.s_addr = ip.addr};
    (void)setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &iface, sizeof(iface));

    err = mdns_send_ptr_query(fd, "_services._dns-sd._udp.local.");
    if (err != ESP_OK) {
        close(fd);
        return err;
    }

    const TickType_t total_ticks = ms_to_nonzero_ticks(config->duration_ms);
    TickType_t discovery_ticks = total_ticks / 3u;
    if (discovery_ticks == 0) discovery_ticks = 1;
    const TickType_t start = xTaskGetTickCount();
    uint8_t type_count = 0;
    err = mdns_receive_until(fd, start, discovery_ticks, config, stats, &type_count);
    if (err == ESP_OK) {
        for (uint8_t i = 0; i < type_count; ++i) {
            err = mdns_send_ptr_query(fd, s_mdns_types[i]);
            if (err != ESP_OK) {
                break;
            }
        }
    }
    if (err == ESP_OK) {
        err = mdns_receive_until(fd, start, total_ticks, config, stats, &type_count);
    }

    (void)setsockopt(fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &membership, sizeof(membership));
    close(fd);
    return err;
}

esp_err_t nearby_ssdp_scan_run(const nearby_ssdp_scan_config_t *config,
                               nearby_ssdp_scan_stats_t *stats)
{
    if (config == NULL || stats == NULL || config->duration_ms == 0u) {
        return ESP_ERR_INVALID_ARG;
    }
    if (require_lan_ipv4(NULL) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(stats, 0, sizeof(*stats));
    int fd = udp_socket_with_timeout();
    if (fd < 0) {
        return ESP_FAIL;
    }

    static const char request[] =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST:239.255.255.250:1900\r\n"
        "MAN:\"ssdp:discover\"\r\n"
        "MX:1\r\n"
        "ST:ssdp:all\r\n\r\n";
    struct sockaddr_in dest = {
        .sin_family = AF_INET,
        .sin_port = htons(SSDP_PORT),
        .sin_addr.s_addr = inet_addr("239.255.255.250"),
    };
    if (sendto(fd, request, sizeof(request) - 1u, 0,
               (const struct sockaddr *)&dest, sizeof(dest)) != (ssize_t)(sizeof(request) - 1u)) {
        close(fd);
        return ESP_FAIL;
    }

    const TickType_t duration = ms_to_nonzero_ticks(config->duration_ms);
    const TickType_t start = xTaskGetTickCount();
    esp_err_t first_error = ESP_OK;
    while (!elapsed(start, duration)) {
        int rc = recv_datagram(fd);
        if (rc < 0) {
            first_error = ESP_FAIL;
            break;
        }
        if (rc == 0) continue;
        ++stats->datagrams;
        nearby_ssdp_facts_t facts;
        nearby_lan_parse_result_t parsed = nearby_ssdp_parse(s_lan_rx, (size_t)rc, &facts);
        if (parsed == NEARBY_LAN_PARSE_ERR_MALFORMED || parsed == NEARBY_LAN_PARSE_ERR_ARG) {
            ++stats->malformed_datagrams;
            continue;
        }
        if (parsed == NEARBY_LAN_PARSE_PARTIAL) ++stats->partial_datagrams;
        if (config->dedup != NULL && !nearby_ssdp_dedup_should_emit(config->dedup, &facts)) {
            ++stats->duplicate_datagrams;
            continue;
        }
        if (config->observation_cb != NULL) {
            first_error = config->observation_cb(&facts, parsed, config->observation_ctx);
            if (first_error != ESP_OK) break;
            ++stats->emitted_datagrams;
        }
    }
    close(fd);
    return first_error;
}

esp_err_t nearby_dhcp_scan_run(const nearby_dhcp_scan_config_t *config,
                               nearby_dhcp_scan_stats_t *stats)
{
    if (config == NULL || stats == NULL || config->duration_ms == 0u) {
        return ESP_ERR_INVALID_ARG;
    }
    if (require_lan_ipv4(NULL) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(stats, 0, sizeof(*stats));
    int fd = udp_socket_with_timeout();
    if (fd < 0) {
        return ESP_FAIL;
    }
    int one = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
    if (bind_udp(fd, DHCP_SERVER_PORT) != ESP_OK) {
        close(fd);
        return ESP_FAIL;
    }

    const TickType_t duration = ms_to_nonzero_ticks(config->duration_ms);
    const TickType_t start = xTaskGetTickCount();
    esp_err_t first_error = ESP_OK;
    while (!elapsed(start, duration)) {
        int rc = recv_datagram(fd);
        if (rc < 0) {
            first_error = ESP_FAIL;
            break;
        }
        if (rc == 0) continue;
        ++stats->datagrams;
        nearby_dhcp_facts_t facts;
        nearby_lan_parse_result_t parsed = nearby_dhcp_parse(s_lan_rx, (size_t)rc, &facts);
        if (parsed == NEARBY_LAN_PARSE_ERR_MALFORMED || parsed == NEARBY_LAN_PARSE_ERR_ARG) {
            ++stats->malformed_datagrams;
            continue;
        }
        if (parsed == NEARBY_LAN_PARSE_PARTIAL) ++stats->partial_datagrams;
        if (config->dedup != NULL && !nearby_dhcp_dedup_should_emit(config->dedup, &facts)) {
            ++stats->duplicate_datagrams;
            continue;
        }
        if (config->observation_cb != NULL) {
            first_error = config->observation_cb(&facts, parsed, config->observation_ctx);
            if (first_error != ESP_OK) break;
            ++stats->emitted_datagrams;
        }
    }
    close(fd);
    return first_error;
}
