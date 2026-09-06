from pathlib import Path
import re


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text()
    n = text.count(old)
    if n != 1:
        raise RuntimeError(f"{path}: expected 1 match, got {n}: {old[:60]!r}")
    p.write_text(text.replace(old, new, 1))


def replace_all_exact(path, old, new, expected):
    p = Path(path)
    text = p.read_text()
    n = text.count(old)
    if n != expected:
        raise RuntimeError(f"{path}: expected {expected} matches, got {n}: {old!r}")
    p.write_text(text.replace(old, new))


ui = "components/nearby_ui/nearby_ui.c"
replace_once(ui, "    char portal_address[32];\n    char portal_pin[24];\n", "    char portal_address[32];\n")
replace_once(ui, 'device->manufacturer[0] && device->model[0] ? " · " : "",',
             'device->manufacturer[0] && device->model[0] ? " - " : "",')
replace_once(ui,
'''        (void)snprintf(text, sizeof(text), "Connect to:\\n%s\\n\\nOpen:\\n%s%s%s",
                       g.portal_ssid[0] ? g.portal_ssid : "NearBy-One",
                       g.portal_address[0] ? g.portal_address : "192.168.4.1",
                       g.portal_pin[0] ? "\\n\\nPassword: " : "",
                       g.portal_pin);
    } else {
        (void)snprintf(text, sizeof(text), "Starting Web Management…");''',
'''        (void)snprintf(text, sizeof(text), "Connect to:\\n%s\\n\\nOpen:\\n%s",
                       g.portal_ssid[0] ? g.portal_ssid : "NearBy-One",
                       g.portal_address[0] ? g.portal_address : "192.168.4.1");
    } else {
        (void)snprintf(text, sizeof(text), "Starting Web Management...");''')
replace_once(ui,
'''void nearby_ui_set_portal_info(bool running, const char *ssid, const char *address, const char *pin)
{
    g.portal_running = running;
    safe_copy(g.portal_ssid, sizeof(g.portal_ssid), ssid);
    safe_copy(g.portal_address, sizeof(g.portal_address), address);
    safe_copy(g.portal_pin, sizeof(g.portal_pin), pin);
    if (g.portal_modal) render_portal_modal();
}''',
'''void nearby_ui_set_portal_info(bool running, const char *ssid, const char *address)
{
    g.portal_running = running;
    safe_copy(g.portal_ssid, sizeof(g.portal_ssid), ssid);
    safe_copy(g.portal_address, sizeof(g.portal_address), address);
    if (g.portal_modal) render_portal_modal();
}''')

ui_h = "components/nearby_ui/include/nearby_ui.h"
replace_once(ui_h,
'''void nearby_ui_set_portal_info(bool running,
                               const char *ssid,
                               const char *address,
                               const char *pin);''',
'''void nearby_ui_set_portal_info(bool running,
                               const char *ssid,
                               const char *address);''')

main = "firmware/main/main.c"
replace_once(main, '"Ready · v%" PRIu32', '"Ready - v%" PRIu32')
replace_all_exact(main, 'nearby_ui_set_portal_info(false, NULL, NULL, NULL);',
                  'nearby_ui_set_portal_info(false, NULL, NULL);', 4)
replace_once(main,
'''        nearby_ui_set_portal_info(true,
                                  status.ssid,
                                  status.ap_ipv4[0] ? status.ap_ipv4 : "192.168.4.1",
                                  status.password);''',
'''        nearby_ui_set_portal_info(true,
                                  status.ssid,
                                  status.ap_ipv4[0] ? status.ap_ipv4 : "192.168.4.1");''')
replace_once(main,
'''    update_sta_status();
    update_db_status();
}''',
'''    update_sta_status();
    if (!nearby_db_storage_format_is_active()) update_db_status();
}''')
replace_once(main,
'''        if (now_us >= next_sta_refresh_us) {
            update_sta_status();
            next_sta_refresh_us = now_us + 500000;
        }''',
'''        if (now_us >= next_sta_refresh_us) {
            esp_err_t maintenance_err = nearby_web_mgmt_maintenance();
            if (maintenance_err != ESP_OK && maintenance_err != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG, "Web Management maintenance failed: %s", esp_err_to_name(maintenance_err));
            }
            update_sta_status();
            next_sta_refresh_us = now_us + 500000;
        }''')

wait_function = r'''
  async function waitForFormatResult() {
    for (let attempt = 0; attempt < 140; ++attempt) {
      await new Promise(resolve => setTimeout(resolve, 500));
      const response = await fetch('/api/status', {cache:'no-store'});
      if (!response.ok) throw new Error(`Format status failed: ${await response.text()}`);
      const status = await response.json();
      const stage = status.db_format_stage || 'unknown';
      if (status.db_format_state === 'ready') return status;
      if (status.db_format_state === 'failed') {
        throw new Error(`Format failed at ${stage} (error ${status.db_format_error}).`);
      }
      setStatus(dbStatus, `Formatting the entire SD card (${stage})...`);
    }
    throw new Error('Format failed: device did not finish before the client deadline.');
  }
'''

for html in ["web-portal/index.html", "firmware/main/portal_index.html"]:
    replace_once(html,
'''  confirmReplace.addEventListener('change', updateImportReady);

  importButton.addEventListener('click', async () => {''',
'''  confirmReplace.addEventListener('change', updateImportReady);
''' + wait_function + '''
  importButton.addEventListener('click', async () => {''')
    replace_once(html,
'''      let response = await fetch('/api/db/format?confirm=ERASE', {method:'POST'});
      if (!response.ok) throw new Error(`Format failed: ${await response.text()}`);

      dbProgress.value = 60;''',
'''      let response = await fetch('/api/db/format?confirm=ERASE', {method:'POST'});
      if (!response.ok) throw new Error(`Format failed: ${await response.text()}`);
      await waitForFormatResult();

      dbProgress.value = 60;''')
