from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    if old not in text:
        if new in text:
            return
        raise SystemExit(f"expected patch context not found: {path}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


# BUG-LCD-004: LVGL produces host-endian RGB565 words in internal RAM while
# the ST7789 SPI pixel stream is MSB-first. Swap each rendered pixel in place
# immediately before the asynchronous panel transfer; LVGL will not reuse the
# draw buffer until the flush-ready callback fires.
replace_once(
    "firmware/main/nearby_lvgl_port.c",
    '#include <stdbool.h>\n#include <stddef.h>\n',
    '#include <stdbool.h>\n#include <stddef.h>\n#include <stdint.h>\n',
)
replace_once(
    "firmware/main/nearby_lvgl_port.c",
    'static bool s_initialized;\n\nstatic bool lcd_color_done',
    '''static bool s_initialized;\n\nstatic void rgb565_swap_bytes_in_place(lv_color_t *pixels, size_t count)\n{\n    _Static_assert(sizeof(lv_color_t) == sizeof(uint16_t),\n                   "LVGL RGB565 pixel must be exactly 16 bits");\n    uint8_t *bytes = (uint8_t *)pixels;\n    for (size_t i = 0; i < count; ++i) {\n        const size_t offset = i * 2u;\n        const uint8_t first = bytes[offset];\n        bytes[offset] = bytes[offset + 1u];\n        bytes[offset + 1u] = first;\n    }\n}\n\nstatic bool lcd_color_done''',
)
replace_once(
    "firmware/main/nearby_lvgl_port.c",
    '''    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel,\n                                               area->x1,\n                                               area->y1,\n                                               area->x2 + 1,\n                                               area->y2 + 1,\n                                               color_map);''',
    '''    const size_t width = (size_t)(area->x2 - area->x1 + 1);\n    const size_t height = (size_t)(area->y2 - area->y1 + 1);\n    rgb565_swap_bytes_in_place(color_map, width * height);\n\n    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel,\n                                               area->x1,\n                                               area->y1,\n                                               area->x2 + 1,\n                                               area->y2 + 1,\n                                               color_map);''',
)

# Extend the existing BENCH_REQUIRED raw panel pattern so physical QA can
# independently validate RGB order, gray direction, and the HA reference blue.
replace_once(
    "firmware/main/agent_d_bench.c",
    '''static uint16_t s_lcd_buffer[NEARBY_BOARD_LCD_H_RES * BENCH_LCD_ROWS];\n\nstatic esp_err_t lcd_draw_rect''',
    '''static uint16_t s_lcd_buffer[NEARBY_BOARD_LCD_H_RES * BENCH_LCD_ROWS];\n\nstatic uint16_t lcd_rgb565_wire_word(uint16_t color)\n{\n    return (uint16_t)((color << 8) | (color >> 8));\n}\n\nstatic esp_err_t lcd_draw_rect''',
)
replace_once(
    "firmware/main/agent_d_bench.c",
    '        for (size_t i = 0; i < pixels; ++i) s_lcd_buffer[i] = color;\n',
    '        for (size_t i = 0; i < pixels; ++i) s_lcd_buffer[i] = lcd_rgb565_wire_word(color);\n',
)
replace_once(
    "firmware/main/agent_d_bench.c",
    '''    static const uint16_t colors[] = {0xf800, 0x07e0, 0x001f, 0xffff, 0x0000};\n    const int band_height = NEARBY_BOARD_LCD_V_RES / (int)(sizeof(colors) / sizeof(colors[0]));''',
    '''    static const uint16_t colors[] = {\n        0xf800, /* red */\n        0x07e0, /* green */\n        0x001f, /* blue */\n        0xffff, /* white */\n        0x0000, /* black */\n        0x8410, /* approximately 50% gray */\n        0x055e, /* NB_BLUE #03A9F4 converted to RGB565 */\n    };\n    ESP_LOGW(TAG, "LCD bands top->bottom: RED GREEN BLUE WHITE BLACK GRAY NB_BLUE(#03A9F4)");\n    const int band_height = NEARBY_BOARD_LCD_V_RES / (int)(sizeof(colors) / sizeof(colors[0]));''',
)

# BUG-UI-003: separate a bounded scrollable credential viewport from the fixed
# action area. This keeps the button from ever covering SSID/IP/password.
old_modal = '''static void render_portal_modal(void)\n{\n    if (!g.settings_screen) return;\n    if (g.portal_modal) NB_OBJ_DELETE(g.portal_modal);\n    g.portal_modal = lv_obj_create(g.settings_screen);\n    lv_obj_set_size(g.portal_modal, 158, 202);\n    lv_obj_align(g.portal_modal, LV_ALIGN_CENTER, 0, 12);\n    lv_obj_set_style_bg_color(g.portal_modal, lv_color_white(), 0);\n    lv_obj_set_style_radius(g.portal_modal, 12, 0);\n    lv_obj_set_style_border_color(g.portal_modal, lv_color_hex(NB_BORDER), 0);\n    lv_obj_set_style_pad_all(g.portal_modal, 10, 0);\n    lv_obj_clear_flag(g.portal_modal, LV_OBJ_FLAG_SCROLLABLE);\n\n    lv_obj_t *title = lv_label_create(g.portal_modal);\n    lv_label_set_text(title, "Web Management");\n    lv_obj_set_style_text_color(title, lv_color_hex(NB_TEXT), 0);\n    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);\n\n    char text[180];\n    if (g.portal_running) {\n        (void)snprintf(text, sizeof(text), "Connect to:\\n%s\\n\\nOpen:\\n%s%s%s",\n                       g.portal_ssid[0] ? g.portal_ssid : "NearBy-One",\n                       g.portal_address[0] ? g.portal_address : "192.168.4.1",\n                       g.portal_pin[0] ? "\\n\\nPassword: " : "",\n                       g.portal_pin);\n    } else {\n        (void)snprintf(text, sizeof(text), "Starting Web Management…");\n    }\n    lv_obj_t *body = lv_label_create(g.portal_modal);\n    lv_label_set_text(body, text);\n    lv_obj_set_width(body, 136);\n    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);\n    lv_obj_set_style_text_color(body, lv_color_hex(NB_MUTED), 0);\n    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, 28);\n\n    lv_obj_t *stop = NB_BUTTON_CREATE(g.portal_modal);\n    lv_obj_set_size(stop, 116, 32);\n    lv_obj_align(stop, LV_ALIGN_BOTTOM_MID, 0, 0);\n    lv_obj_t *label = lv_label_create(stop);\n    lv_label_set_text(label, g.portal_running ? "Stop Portal" : "Close");\n    lv_obj_center(label);\n    lv_obj_add_event_cb(stop, portal_stop_clicked, LV_EVENT_CLICKED, NULL);\n}\n'''
new_modal = '''static void render_portal_modal(void)\n{\n    if (!g.settings_screen) return;\n    if (g.portal_modal) NB_OBJ_DELETE(g.portal_modal);\n    g.portal_modal = lv_obj_create(g.settings_screen);\n    lv_obj_set_size(g.portal_modal, 158, 252);\n    lv_obj_align(g.portal_modal, LV_ALIGN_CENTER, 0, 12);\n    lv_obj_set_style_bg_color(g.portal_modal, lv_color_white(), 0);\n    lv_obj_set_style_radius(g.portal_modal, 12, 0);\n    lv_obj_set_style_border_color(g.portal_modal, lv_color_hex(NB_BORDER), 0);\n    lv_obj_set_style_pad_all(g.portal_modal, 10, 0);\n    lv_obj_clear_flag(g.portal_modal, LV_OBJ_FLAG_SCROLLABLE);\n\n    lv_obj_t *title = lv_label_create(g.portal_modal);\n    lv_label_set_text(title, "Web Management");\n    lv_obj_set_style_text_color(title, lv_color_hex(NB_TEXT), 0);\n    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);\n\n    char text[180];\n    if (g.portal_running) {\n        (void)snprintf(text, sizeof(text), "Connect to:\\n%s\\n\\nOpen:\\n%s%s%s",\n                       g.portal_ssid[0] ? g.portal_ssid : "NearBy-One",\n                       g.portal_address[0] ? g.portal_address : "192.168.4.1",\n                       g.portal_pin[0] ? "\\n\\nPassword: " : "",\n                       g.portal_pin);\n    } else {\n        (void)snprintf(text, sizeof(text), "Starting Web Management…");\n    }\n\n    lv_obj_t *credentials = lv_obj_create(g.portal_modal);\n    lv_obj_set_pos(credentials, 0, 26);\n    lv_obj_set_size(credentials, 136, 154);\n    lv_obj_set_style_bg_opa(credentials, LV_OPA_TRANSP, 0);\n    lv_obj_set_style_border_width(credentials, 0, 0);\n    lv_obj_set_style_radius(credentials, 0, 0);\n    lv_obj_set_style_pad_all(credentials, 0, 0);\n    lv_obj_set_scroll_dir(credentials, LV_DIR_VER);\n    lv_obj_set_scrollbar_mode(credentials, LV_SCROLLBAR_MODE_AUTO);\n    lv_obj_add_flag(credentials, LV_OBJ_FLAG_SCROLLABLE);\n\n    lv_obj_t *body = lv_label_create(credentials);\n    lv_label_set_text(body, text);\n    lv_obj_set_width(body, 132);\n    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);\n    lv_obj_set_style_text_color(body, lv_color_hex(NB_MUTED), 0);\n    lv_obj_set_pos(body, 0, 0);\n\n    lv_obj_t *stop = NB_BUTTON_CREATE(g.portal_modal);\n    lv_obj_set_size(stop, 116, 32);\n    lv_obj_align(stop, LV_ALIGN_BOTTOM_MID, 0, 0);\n    lv_obj_t *label = lv_label_create(stop);\n    lv_label_set_text(label, g.portal_running ? "Stop Portal" : "Close");\n    lv_obj_center(label);\n    lv_obj_add_event_cb(stop, portal_stop_clicked, LV_EVENT_CLICKED, NULL);\n}\n'''
replace_once("components/nearby_ui/nearby_ui.c", old_modal, new_modal)

# BUG-UI-001: selecting Montserrat 14 as LV_FONT_DEFAULT does not itself enable
# the built-in font under LV_CONF_MINIMAL. The built-in font contains LVGL's
# FontAwesome symbol glyphs, so explicitly compile it in.
replace_once(
    "firmware/sdkconfig.defaults",
    'CONFIG_LV_USE_THEME_DEFAULT=y\nCONFIG_LV_FONT_DEFAULT_MONTSERRAT_14=y\n',
    'CONFIG_LV_USE_THEME_DEFAULT=y\nCONFIG_LV_FONT_MONTSERRAT_14=y\nCONFIG_LV_FONT_DEFAULT_MONTSERRAT_14=y\n',
)
