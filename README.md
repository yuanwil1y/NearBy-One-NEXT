# NearBy One NEXT

Portable nearby-device discovery and interaction panel for the Waveshare ESP32-C6-Touch-LCD-1.9.

## Core direction

1. ESP32-C6 / ESP-IDF hardware and protocol drivers.
2. Discovery / scanner layer inspired by mature open-source wireless and network tools.
3. Home Assistant-style integrations, Device / Entity / State model, and upstream recognition data reuse.
4. Home Assistant-like card UI adapted to a 1.9-inch touch display.

The project goal is to maximize upstream code/data reuse and minimize new code. Prefer direct reuse, generated tables, thin glue, and small compatibility shims over new abstractions.

## Research tracks

- Track A: Home Assistant source, discovery databases, Device / Entity / State model, integrations and parser dependencies.
- Track B: Bettercap, Wireshark, Scapy, Kismet, Aircrack-ng, hcxdumptool, mitmproxy, BtleJack, nRF Sniffer and Responder scanner/source reuse map.

## Agent E UI v0.1

The `agent-e/lvgl-ha-ui` branch contains the first mock-driven UI deliverables:

- `components/nearby_ui/` — 170x320 LVGL Home/Device/Settings UI with HA-inspired blue/white cards, real module-completion scan progress, and a full-screen scan-time touch blocker.
- `web-portal/index.html` — mobile-first static Web Management prototype with only Wi-Fi and Database primary sections.
- `docs/ui-v0.1.md` — UI/backend boundary and integration notes for Agents A/B/C/D.

The UI surface is intentionally scanner/protocol agnostic and boots idle; mock scanning is only a bring-up harness.
