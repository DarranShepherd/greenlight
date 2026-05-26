#pragma once

#include <esp_err.h>
#include <esp_lcd_panel_io.h>

#include "board_profile.h"

esp_err_t greenlight_lcd_new_panel_io_qspi(
    const greenlight_display_profile_t *display,
    esp_lcd_panel_io_handle_t *ret_io
);