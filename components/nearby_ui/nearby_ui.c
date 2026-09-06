#include "nearby_ui.h"

#include <stdio.h>
#include <string.h>

#include "ha_core.h"
#include "lvgl.h"

#define NB_W 170
#define NB_H 320
#define NB_TOP_H 34
#define NB_PROGRESS_H 3
#define NB_CONTENT_Y (NB_TOP_H + NB_PROGRESS_H)
#define NB_CARD_H 48

#define NB_BLUE 0x03A9F4
#define NB_BG 0xF4F7F9
#define NB_TEXT 0x263238
#define NB_MUTED 0x607D8B
#define NB_BORDER 0xE3E8EC
#define NB_OK 0x2E7D32

#if LVGL_VERSION_MAJOR >= 9
#define NB_SCREEN_LOAD(screen) lv_screen_load(screen)
#define NB_SCREEN_ACTIVE() lv_screen_active()
#define NB_BUTTON_CREATE(parent) lv_button_create(parent)
#define NB_OBJ_DELETE(obj) lv_obj_delete(obj)
#define NB_OBJ_DELETE_ASYNC(obj) lv_obj_delete_async(obj)
#else
#define NB_SCREEN_LOAD(screen) lv_scr_load(screen)
#define NB_SCREEN_ACTIVE() lv_scr_act()
#define NB_BUTTON_CREATE(parent) lv_btn_create(parent)
#define NB_OBJ_DELETE(obj) lv_obj_del(obj)
#define NB_OBJ_DELETE_ASYNC(obj) lv_obj_del_async(obj)
#endif

static struct {
    nearby_ui_callbacks_t cb;
    nearby_ui_scan_state_t scan_state;
    uint16_t scan_total;
    uint16_t scan_done;

    lv_obj_t *home_screen;
    lv_obj_t *home_list;
    lv_obj_t *progress;
    lv_obj_t *input_blocker;
    lv_obj_t *detail_screen;
    lv_obj_t *settings_screen;
    lv_obj_t *portal_modal;

    char selected_device_id[HA_ID_LEN];
    char db_status[48];
    char firmware[32];
    bool portal_running;
    char portal_ssid[48];
    char portal_address[32];
    char portal_pin[24];
} g;

static void safe_copy(char *dst, size_t cap, const char *src)
{
    if (cap == 0) return;
    (void)snprintf(dst, cap, "%s", src ? src : "");
}

