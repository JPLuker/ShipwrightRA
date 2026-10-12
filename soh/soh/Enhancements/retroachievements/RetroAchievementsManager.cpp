#include "RetroAchievementsManager.h"

#include <cstring>

#include <rc_hash.h>
#include <spdlog/spdlog.h>

namespace ShipwrightRA {

RetroAchievementsManager& RetroAchievementsManager::Instance() {
    static RetroAchievementsManager instance;
    return instance;
}

RetroAchievementsManager::~RetroAchievementsManager() {
    Shutdown();
}

bool RetroAchievementsManager::Initialize() {
    if (mClient != nullptr) {
        return true;
    }

    mClient = rc_client_create(&RetroAchievementsManager::ReadMemory, &RetroAchievementsManager::ServerCall);
    if (mClient == nullptr) {
        spdlog::error("[RetroAchievements] Failed to create rcheevos client");
        return false;
    }

    rc_client_set_userdata(mClient, this);
    rc_client_enable_logging(mClient, RC_CLIENT_LOG_LEVEL_INFO, &RetroAchievementsManager::LogMessage);

    // Hardcore defaults to enabled in rc_client. Keep the experimental client
    // permissive until ShipwrightRA can enforce the full Hardcore restriction
    // contract (cheats, save states, speed controls, etc.).
    rc_client_set_hardcore_enabled(mClient, 0);

    spdlog::info("[RetroAchievements] rcheevos client initialized (Hardcore disabled during bootstrap)");
    return true;
}

void RetroAchievementsManager::Shutdown() {
    if (mClient == nullptr) {
        return;
    }

    rc_client_destroy(mClient);
    mClient = nullptr;
}

bool RetroAchievementsManager::IsInitialized() const {
    return mClient != nullptr;
}

rc_client_t* RetroAchievementsManager::GetClient() const {
    return mClient;
}

std::optional<std::string> RetroAchievementsManager::GenerateN64RomHash(const std::string& romPath) const {
    rc_hash_iterator_t iterator{};
    rc_hash_initialize_iterator(&iterator, romPath.c_str(), nullptr, 0);

    char hash[33] = {};
    const int generated = rc_hash_generate(hash, RC_CONSOLE_NINTENDO_64, &iterator);

    rc_hash_destroy_iterator(&iterator);

    if (generated == 0) {
        return std::nullopt;
    }

    return std::string(hash);
}

uint32_t RC_CCONV RetroAchievementsManager::ReadMemory(uint32_t address, uint8_t* buffer, uint32_t numBytes,
                                                       rc_client_t* client) {
    (void)address;
    (void)client;

    // Phase 0.1 deliberately exposes no emulated N64 memory. Returning zero
    // bytes tells rcheevos that the requested address is currently invalid.
    if (buffer != nullptr && numBytes > 0) {
        std::memset(buffer, 0, numBytes);
    }
    return 0;
}

void RC_CCONV RetroAchievementsManager::ServerCall(const rc_api_request_t* request,
                                                   rc_client_server_callback_t callback, void* callbackData,
                                                   rc_client_t* client) {
    (void)request;
    (void)client;

    // Networking is intentionally not implemented in the bootstrap phase.
    // Always complete the request instead of leaving rc_client waiting for an
    // asynchronous callback that will never arrive.
    spdlog::warn("[RetroAchievements] Server request rejected because HTTP transport is not configured yet");

    if (callback != nullptr) {
        const rc_api_server_response_t response = {
            nullptr,
            0,
            RC_API_SERVER_RESPONSE_CLIENT_ERROR,
        };
        callback(&response, callbackData);
    }
}

void RC_CCONV RetroAchievementsManager::LogMessage(const char* message, const rc_client_t* client) {
    (void)client;

    if (message != nullptr) {
        spdlog::info("[RetroAchievements] {}", message);
    }
}

} // namespace ShipwrightRA
