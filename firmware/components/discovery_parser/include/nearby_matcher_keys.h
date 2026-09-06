#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Exact observation-derived key formats present in Agent C's current DB at
 * bae5150aa7d5c512aa072cb1198d65758b58d6f3.
 *
 * HA Bluetooth/Zeroconf/SSDP/DHCP rows are deliberately NOT represented here:
 * their current stored keys include C-owned domain/index/digest metadata that
 * cannot be derived from an observation. B emits typed normalized facts for
 * those matcher families; C owns matcher iteration/indexing and recognition.
 */
#define NEARBY_MATCHER_KEY_MAX 192u

typedef enum {
    NEARBY_MATCHER_KEY_OK = 0,
    NEARBY_MATCHER_KEY_ERR_ARG = -1,
    NEARBY_MATCHER_KEY_ERR_RANGE = -2,
} nearby_matcher_key_result_t;

nearby_matcher_key_result_t nearby_match_key_zha_manufacturer_model(
    const char *manufacturer,
    const char *model,
    char *out,
    size_t out_size);

nearby_matcher_key_result_t nearby_match_key_z2m_model(
    const char *model,
    char *out,
    size_t out_size);

nearby_matcher_key_result_t nearby_match_key_z2m_fingerprint(
    const char *manufacturer,
    const char *model,
    char *out,
    size_t out_size);

nearby_matcher_key_result_t nearby_match_key_matter_vidpid(
    uint16_t vendor_id,
    uint16_t product_id,
    char *out,
    size_t out_size);

nearby_matcher_key_result_t nearby_match_key_oui24(
    const uint8_t mac[6],
    char *out,
    size_t out_size);

#ifdef __cplusplus
}
#endif
