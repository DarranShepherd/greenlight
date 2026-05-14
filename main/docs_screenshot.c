#include "docs_screenshot.h"

#if CONFIG_GREENLIGHT_DOCS_SCREENSHOT_MODE

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include <esp_check.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <mbedtls/base64.h>
#include <core/lv_obj_draw_private.h>
#include <core/lv_obj_private.h>
#include <core/lv_refr_private.h>
#include <display/lv_display_private.h>
#include <draw/lv_draw_private.h>

#include "app_settings.h"
#include "hardware.h"
#include "tariff_model.h"
#include "ui_router.h"

static const char *TAG = "docs_screenshot";
static const char *DOCS_TZ_POSIX = "GMT0BST,M3.5.0/1,M10.5.0/2";
static const char *DOCS_WIFI_SSID = "Docs Demo Wi-Fi";
static const char *DOCS_WIFI_IP = "192.168.10.24";

#define BASE64_CHUNK_BYTES 48U
#define BASE64_ENCODED_CHUNK_BYTES ((((BASE64_CHUNK_BYTES) + 2U) / 3U) * 4U + 1U)
#define DOCS_SNAPSHOT_WIDTH LCD_V_RES
#define DOCS_SNAPSHOT_SLICE_HEIGHT 40U
#define DOCS_SNAPSHOT_COLOR_FORMAT LV_COLOR_FORMAT_RGB565
#define DOCS_SNAPSHOT_COLOR_FORMAT_NAME "RGB565"

LV_DRAW_BUF_DEFINE_STATIC(s_docs_snapshot_draw_buf, DOCS_SNAPSHOT_WIDTH, DOCS_SNAPSHOT_SLICE_HEIGHT, DOCS_SNAPSHOT_COLOR_FORMAT);

static bool s_docs_snapshot_draw_buf_initialized;

typedef struct {
    const char *name;
    app_screen_t screen;
    tariff_band_t band;
    float current_price;
    time_t current_block_end_local;
    time_t next_block_start_local;
    app_tariff_preview_t previews[APP_TARIFF_PREVIEW_MAX];
    uint8_t preview_count;
} primary_scenario_t;

static void configure_docs_clock(void)
{
    struct tm local_tm = {
        .tm_year = 2026 - 1900,
        .tm_mon = 4,
        .tm_mday = 14,
        .tm_hour = 18,
        .tm_min = 35,
        .tm_sec = 0,
        .tm_isdst = 1,
    };
    struct timeval tv = {0};

    setenv("TZ", DOCS_TZ_POSIX, 1);
    tzset();

    tv.tv_sec = mktime(&local_tm);
    settimeofday(&tv, NULL);
}

static time_t docs_now_local(void)
{
    time_t now_local = 0;

    time(&now_local);
    return now_local;
}

static void fill_day_view(app_tariff_day_view_t *day_view, time_t day_start_local, const float *prices, size_t price_count)
{
    float sum = 0.0f;

    if (day_view == NULL) {
        return;
    }

    memset(day_view, 0, sizeof(*day_view));

    if (prices == NULL || price_count == 0) {
        return;
    }

    if (price_count > APP_TARIFF_DAY_SLOT_MAX) {
        price_count = APP_TARIFF_DAY_SLOT_MAX;
    }

    day_view->available = true;
    day_view->slot_count = (uint8_t)price_count;
    day_view->min_price = prices[0];
    day_view->max_price = prices[0];

    for (size_t index = 0; index < price_count; index++) {
        float price = prices[index];

        day_view->slots[index].valid = true;
        day_view->slots[index].price = price;
        day_view->slots[index].band = tariff_model_classify_price(price);
        sum += price;

        if (price < day_view->min_price) {
            day_view->min_price = price;
        }

        if (price > day_view->max_price) {
            day_view->max_price = price;
        }
    }

    day_view->avg_price = sum / (float)price_count;

    (void)day_start_local;
}

