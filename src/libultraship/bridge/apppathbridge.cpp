#include "libultraship/bridge/apppathbridge.h"

#include <mutex>
#include <utility>

namespace {
std::mutex sAppShortNameMutex;
std::string sAppShortName;
} // namespace

void AppPathSetShortName(std::string shortName) {
    std::lock_guard<std::mutex> lock(sAppShortNameMutex);
    sAppShortName = std::move(shortName);
}

std::string AppPathGetShortName() {
    std::lock_guard<std::mutex> lock(sAppShortNameMutex);
    return sAppShortName;
}
