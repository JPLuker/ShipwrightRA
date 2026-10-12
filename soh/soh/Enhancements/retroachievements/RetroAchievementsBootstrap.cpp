#include "RetroAchievementsManager.h"

#include <cstdlib>

#include <spdlog/spdlog.h>

#include "soh/ShipInit.hpp"

namespace ShipwrightRA {
namespace {

void TryDevelopmentTokenLogin(RetroAchievementsManager& manager) {
    const char* username = std::getenv("SHIPWRIGHT_RA_USERNAME");
    const char* token = std::getenv("SHIPWRIGHT_RA_TOKEN");

    if ((username == nullptr || username[0] == '\0') && (token == nullptr || token[0] == '\0')) {
        return;
    }

    if (username == nullptr || username[0] == '\0' || token == nullptr || token[0] == '\0') {
        spdlog::warn("[RetroAchievements] SHIPWRIGHT_RA_USERNAME and SHIPWRIGHT_RA_TOKEN must both be set");
        return;
    }

    // Development-only authentication probe. The token is never logged or
    // persisted; this hook exists solely to validate the HTTP/auth path before
    // the user-facing account UI and secure token storage are implemented.
    manager.BeginLoginWithToken(username, token);
}

void TryDevelopmentGameLoad(RetroAchievementsManager& manager) {
    const char* romPath = std::getenv("SHIPWRIGHT_RA_ROM_PATH");
    if (romPath == nullptr || romPath[0] == '\0') {
        return;
    }

    // Phase 0.3 development probe. rc_client may enter AWAIT_LOGIN while the
    // token request is in flight; once authentication succeeds it can continue
    // resolving the generated ROM hash to the RetroAchievements game set.
    if (!manager.BeginLoadGameFromRomPath(romPath)) {
        const std::string error = manager.GetLastGameError();
        spdlog::error("[RetroAchievements] Failed to start ROM identification{}{}", error.empty() ? "" : ": ", error);
    }
}

void InitializeRetroAchievements() {
    auto& manager = RetroAchievementsManager::Instance();
    if (!manager.Initialize()) {
        return;
    }

    TryDevelopmentTokenLogin(manager);
    TryDevelopmentGameLoad(manager);
}

static RegisterShipInitFunc registerRetroAchievements(InitializeRetroAchievements);

} // namespace
} // namespace ShipwrightRA
