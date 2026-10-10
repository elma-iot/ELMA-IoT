// ST7701S selection: same verified MD80ET bus wiring; LCD sequence is the
// ESP32_Display_Panel ST7701 default and still requires panel-revision testing.
#pragma once
#include "../lib/ESP32_Display_Panel/src/board/supported/viewe/BOARD_VIEWE_UEDX48480021_MD80ET.h"
#undef ESP_PANEL_BOARD_LCD_CONTROLLER
#define ESP_PANEL_BOARD_LCD_CONTROLLER ST7701
#undef ESP_PANEL_BOARD_LCD_VENDOR_INIT_CMD
#undef ESP_PANEL_BOARD_NAME
#define ESP_PANEL_BOARD_NAME "ELMA:MD80ET-ST7701S-experimental"
#define ESP_PANEL_BOARD_CUSTOM_FILE_VERSION_MAJOR 1
#define ESP_PANEL_BOARD_CUSTOM_FILE_VERSION_MINOR 2
#define ESP_PANEL_BOARD_CUSTOM_FILE_VERSION_PATCH 0
