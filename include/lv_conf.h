#ifndef LV_CONF_H
#define LV_CONF_H
#include <esp_heap_caps.h>
#define LV_COLOR_DEPTH 16
#if APP_ROTARY_HMI
#define LV_FONT_MONTSERRAT_22 1
#define LV_COLOR_16_SWAP 0
#else
#define LV_COLOR_16_SWAP 1
#endif
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE <esp_heap_caps.h>
#define LV_MEM_CUSTOM_ALLOC(size) heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#define LV_MEM_CUSTOM_FREE heap_caps_free
#define LV_MEM_CUSTOM_REALLOC(ptr,size) heap_caps_realloc(ptr,size,MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#define LV_DISP_DEF_REFR_PERIOD 20
#define LV_INDEV_DEF_READ_PERIOD 10
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_18 1
#if APP_ROTARY_HMI
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#endif
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_BASIC 1
#define LV_USE_LOG 0
#define LV_BUILD_EXAMPLES 0
#define LV_USE_DEMO_WIDGETS 0
#endif // LV_CONF_H
