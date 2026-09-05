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
- The only persistent product data is the read-only recognition/matching database owned by Agent C, plus minimal device-local configuration needed for networking or UI operation.

## Global top bar

The main screen uses a compact fixed top bar:

- left: Scan icon button;
- center: small `NearBy One NEXT` title;
- right: Settings icon button.

Keep the bar as small as practical so the device list receives most of the 320px vertical space. Use a classic Home Assistant-inspired blue/white visual language and rounded white cards.

## Scan interaction contract

Boot must enter an idle state and **must not automatically start a scan**.

The scan lifecycle is:

```text
IDLE
  -> user taps Scan
SCANNING
  -> scanner modules complete one by one
  -> progress advances from real module completion
DONE
  -> progress reaches 100%
  -> UI returns to IDLE
  -> Scan button becomes available for another complete pass
```

The progress indicator is a thin progress bar beneath the fixed top bar. Do not add a separate large scanning animation in v0.1.

Progress must represent actual traversal of scanner modules, not an arbitrary wall-clock timer. Agent B/D will provide scan-session/module-completion events; Agent E only renders aggregate progress. Do not expose protocol names such as BLE, Wi-Fi, Zigbee, Thread or Matter in the normal progress UI.

Devices discovered during scanning may be inserted into the visible Device list as results arrive.

### Scan-time interaction freeze

A scan session is an exclusive foreground operation.

While `SCANNING`:

- the screen may continue repainting;
- the progress bar must continue updating;
- newly discovered Device cards may appear;
- **all touch interaction must be disabled**;
- Device cards must not open;
- Settings must not open;
- scrolling must not begin new user interactions;
- the Scan button itself must not start another scan;
- Entity controls/services must never be invoked.

Implement this in LVGL using the simplest reliable mechanism, preferably a full-screen transparent input-blocking object/overlay above the interactive content while keeping progress/content visible beneath it. Remove the blocker only after the scan session has fully ended.

This UI lock is a UX/safety layer, not the hardware-arbitration authority. Agent B/D must also enforce a scan-session/radio-operation lock below the UI so no device interaction or competing radio operation can start while a complete scan owns the shared RF/hardware resources.

When scanning finishes, remove the input blocker, hide/reset the progress bar, restore the normal Scan icon state, and leave the discovered Device cards available for browsing.

## Screen model

### Nearby device list

The home screen is a **single-column vertically scrollable list of Device cards**.

This is a deliberate product decision for the 170px-wide display. Do not implement a two-column grid for v0.1.

Each Device card must be intentionally uniform, compact, and easy to tap:

- device icon
- device display name

Do **not** render Entity values, Entity controls, manufacturer/model, RSSI, protocol, transport, technical metadata, or per-domain custom summaries on the Device card. The purpose of the list is fast visual discovery and selection, not status monitoring.

All cards should use one generic layout regardless of vendor, protocol, or Entity composition. Avoid special-case card implementations for lights, sensors, media players, etc.

The list must support more devices than fit on one physical screen:

- use LVGL native vertical scrolling on the Device-list container;
- keep the top/header region fixed;
- cards scroll vertically beneath the header;
- do not paginate nearby devices into separate pages;
- do not cap the logical device list to the number visible on screen;
- touch targets must remain comfortable on the 170x320 panel;
- favor a compact fixed/consistent card height so scrolling behavior stays predictable;
- device names should receive most of the horizontal space; truncate only when necessary.

Exact pixel sizes should be tuned on real hardware rather than treated as a new layout framework. A roughly 44-50px card height is a reasonable starting point, but implementation should prioritize legibility and touch comfort.

### Device detail

Tapping a Device opens a detail screen. Only here should the UI enumerate and render that Device's Entities using Home Assistant domain/state/attributes semantics.

The Device detail screen must also be vertically scrollable because a Device may expose more Entities than fit on one 170x320 screen.

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

### Settings

Settings exists for device-local setup only; it is **not** a Home Assistant-style configuration center.

