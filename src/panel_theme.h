#pragma once
#include <lvgl.h>

// Shared, allocation-light styling for every portrait LCD dashboard.
lv_theme_t* elmaPanelTheme(lv_disp_t* display);

lv_obj_t* elmaSdFormatDialog(lv_disp_t* display,lv_obj_t** format);
