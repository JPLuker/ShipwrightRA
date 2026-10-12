#pragma once

#include <cstdint>

#include <rc_client.h>

namespace ShipwrightRA {

class RetroAchievementsManager final {
  public:
    static RetroAchievementsManager& Instance();

    RetroAchievementsManager(const RetroAchievementsManager&) = delete;
    RetroAchievementsManager& operator=(const RetroAchievementsManager&) = delete;
    RetroAchievementsManager(RetroAchievementsManager&&) = delete;
    RetroAchievementsManager& operator=(RetroAchievementsManager&&) = delete;

    bool Initialize();
    void Shutdown();

    [[nodiscard]] bool IsInitialized() const;
    [[nodiscard]] rc_client_t* GetClient() const;

  private:
    RetroAchievementsManager() = default;
    ~RetroAchievementsManager();

    static uint32_t RC_CCONV ReadMemory(uint32_t address, uint8_t* buffer, uint32_t numBytes, rc_client_t* client);
    static void RC_CCONV ServerCall(const rc_api_request_t* request, rc_client_server_callback_t callback,
                                    void* callbackData, rc_client_t* client);

    rc_client_t* mClient = nullptr;
};

} // namespace ShipwrightRA
