#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * B/C -> A queue payload. It contains only an already-confirmed recognition
 * result plus observation facts copied out of scanner callback scope. It is
 * deliberately not a provider/action/view-model abstraction.
 */
typedef struct {
    char device_id[33];
    char identity[40];
    char connection_type[12];
    char connection_value[40];
    char manufacturer[48];
    char model[48];
    char name[48];
    char platform[32];
    char transport[16];
    bool has_signal_strength;
    int8_t signal_strength_dbm;
} nearby_matched_semantic_t;

/* Owner-task only: this is the sole product glue that mutates Agent A. */
bool nearby_semantic_apply_to_ha(const nearby_matched_semantic_t *matched);

#ifdef __cplusplus
}
#endif
