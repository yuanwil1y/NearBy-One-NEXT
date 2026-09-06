from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1] if Path(__file__).resolve().parent.name == 'tools' else Path.cwd()

def path(rel):
    return ROOT / rel

def replace_once(rel, old, new):
    p = path(rel)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{rel}: expected one match, found {count}: {old[:100]!r}")
    p.write_text(text.replace(old, new, 1))

ui = "components/nearby_ui/nearby_ui.c"
replace_once(ui,
    "    lv_obj_t *db_value_label;\n    lv_obj_t *fw_value_label;\n\n    char selected_device_id[HA_ID_LEN];",
    "    lv_obj_t *db_value_label;\n    lv_obj_t *fw_value_label;\n    lv_obj_t *sta_value_label;\n\n    char selected_device_id[HA_ID_LEN];")
replace_once(ui,
    "    char db_status[48];\n    char firmware[32];\n    bool portal_running;",
    "    char db_status[48];\n    char firmware[32];\n    nearby_ui_sta_state_t sta_state;\n    char sta_ipv4[16];\n    bool portal_running;")
replace_once(ui,
    "    g.db_value_label = NULL;\n    g.fw_value_label = NULL;\n    NB_OBJ_DELETE_ASYNC(screen);",
    "    g.db_value_label = NULL;\n    g.fw_value_label = NULL;\n    g.sta_value_label = NULL;\n    NB_OBJ_DELETE_ASYNC(screen);")
replace_once(ui,
    "    lv_obj_t *label = lv_label_create(button);\n    lv_label_set_text(label, symbol);\n    lv_obj_set_style_text_color(label, lv_color_white(), 0);",
    "    lv_obj_t *label = lv_label_create(button);\n    lv_label_set_text(label, symbol);\n    /* LVGL's built-in Montserrat 14 contains the official LV_SYMBOL glyphs. */\n    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);\n    lv_obj_set_style_text_color(label, lv_color_white(), 0);")

sta_render = r'''static void render_sta_value(void)
{
    if (!g.sta_value_label) return;

    char value[48];
    switch (g.sta_state) {
    case NEARBY_UI_STA_CONNECTING:
        safe_copy(value, sizeof(value), "Connecting");
        break;
    case NEARBY_UI_STA_CONNECTED:
        if (g.sta_ipv4[0]) {
            (void)snprintf(value, sizeof(value), "Connected\nIPv4: %s", g.sta_ipv4);
        } else {
            safe_copy(value, sizeof(value), "Connected");
        }
        break;
    case NEARBY_UI_STA_FAILED:
        safe_copy(value, sizeof(value), "Disconnected (failed)");
        break;
    case NEARBY_UI_STA_DISCONNECTED:
    default:
        safe_copy(value, sizeof(value), "Disconnected");
        break;
    }

    lv_label_set_text(g.sta_value_label, value);
    lv_obj_set_style_text_color(g.sta_value_label,
                                g.sta_state == NEARBY_UI_STA_CONNECTED
                                    ? lv_color_hex(NB_BLUE)
                                    : lv_color_hex(NB_MUTED),
                                0);
}

'''
replace_once(ui, "static void show_settings(void)\n{", sta_render + "static void show_settings(void)\n{")
replace_once(ui,
    "    lv_obj_add_event_cb(portal, portal_start_clicked, LV_EVENT_CLICKED, NULL);\n\n    lv_obj_t *db_card = make_card(content, 58);",
    "    lv_obj_add_event_cb(portal, portal_start_clicked, LV_EVENT_CLICKED, NULL);\n\n    lv_obj_t *sta_card = make_card(content, 68);\n    lv_obj_t *sta_title = lv_label_create(sta_card);\n    lv_label_set_text(sta_title, \"Wi-Fi STA\");\n    g.sta_value_label = lv_label_create(sta_card);\n    lv_obj_set_width(g.sta_value_label, 136);\n    lv_label_set_long_mode(g.sta_value_label, LV_LABEL_LONG_WRAP);\n    lv_obj_align(g.sta_value_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);\n    render_sta_value();\n\n    lv_obj_t *db_card = make_card(content, 58);")
replace_once(ui,
    "    safe_copy(g.db_status, sizeof(g.db_status), \"Missing\");\n    safe_copy(g.firmware, sizeof(g.firmware), \"unknown\");\n    create_home();",
    "    safe_copy(g.db_status, sizeof(g.db_status), \"Missing\");\n    safe_copy(g.firmware, sizeof(g.firmware), \"unknown\");\n    g.sta_state = NEARBY_UI_STA_DISCONNECTED;\n    create_home();")
replace_once(ui,
    "void nearby_ui_set_portal_info(bool running, const char *ssid, const char *address, const char *pin)\n{\n    g.portal_running = running;\n    safe_copy(g.portal_ssid, sizeof(g.portal_ssid), ssid);\n    safe_copy(g.portal_address, sizeof(g.portal_address), address);\n    safe_copy(g.portal_pin, sizeof(g.portal_pin), pin);\n    if (g.portal_modal) render_portal_modal();\n}\n",
    "void nearby_ui_set_portal_info(bool running, const char *ssid, const char *address, const char *pin)\n{\n    g.portal_running = running;\n    safe_copy(g.portal_ssid, sizeof(g.portal_ssid), ssid);\n    safe_copy(g.portal_address, sizeof(g.portal_address), address);\n    safe_copy(g.portal_pin, sizeof(g.portal_pin), pin);\n    if (g.portal_modal) render_portal_modal();\n}\n\nvoid nearby_ui_set_sta_status(nearby_ui_sta_state_t state, const char *ipv4)\n{\n    g.sta_state = state;\n    safe_copy(g.sta_ipv4, sizeof(g.sta_ipv4), ipv4);\n    render_sta_value();\n}\n")

web_h = "firmware/components/web_mgmt/include/web_mgmt.h"
replace_once(web_h,
    "typedef struct {\n    bool active;",
    "typedef enum {\n    NEARBY_WEB_STA_DISCONNECTED = 0,\n    NEARBY_WEB_STA_CONNECTING,\n    NEARBY_WEB_STA_CONNECTED,\n    NEARBY_WEB_STA_FAILED,\n} nearby_web_mgmt_sta_state_t;\n\ntypedef struct {\n    bool active;")
replace_once(web_h,
    "    char sta_ipv4[16];\n    bool sta_connected;\n    bool db_format_authorized;",
    "    char sta_ipv4[16];\n    bool sta_connected;\n    nearby_web_mgmt_sta_state_t sta_state;\n    bool db_format_authorized;")

web_c = "firmware/components/web_mgmt/web_mgmt.c"
replace_once(web_c,
    '#include "esp_netif_ip_addr.h"\n#include "esp_random.h"',
    '#include "esp_event.h"\n#include "esp_netif_ip_addr.h"\n#include "esp_random.h"\n#include "esp_timer.h"')
replace_once(web_c,
    "#define WEB_UPLOAD_CHUNK_BYTES 2048\n",
    "#define WEB_UPLOAD_CHUNK_BYTES 2048\n#define WEB_STA_CONNECT_TIMEOUT_US (20LL * 1000LL * 1000LL)\n")
replace_once(web_c,
    "static wifi_ap_record_t s_scan_records[WEB_SCAN_MAX_APS];\n",
    "static wifi_ap_record_t s_scan_records[WEB_SCAN_MAX_APS];\nstatic volatile nearby_web_mgmt_sta_state_t s_sta_state = NEARBY_WEB_STA_DISCONNECTED;\nstatic int64_t s_sta_connect_started_us;\nstatic esp_event_handler_instance_t s_sta_disconnect_handler;\nstatic esp_event_handler_instance_t s_sta_got_ip_handler;\nstatic bool s_sta_handlers_registered;\nstatic volatile bool s_ignore_disconnect_once;\n")
replace_once(web_c,
    "    if (esp_netif_get_ip_info(netif, &info) == ESP_OK) {\n        snprintf(out, 16, IPSTR, IP2STR(&info.ip));\n    }",
    "    if (esp_netif_get_ip_info(netif, &info) == ESP_OK && info.ip.addr != 0) {\n        snprintf(out, 16, IPSTR, IP2STR(&info.ip));\n    }")

