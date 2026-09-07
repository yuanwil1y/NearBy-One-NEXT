# Internal ownership placeholder

Phase 1 reserves `firmware/components/internal/` for non-public shared mechanics
such as radio/resource coordination, bounded packet handoff, and common helpers.

No runtime implementation is moved here in Phase 1. `radio_runtime`,
`native_handoff`, and `scan_session` remain in place until their assigned later
phases. This directory must not become a second public HAL or a wrapper layer
that merely renames ESP-IDF, NimBLE, FreeRTOS, lwIP, or LVGL APIs.
