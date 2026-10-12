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

void InitializeRetroAchievements() {
    auto& manager = RetroAchievementsManager::Instance();
    if (!manager.Initialize()) {
        return;
    }

    TryDevelopmentTokenLogin(manager);

    // Experimental Phase 0.3 test hook. Supplying a ROM here lets us validate
    // RetroAchievements' N64 hashing behavior before user-facing ROM detection
    // is implemented. This is intentionally not a normal configuration path.
    const char* romPath = std::getenv("SHIPWRIGHT_RA_ROM_PATH");
    if (romPath == nullptr || romPath[0] == '\0') {
        return;
    }

    const auto hash = manager.GenerateN64RomHash(romPath);
    if (!hash.has_value()) {
        spdlog::error("[RetroAchievements] Failed to generate N64 hash for SHIPWRIGHT_RA_ROM_PATH");
        return;
    }

    spdlog::info("[RetroAchievements] N64 ROM hash probe: {}", *hash);
}

static RegisterShipInitFunc registerRetroAchievements(InitializeRetroAchievements);

} // namespace
} // namespace ShipwrightRA