static void style_screen(lv_obj_t *screen)
{
    lv_obj_set_style_bg_color(screen, lv_color_hex(NB_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *make_card(lv_obj_t *parent, int height)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, 154);
    lv_obj_set_height(card, height);
    lv_obj_set_style_bg_color(card, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(NB_BORDER), 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static void release_detail(void)
{
    if (!g.detail_screen) return;
    lv_obj_t *screen = g.detail_screen;
    g.detail_screen = NULL;
    NB_OBJ_DELETE_ASYNC(screen);
}

static void release_settings(void)
{
    if (!g.settings_screen) return;
    lv_obj_t *screen = g.settings_screen;
    g.settings_screen = NULL;
    g.portal_modal = NULL;
    NB_OBJ_DELETE_ASYNC(screen);
}

static void show_home(void);
static void show_settings(void);
static void show_detail(const char *device_id);
static void rebuild_home_list(void);
static void render_portal_modal(void);

static void back_clicked(lv_event_t *event)
{
    (void)event;
    if (g.scan_state == NEARBY_UI_SCAN_SCANNING) return;
    show_home();
}

static void scan_clicked(lv_event_t *event)
{
    (void)event;
    if (g.scan_state == NEARBY_UI_SCAN_SCANNING || !g.cb.scan_requested) return;
    g.cb.scan_requested(g.cb.ctx);
}

static void settings_clicked(lv_event_t *event)
{
    (void)event;
    if (g.scan_state == NEARBY_UI_SCAN_SCANNING) return;
    show_settings();
}

static void device_clicked(lv_event_t *event)
{
    if (g.scan_state == NEARBY_UI_SCAN_SCANNING) return;
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(event);
    const ha_device_t *device = ha_device_at(index);
    if (device) show_detail(device->id);
}

static void service_clicked(lv_event_t *event)
{
    if (g.scan_state == NEARBY_UI_SCAN_SCANNING) return;
    const ha_entity_t *entity = (const ha_entity_t *)lv_event_get_user_data(event);
    if (!entity) return;

    const char *service = NULL;
    if (strcmp(entity->domain, HA_DOMAIN_BUTTON) == 0) {
        service = HA_SERVICE_PRESS;
    } else if (strcmp(entity->domain, HA_DOMAIN_SWITCH) == 0 ||
               strcmp(entity->domain, HA_DOMAIN_LIGHT) == 0) {
        const ha_state_t *state = ha_state_get(entity->entity_id);
        service = state && strcmp(state->state, HA_STATE_ON) == 0
                    ? HA_SERVICE_TURN_OFF : HA_SERVICE_TURN_ON;
    }
    if (service && ha_entity_supports_service(entity->entity_id, service)) {
        (void)ha_entity_call_service(entity->entity_id, service, NULL, 0);
        show_detail(g.selected_device_id);
    }
}

static void portal_start_clicked(lv_event_t *event)
{
    (void)event;
    if (g.scan_state == NEARBY_UI_SCAN_SCANNING || !g.cb.portal_start_requested) return;
    g.cb.portal_start_requested(g.cb.ctx);
    render_portal_modal();
}

static void portal_stop_clicked(lv_event_t *event)
{
    (void)event;
    if (g.scan_state == NEARBY_UI_SCAN_SCANNING) return;
    if (g.cb.portal_stop_requested) g.cb.portal_stop_requested(g.cb.ctx);
    if (g.portal_modal) {
        NB_OBJ_DELETE(g.portal_modal);
        g.portal_modal = NULL;
    }
}

static lv_obj_t *make_icon_button(lv_obj_t *parent, const char *symbol)
{
    lv_obj_t *button = NB_BUTTON_CREATE(parent);
    lv_obj_set_size(button, 30, 30);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    return button;
}

static void make_top_bar(lv_obj_t *screen, bool home, const char *title)
{
    lv_obj_t *bar = lv_obj_create(screen);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_size(bar, NB_W, NB_TOP_H);
    lv_obj_set_style_bg_color(bar, lv_color_hex(NB_BLUE), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *left = make_icon_button(bar, home ? LV_SYMBOL_REFRESH : LV_SYMBOL_LEFT);
    lv_obj_set_pos(left, 2, 2);
    lv_obj_add_event_cb(left, home ? scan_clicked : back_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(bar);
    lv_label_set_text(label, title ? title : "NearBy One NEXT");
    lv_obj_set_width(label, home ? 102 : 124);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, home ? 0 : 13, 0);

    if (home) {
        lv_obj_t *settings = make_icon_button(bar, LV_SYMBOL_SETTINGS);
        lv_obj_set_pos(settings, 138, 2);
        lv_obj_add_event_cb(settings, settings_clicked, LV_EVENT_CLICKED, NULL);
    }
}

static void create_home(void)
{
    g.home_screen = lv_obj_create(NULL);
    style_screen(g.home_screen);
    make_top_bar(g.home_screen, true, "NearBy One NEXT");

    g.progress = lv_bar_create(g.home_screen);
    lv_obj_set_pos(g.progress, 0, NB_TOP_H);
    lv_obj_set_size(g.progress, NB_W, NB_PROGRESS_H);
    lv_bar_set_range(g.progress, 0, 100);
    lv_obj_set_style_radius(g.progress, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(g.progress, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g.progress, lv_color_hex(0xB3E5FC), LV_PART_MAIN);
    lv_obj_set_style_bg_color(g.progress, lv_color_hex(0x0277BD), LV_PART_INDICATOR);
    lv_obj_add_flag(g.progress, LV_OBJ_FLAG_HIDDEN);

    g.home_list = lv_obj_create(g.home_screen);
    lv_obj_set_pos(g.home_list, 0, NB_CONTENT_Y);
    lv_obj_set_size(g.home_list, NB_W, NB_H - NB_CONTENT_Y);
    lv_obj_set_style_bg_opa(g.home_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g.home_list, 0, 0);
    lv_obj_set_style_pad_left(g.home_list, 8, 0);
    lv_obj_set_style_pad_right(g.home_list, 8, 0);
    lv_obj_set_style_pad_top(g.home_list, 7, 0);
    lv_obj_set_style_pad_bottom(g.home_list, 8, 0);
    lv_obj_set_style_pad_row(g.home_list, 6, 0);
    lv_obj_set_flex_flow(g.home_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(g.home_list, LV_DIR_VER);
    rebuild_home_list();
}

static void rebuild_home_list(void)
{
    if (!g.home_list) return;
    lv_obj_clean(g.home_list);
    for (size_t i = 0; i < ha_device_count(); ++i) {
        const ha_device_t *device = ha_device_at(i);
        if (!device) continue;
        lv_obj_t *card = make_card(g.home_list, NB_CARD_H);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, device_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

        lv_obj_t *icon = lv_label_create(card);
        lv_label_set_text(icon, LV_SYMBOL_HOME);
        lv_obj_set_style_text_color(icon, lv_color_hex(NB_BLUE), 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 1, 0);

        lv_obj_t *name = lv_label_create(card);
        lv_label_set_text(name, device->name[0] ? device->name : device->id);
        lv_obj_set_width(name, 112);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(name, lv_color_hex(NB_TEXT), 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 28, 0);
    }
}

static void show_home(void)
{
    if (!g.home_screen) create_home();
    rebuild_home_list();
    NB_SCREEN_LOAD(g.home_screen);
    release_detail();
    release_settings();
}

static void add_entity_row(lv_obj_t *parent, const ha_entity_t *entity)
{
    const ha_state_t *state = ha_state_get(entity->entity_id);
    const char *state_text = state ? state->state :
        (entity->available ? HA_STATE_UNKNOWN : HA_STATE_UNAVAILABLE);
    bool actionable = ha_entity_supports_service(entity->entity_id, HA_SERVICE_PRESS) ||
                      ha_entity_supports_service(entity->entity_id, HA_SERVICE_TURN_ON) ||
                      ha_entity_supports_service(entity->entity_id, HA_SERVICE_TURN_OFF);

    lv_obj_t *card = make_card(parent, 54);
    lv_obj_t *name = lv_label_create(card);
    lv_label_set_text(name, entity->name[0] ? entity->name : entity->entity_id);
    lv_obj_set_width(name, actionable ? 82 : 100);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(name, lv_color_hex(NB_TEXT), 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 0);

    char value[72];
    (void)snprintf(value, sizeof(value), "%s%s%s", state_text,
                   entity->unit_of_measurement[0] ? " " : "",
                   entity->unit_of_measurement);
    lv_obj_t *state_label = lv_label_create(card);
    lv_label_set_text(state_label, value);
    lv_obj_set_width(state_label, actionable ? 82 : 136);
    lv_label_set_long_mode(state_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(state_label,
                                strcmp(state_text, HA_STATE_UNAVAILABLE) == 0
                                    ? lv_color_hex(NB_MUTED) : lv_color_hex(NB_BLUE), 0);
    lv_obj_align(state_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    if (!actionable) return;
    if (strcmp(entity->domain, HA_DOMAIN_SWITCH) == 0 ||
        strcmp(entity->domain, HA_DOMAIN_LIGHT) == 0) {
        lv_obj_t *sw = lv_switch_create(card);
        lv_obj_set_size(sw, 38, 20);
        lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
        if (strcmp(state_text, HA_STATE_ON) == 0) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, service_clicked, LV_EVENT_CLICKED, (void *)entity);
    } else {
        lv_obj_t *button = NB_BUTTON_CREATE(card);
        lv_obj_set_size(button, 52, 28);
        lv_obj_align(button, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, "Run");
        lv_obj_center(label);
        lv_obj_add_event_cb(button, service_clicked, LV_EVENT_CLICKED, (void *)entity);
    }
}

static void show_detail(const char *device_id)
{
    const ha_device_t *device = ha_device_get(device_id);
    if (!device) return;
    safe_copy(g.selected_device_id, sizeof(g.selected_device_id), device->id);

    release_settings();
    release_detail();
    g.detail_screen = lv_obj_create(NULL);
    style_screen(g.detail_screen);
    make_top_bar(g.detail_screen, false, device->name[0] ? device->name : device->id);

    lv_obj_t *list = lv_obj_create(g.detail_screen);
    lv_obj_set_pos(list, 0, NB_TOP_H);
    lv_obj_set_size(list, NB_W, NB_H - NB_TOP_H);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 8, 0);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    if (device->manufacturer[0] || device->model[0]) {
        char meta[100];
        (void)snprintf(meta, sizeof(meta), "%s%s%s", device->manufacturer,
                       device->manufacturer[0] && device->model[0] ? " · " : "",
                       device->model);
        lv_obj_t *label = lv_label_create(list);
        lv_label_set_text(label, meta);
        lv_obj_set_width(label, 154);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(label, lv_color_hex(NB_MUTED), 0);
    }

    size_t count = ha_entity_count_for_device(device->id);
    if (count == 0) {
        lv_obj_t *empty = lv_label_create(list);
        lv_label_set_text(empty, "No exposed entities");
        lv_obj_set_style_text_color(empty, lv_color_hex(NB_MUTED), 0);
    } else {
        for (size_t i = 0; i < count; ++i) {
            const ha_entity_t *entity = ha_entity_at_for_device(device->id, i);
            if (entity) add_entity_row(list, entity);
        }
    }
    NB_SCREEN_LOAD(g.detail_screen);
}

static void render_portal_modal(void)
{
    if (!g.settings_screen) return;
    if (g.portal_modal) NB_OBJ_DELETE(g.portal_modal);
    g.portal_modal = lv_obj_create(g.settings_screen);
    lv_obj_set_size(g.portal_modal, 158, 202);
    lv_obj_align(g.portal_modal, LV_ALIGN_CENTER, 0, 12);
    lv_obj_set_style_bg_color(g.portal_modal, lv_color_white(), 0);
    lv_obj_set_style_radius(g.portal_modal, 12, 0);
    lv_obj_set_style_border_color(g.portal_modal, lv_color_hex(NB_BORDER), 0);
    lv_obj_set_style_pad_all(g.portal_modal, 10, 0);
    lv_obj_clear_flag(g.portal_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(g.portal_modal);
    lv_label_set_text(title, "Web Management");
    lv_obj_set_style_text_color(title, lv_color_hex(NB_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    char text[180];
    if (g.portal_running) {
        (void)snprintf(text, sizeof(text), "Connect to:\n%s\n\nOpen:\n%s%s%s",
                       g.portal_ssid[0] ? g.portal_ssid : "NearBy-One",
                       g.portal_address[0] ? g.portal_address : "192.168.4.1",
                       g.portal_pin[0] ? "\n\nPassword: " : "",
                       g.portal_pin);
    } else {
        (void)snprintf(text, sizeof(text), "Web Management is not running.");
    }
    lv_obj_t *body = lv_label_create(g.portal_modal);
    lv_label_set_text(body, text);
    lv_obj_set_width(body, 136);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(body, lv_color_hex(NB_MUTED), 0);
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, 28);

    lv_obj_t *stop = NB_BUTTON_CREATE(g.portal_modal);
    lv_obj_set_size(stop, 116, 32);
    lv_obj_align(stop, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_t *label = lv_label_create(stop);
    lv_label_set_text(label, g.portal_running ? "Stop Portal" : "Close");
    lv_obj_center(label);
    lv_obj_add_event_cb(stop, portal_stop_clicked, LV_EVENT_CLICKED, NULL);
}

static void show_settings(void)
{
    release_detail();
    release_settings();
    g.settings_screen = lv_obj_create(NULL);
    style_screen(g.settings_screen);
    make_top_bar(g.settings_screen, false, "Settings");

    lv_obj_t *content = lv_obj_create(g.settings_screen);
    lv_obj_set_pos(content, 0, NB_TOP_H);
    lv_obj_set_size(content, NB_W, NB_H - NB_TOP_H);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 8, 0);
    lv_obj_set_style_pad_row(content, 7, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *portal = NB_BUTTON_CREATE(content);
    lv_obj_set_size(portal, 154, 42);
    lv_obj_set_style_bg_color(portal, lv_color_hex(NB_BLUE), 0);
    lv_obj_t *portal_label = lv_label_create(portal);
    lv_label_set_text(portal_label, "Web Management");
    lv_obj_center(portal_label);
    lv_obj_add_event_cb(portal, portal_start_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *db_card = make_card(content, 58);
    lv_obj_t *db_title = lv_label_create(db_card);
    lv_label_set_text(db_title, "Recognition database");
    lv_obj_t *db_value = lv_label_create(db_card);
    lv_label_set_text(db_value, g.db_status);
    lv_obj_set_width(db_value, 136);
    lv_label_set_long_mode(db_value, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(db_value, lv_color_hex(NB_MUTED), 0);
    lv_obj_align(db_value, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *fw_card = make_card(content, 58);
    lv_obj_t *fw_title = lv_label_create(fw_card);
    lv_label_set_text(fw_title, "Firmware");
    lv_obj_t *fw_value = lv_label_create(fw_card);
    lv_label_set_text(fw_value, g.firmware);
    lv_obj_set_style_text_color(fw_value, lv_color_hex(NB_MUTED), 0);
    lv_obj_align(fw_value, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    NB_SCREEN_LOAD(g.settings_screen);
}

static void create_input_blocker(void)
{
    if (g.input_blocker) return;
    g.input_blocker = lv_obj_create(lv_layer_top());
    lv_obj_set_pos(g.input_blocker, 0, 0);
    lv_obj_set_size(g.input_blocker, NB_W, NB_H);
    lv_obj_set_style_bg_opa(g.input_blocker, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g.input_blocker, 0, 0);
    lv_obj_set_style_pad_all(g.input_blocker, 0, 0);
    lv_obj_clear_flag(g.input_blocker, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g.input_blocker, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g.input_blocker, LV_OBJ_FLAG_HIDDEN);
}

void nearby_ui_init(const nearby_ui_callbacks_t *callbacks)
{
    memset(&g, 0, sizeof(g));
    if (callbacks) g.cb = *callbacks;
    safe_copy(g.db_status, sizeof(g.db_status), "Not mounted");
    safe_copy(g.firmware, sizeof(g.firmware), "unknown");
    create_home();
    create_input_blocker();
    show_home();
}

nearby_ui_scan_state_t nearby_ui_scan_state(void)
{
    return g.scan_state;
}

void nearby_ui_scan_begin(uint16_t total_modules)
{
    if (g.scan_state == NEARBY_UI_SCAN_SCANNING) return;
    if (total_modules == 0) total_modules = 1;
    if (NB_SCREEN_ACTIVE() != g.home_screen) show_home();
    g.scan_state = NEARBY_UI_SCAN_SCANNING;
    g.scan_total = total_modules;
    g.scan_done = 0;
    lv_bar_set_value(g.progress, 0, LV_ANIM_OFF);
    lv_obj_clear_flag(g.progress, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g.input_blocker, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g.input_blocker);
}

void nearby_ui_scan_module_complete(void)
{
    if (g.scan_state != NEARBY_UI_SCAN_SCANNING) return;
    if (g.scan_done < g.scan_total) ++g.scan_done;
    int value = (int)((100u * g.scan_done) / g.scan_total);
    lv_bar_set_value(g.progress, value, LV_ANIM_OFF);
}

void nearby_ui_scan_end(void)
{
    if (g.scan_state != NEARBY_UI_SCAN_SCANNING) return;
    lv_bar_set_value(g.progress, 100, LV_ANIM_OFF);
    g.scan_state = NEARBY_UI_SCAN_IDLE;
    g.scan_total = 0;
    g.scan_done = 0;
    lv_obj_add_flag(g.input_blocker, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g.progress, LV_OBJ_FLAG_HIDDEN);
    lv_bar_set_value(g.progress, 0, LV_ANIM_OFF);
    rebuild_home_list();
}

void nearby_ui_refresh_from_ha(void)
{
    rebuild_home_list();
}

void nearby_ui_set_versions(const char *database_status, const char *firmware_version)
{
    safe_copy(g.db_status, sizeof(g.db_status), database_status ? database_status : "unknown");
    safe_copy(g.firmware, sizeof(g.firmware), firmware_version ? firmware_version : "unknown");
}

void nearby_ui_set_portal_info(bool running, const char *ssid, const char *address, const char *pin)
{
    g.portal_running = running;
    safe_copy(g.portal_ssid, sizeof(g.portal_ssid), ssid);
    safe_copy(g.portal_address, sizeof(g.portal_address), address);
    safe_copy(g.portal_pin, sizeof(g.portal_pin), pin);
    if (g.portal_modal) render_portal_modal();
}