static void apply_base_docs_state(app_state_t *state)
{
    if (state == NULL) {
        return;
    }

    app_state_set_wifi_saved_credentials(state, true);
    app_state_set_wifi_status(state, APP_WIFI_STATUS_CONNECTED, "Connected to Docs Demo Wi-Fi");
    app_state_set_wifi_connection(state, DOCS_WIFI_SSID, DOCS_WIFI_IP);
    app_state_set_time_status(state, APP_TIME_STATUS_VALID, true, "Time synchronized for Europe/London");
    app_state_set_local_time_text(state, "Thu 14 May 18:35");
    app_state_set_tariff_snapshot(
        state,
        "Documentation snapshot mode",
        "Synthetic tariff examples",
        "Curated tariff day views",
        "Updated for docs"
    );
    app_state_set_startup_stage(state, APP_STARTUP_STAGE_COMPLETE, "Startup complete");
}

static void apply_primary_scenario(app_state_t *state, const primary_scenario_t *scenario)
{
    static const float today_prices[APP_TARIFF_DAY_SLOT_MAX] = {
        3.4f, 3.6f, 3.8f, 4.0f, 4.3f, 4.8f, 6.4f, 8.6f,
        10.4f, 11.2f, 12.6f, 14.9f, 16.8f, 17.6f, 18.4f, 19.2f,
        20.3f, 21.4f, 22.0f, 23.2f, 24.1f, 24.9f, 26.8f, 28.1f,
        29.8f, 30.4f, 31.2f, 32.1f, 33.5f, 34.2f, 35.0f, 36.4f,
        38.8f, 39.6f, 41.2f, 42.6f, 43.1f, 39.4f, 34.0f, 28.5f,
        22.1f, 18.5f, 15.0f, 13.2f, 11.4f, 9.8f, 8.0f, 6.2f,
    };
    static const float tomorrow_prices[APP_TARIFF_DAY_SLOT_MAX] = {
        6.2f, 6.0f, 5.8f, 5.6f, 5.5f, 5.4f, 6.0f, 7.2f,
        9.4f, 11.2f, 13.0f, 14.8f, 16.6f, 17.2f, 18.1f, 19.8f,
        21.0f, 22.6f, 23.4f, 24.8f, 26.4f, 28.2f, 29.4f, 30.8f,
        31.6f, 32.5f, 33.2f, 34.0f, 35.8f, 36.2f, 36.6f, 37.1f,
        35.4f, 32.0f, 28.8f, 25.4f, 22.0f, 19.6f, 17.0f, 15.2f,
        13.8f, 12.4f, 11.0f, 10.2f, 9.6f, 8.9f, 8.2f, 7.4f,
    };
    app_tariff_day_view_t today = {0};
    app_tariff_day_view_t tomorrow = {0};
    struct tm local_tm = {0};
    time_t day_start_local = 0;
    time_t now_local = docs_now_local();

    if (state == NULL || scenario == NULL) {
        return;
    }

    localtime_r(&now_local, &local_tm);
    local_tm.tm_hour = 0;
    local_tm.tm_min = 0;
    local_tm.tm_sec = 0;
    day_start_local = mktime(&local_tm);

    fill_day_view(&today, day_start_local, today_prices, APP_TARIFF_DAY_SLOT_MAX);
    fill_day_view(&tomorrow, day_start_local + (24 * 60 * 60), tomorrow_prices, APP_TARIFF_DAY_SLOT_MAX);

    apply_base_docs_state(state);
    app_state_set_tariff_status(state, APP_TARIFF_STATUS_READY, true, true, "Today and tomorrow loaded for region B");
    app_state_set_tariff_primary(
        state,
        true,
        scenario->current_price,
        scenario->band,
        scenario->current_block_end_local,
        scenario->next_block_start_local,
        scenario->previews,
        scenario->preview_count
    );
    app_state_set_tariff_detail(state, &today, &tomorrow);
    app_state_set_active_screen(state, scenario->screen);
}

