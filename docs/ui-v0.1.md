# Agent E UI v0.1 integration notes

This branch contains the first mock-driven UI pass required by `AGENTS.md`.

## LVGL surface

`components/nearby_ui` is an ESP-IDF component that depends only on LVGL. It has no scanner, radio, vendor, protocol, MAC/UUID, cluster, endpoint, or transport dependencies.

Logical display size is fixed to **170x320 portrait** for this pass.

Screen hierarchy:

1. **Nearby** — fixed blue top bar, thin scan progress bar, one vertically scrollable column of uniform 48px white Device cards.
2. **Device detail** — generic vertically scrollable Entity rendering using HA-style domain/state semantics. Mock coverage: `sensor`, `binary_sensor`, `switch`, `light`, `button`.
3. **Settings** — Web Management entry, recognition-database status/version, firmware version. Web Management instructions are shown as a small modal instead of adding another primary navigation destination.

The Nearby card deliberately renders only an icon and Device display name. Entity values, RSSI, manufacturer/model, protocol and transport details are not shown there.

## Scan contract

Boot is always `IDLE`; `nearby_ui_init()` never starts a scan.

Production integration should call:

```c
nearby_ui_scan_begin(module_count);
nearby_ui_device_upsert(&device);      // zero or more times while scanning
nearby_ui_scan_module_complete();      // exactly when one scanner module completes
nearby_ui_scan_end();                  // after the complete pass ends
```

`nearby_ui_scan_begin()` exposes the thin progress bar and raises a fully transparent, clickable object on LVGL's top layer. The blocker covers the complete 170x320 touch surface, so Device cards, Settings, scrolling and controls cannot receive new touch input during `SCANNING`. Content below it may continue repainting and newly discovered Device cards may still be inserted.

`nearby_ui_scan_module_complete()` is the only production API that advances progress. There is no wall-clock progress calculation in the production path.

If no `scan_requested` callback is registered, the Scan button uses `nearby_ui_mock_start_scan()`. Its timer is only a bring-up harness: each tick emits one synthetic module-complete event and inserts one mock Device. Replace that callback path with Agent B/D events on hardware.

The UI blocker is intentionally only a UX guard. Agent B/D still own the lower-level exclusive scan/radio-operation lock.

## Data boundary

The public UI input is intentionally small and scanner-agnostic:

- `nearby_ui_device_t` for a HA-style Device identity/display record;
- aggregate scan lifecycle/module completion events;
- device-local database/firmware strings;
- setup-portal running/SSID/address/PIN strings.

Do not add protocol-specific fields to `nearby_ui_device_t`. Entity data should eventually come from Agent A's Device/Entity/State semantic layer; the current entity list is fixed mock data so Agent E can tune rendering before that contract lands.

## Static Web Portal

`web-portal/index.html` is a single-file, mobile-first prototype with no external assets and exactly two primary tabs:

- **Wi-Fi** — mock nearby-network selection, manual SSID fallback, password entry, one Connect/Save action, progress and result state.
- **Database** — file picker, destructive SD/database replacement warning, explicit confirmation, import progress, validation result, and mock database metadata.

The Database prototype deliberately does not read the selected file into JavaScript memory. The intended backend flow is two-phase/streaming: validate enough header metadata to decide whether the file is acceptable, prepare/reinitialize SD if required, then stream the payload to final storage and validate it. Agent C defines the accepted file/header/checksum/signature rules; Agent D defines HTTP/SoftAP transport and Wi-Fi plumbing.

No backend endpoint names are assumed in this branch.
