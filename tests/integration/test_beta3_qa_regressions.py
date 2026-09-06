from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]

def require(text, needle, label):
    if needle not in text:
        raise AssertionError(f"{label}: missing {needle!r}")

sdk = (ROOT / "firmware/sdkconfig.defaults").read_text()
require(sdk, "CONFIG_LV_CONF_MINIMAL=y", "LVGL minimal baseline")
require(sdk, "CONFIG_LV_TXT_ENC_UTF8=y", "LVGL UTF-8 symbol decoder")
require(sdk, "CONFIG_LV_TXT_ENC_ASCII=n", "LVGL ASCII decoder disabled")
require(sdk, "CONFIG_LV_FONT_MONTSERRAT_14=y", "LVGL official symbol-bearing font")

ui = (ROOT / "components/nearby_ui/nearby_ui.c").read_text()
require(ui, "lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);", "explicit top icon font")
for state in ("Disconnected", "Connecting", "Connected", "IPv4:"):
    require(ui, state, "Settings STA status")

radio = (ROOT / "firmware/components/radio_runtime/wifi_runtime.c").read_text()
require(radio, "init_cfg.nvs_enable = false;", "session-only Wi-Fi")
require(radio, "esp_wifi_set_storage(WIFI_STORAGE_RAM)", "RAM-only Wi-Fi config")

web_h = (ROOT / "firmware/components/web_mgmt/include/web_mgmt.h").read_text()
web_c = (ROOT / "firmware/components/web_mgmt/web_mgmt.c").read_text()
for state in ("NEARBY_WEB_STA_DISCONNECTED", "NEARBY_WEB_STA_CONNECTING", "NEARBY_WEB_STA_CONNECTED", "NEARBY_WEB_STA_FAILED"):
    require(web_h, state, "runtime STA state")
require(web_c, "IP_EVENT_STA_GOT_IP", "runtime STA success event")
require(web_c, "WIFI_EVENT_STA_DISCONNECTED", "runtime STA failure/disconnect event")
require(web_c, "WEB_STA_CONNECT_TIMEOUT_US", "runtime STA timeout")
require(web_c, '\\"sta_state\\":\\"%s\\"', "status API state field")
require(web_c, "nearby_db_upload_finish()", "server upload finalization")

main = (ROOT / "firmware/main/main.c").read_text()
require(main, "nearby_db_validate_release_file(part_path, &info)", "server release DB validation")
require(main, "nearby_ui_set_sta_status", "Settings runtime state wiring")

canonical = (ROOT / "web-portal/index.html").read_text()
embedded = (ROOT / "firmware/main/portal_index.html").read_text()
if canonical != embedded:
    raise AssertionError("canonical and embedded portal HTML diverged")
require(canonical, "function sha256Fallback(bytes)", "SoftAP SHA-256 fallback")
require(canonical, "async function sha256Digest(bytes)", "SHA compatibility wrapper")
require(canonical, "await waitForStaResult()", "final Wi-Fi result polling")
if "crypto.subtle.digest('SHA-256', payload)" in canonical:
    raise AssertionError("unguarded SubtleCrypto payload digest remains")
if "Connection started. Check the device/session status" in canonical:
    raise AssertionError("one-shot Wi-Fi result message remains")

start = canonical.index("  function sha256Fallback(bytes)")
end = canonical.index("  async function sha256Digest(bytes)", start)
fallback = canonical[start:end]
js = fallback + r"""
const got = [...sha256Fallback(new Uint8Array([97,98,99]))]
  .map(v => v.toString(16).padStart(2, '0')).join('');
if (got !== 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad') {
  throw new Error(`SHA-256 fallback mismatch: ${got}`);
}
"""
subprocess.run(["node", "-e", js], check=True)
print("beta.3 QA regression guards: PASS")
