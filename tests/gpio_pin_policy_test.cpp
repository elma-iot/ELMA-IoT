#include "../include/gpio_pin_policy.h"
using namespace GpioPinPolicy;
static_assert(output(Chip::Esp32, 25) && output(Chip::Esp32, 26) && output(Chip::Esp32, 27), "legacy DAC pins");
static_assert(!output(Chip::Esp32, 8) && !output(Chip::Esp32, 10) && !output(Chip::Esp32, 11), "flash pins");
static_assert(!output(Chip::Esp32, 48) && !output(Chip::Esp32, 24), "absent pins");
static_assert(input(Chip::Esp32, 36) && !output(Chip::Esp32, 36), "input only");
static_assert(output(Chip::S3, 8) && output(Chip::S3, 48) && output(Chip::S3, 15), "flexible S3 outputs");
static_assert(!output(Chip::S3, 26) && !output(Chip::S3, 46), "S3 reserved/input only");
static_assert(output(Chip::C3, 8) && !output(Chip::C3, 12), "C3 flash excluded");
int main() {}
