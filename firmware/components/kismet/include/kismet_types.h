#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KISMET_WIFI_SSID_MAX 32u
#define KISMET_DEDUP_SLOT_COUNT 24u

typedef struct {
    uint8_t bssid[6];
    uint8_t ssid[KISMET_WIFI_SSID_MAX];
    uint8_t ssid_len;
    bool hidden_ssid;
    uint8_t primary_channel;
    int8_t rssi;
    int authmode;
} kismet_wifi_active_facts_t;

typedef struct {
    uint64_t h1;
    uint64_t h2;
    bool used;
} kismet_dedup_slot_t;

typedef struct {
    kismet_dedup_slot_t slots[KISMET_DEDUP_SLOT_COUNT];
    uint8_t replace_cursor;
    uint32_t duplicates;
    uint32_t replacements;
} kismet_dedup_table_t;

typedef struct {
    uint32_t generation;
    kismet_dedup_table_t ble;
    kismet_dedup_table_t mdns;
    kismet_dedup_table_t ssdp;
    kismet_dedup_table_t dhcp;
    kismet_dedup_table_t wifi_active;
    kismet_dedup_table_t wifi_passive;
} kismet_dedup_t;

#ifdef __cplusplus
}
#endif
