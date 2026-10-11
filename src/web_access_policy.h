#pragma once
#include <string>
#include <string_view>
namespace WebAccessPolicy {
inline bool hostname(std::string_view input, std::string& output) {
    output.assign(input);
    for (char& c : output) if (c >= 'A' && c <= 'Z') c += 32;
    if (output.size() > 6 && output.substr(output.size() - 6) == ".local") output.resize(output.size() - 6);
    if (output.empty() || output.size() > 63 || output.front() == '-' || output.back() == '-') return false;
    for (char c : output) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    return true;
}
}
