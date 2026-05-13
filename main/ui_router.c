#include "ui_router.h"

#include <esp_check.h>
#include <esp_lvgl_port.h>

#include "app_settings.h"
#include "lcd.h"

typedef struct {
    lv_obj_t *tileview;
    lv_obj_t *tiles[APP_SCREEN_COUNT];
    lv_obj_t *route_label;
    lv_obj_t *uptime_label;
    lv_obj_t *brightness_label;
    lv_obj_t *brightness_bar;
    lv_obj_t *region_label;
    app_state_t *state;
} ui_router_view_t;

static ui_router_view_t s_view;

static uint8_t clamp_brightness_value(int32_t brightness_percent)
{
    if (brightness_percent < 10) {
        return 10;
    }

    if (brightness_percent > 100) {
        return 100;
    }

    return (uint8_t)brightness_percent;
}

static void apply_brightness_locked(uint8_t brightness_percent)
{
    app_state_set_brightness(s_view.state, brightness_percent);
    (void)lcd_set_brightness(brightness_percent);
    (void)app_settings_save_brightness(brightness_percent);

    if (s_view.brightness_label != NULL) {
        lv_label_set_text_fmt(s_view.brightness_label, "%u%%", (unsigned int)brightness_percent);
    }

    if (s_view.brightness_bar != NULL) {
        lv_bar_set_value(s_view.brightness_bar, brightness_percent, LV_ANIM_OFF);
    }
}

static lv_obj_t *create_section_card(lv_obj_t *parent, lv_color_t bg_color)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_set_style_bg_color(card, bg_color, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_set_style_pad_row(card, 12, 0);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static void add_tile_header(lv_obj_t *parent, const char *eyebrow, const char *title, const char *body, lv_color_t text_color)
{
    lv_obj_t *eyebrow_label = lv_label_create(parent);
    lv_label_set_text(eyebrow_label, eyebrow);
    lv_obj_set_style_text_color(eyebrow_label, text_color, 0);
    lv_obj_set_style_text_opa(eyebrow_label, LV_OPA_70, 0);

    lv_obj_t *title_label = lv_label_create(parent);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(title_label, text_color, 0);

    lv_obj_t *body_label = lv_label_create(parent);
    lv_label_set_text(body_label, body);
    lv_obj_set_style_text_color(body_label, text_color, 0);
    lv_obj_set_width(body_label, lv_pct(100));
    lv_label_set_long_mode(body_label, LV_LABEL_LONG_WRAP);
}

static void tileview_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_SCROLL_END) {
        return;
    }

    lv_obj_t *active_tile = lv_tileview_get_tile_active(s_view.tileview);
    for (uint32_t index = 0; index < APP_SCREEN_COUNT; index++) {
        if (s_view.tiles[index] == active_tile) {
            app_state_set_active_screen(s_view.state, (app_screen_t)index);
            break;
        }
    }
}

static void brightness_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    int32_t delta = (int32_t)(intptr_t)lv_event_get_user_data(event);
    uint8_t next_brightness = clamp_brightness_value((int32_t)s_view.state->settings.brightness_percent + delta);
    apply_brightness_locked(next_brightness);
}

static void apply_state_locked(const app_state_t *state)
{
    if (s_view.route_label != NULL) {
        lv_label_set_text_fmt(s_view.route_label, "Screen: %s", app_state_get_screen_name(state->active_screen));
    }

    if (s_view.uptime_label != NULL) {
        lv_label_set_text_fmt(s_view.uptime_label, "Uptime %lus", (unsigned long)state->uptime_seconds);
    }

    if (s_view.brightness_label != NULL) {
        lv_label_set_text_fmt(s_view.brightness_label, "%u%%", (unsigned int)state->settings.brightness_percent);
    }

    if (s_view.brightness_bar != NULL) {
        lv_bar_set_value(s_view.brightness_bar, state->settings.brightness_percent, LV_ANIM_OFF);
    }

    if (s_view.region_label != NULL) {
        lv_label_set_text_fmt(s_view.region_label, "Region %s", state->settings.region_code);
    }
}

static void create_primary_tile(lv_obj_t *tile)
{
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x0f3d2e), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(tile, 18, 0);
    lv_obj_set_layout(tile, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tile, 14, 0);

    add_tile_header(
        tile,
        "Greenlight / Foundation",
        "Primary screen shell",
        "This replaces the bring-up demo. The live tariff card lands on this route in phase 4.",
        lv_color_white()
    );

    lv_obj_t *card = create_section_card(tile, lv_color_hex(0x14532d));
    lv_obj_t *status = lv_label_create(card);
    lv_label_set_text(status, "Startup path ready for app state, networking, and tariff refresh modules.");
    lv_obj_set_style_text_color(status, lv_color_white(), 0);
    lv_obj_set_width(status, lv_pct(100));
    lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);

    s_view.route_label = lv_label_create(card);
    lv_obj_set_style_text_color(s_view.route_label, lv_color_hex(0xd1fae5), 0);

    s_view.uptime_label = lv_label_create(card);
    lv_obj_set_style_text_color(s_view.uptime_label, lv_color_hex(0xa7f3d0), 0);
}

static void create_detail_tile(lv_obj_t *tile)
{
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x1f2937), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(tile, 18, 0);
    lv_obj_set_layout(tile, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tile, 14, 0);

    add_tile_header(
        tile,
        "Greenlight / Detail",
        "Detail screen shell",
        "The histogram and daily summaries will attach here once the Octopus tariff pipeline exists.",
        lv_color_white()
    );

    lv_obj_t *card = create_section_card(tile, lv_color_hex(0x374151));
    lv_obj_t *status = lv_label_create(card);
    lv_label_set_text(status, "Current build keeps the route live and swipeable so later phases only need to fill in widgets.");
    lv_obj_set_style_text_color(status, lv_color_hex(0xe5e7eb), 0);
    lv_obj_set_width(status, lv_pct(100));
    lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
}