static void apply_detail_scenario(app_state_t *state)
{
    static const float today_prices[APP_TARIFF_DAY_SLOT_MAX] = {
        2.6f, 2.9f, 3.1f, 3.5f, 4.0f, 4.4f, 5.8f, 7.5f,
        9.2f, 10.8f, 12.2f, 14.1f, 15.8f, 17.0f, 18.6f, 19.8f,
        20.9f, 22.0f, 23.1f, 24.0f, 25.6f, 27.8f, 29.4f, 31.2f,
        33.0f, 34.8f, 36.5f, 37.4f, 38.1f, 39.0f, 40.6f, 42.8f,
        44.0f, 41.8f, 38.0f, 33.6f, 28.4f, 24.2f, 21.0f, 18.8f,
        16.4f, 14.8f, 13.0f, 11.6f, 10.0f, 8.6f, 7.2f, 6.1f,
    };
    static const float tomorrow_prices[APP_TARIFF_DAY_SLOT_MAX] = {
        7.0f, 6.8f, 6.5f, 6.4f, 6.3f, 6.1f, 6.8f, 8.2f,
        10.0f, 12.0f, 13.8f, 15.2f, 16.8f, 18.0f, 19.0f, 20.4f,
        21.8f, 23.0f, 24.2f, 25.0f, 25.8f, 26.4f, 27.2f, 28.6f,
        29.0f, 30.2f, 31.5f, 32.8f, 33.6f, 34.0f, 34.4f, 35.0f,
        33.8f, 31.6f, 28.8f, 25.6f, 22.4f, 19.8f, 17.6f, 16.0f,
        14.8f, 13.6f, 12.2f, 11.6f, 10.8f, 9.8f, 8.8f, 8.0f,
    };
    app_tariff_preview_t previews[APP_TARIFF_PREVIEW_MAX] = {0};
    app_tariff_day_view_t today = {0};
    app_tariff_day_view_t tomorrow = {0};
    struct tm local_tm = {0};
    time_t now_local = docs_now_local();
    time_t day_start_local = 0;

    localtime_r(&now_local, &local_tm);
    local_tm.tm_hour = 0;
    local_tm.tm_min = 0;
    local_tm.tm_sec = 0;
    day_start_local = mktime(&local_tm);

    fill_day_view(&today, day_start_local, today_prices, APP_TARIFF_DAY_SLOT_MAX);
    fill_day_view(&tomorrow, day_start_local + (24 * 60 * 60), tomorrow_prices, APP_TARIFF_DAY_SLOT_MAX);

    previews[0] = (app_tariff_preview_t){
        .valid = true,
        .start_local = now_local + (25 * 60),
        .end_local = now_local + (55 * 60),
        .representative_price = 28.6f,
        .band = TARIFF_BAND_EXPENSIVE,
    };
    previews[1] = (app_tariff_preview_t){
        .valid = true,
        .start_local = now_local + (55 * 60),
        .end_local = now_local + (85 * 60),
        .representative_price = 17.4f,
        .band = TARIFF_BAND_NORMAL,
    };
    previews[2] = (app_tariff_preview_t){
        .valid = true,
        .start_local = now_local + (85 * 60),
        .end_local = now_local + (115 * 60),
        .representative_price = 9.2f,
        .band = TARIFF_BAND_CHEAP,
    };

    apply_base_docs_state(state);
    app_state_set_tariff_status(state, APP_TARIFF_STATUS_READY, true, true, "Today and tomorrow loaded for region B");
    app_state_set_tariff_primary(
        state,
        true,
        24.2f,
        TARIFF_BAND_NORMAL,
        now_local + (25 * 60),
        now_local + (25 * 60),
        previews,
        APP_TARIFF_PREVIEW_MAX
    );
    app_state_set_tariff_detail(state, &today, &tomorrow);
    app_state_set_active_screen(state, APP_SCREEN_DETAIL);
}

