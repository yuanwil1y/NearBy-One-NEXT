from pathlib import Path


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text()
    if text.count(old) != 1:
        raise RuntimeError(f"{path}: expected one match, got {text.count(old)}")
    p.write_text(text.replace(old, new, 1))


path = "firmware/components/db_storage/db_storage.c"
replace_once(path,
'''esp_err_t nearby_db_storage_set_preflight_safe_to_format(bool safe_to_format)
{
    if (require_storage_lease() != ESP_OK) return ESP_ERR_INVALID_STATE;
    if (s_part_file != NULL || s_prepared || s_format_task != NULL) return ESP_ERR_INVALID_STATE;
    s_preflight_safe_to_format = safe_to_format;
    if (!safe_to_format && s_format_state != NEARBY_DB_FORMAT_RUNNING) {
        s_format_state = NEARBY_DB_FORMAT_IDLE;
        s_format_error = ESP_OK;
    }
    return ESP_OK;
}''',
'''esp_err_t nearby_db_storage_set_preflight_safe_to_format(bool safe_to_format)
{
    if (require_storage_lease() != ESP_OK) return ESP_ERR_INVALID_STATE;
    if (s_part_file != NULL || s_format_task != NULL) return ESP_ERR_INVALID_STATE;
    if (!safe_to_format) {
        s_preflight_safe_to_format = false;
        s_prepared = false;
        s_format_state = NEARBY_DB_FORMAT_IDLE;
        s_format_error = ESP_OK;
        return ESP_OK;
    }
    if (s_prepared) return ESP_ERR_INVALID_STATE;
    s_preflight_safe_to_format = true;
    return ESP_OK;
}''')

replace_once(path,
'''    TaskHandle_t task = NULL;
    if (xTaskCreate(format_worker, "db_format", FORMAT_TASK_STACK, NULL,
                    FORMAT_TASK_PRIORITY, &task) != pdPASS) {
        (void)esp_timer_stop(s_format_timeout_timer);
        err = ESP_ERR_NO_MEM;
        goto fail;
    }
    s_format_task = task;
    return ESP_OK;''',
'''    TaskHandle_t task = NULL;
    s_format_task = (TaskHandle_t)1;
    if (xTaskCreate(format_worker, "db_format", FORMAT_TASK_STACK, NULL,
                    FORMAT_TASK_PRIORITY, &task) != pdPASS) {
        s_format_task = NULL;
        (void)esp_timer_stop(s_format_timeout_timer);
        err = ESP_ERR_NO_MEM;
        goto fail;
    }
    if (s_format_task != NULL) s_format_task = task;
    return ESP_OK;''')
