#pragma once
#include <stdint.h>

namespace UpdateScreen {
constexpr uint8_t percent(unsigned value) { return value > 100 ? 100 : value; }
constexpr unsigned arcDegrees(unsigned value) { return percent(value) * 360U / 100U; }
constexpr unsigned dots(uint32_t milliseconds) { return milliseconds / 400U % 4U; }
constexpr uint16_t blue = 0x3ddf; // RGB565 for the clock's #39baff accent.
}
