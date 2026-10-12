#include "RetroAchievementsManager.h"

#include <cstdlib>

#include <spdlog/spdlog.h>

#include "soh/ShipInit.hpp"

namespace ShipwrightRA {
namespace {

void InitializeRetroAchievements() {
    auto& manager = RetroAchievementsManager::Instance();
    if (!manager.Initialize()) {
        return;
    }

    // Experimental Phase 0.3 test hook. Supplying a ROM here lets us validate
    // RetroAchievements' N64 hashing behavior before authentication/networking
    // is implemented. This is intentionally not a user-facing configuration.
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
