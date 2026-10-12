#include "RetroAchievementsManager.h"

#include <cstring>

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
        return false;
    }

    // Hardcore defaults to enabled in rc_client. Keep the experimental client
    // permissive until ShipwrightRA can enforce the full Hardcore restriction
    // contract (cheats, save states, speed controls, etc.).
    rc_client_set_hardcore_enabled(mClient, 0);
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
    if (callback != nullptr) {
        const rc_api_server_response_t response = {
            nullptr,
            0,
            RC_API_SERVER_RESPONSE_CLIENT_ERROR,
        };
        callback(&response, callbackData);
    }
}

} // namespace ShipwrightRA
