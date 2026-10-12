#pragma once

#include <memory>
#include <rc_api_request.h>
#include <rc_client.h>

namespace ShipwrightRA {

// Owned and pumped by the game thread. Callbacks never run on a worker thread.
class RetroAchievementsHttp final {
  public:
    RetroAchievementsHttp();
    ~RetroAchievementsHttp();
    RetroAchievementsHttp(const RetroAchievementsHttp&) = delete;
    RetroAchievementsHttp& operator=(const RetroAchievementsHttp&) = delete;

    bool Initialize();
    void Enqueue(const rc_api_request_t* request, rc_client_server_callback_t callback, void* callbackData);
    void Pump();
    // Completes queued callbacks with a client error while rc_client still exists.
    void Shutdown();

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace ShipwrightRA
