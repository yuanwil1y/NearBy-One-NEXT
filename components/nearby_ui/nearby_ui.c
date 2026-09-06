#include "nearby_ui.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"

#define NB_W 170
#define NB_H 320
#define NB_TOP_H 34
#define NB_PROGRESS_H 3
#define NB_CONTENT_Y (NB_TOP_H + NB_PROGRESS_H)
#define NB_CARD_H 48
#define NB_MAX_DEVICES 32

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

typedef struct {
    bool used;
    char id[40];
    char name[56];
    char icon[16];
    char manufacturer[40];
    char model[40];
    lv_obj_t *card;
    lv_obj_t *name_label;
} device_slot_t;

typedef struct {
    const char *domain;
    const char *name;
    const char *state;
    const char *unit;
    int value;
} mock_entity_t;

static const mock_entity_t k_mock_entities[] = {
    {"sensor", "Temperature", "22.8", "°C", 0},
    {"binary_sensor", "Door", "Closed", NULL, 0},
    {"switch", "Power", "on", NULL, 1},
    {"light", "Light", "on", NULL, 68},
    {"button", "Identify", "ready", NULL, 0},
};

static struct {
    nearby_ui_callbacks_t cb;
    nearby_ui_scan_state_t scan_state;
    uint16_t scan_total;
    uint16_t scan_done;

    lv_obj_t *home_screen;
    lv_obj_t *home_list;
    lv_obj_t *progress;
    lv_obj_t *scan_button;
    lv_obj_t *input_blocker;

    lv_obj_t *detail_screen;
    lv_obj_t *settings_screen;
    lv_obj_t *portal_modal;
    lv_obj_t *portal_state_label;

    device_slot_t devices[NB_MAX_DEVICES];
    size_t device_count;
    size_t selected_device;

    char db_status[48];
    char firmware[32];
    bool portal_running;
    char portal_ssid[48];
    char portal_address[32];
    char portal_pin[24];

    lv_timer_t *mock_timer;
    uint8_t mock_step;
} g;

static void safe_copy(char *dst, size_t cap, const char *src)
{
    if (cap == 0) return;
    if (!src) src = "";
    snprintf(dst, cap, "%s", src);
}

static void style_screen(lv_obj_t *screen)
{
    lv_obj_set_style_bg_color(screen, lv_color_hex(NB_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *make_top_bar(lv_obj_t *screen, bool home, const char *title);
static void show_home(void);
static void show_settings(void);
static void show_detail(size_t index);
static void rebuild_home_list(void);
static void render_portal_modal(void);

static void release_detail_screen(void)
{
    if (!g.detail_screen) return;
    lv_obj_t *screen = g.detail_screen;
    g.detail_screen = NULL;
    NB_OBJ_DELETE_ASYNC(screen);
}

static void release_settings_screen(void)
{
    if (!g.settings_screen) return;
    lv_obj_t *screen = g.settings_screen;
    g.settings_screen = NULL;
    g.portal_modal = NULL;
    g.portal_state_label = NULL;
    NB_OBJ_DELETE_ASYNC(screen);
}

static void release_transient_screens(void)
{
    release_detail_screen();
    release_settings_screen();
}

static void back_clicked(lv_event_t *event)
{
    (void)event;
    if (g.scan_state == NEARBY_UI_SCAN_SCANNING) return;
    show_home();
}

static void scan_clicked(lv_event_t *event)
{
    (void)event;
    if (g.scan_state == NEARBY_UI_SCAN_SCANNING) return;

    if (g.cb.scan_requested) {
        g.cb.scan_requested(g.cb.ctx);
    } else {
        nearby_ui_mock_start_scan();
    }
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
    if (index < g.device_count && g.devices[index].used) {
        show_detail(index);
    }
}

static void mock_control_clicked(lv_event_t *event)
{
    if (g.scan_state == NEARBY_UI_SCAN_SCANNING) return;
    lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(event);
    if (label) lv_label_set_text(label, "Action sent (mock)");
}

static void portal_stop_clicked(lv_event_t *event)
{
    (void)event;
    if (g.scan_state == NEARBY_UI_SCAN_SCANNING) return;
    if (g.cb.portal_stop_requested) g.cb.portal_stop_requested(g.cb.ctx);
    g.portal_running = false;
    if (g.portal_modal) {
        NB_OBJ_DELETE(g.portal_modal);
        g.portal_modal = NULL;
        g.portal_state_label = NULL;
    }
}

static void portal_start_clicked(lv_event_t *event)
{
    (void)event;
    if (g.scan_state == NEARBY_UI_SCAN_SCANNING) return;
    if (g.cb.portal_start_requested) {
        g.cb.portal_start_requested(g.cb.ctx);
    } else {
        g.portal_running = true;
        safe_copy(g.portal_ssid, sizeof(g.portal_ssid), "NearBy-One-MOCK");
        safe_copy(g.portal_address, sizeof(g.portal_address), "192.168.4.1");
        safe_copy(g.portal_pin, sizeof(g.portal_pin), "12345678");
    }
    render_portal_modal();
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

static lv_obj_t *make_top_bar(lv_obj_t *screen, bool home, const char *title)
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
    if (home) g.scan_button = left;

    lv_obj_t *label = lv_label_create(bar);
    lv_label_set_text(label, title);
    lv_obj_set_width(label, home ? 102 : 124);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_align(label, LV_ALIGN_CENTER, home ? 0 : 13, 0);

    if (home) {
        lv_obj_t *settings = make_icon_button(bar, LV_SYMBOL_SETTINGS);
        lv_obj_set_pos(settings, 138, 2);
        lv_obj_add_event_cb(settings, settings_clicked, LV_EVENT_CLICKED, NULL);
    }
    return bar;
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

static void create_device_card(size_t index)
{
    device_slot_t *slot = &g.devices[index];
    lv_obj_t *card = make_card(g.home_list, NB_CARD_H);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, device_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)index);

    lv_obj_t *icon = lv_label_create(card);
    lv_label_set_text(icon, slot->icon[0] ? slot->icon : LV_SYMBOL_HOME);
    lv_obj_set_style_text_color(icon, lv_color_hex(NB_BLUE), 0);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 1, 0);

    lv_obj_t *name = lv_label_create(card);
    lv_label_set_text(name, slot->name);
    lv_obj_set_width(name, 112);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(name, lv_color_hex(NB_TEXT), 0);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 28, 0);

    slot->card = card;
    slot->name_label = name;
}

static void rebuild_home_list(void)
{
    if (!g.home_list) return;
    lv_obj_clean(g.home_list);
    for (size_t i = 0; i < g.device_count; ++i) {
        if (g.devices[i].used) create_device_card(i);
    }
}

static void create_home_screen(void)
{
    g.home_screen = lv_obj_create(NULL);
    style_screen(g.home_screen);
    make_top_bar(g.home_screen, true, "NearBy One NEXT");

    g.progress = lv_bar_create(g.home_screen);
    lv_obj_set_pos(g.progress, 0, NB_TOP_H);
    lv_obj_set_size(g.progress, NB_W, NB_PROGRESS_H);
    lv_bar_set_range(g.progress, 0, 100);
    lv_bar_set_value(g.progress, 0, LV_ANIM_OFF);
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
    lv_obj_set_scrollbar_mode(g.home_list, LV_SCROLLBAR_MODE_AUTO);

    rebuild_home_list();
}

static void show_home(void)
{
    if (!g.home_screen) create_home_screen();

    /*
     * Load Home first, then asynchronously delete the previous screen. The
     * async delete is safe when this path runs from a Back-button event callback
     * and still releases the transient object tree on the next LVGL handler pass.
     */
    NB_SCREEN_LOAD(g.home_screen);
    release_transient_screens();
}

static lv_obj_t *make_section_title(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(NB_MUTED), 0);
    return label;
}

static void add_entity_row(lv_obj_t *parent, const mock_entity_t *entity)
{
    lv_obj_t *card = make_card(parent, strcmp(entity->domain, "light") == 0 ? 72 : 52);

    lv_obj_t *name = lv_label_create(card);
    lv_label_set_text(name, entity->name);
    lv_obj_set_width(name, 82);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(name, lv_color_hex(NB_TEXT), 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 0);

    if (strcmp(entity->domain, "sensor") == 0) {
        char value[32];
        snprintf(value, sizeof(value), "%s %s", entity->state, entity->unit ? entity->unit : "");
        lv_obj_t *state = lv_label_create(card);
        lv_label_set_text(state, value);
        lv_obj_set_style_text_color(state, lv_color_hex(NB_BLUE), 0);
        lv_obj_align(state, LV_ALIGN_RIGHT_MID, 0, 0);
    } else if (strcmp(entity->domain, "binary_sensor") == 0) {
        lv_obj_t *badge = lv_label_create(card);
        lv_label_set_text(badge, entity->state);
        lv_obj_set_style_text_color(badge, lv_color_hex(NB_OK), 0);
        lv_obj_align(badge, LV_ALIGN_RIGHT_MID, 0, 0);
    } else if (strcmp(entity->domain, "switch") == 0 || strcmp(entity->domain, "light") == 0) {
        lv_obj_t *sw = lv_switch_create(card);
        lv_obj_set_size(sw, 38, 20);
        lv_obj_align(sw, LV_ALIGN_TOP_RIGHT, 0, -1);
        if (strcmp(entity->state, "on") == 0) lv_obj_add_state(sw, LV_STATE_CHECKED);
        if (strcmp(entity->domain, "light") == 0) {
            lv_obj_t *slider = lv_slider_create(card);
            lv_obj_set_size(slider, 136, 10);
            lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, 0);
            lv_slider_set_range(slider, 0, 100);
            lv_slider_set_value(slider, entity->value, LV_ANIM_OFF);
        }
    } else if (strcmp(entity->domain, "button") == 0) {
        lv_obj_t *button = NB_BUTTON_CREATE(card);
        lv_obj_set_size(button, 58, 28);
        lv_obj_align(button, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_t *button_label = lv_label_create(button);
        lv_label_set_text(button_label, "Run");
        lv_obj_center(button_label);

        lv_obj_t *status = lv_label_create(card);
        lv_label_set_text(status, "Ready");
        lv_obj_set_style_text_color(status, lv_color_hex(NB_MUTED), 0);
        lv_obj_align(status, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        lv_obj_add_event_cb(button, mock_control_clicked, LV_EVENT_CLICKED, status);
    }
}

static void show_detail(size_t index)
{
    if (index >= g.device_count || !g.devices[index].used) return;
    g.selected_device = index;

    release_settings_screen();
    release_detail_screen();

    g.detail_screen = lv_obj_create(NULL);
    style_screen(g.detail_screen);
    make_top_bar(g.detail_screen, false, g.devices[index].name);

    lv_obj_t *list = lv_obj_create(g.detail_screen);
    lv_obj_set_pos(list, 0, NB_TOP_H);
    lv_obj_set_size(list, NB_W, NB_H - NB_TOP_H);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 8, 0);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    if (g.devices[index].manufacturer[0] || g.devices[index].model[0]) {
        char meta[96];
        snprintf(meta, sizeof(meta), "%s%s%s",
                 g.devices[index].manufacturer,
                 g.devices[index].manufacturer[0] && g.devices[index].model[0] ? " · " : "",
                 g.devices[index].model);
        lv_obj_t *meta_label = make_section_title(list, meta);
        lv_obj_set_width(meta_label, 154);
        lv_label_set_long_mode(meta_label, LV_LABEL_LONG_DOT);
    }

    for (size_t i = 0; i < sizeof(k_mock_entities) / sizeof(k_mock_entities[0]); ++i) {
        add_entity_row(list, &k_mock_entities[i]);
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
        snprintf(text, sizeof(text),
                 "Connect your phone to:\n%s\n\nThen open:\n%s%s%s",
                 g.portal_ssid[0] ? g.portal_ssid : "NearBy-One-XXXX",
                 g.portal_address[0] ? g.portal_address : "192.168.4.1",
                 g.portal_pin[0] ? "\n\nPIN: " : "",
                 g.portal_pin[0] ? g.portal_pin : "");
    } else {
        snprintf(text, sizeof(text), "Portal start requested.\nWaiting for network layer status…");
    }

    g.portal_state_label = lv_label_create(g.portal_modal);
    lv_label_set_text(g.portal_state_label, text);
    lv_obj_set_width(g.portal_state_label, 136);
    lv_label_set_long_mode(g.portal_state_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(g.portal_state_label, lv_color_hex(NB_MUTED), 0);
    lv_obj_align(g.portal_state_label, LV_ALIGN_TOP_LEFT, 0, 28);

    lv_obj_t *stop = NB_BUTTON_CREATE(g.portal_modal);
    lv_obj_set_size(stop, 116, 32);
    lv_obj_align(stop, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_t *stop_label = lv_label_create(stop);
    lv_label_set_text(stop_label, g.portal_running ? "Stop Portal" : "Close");
    lv_obj_center(stop_label);
    lv_obj_add_event_cb(stop, portal_stop_clicked, LV_EVENT_CLICKED, NULL);
}

static void show_settings(void)
{
    release_detail_screen();
    release_settings_screen();

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
    lv_obj_set_style_radius(portal, 10, 0);
    lv_obj_t *portal_label = lv_label_create(portal);
    lv_label_set_text(portal_label, "Web Management");
    lv_obj_center(portal_label);
    lv_obj_add_event_cb(portal, portal_start_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *db_card = make_card(content, 58);
    lv_obj_t *db_title = lv_label_create(db_card);
    lv_label_set_text(db_title, "Recognition database");
    lv_obj_set_style_text_color(db_title, lv_color_hex(NB_TEXT), 0);
    lv_obj_t *db_value = lv_label_create(db_card);
    lv_label_set_text(db_value, g.db_status);
    lv_obj_set_width(db_value, 136);
    lv_label_set_long_mode(db_value, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(db_value, lv_color_hex(NB_MUTED), 0);
    lv_obj_align(db_value, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *fw_card = make_card(content, 58);
    lv_obj_t *fw_title = lv_label_create(fw_card);
    lv_label_set_text(fw_title, "Firmware");
    lv_obj_set_style_text_color(fw_title, lv_color_hex(NB_TEXT), 0);
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
    g.scan_state = NEARBY_UI_SCAN_IDLE;
    safe_copy(g.db_status, sizeof(g.db_status), "mock-db 0.1 · ready");
    safe_copy(g.firmware, sizeof(g.firmware), "v0.1.0-ui-mock");
    safe_copy(g.portal_ssid, sizeof(g.portal_ssid), "NearBy-One-MOCK");
    safe_copy(g.portal_address, sizeof(g.portal_address), "192.168.4.1");

    create_home_screen();
    create_input_blocker();

    /* Boot is IDLE with an intentionally empty Device list. */
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
    if (g.progress) {
        lv_bar_set_value(g.progress, 0, LV_ANIM_OFF);
        lv_obj_clear_flag(g.progress, LV_OBJ_FLAG_HIDDEN);
    }
    create_input_blocker();
    lv_obj_clear_flag(g.input_blocker, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g.input_blocker);
}

void nearby_ui_scan_module_complete(void)
{
    if (g.scan_state != NEARBY_UI_SCAN_SCANNING) return;
    if (g.scan_done < g.scan_total) ++g.scan_done;
    int value = (int)((100u * g.scan_done) / g.scan_total);
    if (g.progress) lv_bar_set_value(g.progress, value, LV_ANIM_OFF);
}

void nearby_ui_scan_end(void)
{
    if (g.scan_state != NEARBY_UI_SCAN_SCANNING) return;
    if (g.progress) lv_bar_set_value(g.progress, 100, LV_ANIM_OFF);

    g.scan_state = NEARBY_UI_SCAN_IDLE;
    g.scan_total = 0;
    g.scan_done = 0;
    if (g.input_blocker) lv_obj_add_flag(g.input_blocker, LV_OBJ_FLAG_HIDDEN);
    if (g.progress) {
        lv_obj_add_flag(g.progress, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(g.progress, 0, LV_ANIM_OFF);
    }
}

void nearby_ui_device_upsert(const nearby_ui_device_t *device)
{
    if (!device || !device->name) return;

    size_t index = g.device_count;
    if (device->device_id) {
        for (size_t i = 0; i < g.device_count; ++i) {
            if (g.devices[i].used && strcmp(g.devices[i].id, device->device_id) == 0) {
                index = i;
                break;
            }
        }
    }
    if (index == g.device_count) {
        if (g.device_count >= NB_MAX_DEVICES) return;
        ++g.device_count;
    }

    device_slot_t *slot = &g.devices[index];
    slot->used = true;
    safe_copy(slot->id, sizeof(slot->id), device->device_id);
    safe_copy(slot->name, sizeof(slot->name), device->name);
    safe_copy(slot->icon, sizeof(slot->icon), device->icon);
    safe_copy(slot->manufacturer, sizeof(slot->manufacturer), device->manufacturer);
    safe_copy(slot->model, sizeof(slot->model), device->model);

    if (slot->name_label) {
        lv_label_set_text(slot->name_label, slot->name);
    } else if (g.home_list) {
        create_device_card(index);
    }
}

void nearby_ui_clear_devices(void)
{
    memset(g.devices, 0, sizeof(g.devices));
    g.device_count = 0;
    if (g.home_list) lv_obj_clean(g.home_list);
}

void nearby_ui_set_versions(const char *database_status, const char *firmware_version)
{
    safe_copy(g.db_status, sizeof(g.db_status), database_status ? database_status : "unknown");
    safe_copy(g.firmware, sizeof(g.firmware), firmware_version ? firmware_version : "unknown");
}

void nearby_ui_set_portal_info(bool running,
                               const char *ssid,
                               const char *address,
                               const char *pin)
{
    g.portal_running = running;
    safe_copy(g.portal_ssid, sizeof(g.portal_ssid), ssid);
    safe_copy(g.portal_address, sizeof(g.portal_address), address);
    safe_copy(g.portal_pin, sizeof(g.portal_pin), pin);
    if (g.portal_modal) render_portal_modal();
}

void nearby_ui_mock_seed_devices(void)
{
    static const nearby_ui_device_t seed[] = {
        {"mock-desk-lamp", "Desk Lamp", LV_SYMBOL_HOME, "Mock Labs", "Light One"},
        {"mock-door-sensor", "Front Door Sensor", LV_SYMBOL_EYE_OPEN, "Mock Labs", "Contact Mini"},
        {"mock-speaker", "Kitchen Speaker", LV_SYMBOL_AUDIO, "Mock Labs", "Audio One"},
    };
    for (size_t i = 0; i < sizeof(seed) / sizeof(seed[0]); ++i) {
        nearby_ui_device_upsert(&seed[i]);
    }
}

static void mock_timer_cb(lv_timer_t *timer)
{
    static const nearby_ui_device_t scan_results[] = {
        {"mock-air-sensor", "Air Quality Sensor", LV_SYMBOL_EYE_OPEN, "Mock Labs", "Air Mini"},
        {"mock-plug", "Coffee Plug", LV_SYMBOL_POWER, "Mock Labs", "Plug One"},
        {"mock-thermostat", "Hall Thermostat", LV_SYMBOL_HOME, "Mock Labs", "Climate One"},
        {"mock-blind", "Window Blind", LV_SYMBOL_HOME, "Mock Labs", "Blind One"},
    };

    if (g.mock_step < sizeof(scan_results) / sizeof(scan_results[0])) {
        nearby_ui_device_upsert(&scan_results[g.mock_step]);
        nearby_ui_scan_module_complete();
        ++g.mock_step;
        return;
    }

    nearby_ui_scan_end();
    lv_timer_del(timer);
    g.mock_timer = NULL;
}

void nearby_ui_mock_start_scan(void)
{
    if (g.scan_state == NEARBY_UI_SCAN_SCANNING) return;

    /*
     * Each mock scan is a complete fresh pass. Clear the previous pass before
     * SCANNING so results appear only after the user presses Scan and then
     * arrive again as synthetic module-complete events fire.
     */
    nearby_ui_clear_devices();
    g.mock_step = 0;
    nearby_ui_scan_begin(4);

    /* Test harness only: each tick stands in for one backend module-complete event. */
    g.mock_timer = lv_timer_create(mock_timer_cb, 450, NULL);
}