event_helpers = r'''static const char *sta_state_name(nearby_web_mgmt_sta_state_t state)
{
    switch (state) {
    case NEARBY_WEB_STA_CONNECTING: return "connecting";
    case NEARBY_WEB_STA_CONNECTED: return "connected";
    case NEARBY_WEB_STA_FAILED: return "failed";
    case NEARBY_WEB_STA_DISCONNECTED:
    default: return "disconnected";
    }
}

static void sta_event_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_sta_state = NEARBY_WEB_STA_CONNECTED;
        return;
    }

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_ignore_disconnect_once) {
            s_ignore_disconnect_once = false;
            return;
        }
        if (s_sta_state == NEARBY_WEB_STA_CONNECTING) {
            s_sta_state = NEARBY_WEB_STA_FAILED;
        } else if (s_sta_state == NEARBY_WEB_STA_CONNECTED) {
            s_sta_state = NEARBY_WEB_STA_DISCONNECTED;
        }
    }
}

static esp_err_t register_sta_handlers(void)
{
    if (s_sta_handlers_registered) return ESP_OK;

    esp_err_t err = esp_event_handler_instance_register(WIFI_EVENT,
                                                         WIFI_EVENT_STA_DISCONNECTED,
                                                         sta_event_handler,
                                                         NULL,
                                                         &s_sta_disconnect_handler);
    if (err != ESP_OK) return err;

    err = esp_event_handler_instance_register(IP_EVENT,
                                              IP_EVENT_STA_GOT_IP,
                                              sta_event_handler,
                                              NULL,
                                              &s_sta_got_ip_handler);
    if (err != ESP_OK) {
        (void)esp_event_handler_instance_unregister(WIFI_EVENT,
                                                    WIFI_EVENT_STA_DISCONNECTED,
                                                    s_sta_disconnect_handler);
        return err;
    }

    s_sta_handlers_registered = true;
    return ESP_OK;
}

static void unregister_sta_handlers(void)
{
    if (!s_sta_handlers_registered) return;

    (void)esp_event_handler_instance_unregister(WIFI_EVENT,
                                                WIFI_EVENT_STA_DISCONNECTED,
                                                s_sta_disconnect_handler);
    (void)esp_event_handler_instance_unregister(IP_EVENT,
                                                IP_EVENT_STA_GOT_IP,
                                                s_sta_got_ip_handler);
    s_sta_handlers_registered = false;
}

'''
replace_once(web_c, "static esp_err_t status_handler(httpd_req_t *req)\n{", event_helpers + "static esp_err_t status_handler(httpd_req_t *req)\n{")
replace_once(web_c,
    '    char response[320];\n    snprintf(response, sizeof(response),\n             "{\\"active\\":%s,\\"ssid\\":\\"%s\\",\\"ap_ipv4\\":\\"%s\\","\n             "\\"sta_connected\\":%s,\\"sta_ipv4\\":\\"%s\\","\n             "\\"db_format_authorized\\":%s}",\n             status.active ? "true" : "false",\n             status.ssid,\n             status.ap_ipv4,\n             status.sta_connected ? "true" : "false",\n             status.sta_ipv4,\n             status.db_format_authorized ? "true" : "false");',
    '    char response[384];\n    snprintf(response, sizeof(response),\n             "{\\"active\\":%s,\\"ssid\\":\\"%s\\",\\"ap_ipv4\\":\\"%s\\","\n             "\\"sta_connected\\":%s,\\"sta_state\\":\\"%s\\",\\"sta_ipv4\\":\\"%s\\","\n             "\\"db_format_authorized\\":%s}",\n             status.active ? "true" : "false",\n             status.ssid,\n             status.ap_ipv4,\n             status.sta_connected ? "true" : "false",\n             sta_state_name(status.sta_state),\n             status.sta_ipv4,\n             status.db_format_authorized ? "true" : "false");')
replace_once(web_c,
    "    (void)nearby_wifi_sta_disconnect();\n    err = nearby_wifi_set_sta_config(&config);\n    if (err == ESP_OK) err = nearby_wifi_sta_connect();\n    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, \"connect start failed\");\n\n    return send_status_plain(req, \"202 Accepted\", \"connecting\");",
    "    wifi_ap_record_t current_ap;\n    if (esp_wifi_sta_get_ap_info(&current_ap) == ESP_OK) {\n        s_ignore_disconnect_once = true;\n        s_sta_state = NEARBY_WEB_STA_DISCONNECTED;\n        (void)nearby_wifi_sta_disconnect();\n    }\n\n    err = nearby_wifi_set_sta_config(&config);\n    if (err != ESP_OK) {\n        s_sta_state = NEARBY_WEB_STA_FAILED;\n        s_sta_connect_started_us = 0;\n        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, \"connect start failed\");\n    }\n\n    s_sta_state = NEARBY_WEB_STA_CONNECTING;\n    s_sta_connect_started_us = esp_timer_get_time();\n    err = nearby_wifi_sta_connect();\n    if (err != ESP_OK) {\n        s_sta_state = NEARBY_WEB_STA_FAILED;\n        s_sta_connect_started_us = 0;\n        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, \"connect start failed\");\n    }\n\n    return send_status_plain(req, \"202 Accepted\", \"connecting\");")
replace_once(web_c,
    "    err = nearby_wifi_driver_init();\n    if (err != ESP_OK) goto fail_gate;\n\n    s_ap_netif = esp_netif_create_default_wifi_ap();",
    "    err = nearby_wifi_driver_init();\n    if (err != ESP_OK) goto fail_gate;\n    s_sta_state = NEARBY_WEB_STA_DISCONNECTED;\n    s_sta_connect_started_us = 0;\n    s_ignore_disconnect_once = false;\n    err = register_sta_handlers();\n    if (err != ESP_OK) goto fail_wifi;\n\n    s_ap_netif = esp_netif_create_default_wifi_ap();")
replace_once(web_c,
    "fail_wifi:\n    (void)nearby_wifi_driver_deinit();",
    "fail_wifi:\n    unregister_sta_handlers();\n    (void)nearby_wifi_driver_deinit();")
replace_once(web_c,
    "    (void)nearby_db_storage_set_preflight_safe_to_format(false);\n    (void)nearby_wifi_sta_disconnect();\n\n    if (s_ap_netif != NULL) {",
    "    (void)nearby_db_storage_set_preflight_safe_to_format(false);\n    if (s_sta_state == NEARBY_WEB_STA_CONNECTED) s_ignore_disconnect_once = true;\n    (void)nearby_wifi_sta_disconnect();\n    unregister_sta_handlers();\n    s_sta_state = NEARBY_WEB_STA_DISCONNECTED;\n    s_sta_connect_started_us = 0;\n    s_ignore_disconnect_once = false;\n\n    if (s_ap_netif != NULL) {")
replace_once(web_c,
    "    status.active = s_server != NULL;\n    format_ipv4(s_ap_netif, status.ap_ipv4);\n    format_ipv4(nearby_wifi_sta_netif(), status.sta_ipv4);\n    wifi_ap_record_t ap_info;\n    status.sta_connected = esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK;\n    status.db_format_authorized = nearby_db_storage_preflight_is_authorized();\n    *out_status = status;",
    "    status.active = s_server != NULL;\n    format_ipv4(s_ap_netif, status.ap_ipv4);\n    format_ipv4(nearby_wifi_sta_netif(), status.sta_ipv4);\n    status.db_format_authorized = nearby_db_storage_preflight_is_authorized();\n\n    if (!status.active) {\n        status.sta_ipv4[0] = '\\0';\n        status.sta_state = NEARBY_WEB_STA_DISCONNECTED;\n    } else if (status.sta_ipv4[0] != '\\0') {\n        s_sta_state = NEARBY_WEB_STA_CONNECTED;\n        status.sta_state = NEARBY_WEB_STA_CONNECTED;\n    } else if (s_sta_state == NEARBY_WEB_STA_CONNECTING &&\n               s_sta_connect_started_us > 0 &&\n               esp_timer_get_time() - s_sta_connect_started_us >= WEB_STA_CONNECT_TIMEOUT_US) {\n        s_sta_state = NEARBY_WEB_STA_FAILED;\n        status.sta_state = NEARBY_WEB_STA_FAILED;\n    } else {\n        status.sta_state = s_sta_state;\n    }\n    status.sta_connected = status.sta_state == NEARBY_WEB_STA_CONNECTED;\n    *out_status = status;")

