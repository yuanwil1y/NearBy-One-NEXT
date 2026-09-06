#pragma once

#include "nearby_i154_discovery.h"
#include "native_handoff.h"

#ifdef __cplusplus
extern "C" {
#endif

nearby_i154_parse_result_t nearby_i154_normalize_record(
    const nearby_i154_rx_record_t *record,
    nearby_i154_normalized_t *out);

#ifdef __cplusplus
}
#endif
