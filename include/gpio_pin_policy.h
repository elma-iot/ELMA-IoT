#pragma once

namespace GpioPinPolicy {
enum class Chip { Esp32, S2, S3, C2, C3, C6 };
constexpr bool input(Chip chip, int pin) {
    if (chip == Chip::C2) return pin >= 0 && pin <= 20;
    if (chip == Chip::C3) return (pin >= 0 && pin <= 10) || pin == 20 || pin == 21;
    if (chip == Chip::C6) return pin >= 0 && pin <= 23;
    if (chip == Chip::S2) return (pin >= 0 && pin <= 21) || (pin >= 26 && pin <= 46);
    if (chip == Chip::S3) return (pin >= 0 && pin <= 21) || (pin >= 33 && pin <= 48);
    return (pin >= 0 && pin <= 5) || (pin >= 12 && pin <= 19) ||
           (pin >= 21 && pin <= 23) || (pin >= 25 && pin <= 27) ||
           (pin >= 32 && pin <= 36) || pin == 39;
}
constexpr bool output(Chip chip, int pin) {
    return input(chip, pin) && !(chip == Chip::Esp32 && pin >= 34) && !((chip == Chip::S2 || chip == Chip::S3) && pin == 46);
}
}
