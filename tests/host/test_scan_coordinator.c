#include "nearby_scan_coordinator.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int begin_calls;
static int end_calls;
static int cleanup_calls;
static bool owner;
static esp_err_t begin_result;
static esp_err_t cleanup_result;
static esp_err_t fatal_error;
static uint32_t generation = 41;
static int event_calls;
static nearby_scan_event_t last_event;

esp_err_t scan_session_begin(TickType_t timeout_ticks)
{
    (void)timeout_ticks;
    ++begin_calls;
    if (begin_result == ESP_OK) {
        owner = true;
        ++generation;
    }
    return begin_result;
}

esp_err_t scan_session_end(void)
{
    ++end_calls;
    if (!owner || fatal_error != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    owner = false;
    return ESP_OK;
}

esp_err_t scan_session_require_scan_owner(void)
{
    return owner ? ESP_OK : ESP_ERR_INVALID_STATE;
}

uint32_t scan_session_generation(void)
{
    return generation;
}

esp_err_t scan_session_fatal_error(void)
{
    return fatal_error;
}

esp_err_t nearby_radio_scan_cleanup_all(void)
{
    ++cleanup_calls;
    if (cleanup_result == ESP_OK) {
        fatal_error = ESP_OK;
    } else {
        fatal_error = cleanup_result;
    }
    return cleanup_result;
}

static void on_event(const nearby_scan_event_t *event, void *ctx)
{
    (void)ctx;
    ++event_calls;
    last_event = *event;
}

static nearby_scan_module_outcome_t ok_runner(void *ctx)
{
    int *calls = ctx;
    ++*calls;
    return (nearby_scan_module_outcome_t){NEARBY_SCAN_MODULE_COMPLETE, ESP_OK};
}

static nearby_scan_module_outcome_t soft_fail_runner(void *ctx)
{
    int *calls = ctx;
    ++*calls;
    return (nearby_scan_module_outcome_t){NEARBY_SCAN_MODULE_ERROR, ESP_ERR_NOT_FOUND};
}

static void reset_stubs(void)
{
    begin_calls = end_calls = cleanup_calls = event_calls = 0;
    owner = false;
    begin_result = ESP_OK;
    cleanup_result = ESP_OK;
    fatal_error = ESP_OK;
    memset(&last_event, 0, sizeof(last_event));
}

static void test_real_terminal_progress(void)
{
    reset_stubs();
    nearby_scan_coordinator_t c;
    nearby_scan_coordinator_init(&c, on_event, NULL);
    const uint32_t mask = NEARBY_SCAN_MODULE_BIT(NEARBY_SCAN_MODULE_BLE) |
                          NEARBY_SCAN_MODULE_BIT(NEARBY_SCAN_MODULE_SSDP);
    assert(nearby_scan_coordinator_begin(&c, mask, 10) == ESP_OK);
    assert(begin_calls == 1 && nearby_scan_coordinator_progress(&c) == 0);
    assert(nearby_scan_coordinator_module_start(&c, NEARBY_SCAN_MODULE_SSDP) == ESP_OK);
    assert(nearby_scan_coordinator_progress(&c) == 0);
    assert(nearby_scan_coordinator_module_terminal(&c, NEARBY_SCAN_MODULE_SSDP,
            NEARBY_SCAN_MODULE_COMPLETE, ESP_OK) == ESP_OK);
    assert(nearby_scan_coordinator_progress(&c) == 50);
    assert(nearby_scan_coordinator_module_start(&c, NEARBY_SCAN_MODULE_BLE) == ESP_OK);
    assert(nearby_scan_coordinator_module_terminal(&c, NEARBY_SCAN_MODULE_BLE,
            NEARBY_SCAN_MODULE_SKIPPED, ESP_OK) == ESP_OK);
    assert(nearby_scan_coordinator_progress(&c) == 100);
    assert(nearby_scan_coordinator_finish(&c) == ESP_OK);
    assert(cleanup_calls == 1 && end_calls == 1 && !owner);
    assert(c.state == NEARBY_SCAN_STATE_COMPLETE && !c.gate_held);
    assert(last_event.progress_percent == 100);
}

static void test_fixed_run_and_soft_error(void)
{
    reset_stubs();
    nearby_scan_coordinator_t c;
    nearby_scan_coordinator_init(&c, on_event, NULL);
    int calls = 0;
    nearby_scan_runners_t runners = {
        .ssdp = soft_fail_runner,
        .ble = ok_runner,
        .ctx = &calls,
    };
    const uint32_t mask = NEARBY_SCAN_MODULE_BIT(NEARBY_SCAN_MODULE_SSDP) |
                          NEARBY_SCAN_MODULE_BIT(NEARBY_SCAN_MODULE_BLE) |
                          NEARBY_SCAN_MODULE_BIT(NEARBY_SCAN_MODULE_DHCP);
    assert(nearby_scan_coordinator_run(&c, mask, 10, &runners) == ESP_OK);
    assert(calls == 2); /* DHCP has no runner and is deliberately skipped. */
    assert(c.module_status[NEARBY_SCAN_MODULE_SSDP] == NEARBY_SCAN_MODULE_ERROR);
    assert(c.module_status[NEARBY_SCAN_MODULE_DHCP] == NEARBY_SCAN_MODULE_SKIPPED);
    assert(c.module_status[NEARBY_SCAN_MODULE_BLE] == NEARBY_SCAN_MODULE_COMPLETE);
    assert(nearby_scan_coordinator_first_module_error(&c) == ESP_ERR_NOT_FOUND);
    assert(nearby_scan_coordinator_progress(&c) == 100);
    assert(end_calls == 1 && !owner);
}

static void test_fatal_cleanup_holds_gate_until_recovery(void)
{
    reset_stubs();
    nearby_scan_coordinator_t c;
    nearby_scan_coordinator_init(&c, on_event, NULL);
    const uint32_t mask = NEARBY_SCAN_MODULE_BIT(NEARBY_SCAN_MODULE_BLE);
    assert(nearby_scan_coordinator_begin(&c, mask, 10) == ESP_OK);
    assert(nearby_scan_coordinator_module_start(&c, NEARBY_SCAN_MODULE_BLE) == ESP_OK);
    assert(nearby_scan_coordinator_module_terminal(&c, NEARBY_SCAN_MODULE_BLE,
            NEARBY_SCAN_MODULE_COMPLETE, ESP_OK) == ESP_OK);
    cleanup_result = ESP_FAIL;
    assert(nearby_scan_coordinator_finish(&c) == ESP_FAIL);
    assert(c.state == NEARBY_SCAN_STATE_FATAL && c.gate_held && owner);
    assert(end_calls == 0);

    cleanup_result = ESP_OK;
    assert(nearby_scan_coordinator_recover(&c) == ESP_OK);
    assert(c.state == NEARBY_SCAN_STATE_COMPLETE && !c.gate_held && !owner);
    assert(cleanup_calls == 2 && end_calls == 1);
}

static void test_gate_acquire_failure_is_idle(void)
{
    reset_stubs();
    begin_result = ESP_ERR_TIMEOUT;
    nearby_scan_coordinator_t c;
    nearby_scan_coordinator_init(&c, on_event, NULL);
    assert(nearby_scan_coordinator_begin(&c,
        NEARBY_SCAN_MODULE_BIT(NEARBY_SCAN_MODULE_BLE), 1) == ESP_ERR_TIMEOUT);
    assert(c.state == NEARBY_SCAN_STATE_IDLE && !c.gate_held && !owner);
    assert(cleanup_calls == 0 && end_calls == 0);
}

int main(void)
{
    test_real_terminal_progress();
    test_fixed_run_and_soft_error();
    test_fatal_cleanup_holds_gate_until_recovery();
    test_gate_acquire_failure_is_idle();
    puts("scan coordinator host tests: OK");
    return 0;
}
