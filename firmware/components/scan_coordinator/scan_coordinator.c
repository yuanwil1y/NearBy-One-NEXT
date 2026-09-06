#include "nearby_scan_coordinator.h"

#include <string.h>

#include "radio_runtime.h"
#include "scan_session.h"

static const nearby_scan_module_t k_module_run_order[NEARBY_SCAN_MODULE_COUNT] = {
    NEARBY_SCAN_MODULE_WIFI_ACTIVE,
    NEARBY_SCAN_MODULE_ZEROCONF,
    NEARBY_SCAN_MODULE_SSDP,
    NEARBY_SCAN_MODULE_DHCP,
    NEARBY_SCAN_MODULE_WIFI_MANAGEMENT,
    NEARBY_SCAN_MODULE_BLE,
    NEARBY_SCAN_MODULE_IEEE802154,
};

static bool module_valid(nearby_scan_module_t module)
{
    return (unsigned)module < NEARBY_SCAN_MODULE_COUNT;
}

static bool module_terminal_status(nearby_scan_module_status_t status)
{
    return status == NEARBY_SCAN_MODULE_COMPLETE ||
           status == NEARBY_SCAN_MODULE_SKIPPED ||
           status == NEARBY_SCAN_MODULE_ERROR ||
           status == NEARBY_SCAN_MODULE_CANCELLED;
}

static uint8_t progress_percent(const nearby_scan_coordinator_t *coordinator)
{
    if (coordinator->enabled_count == 0) {
        return 0;
    }
    return (uint8_t)(((uint16_t)coordinator->terminal_count * 100u) /
                     coordinator->enabled_count);
}

static void publish(const nearby_scan_coordinator_t *coordinator, esp_err_t error)
{
    if (coordinator->event_cb == NULL) {
        return;
    }
    nearby_scan_event_t event = {
        .state = coordinator->state,
        .generation = coordinator->generation,
        .enabled_mask = coordinator->enabled_mask,
        .enabled_count = coordinator->enabled_count,
        .terminal_count = coordinator->terminal_count,
        .progress_percent = progress_percent(coordinator),
        .error = error,
    };
    memcpy(event.module_status, coordinator->module_status,
           sizeof(event.module_status));
    coordinator->event_cb(&event, coordinator->event_ctx);
}

static void mark_remaining(nearby_scan_coordinator_t *coordinator,
                           nearby_scan_module_status_t status,
                           esp_err_t error)
{
    for (unsigned i = 0; i < NEARBY_SCAN_MODULE_COUNT; ++i) {
        if ((coordinator->enabled_mask & NEARBY_SCAN_MODULE_BIT(i)) == 0) {
            continue;
        }
        if (module_terminal_status(coordinator->module_status[i])) {
            continue;
        }
        coordinator->module_status[i] = status;
        ++coordinator->terminal_count;
    }
    if (error != ESP_OK && coordinator->first_module_error == ESP_OK) {
        coordinator->first_module_error = error;
    }
}

static nearby_scan_module_runner_t runner_for(const nearby_scan_runners_t *runners,
                                               nearby_scan_module_t module)
{
    if (runners == NULL) {
        return NULL;
    }
    switch (module) {
    case NEARBY_SCAN_MODULE_WIFI_ACTIVE: return runners->wifi_active;
    case NEARBY_SCAN_MODULE_WIFI_MANAGEMENT: return runners->wifi_management;
    case NEARBY_SCAN_MODULE_ZEROCONF: return runners->zeroconf;
    case NEARBY_SCAN_MODULE_SSDP: return runners->ssdp;
    case NEARBY_SCAN_MODULE_DHCP: return runners->dhcp;
    case NEARBY_SCAN_MODULE_BLE: return runners->ble;
    case NEARBY_SCAN_MODULE_IEEE802154: return runners->ieee802154;
    default: return NULL;
    }
}

void nearby_scan_coordinator_init(nearby_scan_coordinator_t *coordinator,
                                  nearby_scan_event_cb_t event_cb,
                                  void *event_ctx)
{
    if (coordinator == NULL) {
        return;
    }
    memset(coordinator, 0, sizeof(*coordinator));
    coordinator->state = NEARBY_SCAN_STATE_IDLE;
    coordinator->event_cb = event_cb;
    coordinator->event_ctx = event_ctx;
}