main = "firmware/main/main.c"
sta_main = r'''static void update_sta_status(void)
{
    nearby_web_mgmt_status_t status;
    if (nearby_web_mgmt_get_status(&status) != ESP_OK) {
        nearby_ui_set_sta_status(NEARBY_UI_STA_DISCONNECTED, NULL);
        return;
    }

    nearby_ui_sta_state_t ui_state = NEARBY_UI_STA_DISCONNECTED;
    switch (status.sta_state) {
    case NEARBY_WEB_STA_CONNECTING:
        ui_state = NEARBY_UI_STA_CONNECTING;
        break;
    case NEARBY_WEB_STA_CONNECTED:
        ui_state = NEARBY_UI_STA_CONNECTED;
        break;
    case NEARBY_WEB_STA_FAILED:
        ui_state = NEARBY_UI_STA_FAILED;
        break;
    case NEARBY_WEB_STA_DISCONNECTED:
    default:
        break;
    }
    nearby_ui_set_sta_status(ui_state, status.sta_ipv4);
}

'''
replace_once(main, "static bool queue_owner_command(owner_command_type_t type,", sta_main + "static bool queue_owner_command(owner_command_type_t type,")
replace_once(main,
    "        nearby_ui_set_portal_info(true,\n                                  status.ssid,\n                                  status.ap_ipv4[0] ? status.ap_ipv4 : \"192.168.4.1\",\n                                  status.password);\n    }\n}",
    "        nearby_ui_set_portal_info(true,\n                                  status.ssid,\n                                  status.ap_ipv4[0] ? status.ap_ipv4 : \"192.168.4.1\",\n                                  status.password);\n    }\n    update_sta_status();\n}")
replace_once(main,
    "    nearby_ui_set_portal_info(false, NULL, NULL, NULL);\n    update_db_status();\n}",
    "    nearby_ui_set_portal_info(false, NULL, NULL, NULL);\n    update_sta_status();\n    update_db_status();\n}")
replace_once(main,
    "    update_db_status();\n    ESP_LOGI(TAG, \"Product owner ready: boot is empty + idle; waiting for explicit Scan\");\n\n    for (;;) {",
    "    update_db_status();\n    update_sta_status();\n    ESP_LOGI(TAG, \"Product owner ready: boot is empty + idle; waiting for explicit Scan\");\n\n    int64_t next_sta_refresh_us = 0;\n    for (;;) {")
replace_once(main,
    "        /* LVGL callbacks execute here, on the same task that owns all HA access. */\n        (void)lv_timer_handler();",
    "        const int64_t now_us = esp_timer_get_time();\n        if (now_us >= next_sta_refresh_us) {\n            update_sta_status();\n            next_sta_refresh_us = now_us + 500000;\n        }\n\n        /* LVGL callbacks execute here, on the same task that owns all HA access. */\n        (void)lv_timer_handler();")

sha_helpers = r'''  function sha256Fallback(bytes) {
    const K = new Uint32Array([
      0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
      0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
      0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
      0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
      0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
      0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
      0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
      0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    ]);
    const rotr = (value, bits) => (value >>> bits) | (value << (32 - bits));
    const bitLength = bytes.length * 8;
    const paddedLength = Math.ceil((bytes.length + 9) / 64) * 64;
    const padded = new Uint8Array(paddedLength);
    padded.set(bytes);
    padded[bytes.length] = 0x80;
    const paddedView = new DataView(padded.buffer);
    paddedView.setUint32(paddedLength - 8, Math.floor(bitLength / 0x100000000), false);
    paddedView.setUint32(paddedLength - 4, bitLength >>> 0, false);

    const h = new Uint32Array([0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19]);
    const w = new Uint32Array(64);
    for (let offset = 0; offset < padded.length; offset += 64) {
      for (let i = 0; i < 16; ++i) w[i] = paddedView.getUint32(offset + i * 4, false);
      for (let i = 16; i < 64; ++i) {
        const s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >>> 3);
        const s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >>> 10);
        w[i] = (w[i - 16] + s0 + w[i - 7] + s1) >>> 0;
      }
      let [a,b,c,d,e,f,g,hh] = h;
      for (let i = 0; i < 64; ++i) {
        const s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const ch = (e & f) ^ (~e & g);
        const t1 = (hh + s1 + ch + K[i] + w[i]) >>> 0;
        const s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const maj = (a & b) ^ (a & c) ^ (b & c);
        const t2 = (s0 + maj) >>> 0;
        hh = g; g = f; f = e; e = (d + t1) >>> 0;
        d = c; c = b; b = a; a = (t1 + t2) >>> 0;
      }
      h[0] = (h[0] + a) >>> 0; h[1] = (h[1] + b) >>> 0; h[2] = (h[2] + c) >>> 0; h[3] = (h[3] + d) >>> 0;
      h[4] = (h[4] + e) >>> 0; h[5] = (h[5] + f) >>> 0; h[6] = (h[6] + g) >>> 0; h[7] = (h[7] + hh) >>> 0;
    }
    const out = new Uint8Array(32);
    const outView = new DataView(out.buffer);
    h.forEach((value, index) => outView.setUint32(index * 4, value, false));
    return out;
  }

  async function sha256Digest(bytes) {
    const subtle = window.crypto && window.crypto.subtle;
    if (subtle && typeof subtle.digest === 'function') {
      try {
        return new Uint8Array(await subtle.digest('SHA-256', bytes));
      } catch (_) {
        // SoftAP is commonly served from http://192.168.4.1 (non-secure context).
      }
    }
    return sha256Fallback(bytes);
  }

'''

