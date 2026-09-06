from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def main() -> None:
    lvgl = read("firmware/main/nearby_lvgl_port.c")
    ui = read("components/nearby_ui/nearby_ui.c")
    bench = read("firmware/main/agent_d_bench.c")
    sdk = read("firmware/sdkconfig.defaults")

    # BUG-LCD-004: #03A9F4 -> RGB565 0x055e. On little-endian RAM the raw
    # bytes would be 5e 05; ST7789 expects the MSB-first 05 5e stream.
    nb_blue_rgb565 = 0x055E
    swapped = ((nb_blue_rgb565 << 8) | (nb_blue_rgb565 >> 8)) & 0xFFFF
    assert swapped == 0x5E05
    assert "rgb565_swap_bytes_in_place(color_map, width * height);" in lvgl
    assert lvgl.index("rgb565_swap_bytes_in_place(color_map, width * height);") < lvgl.index(
        "esp_lcd_panel_draw_bitmap(s_panel"
    )
    assert "_Static_assert(sizeof(lv_color_t) == sizeof(uint16_t)" in lvgl

    # The BENCH_REQUIRED raw-panel pattern must independently use wire-order
    # RGB565 and include every physical acceptance color from the QA report.
    assert "lcd_rgb565_wire_word(color)" in bench
    for token in ("0xf800", "0x07e0", "0x001f", "0xffff", "0x0000", "0x8410", "0x055e"):
        assert token in bench, token
    assert "NB_BLUE(#03A9F4)" in bench

    # BUG-UI-003: credentials live in a dedicated bounded vertical-scroll
    # viewport; the action button remains a sibling pinned to modal bottom.
    assert "lv_obj_set_size(g.portal_modal, 158, 252);" in ui
    assert "lv_obj_t *credentials = lv_obj_create(g.portal_modal);" in ui
    assert "lv_obj_set_size(credentials, 136, 154);" in ui
    assert "lv_obj_set_scroll_dir(credentials, LV_DIR_VER);" in ui
    assert "lv_obj_set_scrollbar_mode(credentials, LV_SCROLLBAR_MODE_AUTO);" in ui
    assert "lv_obj_t *body = lv_label_create(credentials);" in ui
    assert ui.index("lv_obj_t *body = lv_label_create(credentials);") < ui.index(
        "lv_obj_t *stop = NB_BUTTON_CREATE(g.portal_modal);"
    )

    # BUG-UI-001: selecting a default font is not enough under minimal config;
    # the actual built-in Montserrat font (which owns LVGL symbol glyphs) must
    # be compiled in. The target build additionally checks the link map.
    assert "CONFIG_LV_CONF_MINIMAL=y" in sdk
    assert "CONFIG_LV_FONT_MONTSERRAT_14=y" in sdk
    assert "CONFIG_LV_FONT_DEFAULT_MONTSERRAT_14=y" in sdk

    print("beta.1 bench regression guards: PASS (software-only; physical QA still required)")


if __name__ == "__main__":
    main()
