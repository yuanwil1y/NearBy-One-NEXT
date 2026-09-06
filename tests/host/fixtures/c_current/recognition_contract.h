#pragma once

/*
 * Test-only snapshot of Agent C runtime/generator semantics.
 * Source branch: agent-c/device-database
 * Frozen C commit used by this fixture:
 * bae5150aa7d5c512aa072cb1198d65758b58d6f3
 *
 * This is not a second recognition implementation. It exists only to prove B's
 * emitted exact keys and ambiguity handling remain byte-compatible with C.
 */
#define C_CURRENT_COMMIT "bae5150aa7d5c512aa072cb1198d65758b58d6f3"
#define C_DB_OK 0
#define C_DB_AMBIGUOUS 1
#define C_DB_ERR_NOT_FOUND -5
#define C_DB_VALUE_FLAG_AMBIGUOUS 0x0001u

#define C_KEY_ZHA_EXAMPLE "zigbee:Acme\x1fModel-1"
#define C_KEY_Z2M_MODEL_EXAMPLE "zigbee:model:TS0601"
#define C_KEY_Z2M_FINGERPRINT_EXAMPLE "zigbee:fingerprint:_TZE200_test\x1fTS0601"
#define C_KEY_MATTER_EXAMPLE "matter:vidpid:1234:00ab"
#define C_KEY_OUI_EXAMPLE "oui:A1B2C3"