static void create_settings_tile(lv_obj_t *tile)
{
    lv_obj_set_style_bg_color(tile, lv_color_hex(0xf3efe5), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(tile, 18, 0);
    lv_obj_set_layout(tile, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tile, 14, 0);
    lv_obj_set_scroll_dir(tile, LV_DIR_VER);

    add_tile_header(
        tile,
        "Greenlight / Settings",
        "Settings foundation",
        "Brightness already persists in NVS. Wi-Fi and tariff controls land here later.",
        lv_color_hex(0x111827)
    );

    lv_obj_t *card = create_section_card(tile, lv_color_hex(0xfffbeb));
    lv_obj_set_height(card, 156);
    lv_obj_set_layout(card, LV_LAYOUT_NONE);

    lv_obj_t *brightness_title = lv_label_create(card);
    lv_label_set_text(brightness_title, "Brightness");
    lv_obj_set_style_text_color(brightness_title, lv_color_hex(0x111827), 0);
    lv_obj_align(brightness_title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_view.brightness_label = lv_label_create(card);
    lv_obj_set_style_text_color(s_view.brightness_label, lv_color_hex(0x92400e), 0);
    lv_obj_align(s_view.brightness_label, LV_ALIGN_TOP_LEFT, 118, 0);

    lv_obj_t *minus_button = lv_button_create(card);
    lv_obj_set_size(minus_button, 40, 36);
    lv_obj_set_style_bg_color(minus_button, lv_color_hex(0x111827), 0);
    lv_obj_add_event_cb(minus_button, brightness_button_event_cb, LV_EVENT_CLICKED, (void *)-10);
    lv_obj_align(minus_button, LV_ALIGN_TOP_LEFT, 0, 34);
    lv_obj_t *minus_label = lv_label_create(minus_button);
    lv_label_set_text(minus_label, "-");
    lv_obj_center(minus_label);

    s_view.brightness_bar = lv_bar_create(card);
    lv_obj_set_width(s_view.brightness_bar, 120);
    lv_obj_set_height(s_view.brightness_bar, 18);
    lv_bar_set_range(s_view.brightness_bar, 10, 100);
    lv_obj_set_style_bg_color(s_view.brightness_bar, lv_color_hex(0xd1d5db), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_view.brightness_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_view.brightness_bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_view.brightness_bar, lv_color_hex(0xf59e0b), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_view.brightness_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_view.brightness_bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_align(s_view.brightness_bar, LV_ALIGN_TOP_LEFT, 52, 43);

    lv_obj_t *plus_button = lv_button_create(card);
    lv_obj_set_size(plus_button, 40, 36);
    lv_obj_set_style_bg_color(plus_button, lv_color_hex(0x111827), 0);
    lv_obj_add_event_cb(plus_button, brightness_button_event_cb, LV_EVENT_CLICKED, (void *)10);
    lv_obj_align(plus_button, LV_ALIGN_TOP_LEFT, 184, 34);
    lv_obj_t *plus_label = lv_label_create(plus_button);
    lv_label_set_text(plus_label, "+");
    lv_obj_center(plus_label);

    s_view.region_label = lv_label_create(card);
    lv_obj_set_style_text_color(s_view.region_label, lv_color_hex(0x374151), 0);
    lv_obj_align(s_view.region_label, LV_ALIGN_TOP_LEFT, 0, 78);

    lv_obj_t *hint = lv_label_create(card);
    lv_label_set_text(hint, "Wi-Fi and tariff controls arrive later.");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x6b7280), 0);
    lv_obj_set_width(hint, lv_pct(100));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 0, 96);
}

esp_err_t ui_router_init(app_state_t *state)
{
    if (!lvgl_port_lock(0)) {
        return ESP_ERR_TIMEOUT;
    }

    s_view = (ui_router_view_t){
        .state = state,
    };

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x081018), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    s_view.tileview = lv_tileview_create(screen);
    lv_obj_set_size(s_view.tileview, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(s_view.tileview, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_view.tileview, 0, 0);
    lv_obj_add_event_cb(s_view.tileview, tileview_event_cb, LV_EVENT_SCROLL_END, NULL);

    s_view.tiles[APP_SCREEN_PRIMARY] = lv_tileview_add_tile(s_view.tileview, 0, 0, LV_DIR_HOR);
    s_view.tiles[APP_SCREEN_DETAIL] = lv_tileview_add_tile(s_view.tileview, 1, 0, LV_DIR_HOR);
    s_view.tiles[APP_SCREEN_SETTINGS] = lv_tileview_add_tile(s_view.tileview, 2, 0, LV_DIR_LEFT);

    create_primary_tile(s_view.tiles[APP_SCREEN_PRIMARY]);
    create_detail_tile(s_view.tiles[APP_SCREEN_DETAIL]);
    create_settings_tile(s_view.tiles[APP_SCREEN_SETTINGS]);

    lv_tileview_set_tile_by_index(s_view.tileview, 0, 0, LV_ANIM_OFF);
    apply_state_locked(state);

    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t ui_router_update(const app_state_t *state)
{
    if (!lvgl_port_lock(0)) {
        return ESP_ERR_TIMEOUT;
    }

    apply_state_locked(state);

    lvgl_port_unlock();
    return ESP_OK;
}