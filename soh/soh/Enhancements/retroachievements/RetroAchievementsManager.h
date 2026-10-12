#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include <rc_client.h>

namespace ShipwrightRA {

enum class RetroAchievementsAuthState {
    LoggedOut,
    Authenticating,
    LoggedIn,
    Failed,
};

struct RetroAchievementsUserInfo {
    std::string username;
    std::string displayName;
    uint32_t score = 0;
    uint32_t scoreSoftcore = 0;
    uint32_t unreadMessages = 0;
};

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
    [[nodiscard]] std::optional<std::string> GenerateN64RomHash(const std::string& romPath) const;

    // Starts authentication through rcheevos. A true return value means the
    // request was accepted; inspect GetAuthState/GetLastAuthError for outcome.
    bool BeginLoginWithPassword(const std::string& username, const std::string& password);
    bool BeginLoginWithToken(const std::string& username, const std::string& token);
    void Logout();

    [[nodiscard]] RetroAchievementsAuthState GetAuthState() const;
    [[nodiscard]] std::string GetLastAuthError() const;
    [[nodiscard]] std::optional<RetroAchievementsUserInfo> GetUserInfo() const;

  private:
    RetroAchievementsManager() = default;
    ~RetroAchievementsManager();

    static uint32_t RC_CCONV ReadMemory(uint32_t address, uint8_t* buffer, uint32_t numBytes, rc_client_t* client);
    static void RC_CCONV ServerCall(const rc_api_request_t* request, rc_client_server_callback_t callback,
                                    void* callbackData, rc_client_t* client);
    static void RC_CCONV LoginCallback(int result, const char* errorMessage, rc_client_t* client, void* userdata);
    static void RC_CCONV LogMessage(const char* message, const rc_client_t* client);

    void SetAuthState(RetroAchievementsAuthState state, std::string error = {});

    rc_client_t* mClient = nullptr;
    mutable std::mutex mAuthMutex;
    RetroAchievementsAuthState mAuthState = RetroAchievementsAuthState::LoggedOut;
    std::string mLastAuthError;
};

} // namespace ShipwrightRA