esp_err_t nearby_scan_coordinator_begin(nearby_scan_coordinator_t *coordinator,
                                        uint32_t enabled_mask,
                                        TickType_t gate_timeout_ticks)
{
    if (coordinator == NULL || enabled_mask == 0 ||
        (enabled_mask & ~NEARBY_SCAN_ALL_MODULES_MASK) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (coordinator->gate_held ||
        (coordinator->state != NEARBY_SCAN_STATE_IDLE &&
         coordinator->state != NEARBY_SCAN_STATE_COMPLETE)) {
        return ESP_ERR_INVALID_STATE;
    }

    coordinator->state = NEARBY_SCAN_STATE_STARTING;
    coordinator->enabled_mask = enabled_mask;
    coordinator->enabled_count = 0;
    coordinator->terminal_count = 0;
    coordinator->first_module_error = ESP_OK;
    for (unsigned i = 0; i < NEARBY_SCAN_MODULE_COUNT; ++i) {
        if ((enabled_mask & NEARBY_SCAN_MODULE_BIT(i)) != 0) {
            coordinator->module_status[i] = NEARBY_SCAN_MODULE_PENDING;
            ++coordinator->enabled_count;
        } else {
            coordinator->module_status[i] = NEARBY_SCAN_MODULE_DISABLED;
        }
    }
    publish(coordinator, ESP_OK);

    esp_err_t err = scan_session_begin(gate_timeout_ticks);
    if (err != ESP_OK) {
        coordinator->state = NEARBY_SCAN_STATE_IDLE;
        coordinator->enabled_mask = 0;
        coordinator->enabled_count = 0;
        publish(coordinator, err);
        return err;
    }

    coordinator->gate_held = true;
    coordinator->generation = scan_session_generation();
    coordinator->state = NEARBY_SCAN_STATE_RUNNING;
    publish(coordinator, ESP_OK);
    return ESP_OK;
}

esp_err_t nearby_scan_coordinator_module_start(
    nearby_scan_coordinator_t *coordinator,
    nearby_scan_module_t module)
{
    if (coordinator == NULL || !module_valid(module)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!coordinator->gate_held || coordinator->state != NEARBY_SCAN_STATE_RUNNING ||
        scan_session_require_scan_owner() != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    if (coordinator->module_status[module] != NEARBY_SCAN_MODULE_PENDING) {
        return ESP_ERR_INVALID_STATE;
    }

    for (unsigned i = 0; i < NEARBY_SCAN_MODULE_COUNT; ++i) {
        if (coordinator->module_status[i] == NEARBY_SCAN_MODULE_RUNNING) {
            return ESP_ERR_INVALID_STATE;
        }
    }
    coordinator->module_status[module] = NEARBY_SCAN_MODULE_RUNNING;
    publish(coordinator, ESP_OK);
    return ESP_OK;
}

esp_err_t nearby_scan_coordinator_module_terminal(
    nearby_scan_coordinator_t *coordinator,
    nearby_scan_module_t module,
    nearby_scan_module_status_t terminal_status,
    esp_err_t error)
{
    if (coordinator == NULL || !module_valid(module) ||
        !module_terminal_status(terminal_status)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!coordinator->gate_held || coordinator->state != NEARBY_SCAN_STATE_RUNNING ||
        coordinator->module_status[module] != NEARBY_SCAN_MODULE_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((terminal_status == NEARBY_SCAN_MODULE_COMPLETE ||
         terminal_status == NEARBY_SCAN_MODULE_SKIPPED) && error != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }

    coordinator->module_status[module] = terminal_status;
    ++coordinator->terminal_count;
    if (error != ESP_OK && coordinator->first_module_error == ESP_OK) {
        coordinator->first_module_error = error;
    }
    publish(coordinator, error);
    return ESP_OK;
}

esp_err_t nearby_scan_coordinator_finish(nearby_scan_coordinator_t *coordinator)
{
    if (coordinator == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!coordinator->gate_held || coordinator->state != NEARBY_SCAN_STATE_RUNNING ||
        coordinator->terminal_count != coordinator->enabled_count) {
        return ESP_ERR_INVALID_STATE;
    }

    coordinator->state = NEARBY_SCAN_STATE_FINISHING;
    publish(coordinator, coordinator->first_module_error);

    esp_err_t err = nearby_radio_scan_cleanup_all();
    if (err != ESP_OK || scan_session_fatal_error() != ESP_OK) {
        if (err == ESP_OK) {
            err = scan_session_fatal_error();
        }
        coordinator->state = NEARBY_SCAN_STATE_FATAL;
        publish(coordinator, err);
        return err;
    }

    err = scan_session_end();
    if (err != ESP_OK) {
        coordinator->state = NEARBY_SCAN_STATE_FATAL;
        publish(coordinator, err);
        return err;
    }

    coordinator->gate_held = false;
    coordinator->state = NEARBY_SCAN_STATE_COMPLETE;
    publish(coordinator, coordinator->first_module_error);
    return ESP_OK;
}

esp_err_t nearby_scan_coordinator_cancel(nearby_scan_coordinator_t *coordinator,
                                         esp_err_t reason)
{
    if (coordinator == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!coordinator->gate_held || coordinator->state != NEARBY_SCAN_STATE_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }
    mark_remaining(coordinator, NEARBY_SCAN_MODULE_CANCELLED, reason);
    publish(coordinator, reason);
    return nearby_scan_coordinator_finish(coordinator);
}

esp_err_t nearby_scan_coordinator_recover(nearby_scan_coordinator_t *coordinator)
{
    if (coordinator == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!coordinator->gate_held || coordinator->state != NEARBY_SCAN_STATE_FATAL ||
        scan_session_require_scan_owner() != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = nearby_radio_scan_cleanup_all();
    if (err != ESP_OK || scan_session_fatal_error() != ESP_OK) {
        if (err == ESP_OK) {
            err = scan_session_fatal_error();
        }
        publish(coordinator, err);
        return err;
    }

    mark_remaining(coordinator, NEARBY_SCAN_MODULE_ERROR,
                   coordinator->first_module_error != ESP_OK
                       ? coordinator->first_module_error : ESP_FAIL);
    err = scan_session_end();
    if (err != ESP_OK) {
        publish(coordinator, err);
        return err;
    }
    coordinator->gate_held = false;
    coordinator->state = NEARBY_SCAN_STATE_COMPLETE;
    publish(coordinator, coordinator->first_module_error);
    return ESP_OK;
}

esp_err_t nearby_scan_coordinator_run(nearby_scan_coordinator_t *coordinator,
                                      uint32_t enabled_mask,
                                      TickType_t gate_timeout_ticks,
                                      const nearby_scan_runners_t *runners)
{
    esp_err_t err = nearby_scan_coordinator_begin(coordinator, enabled_mask,
                                                  gate_timeout_ticks);
    if (err != ESP_OK) {
        return err;
    }

    for (unsigned order = 0; order < NEARBY_SCAN_MODULE_COUNT; ++order) {
        const nearby_scan_module_t module = k_module_run_order[order];
        if ((enabled_mask & NEARBY_SCAN_MODULE_BIT(module)) == 0) {
            continue;
        }
        err = nearby_scan_coordinator_module_start(coordinator, module);
        if (err != ESP_OK) {
            return nearby_scan_coordinator_cancel(coordinator, err);
        }

        const nearby_scan_module_runner_t runner = runner_for(runners, module);
        nearby_scan_module_outcome_t outcome = {
            .status = NEARBY_SCAN_MODULE_SKIPPED,
            .error = ESP_OK,
        };
        if (runner != NULL) {
            outcome = runner(runners->ctx);
        }
        if (!module_terminal_status(outcome.status) ||
            outcome.status == NEARBY_SCAN_MODULE_CANCELLED) {
            outcome.status = NEARBY_SCAN_MODULE_ERROR;
            outcome.error = ESP_ERR_INVALID_RESPONSE;
        }
        err = nearby_scan_coordinator_module_terminal(coordinator, module,
                                                      outcome.status,
                                                      outcome.error);
        if (err != ESP_OK) {
            return nearby_scan_coordinator_cancel(coordinator, err);
        }

        const esp_err_t fatal = scan_session_fatal_error();
        if (fatal != ESP_OK) {
            coordinator->state = NEARBY_SCAN_STATE_FATAL;
            publish(coordinator, fatal);
            return fatal;
        }
    }

    return nearby_scan_coordinator_finish(coordinator);
}

uint8_t nearby_scan_coordinator_progress(const nearby_scan_coordinator_t *coordinator)
{
    return coordinator == NULL ? 0 : progress_percent(coordinator);
}

bool nearby_scan_coordinator_gate_held(const nearby_scan_coordinator_t *coordinator)
{
    return coordinator != NULL && coordinator->gate_held;
}

esp_err_t nearby_scan_coordinator_first_module_error(
    const nearby_scan_coordinator_t *coordinator)
{
    return coordinator == NULL ? ESP_ERR_INVALID_ARG : coordinator->first_module_error;
}
