# Agent E — LVGL Home Assistant-style UI

## Mission

Build the NearBy One NEXT user interface for the Waveshare ESP32-C6-Touch-LCD-1.9 using LVGL, while reusing Home Assistant's Device / Entity / State semantics as directly as practical.

## Hard constraints

- Target display: 170x320 portrait touch display.
- UI framework: LVGL on ESP-IDF.
- UI consumes only Home Assistant-style `Device`, `Entity`, `State`, and `attributes` data from Agent A's semantic layer.
- UI must not depend on Wi-Fi, BLE, Zigbee, Matter, mDNS, scanner internals, vendor parsers, UUIDs, MAC addresses, clusters, endpoints, or transport-specific code.
- Do not introduce a Capability layer, ViewModel framework, or new product-level abstraction unless absolutely unavoidable.
- Prefer direct reuse of Home Assistant domain/device_class/state conventions over inventing NearBy-specific semantics.
- Prefer copying/adapting existing LVGL examples and Home Assistant interaction patterns over writing new UI frameworks.
- Optimize for ESP32-C6 RAM/Flash limits: flat widgets, small object trees, limited animation, minimal dynamic allocation, no heavy shadow/blur effects.
- Runtime is session-only. Every boot is a new environment. Do not implement persistent devices, favorites, rooms, dashboards, history, automations, scenes, user layouts, or per-device configuration storage.
- The only persistent data in the product architecture is the read-only recognition/matching database owned by Agent C; Agent E does not own or modify it.

## Screen model

### Nearby device list

The home screen is a single scrollable list/grid of Device cards.

Each Device card must be intentionally uniform and minimal:

- device icon
- device display name

Do **not** render Entity values, Entity controls, manufacturer/model, RSSI, protocol, transport, technical metadata, or per-domain custom summaries on the Device card. The purpose of the list is fast visual discovery and selection, not status monitoring.

All cards should use one generic layout regardless of vendor, protocol, or Entity composition. Avoid special-case card implementations for lights, sensors, media players, etc.

### Device detail

Tapping a Device opens a detail screen. Only here should the UI enumerate and render that Device's Entities using Home Assistant domain/state/attributes semantics.

Examples:

- `sensor` -> value/readout
- `binary_sensor` -> state badge
- `switch` -> toggle
- `light` -> on/off plus optional brightness/color controls
- `number` -> slider
- `select` -> picker
- `button` -> action button
- `media_player` -> compact media controls

Manufacturer/model may be shown in the Device detail header if available, but are not required on the Nearby card.

## Product UX rule

Normal users see real-world Device names/icons on the Nearby screen and meaningful Entity controls only after opening a Device. Protocol/transport details stay out of the normal UI.

The product is a portable, transient nearby-device browser/controller, not a fixed-home dashboard. Therefore do not implement Home Assistant features whose value depends on a stable environment, including Automations, Scenes, Areas/Rooms, persistent dashboards, history, long-term statistics, favorites, or saved device organization.

## Current phase

Do not implement the final screen hierarchy until the UI design is agreed in the project discussion. The first implementation target will be a minimal static LVGL prototype with:

1. one Nearby screen containing uniform icon + name Device cards using mock data;
2. one generic Device detail screen that renders mock Entity/State data.
