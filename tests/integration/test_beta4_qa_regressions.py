from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text()


def require(text, needle, label):
    if needle not in text:
        raise AssertionError(f"{label}: missing {needle!r}")


def forbid(text, needle, label):
    if needle in text:
        raise AssertionError(f"{label}: forbidden {needle!r}")


ui = read("components/nearby_ui/nearby_ui.c")
main = read("firmware/main/main.c")
require(ui, "Starting Web Management...", "ASCII LVGL portal status")
forbid(ui, "Starting Web Management…", "unsupported LVGL ellipsis")
forbid(ui, "Password:", "open SoftAP product UI")
forbid(ui, "portal_pin", "open SoftAP product UI state")
forbid(ui, ' ? " · " : ""', "nonessential LVGL middle dot")
forbid(main, "Ready · v", "nonessential LVGL middle dot")
require(main, "Ready - v", "ASCII DB status")

web_h = read("firmware/components/web_mgmt/include/web_mgmt.h")
web_c = read("firmware/components/web_mgmt/web_mgmt.c")
require(web_c, "ap_config.ap.authmode = WIFI_AUTH_OPEN;", "open beta SoftAP")
forbid(web_c, "WIFI_AUTH_WPA2_PSK", "SoftAP password auth")
forbid(web_h, "char password[65]", "SoftAP password status field")
require(web_c, "nearby_wifi_set_mode(WIFI_MODE_STA)", "Stop Portal AP-to-STA transition")
require(web_c, "if (sta_runtime_active() || nearby_db_storage_format_is_active())", "preserve active STA")
require(web_c, "nearby_web_mgmt_maintenance", "deferred STA cleanup")
require(main, "nearby_web_mgmt_maintenance()", "owner-task deferred cleanup")
forbid(web_c, "if (!status.active) {\n        status.sta_ipv4[0]", "inactive portal must not erase STA status")

radio = read("firmware/components/radio_runtime/wifi_runtime.c")
require(radio, "init_cfg.nvs_enable = false;", "no Wi-Fi credential persistence")
require(radio, "esp_wifi_set_storage(WIFI_STORAGE_RAM)", "RAM-only Wi-Fi credentials")
require(main, "scan_session_competing_op_is_active()", "Scan RF exclusivity")

board_h = read("firmware/components/nearby_board/include/nearby_board.h")
board_c = read("firmware/components/nearby_board/nearby_storage.c")
for stage in ("UNMOUNT", "RAW_OPEN", "ERASE_HEAD", "ERASE_TAIL", "RAW_CLOSE", "REMOUNT_FAT", "COMPLETE"):
    require(board_h, f"NEARBY_BOARD_SD_FORMAT_{stage}", "SD format stage enum")
require(board_c, 'ESP_LOGI(TAG, "whole-SD format stage=%s"', "SD stage logging")
require(board_c, "return format_fail(err);", "SD error propagation")

db_h = read("firmware/components/db_storage/include/db_storage.h")
db_c = read("firmware/components/db_storage/db_storage.c")
require(db_h, "NEARBY_DB_FORMAT_RUNNING", "async format state")
require(db_c, "xTaskCreate(format_worker", "async SD format worker")
require(db_c, "FORMAT_TIMEOUT_US", "format deadline")
require(db_c, "esp_timer_start_once", "format timeout watchdog")
require(db_c, "ESP_ERR_TIMEOUT", "explicit timeout failure")
require(db_c, "nearby_board_sd_format_whole(true)", "whole-card worker path")
require(web_c, 'return send_status_plain(req, "202 Accepted", "formatting")', "nonblocking format endpoint")
require(web_c, '\\"db_format_stage\\":\\"%s\\"', "format stage status API")
require(web_c, '\\"db_format_error\\":%d', "format error status API")

finish = db_c.index("s_hooks.on_finish(NEARBY_DB_PART_PATH")
promote = db_c.index("rename(NEARBY_DB_PART_PATH, NEARBY_DB_FINAL_PATH)")
if finish >= promote:
    raise AssertionError("server validation must happen before DB promotion")

canonical = read("web-portal/index.html")
embedded = read("firmware/main/portal_index.html")
if canonical != embedded:
    raise AssertionError("canonical and embedded portal HTML diverged")
require(canonical, "async function waitForFormatResult()", "format polling")
require(canonical, "await waitForFormatResult();", "format must finish before upload")
require(canonical, "status.db_format_state === 'ready'", "format ready gate")
require(canonical, "status.db_format_state === 'failed'", "format failure propagation")
if canonical.index("await waitForFormatResult();") >= canonical.index("fetch('/api/db/upload'"):
    raise AssertionError("upload starts before format reaches ready")

# Retain beta.3 functional contracts most likely to regress in this patch.
require(ui, "#define NB_BLUE 0x03A9F4", "HA/product blue")
require(ui, "lv_font_montserrat_14", "top-bar symbol font")
require(main, "nearby_ui_set_sta_status", "STA Settings status")
require(web_c, "nearby_wifi_portal_scan", "Portal Wi-Fi scan")
require(web_c, "nearby_db_upload_finish()", "NBDB upload finalization")

print("beta.4 QA regression guards: PASS")