wifi_helpers = r'''  function renderStaStatus(status) {
    const state = status.sta_state || (status.sta_connected ? 'connected' : 'disconnected');
    if (state === 'connected') {
      setStatus(wifiStatus, `STA: Connected${status.sta_ipv4 ? ' · IPv4 ' + status.sta_ipv4 : ''}`, 'ok');
    } else if (state === 'connecting') {
      setStatus(wifiStatus, 'STA: Connecting…');
    } else if (state === 'failed') {
      setStatus(wifiStatus, 'STA: Connection failed.', 'error');
    } else {
      setStatus(wifiStatus, 'STA: Disconnected.');
    }
    return state;
  }

  async function refreshStaStatus() {
    const response = await fetch('/api/status', {cache:'no-store'});
    if (!response.ok) throw new Error(await response.text());
    const status = await response.json();
    renderStaStatus(status);
    return status;
  }

  async function waitForStaResult() {
    for (let attempt = 0; attempt < 25; ++attempt) {
      await new Promise(resolve => setTimeout(resolve, 1000));
      const status = await refreshStaStatus();
      if (status.sta_state === 'connected' || status.sta_state === 'failed') return status;
    }
    setStatus(wifiStatus, 'STA: Connection failed (timeout).', 'error');
    return null;
  }

'''

connect_old = r'''  document.querySelector('#connectButton').addEventListener('click', async () => {
    const ssid = ssidInput.value.trim();
    if (!ssid) return setStatus(wifiStatus, 'Enter or choose an SSID.', 'error');
    setStatus(wifiStatus, 'Starting session-only STA connection…');
    try {
      const body = new URLSearchParams({ssid, password:passwordInput.value});
      const response = await fetch('/api/wifi/connect', {
        method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body
      });
      if (!response.ok) throw new Error(await response.text());
      setStatus(wifiStatus, 'Connection started. Check the device/session status as the STA associates.', 'ok');
    } catch (error) {
      setStatus(wifiStatus, `Connection failed: ${error.message}`, 'error');
    }
  });'''

connect_new = r'''  document.querySelector('#connectButton').addEventListener('click', async () => {
    const ssid = ssidInput.value.trim();
    if (!ssid) return setStatus(wifiStatus, 'Enter or choose an SSID.', 'error');
    setStatus(wifiStatus, 'STA: Connecting…');
    try {
      const body = new URLSearchParams({ssid, password:passwordInput.value});
      const response = await fetch('/api/wifi/connect', {
        method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body
      });
      if (!response.ok) throw new Error(await response.text());
      await waitForStaResult();
    } catch (error) {
      setStatus(wifiStatus, `STA: Connection failed: ${error.message}`, 'error');
    }
  });
  refreshStaStatus().catch(error => setStatus(wifiStatus, `STA status unavailable: ${error.message}`, 'error'));'''

