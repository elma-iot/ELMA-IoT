#pragma once
#if APP_HAS_ONBOARD_PANEL
#include <lvgl.h>
class PanelDisplay;
void drawPanelBootLogo(PanelDisplay& panel);
void animatePanelBootLogo(lv_disp_t* display);
#endif
