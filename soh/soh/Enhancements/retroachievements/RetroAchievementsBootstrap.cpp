#include "RetroAchievementsManager.h"

#include <cstdlib>

#include <spdlog/spdlog.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"

namespace ShipwrightRA {
namespace {

bool developmentGameLoadStarted = false;

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

    // Start only with live gameplay state: rcheevos validates memory addresses
    // during game activation and would disable scene-dependent achievements
    // if the set were loaded while gPlayState is unavailable.
    if (!manager.BeginLoadGameFromRomPath(romPath)) {
        const std::string error = manager.GetLastGameError();
        spdlog::error("[RetroAchievements] Failed to start ROM identification{}{}", error.empty() ? "" : ": ", error);
    }
}

void ProcessRetroAchievementsFrame() {
    auto& manager = RetroAchievementsManager::Instance();
    manager.Idle();
    // Title/file-select states also run this hook. Do not evaluate stale save
    // data or invalidate scene-dependent achievements before gameplay starts.
    if (GameInteractor::IsSaveLoaded()) {
        if (!developmentGameLoadStarted && manager.GetAuthState() == RetroAchievementsAuthState::LoggedIn) {
            developmentGameLoadStarted = true;
            TryDevelopmentGameLoad(manager);
        }
        manager.DoFrame();
    }
}

void InitializeRetroAchievements() {
    auto& manager = RetroAchievementsManager::Instance();
    if (!manager.Initialize()) {
        return;
    }

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(ProcessRetroAchievementsFrame);

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnLoadGame>(
        [](int32_t) { RetroAchievementsManager::Instance().ResetRuntime(); });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnExitGame>([](int32_t) {
        RetroAchievementsManager::Instance().UnloadGame();
        developmentGameLoadStarted = false;
    });

    TryDevelopmentTokenLogin(manager);
}

static RegisterShipInitFunc registerRetroAchievements(InitializeRetroAchievements);

} // namespace
} // namespace ShipwrightRA
