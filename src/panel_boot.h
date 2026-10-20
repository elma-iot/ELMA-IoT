#pragma once
#if APP_HAS_ONBOARD_PANEL
#include <lvgl.h>
class PanelDisplay;
// Shared dimensions keep the direct LCD splash and LVGL animation identical.
#if APP_ROTARY_HMI
inline constexpr int kPanelBootLogoScale=2;
#else
inline constexpr int kPanelBootLogoScale=1;
#endif
inline constexpr int kPanelBootLogoSize=128*kPanelBootLogoScale;
void drawPanelBootLogo(PanelDisplay& panel);
void animatePanelBootLogo(lv_disp_t* display,bool staticFirst=false);
#endif
