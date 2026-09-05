#include "radio_runtime.h"
#include "radio_runtime_internal.h"

#include "esp_ieee802154.h"
#include "native_handoff.h"
#include "scan_session.h"

static bool s_i154_active;

static void release_i154_phase_if_owned(void)
{
    if (nearby_radio_phase_current() == NEARBY_RADIO_PHASE_I154) {
        (void)nearby_radio_phase_release(NEARBY_RADIO_PHASE_I154);
    }
}

esp_err_t nearby_i154_receive_start(uint8_t channel)
{
    if (scan_session_require_scan_owner() != ESP_OK) return ESP_ERR_INVALID_STATE;
    if (channel < 11 || channel > 26) return ESP_ERR_INVALID_ARG;
    if (s_i154_active) return ESP_ERR_INVALID_STATE;

    esp_err_t err = nearby_radio_phase_claim(NEARBY_RADIO_PHASE_I154);
    if (err != ESP_OK) return err;

    /* Public callback registration is only legal while the subsystem is off. */
    err = nearby_i154_handoff_init();
    if (err != ESP_OK) goto fail_phase;
    err = esp_ieee802154_enable();
    if (err != ESP_OK) goto fail_phase;

    err = esp_ieee802154_set_channel(channel);
    if (err != ESP_OK) goto fail_disable;
    err = esp_ieee802154_set_promiscuous(true);
    if (err != ESP_OK) goto fail_disable;
    err = esp_ieee802154_receive();
    if (err != ESP_OK) {
        (void)esp_ieee802154_set_promiscuous(false);
        goto fail_disable;
    }

    s_i154_active = true;
    return ESP_OK;

fail_disable:
    if (esp_ieee802154_disable() == ESP_OK) release_i154_phase_if_owned();
    return err;
fail_phase:
    release_i154_phase_if_owned();
    return err;
}

esp_err_t nearby_i154_receive_stop(void)
{
    if (scan_session_require_scan_owner() != ESP_OK) return ESP_ERR_INVALID_STATE;

    if (!s_i154_active) {
        release_i154_phase_if_owned();
        return ESP_OK;
    }

    esp_err_t first_err = esp_ieee802154_set_promiscuous(false);
    esp_err_t err = esp_ieee802154_sleep();
    if (first_err == ESP_OK && err != ESP_OK) first_err = err;

    esp_err_t disable_err = esp_ieee802154_disable();
    if (disable_err != ESP_OK) {
        return first_err != ESP_OK ? first_err : disable_err;
    }

    s_i154_active = false;
    esp_err_t phase_err = nearby_radio_phase_release(NEARBY_RADIO_PHASE_I154);
    return first_err != ESP_OK ? first_err : phase_err;
}

bool nearby_i154_receive_is_active(void)
{
    return s_i154_active;
}