for portal in ("web-portal/index.html", "firmware/main/portal_index.html"):
    replace_once(portal, "  async function scanNetworks() {", wifi_helpers + "  async function scanNetworks() {")
    replace_once(portal, connect_old, connect_new)
    replace_once(portal, "  async function clearServerAuthorization() {", sha_helpers + "  async function clearServerAuthorization() {")
    replace_once(portal, "    const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', payload));", "    const digest = await sha256Digest(payload);")

test_text = r'''from pathlib import Path
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
'''
path("tests/integration/test_beta3_qa_regressions.py").write_text(test_text)

qa = ".github/workflows/qa-beta1-bench-fix.yml"
replace_once(qa,
    "      - name: Guard the three reported bench fixes\n        run: python tests/integration/test_beta1_bench_regressions.py\n",
    "      - name: Guard the three reported bench fixes\n        run: python tests/integration/test_beta1_bench_regressions.py\n      - name: Guard beta.3 QA fixes\n        run: python tests/integration/test_beta3_qa_regressions.py\n")
replace_once(qa,
    "            idf.py build &&\n            grep -q \"lv_font_montserrat_14\" build/nearby_one_next.map &&",
    "            idf.py build &&\n            grep -q '^CONFIG_LV_TXT_ENC_UTF8=y$' sdkconfig &&\n            grep -q \"lv_font_montserrat_14\" build/nearby_one_next.map &&")

release = ".github/workflows/beta-release.yml"
release_text = path(release).read_text()
release_text = release_text.replace("name: NearBy One NEXT beta.2 release", "name: NearBy One NEXT beta.3 release", 1)
release_text = release_text.replace("on:\n  push:\n    branches:\n      - qa/beta-0.1-bench-bugs\n  workflow_dispatch:\n", "on:\n  workflow_dispatch:\n", 1)
release_text = release_text.replace("    if: github.ref == 'refs/heads/qa/beta-0.1-bench-bugs'\n", "", 1)
release_text = release_text.replace("Publish v0.1.0-beta.2 prerelease", "Publish v0.1.0-beta.3 prerelease", 1)
release_text = release_text.replace("gh release create v0.1.0-beta.2", "gh release create v0.1.0-beta.3", 1)
release_text = release_text.replace('--title "NearBy One NEXT v0.1.0-beta.2"', '--title "NearBy One NEXT v0.1.0-beta.3"', 1)
notes = r'''          Third public beta for Waveshare ESP32-C6-Touch-LCD-1.9.

          QA fixes in this beta:
          - Top Scan / Back / Settings LVGL symbols: fix the real UTF-8 decoder + symbol-font path. `LV_SYMBOL_*` is decoded as UTF-8 and rendered with LVGL's built-in Montserrat 14 symbol glyphs; no additional font asset is introduced.
          - `.nbdb` import over the real SoftAP origin (`http://192.168.4.1`): client preflight now uses WebCrypto when available and a local SHA-256 fallback when `crypto.subtle` is unavailable in a non-secure context. Server-side release validation remains mandatory before promotion.
          - Wi-Fi STA runtime status: Settings and Web Management now expose Disconnected / Connecting / Connected, show the current STA IPv4 when connected, and surface a final failed state instead of stopping at “Connection started”. Credentials remain RAM-only/session-only.

          Regression scope retained:
          - HA blue rendering.
          - Complete SoftAP credentials display.
          - Portal start/stop.
          - Scan lifecycle.

          Release files:
          - `NearBy-One-NEXT-ESP32C6.bin` — merged ESP32-C6 image; flash from address `0x0`.
          - `NearBy-One-NEXT.nbdb` — recognition database; install through Web Management → Database.

          Verification status:
          - Host/integration regression tests and ESP-IDF v6.1 `esp32c6` builds are required before this prerelease is dispatched.
          - Physical-board acceptance for all three fixes remains `RETEST_REQUIRED`.
          - This prerelease does NOT claim physical-device PASS until the Waveshare board is retested.
'''
release_text, count = re.subn(r"          Second public beta for Waveshare ESP32-C6-Touch-LCD-1\.9\.\n.*?(?=          EOF\n)", lambda _: notes, release_text, count=1, flags=re.S)
if count != 1:
    raise SystemExit("beta-release.yml: release notes block not found")
path(release).write_text(release_text)

print("beta.3 scoped patch applied")
