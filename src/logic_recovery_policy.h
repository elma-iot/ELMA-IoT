#pragma once
#include <cstdint>
#include <string>

namespace ElmaLogic {
constexpr uint8_t MaxAutoRetries = 3;
enum class RecoveryAction { None, Retry, Stop };
inline RecoveryAction recoveryAction(bool abnormalReset, bool playing, bool requiresConfirmation, uint8_t retries) {
    if (!abnormalReset || !playing || requiresConfirmation) return RecoveryAction::None;
    return retries < MaxAutoRetries ? RecoveryAction::Retry : RecoveryAction::Stop;
}
inline bool legacyPrematureStop(const std::string& warning, uint8_t retries) {
    if (retries) return false;
    return warning == "Saved Logics were restored stopped after the storage safety upgrade. Review the graph, then explicitly start it."
        || warning.find("Logics was stopped after an abnormal restart while running ") == 0;
}
}