Keep the Settings page minimal:

- Web Management / Setup Portal button;
- current recognition-database version/status;
- current firmware version.

Because the physical screen is too small for comfortable text entry or file selection, **do not implement an on-device software keyboard for Wi-Fi credentials and do not implement database file selection on the LCD**.

Opening Web Management should ask the lower hardware/network layer to start a temporary SoftAP + local Web Portal, then show only the connection instructions needed by the user, for example:

```text
Web Management

Connect your phone to:
NearBy-One-XXXX

Then open:
192.168.4.1

[ Stop Portal ]
```

If the networking layer exposes a temporary AP password/PIN, display it here as well. The on-device UI should clearly show portal running/stopped/success/error state but should not duplicate the browser forms.

## Web Portal UX contract

Agent E owns the **visual/interaction design** of the setup portal. Agent D owns SoftAP, HTTP server, Wi-Fi scan/connection plumbing and transport. Agent C owns recognition-database format validation and SD database import semantics.

The portal should be mobile-first and contain exactly two primary sections/tabs for v0.1:

### 1. Wi-Fi

- show nearby scanned Wi-Fi networks as a selectable list;
- show SSID and useful signal indication;
- allow manual SSID entry as a fallback if needed;
- after choosing a network, provide a password field;
- provide a single clear Connect/Save action;
- show progress and success/failure in the browser;
- after successful provisioning, the device may stop the setup SoftAP according to Agent D's networking lifecycle.

Do not expose low-level Wi-Fi driver terminology to the user.

### 2. Database

- provide a browser file picker for the NearBy recognition database file;
- clearly state that importing a database will replace the SD-card database and may format/reinitialize the SD card;
- require an explicit destructive confirmation before formatting/replacing database contents;
- show upload/import progress and validation result;
- after successful import, show database version/build metadata if Agent C provides it.

The browser workflow should be designed so that the device can format/reinitialize the SD card **before the actual database payload is streamed to its final location**, avoiding any design that assumes the full uploaded database can be buffered in ESP32 RAM or internal flash.

Do not let Agent E invent the database binary format. Agent C defines accepted file structure, header/version/checksum/signature rules, and import success criteria.

## Product UX rule

Normal users see real-world Device names/icons on the Nearby screen and meaningful Entity controls only after opening a Device. Protocol/transport details stay out of the normal UI.

The product is a portable, transient nearby-device browser/controller, not a fixed-home dashboard. Therefore do not implement Home Assistant features whose value depends on a stable environment, including Automations, Scenes, Areas/Rooms, persistent dashboards, history, long-term statistics, favorites, or saved device organization.

## Start immediately: first deliverables

Do not wait for A/B/C/D before building the first UI pass. Use mock data and stub callbacks.

1. Implement the 170x320 Home screen with compact top bar, Home Assistant-inspired blue/white styling, rounded white cards and vertical scrolling.
2. Implement IDLE and SCANNING mock states: no auto-scan on boot, thin real-step-style progress bar, full-screen transparent touch blocker while scanning, and dynamic insertion of mock Device cards.
3. Implement one generic Device detail screen using mock HA-style `sensor`, `binary_sensor`, `switch`, `light`, and `button` Entities.
4. Implement minimal Settings: Web Management button, database version/status, firmware version.
5. Build a static/mobile-first Web Portal prototype with `Wi-Fi` and `Database` tabs using stub data only; do not guess D/C backend APIs.
6. Keep UI code independent from scanner/vendor/protocol details and keep reusable widget code minimal.

## Current phase

The v0.1 screen hierarchy is constrained to:

1. one single-column scrollable Nearby Device-card screen using mock data;
2. one vertically scrollable generic Device detail screen that renders mock Entity/State data;
3. one minimal Settings screen with Web Management entry plus database/firmware versions;
4. one mobile-first Web Setup Portal with Wi-Fi and Database sections.

Do not add additional primary navigation or dashboard concepts until these flows work well on the real 170x320 touch panel.