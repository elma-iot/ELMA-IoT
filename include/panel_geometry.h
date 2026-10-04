#pragma once

// The VIEWE PCB marking is shared by different fitted LCD panels.
#if defined(BOARD_VIEWE_UEDX32480035E_WB_A)
constexpr int kPanelWidth = 320;
constexpr int kPanelHeight = 480;
#else
constexpr int kPanelWidth = 240;
constexpr int kPanelHeight = 320;
#endif
