#include "radio_runtime.h"
#include "radio_runtime_internal.h"

#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "native_handoff.h"
#include "scan_session.h"

static bool s_wifi_initialized;
static bool s_wifi_started;
static bool s_wifi_promiscuous;
static esp_netif_t *s_sta_netif;

static esp_err_t require_radio_access(void)
{
    if (scan_session_require_scan_owner() == ESP_OK ||
        scan_session_require_competing_active() == ESP_OK) {
        return ESP_OK;
    }
    return ESP_ERR_INVALID_STATE;
}

static void destroy_sta_netif(void)
{
    if (s_sta_netif != NULL) {
        esp_netif_destroy_default_wifi(s_sta_netif);
        s_sta_netif = NULL;
    }
}

static void release_wifi_phase_if_owned(void)
{
    if (nearby_radio_phase_current() == NEARBY_RADIO_PHASE_WIFI) {
        (void)nearby_radio_phase_release(NEARBY_RADIO_PHASE_WIFI);
    }
}

esp_err_t nearby_wifi_driver_init(void)
{
    if (require_radio_access() != ESP_OK) return ESP_ERR_INVALID_STATE;
    if (s_wifi_started) return ESP_OK;

    esp_err_t err = nearby_radio_phase_claim(NEARBY_RADIO_PHASE_WIFI);
    if (err != ESP_OK) return err;

    err = nearby_wifi_handoff_init();
    if (err != ESP_OK) goto fail_phase;
    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) goto fail_phase;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) goto fail_phase;

    if (s_sta_netif == NULL) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
        if (s_sta_netif == NULL) {
            err = ESP_ERR_NO_MEM;
            goto fail_phase;
        }
    }

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    init_cfg.nvs_enable = false;
    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK) {
        destroy_sta_netif();
        goto fail_phase;
    }
    s_wifi_initialized = true;

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) goto fail_driver;
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) goto fail_driver;
    err = esp_wifi_start();
    if (err != ESP_OK) goto fail_driver;

    s_wifi_started = true;
    return ESP_OK;

fail_driver:
    (void)esp_wifi_deinit();
    s_wifi_initialized = false;
    destroy_sta_netif();
fail_phase:
    release_wifi_phase_if_owned();
    return err;
}

static esp_err_t perform_active_scan(wifi_ap_record_t *records,
                                     uint16_t *inout_count,
                                     bool show_hidden)
{
    if (records == NULL || inout_count == NULL || *inout_count == 0) return ESP_ERR_INVALID_ARG;
    esp_err_t err = nearby_wifi_driver_init();
    if (err != ESP_OK) return err;
    if (s_wifi_promiscuous) return ESP_ERR_INVALID_STATE;

    wifi_scan_config_t scan_cfg = {
        .show_hidden = show_hidden,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) return err;
    return esp_wifi_scan_get_ap_records(inout_count, records);
}

esp_err_t nearby_wifi_active_scan(wifi_ap_record_t *records,
                                  uint16_t *inout_count,
                                  bool show_hidden)
{
    if (scan_session_require_scan_owner() != ESP_OK) return ESP_ERR_INVALID_STATE;
    return perform_active_scan(records, inout_count, show_hidden);
}

esp_err_t nearby_wifi_portal_scan(wifi_ap_record_t *records,
                                  uint16_t *inout_count,
                                  bool show_hidden)
{
    if (scan_session_require_competing_active() != ESP_OK) return ESP_ERR_INVALID_STATE;
    return perform_active_scan(records, inout_count, show_hidden);
}

esp_err_t nearby_wifi_promiscuous_start(uint8_t channel, uint32_t filter_mask)
{
    if (scan_session_require_scan_owner() != ESP_OK) return ESP_ERR_INVALID_STATE;
    if (channel < 1 || channel > 14) return ESP_ERR_INVALID_ARG;

    esp_err_t err = nearby_wifi_driver_init();
    if (err != ESP_OK) return err;
    if (s_wifi_promiscuous) return ESP_ERR_INVALID_STATE;

    wifi_promiscuous_filter_t filter = {
        .filter_mask = filter_mask != 0 ? filter_mask :
            (WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_CTRL | WIFI_PROMIS_FILTER_MASK_DATA),
    };
    err = esp_wifi_set_promiscuous(false);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_promiscuous_filter(&filter);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_promiscuous_rx_cb(nearby_wifi_promiscuous_rx_cb);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_promiscuous(true);
    if (err != ESP_OK) return err;

    s_wifi_promiscuous = true;
    return ESP_OK;
}

esp_err_t nearby_wifi_promiscuous_stop(void)
{
    if (!s_wifi_promiscuous) return ESP_OK;
    if (require_radio_access() != ESP_OK) return ESP_ERR_INVALID_STATE;
    esp_err_t err = esp_wifi_set_promiscuous(false);
    if (err == ESP_OK) s_wifi_promiscuous = false;
    return err;
}

esp_err_t nearby_wifi_set_mode(wifi_mode_t mode)
{
    if (require_radio_access() != ESP_OK) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nearby_wifi_driver_init();
    if (err != ESP_OK) return err;
    if (s_wifi_promiscuous) return ESP_ERR_INVALID_STATE;
    return esp_wifi_set_mode(mode);
}

esp_err_t nearby_wifi_set_ap_config(const wifi_config_t *config)
{
    if (config == NULL) return ESP_ERR_INVALID_ARG;
    if (scan_session_require_competing_active() != ESP_OK) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nearby_wifi_driver_init();
    if (err != ESP_OK) return err;
    return esp_wifi_set_config(WIFI_IF_AP, config);
}

esp_err_t nearby_wifi_set_sta_config(const wifi_config_t *config)
{
    if (config == NULL) return ESP_ERR_INVALID_ARG;
    if (scan_session_require_competing_active() != ESP_OK) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nearby_wifi_driver_init();
    if (err != ESP_OK) return err;
    return esp_wifi_set_config(WIFI_IF_STA, config);
}

esp_err_t nearby_wifi_sta_connect(void)
{
    if (scan_session_require_competing_active() != ESP_OK) return ESP_ERR_INVALID_STATE;
    return s_wifi_started ? esp_wifi_connect() : ESP_ERR_INVALID_STATE;
}

esp_err_t nearby_wifi_sta_disconnect(void)
{
    if (scan_session_require_competing_active() != ESP_OK) return ESP_ERR_INVALID_STATE;
    return s_wifi_started ? esp_wifi_disconnect() : ESP_ERR_INVALID_STATE;
}

esp_err_t nearby_wifi_driver_deinit(void)
{
    if (require_radio_access() != ESP_OK) return ESP_ERR_INVALID_STATE;
    if (!s_wifi_initialized) {
        destroy_sta_netif();
        release_wifi_phase_if_owned();
        return ESP_OK;
    }

    esp_err_t first_err = nearby_wifi_promiscuous_stop();
    esp_err_t err = esp_wifi_stop();
    if (first_err == ESP_OK && err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) first_err = err;
    s_wifi_started = false;

    err = esp_wifi_deinit();
    if (first_err == ESP_OK && err != ESP_OK) first_err = err;
    s_wifi_initialized = false;
    s_wifi_promiscuous = false;
    destroy_sta_netif();

    err = nearby_radio_phase_release(NEARBY_RADIO_PHASE_WIFI);
    if (first_err == ESP_OK && err != ESP_OK) first_err = err;
    return first_err;
}

bool nearby_wifi_driver_is_started(void)
{
    return s_wifi_started;
}

esp_netif_t *nearby_wifi_sta_netif(void)
{
    return s_sta_netif;
}
