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
- Runtime is session-only. Do not design UI around persistent device history or configuration databases.

## Primary UI responsibility

Render nearby physical devices as user-facing cards. A Device groups its Entities; Entity domain/state/attributes determine what the UI displays and which controls are available.

Examples:

- `sensor` -> value/readout
- `binary_sensor` -> state badge
- `switch` -> toggle
- `light` -> on/off plus optional brightness/color controls
- `number` -> slider
- `select` -> picker
- `button` -> action button
- `media_player` -> compact media controls

## Product UX rule

Normal users should see real-world device semantics only: name, manufacturer/model when known, current state, nearby/presence hint, and meaningful controls. Protocol/transport details stay out of the normal UI.

## Current phase

Do not implement the final screen hierarchy until the UI design is agreed in the project discussion. The first implementation target will be a minimal static LVGL prototype of the agreed Nearby device list and one Device detail screen, using mock Device/Entity/State data.
