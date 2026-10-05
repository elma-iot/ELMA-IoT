#pragma once

// The VIEWE PCB marking is shared by different fitted LCD panels.
#if defined(BOARD_VIEWE_UEDX32480035E_WB_A) || APP_SUNTON_PANEL == 3
constexpr int kPanelWidth = 320;
constexpr int kPanelHeight = 480;
#else
constexpr int kPanelWidth = 240;
constexpr int kPanelHeight = 320;
#endif

#if APP_SUNTON_PANEL == 1
constexpr const char* kPanelTouchName="XPT2046";
#elif APP_SUNTON_PANEL == 2
constexpr const char* kPanelTouchName="CST820";
#elif APP_SUNTON_PANEL == 3
constexpr const char* kPanelTouchName="GT911";
#else
constexpr const char* kPanelTouchName="CHSC6540";
#endif
