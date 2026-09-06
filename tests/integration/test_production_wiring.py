#!/usr/bin/env python3
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
main = (root / "firmware/main/main.c").read_text(encoding="utf-8")
cmake = (root / "firmware/main/CMakeLists.txt").read_text(encoding="utf-8")
ui = (root / "components/nearby_ui/nearby_ui.c").read_text(encoding="utf-8")
pipeline = (root / "firmware/main/nearby_scan_pipeline.c").read_text(encoding="utf-8")
portal = (root / "web-portal/index.html").read_text(encoding="utf-8")
embedded_portal = (root / "firmware/main/portal_index.html").read_text(encoding="utf-8")

required_main = [
    "NearBy One NEXT production boot",
    "product_owner_task",
    "xTaskCreate(product_owner_task",
    "ha_core_reset()",
    "nearby_lvgl_port_init(&s_lcd)",
    "nearby_ui_init(&callbacks)",
    "nearby_scan_pipeline_init()",
    "nearby_scan_pipeline_request()",
    "nearby_scan_pipeline_receive(&event, 0)",
    "nearby_semantic_apply_to_ha(&event->matched)",
    "nearby_ui_scan_begin(PRODUCT_SCAN_MODULES)",
    "nearby_ui_scan_module_complete()",
    "nearby_ui_scan_end()",
    "nearby_board_sd_mount(false)",
    "nearby_db_validate_release_file",
    "nearby_web_mgmt_set_db_validation_hooks",
    "nearby_web_mgmt_start()",
    "nearby_web_mgmt_stop()",
    "nearby_web_portal_register",
    "lv_timer_handler()",
]
for token in required_main:
    assert token in main, f"production wiring missing: {token}"

assert "scan_session_smoke_test" not in main, "default product main still carries D gate smoke"
assert "Agent-D hardware bring-up" not in main, "hardware-audit main leaked into production"
assert '#if CONFIG_NEARBY_AGENT_D_RADIO_SMOKE' in main
assert '#if CONFIG_NEARBY_AGENT_D_BOARD_IO_SMOKE' in main

app_match = re.search(r"void\s+app_main\s*\(void\)\s*\{(?P<body>.*)\}\s*$", main, re.S)
assert app_match, "app_main not found"
app_body = app_match.group("body")
assert "nearby_scan_pipeline_request" not in app_body, "boot must not auto-scan"
assert "nearby_ui_scan_begin" not in app_body, "boot must remain idle"

scan_match = re.search(r"static\s+void\s+owner_start_scan\s*\(void\)\s*\{(?P<body>.*?)\n\}", main, re.S)
assert scan_match, "owner_start_scan not found"
scan_body = scan_match.group("body")
ordered = [
    scan_body.index("ha_core_reset()"),
    scan_body.index("nearby_ui_scan_begin(PRODUCT_SCAN_MODULES)"),
    scan_body.index("nearby_scan_pipeline_request()"),
]
assert ordered == sorted(ordered), "fresh HA generation / progress / pipeline request order regressed"
assert "#define PRODUCT_SCAN_MODULES 7u" in main

assert '"nearby_lvgl_port.c"' in cmake and '"nearby_web_portal.c"' in cmake
assert 'EMBED_TXTFILES "portal_index.html"' in cmake
assert "service_requested" in ui
assert "ha_entity_call_service(" not in ui, "E must enqueue service commands instead of mutating A"
assert "NEARBY_SCAN_ALL_MODULES_MASK" in pipeline
for token in [
    "nearby_recognition_match_ble",
    "nearby_recognition_match_zeroconf",
    "nearby_recognition_match_ssdp",
    "nearby_recognition_match_dhcp",
    "nearby_match_key_oui24",
    "nearby_match_key_zha_manufacturer_model",
    "nearby_match_key_matter_vidpid",
]:
    assert token in pipeline, f"D→B→C path missing: {token}"

assert portal == embedded_portal, "served portal drifted from web-portal source"
assert "mock" not in portal.lower(), "production Web Portal contains mock behavior"
for endpoint in [
    "/api/wifi/scan",
    "/api/wifi/connect",
    "/api/db/preflight-result",
    "/api/db/format?confirm=ERASE",
    "/api/db/upload",
]:
    assert endpoint in portal, f"Web Portal missing real endpoint: {endpoint}"
assert "safe_to_format=1" in portal
assert "crypto.subtle.digest('SHA-256'" in portal

print("production startup/runtime wiring guard: ok")