static esp_err_t stream_snapshot_begin(const char *name, uint32_t width, uint32_t height, uint32_t stride)
{
    uint32_t total_size = stride * height;

    if (name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    printf(
        "GLSHOT BEGIN %s %u %u %s %u %" PRIu32 "\n",
        name,
        (unsigned int)width,
        (unsigned int)height,
        DOCS_SNAPSHOT_COLOR_FORMAT_NAME,
        (unsigned int)stride,
        total_size
    );
    fflush(stdout);
    return ESP_OK;
}

static esp_err_t stream_snapshot_data(size_t base_offset, const uint8_t *data, size_t data_size)
{
    uint8_t raw_chunk[BASE64_CHUNK_BYTES] = {0};
    unsigned char encoded_chunk[BASE64_ENCODED_CHUNK_BYTES] = {0};
    size_t offset = 0;

    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    while (offset < data_size) {
        size_t chunk_size = data_size - offset;
        size_t encoded_size = 0;

        if (chunk_size > sizeof(raw_chunk)) {
            chunk_size = sizeof(raw_chunk);
        }

        memcpy(raw_chunk, data + offset, chunk_size);
        ESP_RETURN_ON_ERROR(
            mbedtls_base64_encode(encoded_chunk, sizeof(encoded_chunk), &encoded_size, raw_chunk, chunk_size),
            TAG,
            "base64-encode snapshot chunk"
        );
        encoded_chunk[encoded_size] = '\0';
        for (int attempt = 0; attempt < 2; attempt++) {
            printf("GLSHOT DATA %zu %s\n", base_offset + offset, encoded_chunk);
            fflush(stdout);
            vTaskDelay(pdMS_TO_TICKS(2));
        }

        offset += chunk_size;
    }

    return ESP_OK;
}

static esp_err_t stream_snapshot_end(const char *name)
{
    if (name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    printf("GLSHOT END %s\n", name);
    fflush(stdout);
    return ESP_OK;
}

static lv_result_t render_snapshot_slice_locked(lv_obj_t *obj, const lv_area_t *slice_area)
{
    lv_obj_t *top_obj = NULL;
    lv_layer_t layer;
    lv_display_t *disp_old = NULL;
    lv_display_t *disp_new = NULL;
    lv_layer_t *layer_old = NULL;

    LV_ASSERT_NULL(obj);
    LV_ASSERT_NULL(slice_area);

    lv_draw_buf_clear(&s_docs_snapshot_draw_buf, NULL);
    top_obj = lv_refr_get_top_obj(slice_area, obj);
    if (top_obj == NULL) {
        top_obj = obj;
    }

    lv_layer_init(&layer);
    layer.draw_buf = &s_docs_snapshot_draw_buf;
    layer.buf_area = *slice_area;
    layer.color_format = DOCS_SNAPSHOT_COLOR_FORMAT;
    layer._clip_area = *slice_area;
    layer.phy_clip_area = *slice_area;

    lv_draw_unit_send_event(NULL, LV_EVENT_CHILD_CREATED, &layer);

    disp_old = lv_refr_get_disp_refreshing();
    disp_new = lv_obj_get_display(obj);
    layer_old = disp_new->layer_head;
    disp_new->layer_head = &layer;

    lv_refr_set_disp_refreshing(disp_new);

    if (top_obj == obj) {
        lv_obj_redraw(&layer, top_obj);
    }
    else {
        lv_obj_refr(&layer, top_obj);

        lv_obj_t *parent = lv_obj_get_parent(top_obj);
        lv_obj_t *border_p = top_obj;

        while (parent != NULL && border_p != obj) {
            bool go = false;
            uint32_t child_cnt = lv_obj_get_child_count(parent);

            for (uint32_t i = 0; i < child_cnt; i++) {
                lv_obj_t *child = parent->spec_attr->children[i];
                if (!go) {
                    if (child == border_p) {
                        go = true;
                    }
                }
                else {
                    lv_obj_refr(&layer, child);
                }
            }

            lv_obj_send_event(parent, LV_EVENT_DRAW_POST_BEGIN, (void *)&layer);
            lv_obj_send_event(parent, LV_EVENT_DRAW_POST, (void *)&layer);
            lv_obj_send_event(parent, LV_EVENT_DRAW_POST_END, (void *)&layer);

            border_p = parent;
            parent = lv_obj_get_parent(parent);
        }
    }

    layer.all_tasks_added = true;
    while (layer.draw_task_head) {
        lv_draw_dispatch_wait_for_request();
        lv_draw_dispatch();
    }

    disp_new->layer_head = layer_old;
    lv_refr_set_disp_refreshing(disp_old);

    lv_draw_unit_send_event(NULL, LV_EVENT_SCREEN_LOAD_START, &layer);
    lv_draw_unit_send_event(NULL, LV_EVENT_CHILD_DELETED, &layer);

    return LV_RESULT_OK;
}

static esp_err_t capture_screen(const char *name, app_screen_t screen)
{
    lv_obj_t *root = NULL;
    lv_area_t snapshot_area;
    int32_t width = 0;
    int32_t height = 0;
    int32_t ext_size = 0;
    uint32_t stride = 0;

    if (!lvgl_port_lock(0)) {
        return ESP_ERR_TIMEOUT;
    }

    if (!s_docs_snapshot_draw_buf_initialized) {
        LV_DRAW_BUF_INIT_STATIC(s_docs_snapshot_draw_buf);
        s_docs_snapshot_draw_buf_initialized = true;
    }

    root = ui_router_get_screen_root(screen);
    if (root != NULL) {
        lv_obj_update_layout(root);
        lv_refr_now(NULL);
        width = lv_obj_get_width(root);
        height = lv_obj_get_height(root);
        ext_size = lv_obj_get_ext_draw_size(root);
        width += ext_size * 2;
        height += ext_size * 2;
        lv_obj_get_coords(root, &snapshot_area);
        lv_area_increase(&snapshot_area, ext_size, ext_size);
        stride = lv_draw_buf_width_to_stride((uint32_t)width, DOCS_SNAPSHOT_COLOR_FORMAT);
    }

    ESP_RETURN_ON_FALSE(root != NULL && width > 0 && height > 0, ESP_ERR_INVALID_STATE, TAG, "resolve screenshot root");
    ESP_RETURN_ON_ERROR(stream_snapshot_begin(name, (uint32_t)width, (uint32_t)height, stride), TAG, "begin screenshot stream");

    for (int32_t y = 0; y < height; y += DOCS_SNAPSHOT_SLICE_HEIGHT) {
        lv_area_t slice_area = snapshot_area;
        uint32_t slice_height = (uint32_t)(height - y);
        size_t slice_offset = (size_t)y * stride;
        size_t slice_size = 0;

        if (slice_height > DOCS_SNAPSHOT_SLICE_HEIGHT) {
            slice_height = DOCS_SNAPSHOT_SLICE_HEIGHT;
        }

        slice_area.y1 = snapshot_area.y1 + y;
        slice_area.y2 = slice_area.y1 + (lv_coord_t)slice_height - 1;

        ESP_RETURN_ON_FALSE(
            render_snapshot_slice_locked(root, &slice_area) == LV_RESULT_OK,
            ESP_ERR_INVALID_STATE,
            TAG,
            "render LVGL snapshot slice"
        );

        slice_size = (size_t)stride * slice_height;
        ESP_RETURN_ON_ERROR(
            stream_snapshot_data(slice_offset, (const uint8_t *)s_docs_snapshot_draw_buf.data, slice_size),
            TAG,
            "stream snapshot slice"
        );
    }

    lvgl_port_unlock();
    return stream_snapshot_end(name);
}

esp_err_t docs_screenshot_run(app_state_t *state)
{
    time_t now_local = 0;
    primary_scenario_t primary_scenarios[3] = {0};

    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    configure_docs_clock();
    now_local = docs_now_local();

    primary_scenarios[0] = (primary_scenario_t){
        .name = "primary-super-cheap",
        .screen = APP_SCREEN_PRIMARY,
        .band = TARIFF_BAND_SUPER_CHEAP,
        .current_price = 3.8f,
        .current_block_end_local = now_local + (25 * 60),
        .next_block_start_local = now_local + (25 * 60),
        .previews = {
            {
                .valid = true,
                .start_local = now_local + (25 * 60),
                .end_local = now_local + (55 * 60),
                .representative_price = 8.6f,
                .band = TARIFF_BAND_CHEAP,
            },
            {
                .valid = true,
                .start_local = now_local + (55 * 60),
                .end_local = now_local + (85 * 60),
                .representative_price = 19.2f,
                .band = TARIFF_BAND_NORMAL,
            },
            {
                .valid = true,
                .start_local = now_local + (85 * 60),
                .end_local = now_local + (115 * 60),
                .representative_price = 31.4f,
                .band = TARIFF_BAND_EXPENSIVE,
            },
        },
        .preview_count = APP_TARIFF_PREVIEW_MAX,
    };
    primary_scenarios[1] = (primary_scenario_t){
        .name = "primary-normal",
        .screen = APP_SCREEN_PRIMARY,
        .band = TARIFF_BAND_NORMAL,
        .current_price = 21.8f,
        .current_block_end_local = now_local + (25 * 60),
        .next_block_start_local = now_local + (25 * 60),
        .previews = {
            {
                .valid = true,
                .start_local = now_local + (25 * 60),
                .end_local = now_local + (55 * 60),
                .representative_price = 27.4f,
                .band = TARIFF_BAND_EXPENSIVE,
            },
            {
                .valid = true,
                .start_local = now_local + (55 * 60),
                .end_local = now_local + (85 * 60),
                .representative_price = 14.2f,
                .band = TARIFF_BAND_CHEAP,
            },
            {
                .valid = true,
                .start_local = now_local + (85 * 60),
                .end_local = now_local + (115 * 60),
                .representative_price = 4.8f,
                .band = TARIFF_BAND_SUPER_CHEAP,
            },
        },
        .preview_count = APP_TARIFF_PREVIEW_MAX,
    };
    primary_scenarios[2] = (primary_scenario_t){
        .name = "primary-very-expensive",
        .screen = APP_SCREEN_PRIMARY,
        .band = TARIFF_BAND_VERY_EXPENSIVE,
        .current_price = 43.6f,
        .current_block_end_local = now_local + (25 * 60),
        .next_block_start_local = now_local + (25 * 60),
        .previews = {
            {
                .valid = true,
                .start_local = now_local + (25 * 60),
                .end_local = now_local + (55 * 60),
                .representative_price = 36.2f,
                .band = TARIFF_BAND_EXPENSIVE,
            },
            {
                .valid = true,
                .start_local = now_local + (55 * 60),
                .end_local = now_local + (85 * 60),
                .representative_price = 21.0f,
                .band = TARIFF_BAND_NORMAL,
            },
            {
                .valid = true,
                .start_local = now_local + (85 * 60),
                .end_local = now_local + (115 * 60),
                .representative_price = 10.2f,
                .band = TARIFF_BAND_CHEAP,
            },
        },
        .preview_count = APP_TARIFF_PREVIEW_MAX,
    };

    ESP_LOGI(TAG, "Documentation screenshot mode active");

    for (size_t index = 0; index < sizeof(primary_scenarios) / sizeof(primary_scenarios[0]); index++) {
        apply_primary_scenario(state, &primary_scenarios[index]);
        ESP_RETURN_ON_ERROR(ui_router_update(state), TAG, "update primary screenshot scenario");
        ESP_RETURN_ON_ERROR(capture_screen(primary_scenarios[index].name, primary_scenarios[index].screen), TAG, "capture primary screenshot");
        vTaskDelay(pdMS_TO_TICKS(80));
    }

    apply_detail_scenario(state);
    ESP_RETURN_ON_ERROR(ui_router_update(state), TAG, "update detail screenshot scenario");
    ESP_RETURN_ON_ERROR(capture_screen("detail-daily-prices", APP_SCREEN_DETAIL), TAG, "capture detail screenshot");

    ESP_LOGI(TAG, "Documentation screenshot export complete");
    return ESP_OK;
}

#else

esp_err_t docs_screenshot_run(app_state_t *state)
{
    (void)state;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif