#include "nearby_scan_default_runners.h"

#include <string.h>

static nearby_scan_module_outcome_t outcome_from_err(esp_err_t err)
{
    nearby_scan_module_outcome_t out = {
        .status = err == ESP_OK ? NEARBY_SCAN_MODULE_COMPLETE : NEARBY_SCAN_MODULE_ERROR,
        .error = err,
    };
    return out;
}

static nearby_scan_module_outcome_t skipped(void)
{
    nearby_scan_module_outcome_t out = {
        .status = NEARBY_SCAN_MODULE_SKIPPED,
        .error = ESP_OK,
    };
    return out;
}

static nearby_scan_module_outcome_t run_wifi_active(void *opaque)
{
    nearby_scan_default_context_t *ctx = (nearby_scan_default_context_t *)opaque;
    if (ctx == NULL || ctx->wifi_active_config == NULL || ctx->wifi_active_stats == NULL) {
        return skipped();
    }
    return outcome_from_err(nearby_wifi_active_scan_run(ctx->wifi_active_config,
                                                        ctx->wifi_active_stats));
}

static nearby_scan_module_outcome_t run_wifi_management(void *opaque)
{
    nearby_scan_default_context_t *ctx = (nearby_scan_default_context_t *)opaque;
    if (ctx == NULL || ctx->wifi_management_config == NULL ||
        ctx->wifi_management_stats == NULL) {
        return skipped();
    }
    return outcome_from_err(nearby_wifi_passive_scan_run(ctx->wifi_management_config,
                                                         ctx->wifi_management_stats));
}

static nearby_scan_module_outcome_t run_zeroconf(void *opaque)
{
    nearby_scan_default_context_t *ctx = (nearby_scan_default_context_t *)opaque;
    if (ctx == NULL || ctx->zeroconf_config == NULL || ctx->zeroconf_stats == NULL) {
        return skipped();
    }
    if (!nearby_lan_scan_prerequisite_ready()) {
        return skipped();
    }
    return outcome_from_err(nearby_zeroconf_scan_run(ctx->zeroconf_config,
                                                     ctx->zeroconf_stats));
}

static nearby_scan_module_outcome_t run_ssdp(void *opaque)
{
    nearby_scan_default_context_t *ctx = (nearby_scan_default_context_t *)opaque;
    if (ctx == NULL || ctx->ssdp_config == NULL || ctx->ssdp_stats == NULL) {
        return skipped();
    }
    if (!nearby_lan_scan_prerequisite_ready()) {
        return skipped();
    }
    return outcome_from_err(nearby_ssdp_scan_run(ctx->ssdp_config, ctx->ssdp_stats));
}

static nearby_scan_module_outcome_t run_dhcp(void *opaque)
{
    nearby_scan_default_context_t *ctx = (nearby_scan_default_context_t *)opaque;
    if (ctx == NULL || ctx->dhcp_config == NULL || ctx->dhcp_stats == NULL) {
        return skipped();
    }
    if (!nearby_lan_scan_prerequisite_ready()) {
        return skipped();
    }
    return outcome_from_err(nearby_dhcp_scan_run(ctx->dhcp_config, ctx->dhcp_stats));
}

static nearby_scan_module_outcome_t run_ble(void *opaque)
{
    nearby_scan_default_context_t *ctx = (nearby_scan_default_context_t *)opaque;
    if (ctx == NULL || ctx->ble_config == NULL || ctx->ble_stats == NULL) {
        return skipped();
    }
    return outcome_from_err(nearby_ble_scan_run(ctx->ble_config, ctx->ble_stats));
}

static nearby_scan_module_outcome_t run_i154(void *opaque)
{
    nearby_scan_default_context_t *ctx = (nearby_scan_default_context_t *)opaque;
    if (ctx == NULL || ctx->i154_config == NULL || ctx->i154_stats == NULL) {
        return skipped();
    }
    return outcome_from_err(nearby_i154_scan_run(ctx->i154_config, ctx->i154_stats));
}

void nearby_scan_default_runners_init(nearby_scan_default_context_t *context,
                                      nearby_scan_runners_t *out_runners)
{
    if (out_runners == NULL) {
        return;
    }
    memset(out_runners, 0, sizeof(*out_runners));
    out_runners->wifi_active = run_wifi_active;
    out_runners->wifi_management = run_wifi_management;
    out_runners->zeroconf = run_zeroconf;
    out_runners->ssdp = run_ssdp;
    out_runners->dhcp = run_dhcp;
    out_runners->ble = run_ble;
    out_runners->ieee802154 = run_i154;
    out_runners->ctx = context;
}
