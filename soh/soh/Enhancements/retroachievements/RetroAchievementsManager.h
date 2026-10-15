#pragma once

#include <cstdint>
#include <mutex>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <rc_client.h>

namespace ShipwrightRA {

class RetroAchievementsHttp;

enum class RetroAchievementsAuthState {
    LoggedOut,
    Authenticating,
    LoggedIn,
    Failed,
};

enum class RetroAchievementsGameState {
    Unloaded,
    Loading,
    Loaded,
    Failed,
};

struct RetroAchievementsUserInfo {
    std::string username;
    std::string displayName;
    uint32_t score = 0;
    uint32_t scoreSoftcore = 0;
    uint32_t unreadMessages = 0;
};

struct RetroAchievementsGameInfo {
    uint32_t id = 0;
    uint32_t consoleId = 0;
    std::string title;
    std::string hash;
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

    // Phase 0.3 game-identification probe. Hashes the supplied N64 ROM locally,
    // then asks rc_client to resolve and load the matching RetroAchievements game.
    bool BeginLoadGameFromRomPath(const std::string& romPath);
    void UnloadGame();

    [[nodiscard]] RetroAchievementsGameState GetGameState() const;
    [[nodiscard]] std::string GetLastGameError() const;
    [[nodiscard]] std::optional<RetroAchievementsGameInfo> GetGameInfo() const;

    // Processes one logical Shipwright game update through the rcheevos runtime.
    // Does nothing until a RetroAchievements game is fully loaded.
    void DoFrame();
    void Idle();
    void ResetRuntime();

    // Session-only test controls; never persist credentials or ROM paths.
    void SetRomPath(std::string path);
    [[nodiscard]] const std::string& GetRomPath() const;
    void ProcessGameFrame(bool gameplayActive);
    bool SetTestMode(bool enabled);
    [[nodiscard]] bool IsTestMode() const;
    [[nodiscard]] bool IsGameplayActive() const;
    [[nodiscard]] uint64_t GetEvaluatedFrames() const;
    [[nodiscard]] const std::vector<std::string>& GetRecentEvents() const;
    [[nodiscard]] std::string GetDiagnosticReport() const;

  private:
    RetroAchievementsManager() = default;
    ~RetroAchievementsManager();

    static uint32_t RC_CCONV ReadMemory(uint32_t address, uint8_t* buffer, uint32_t numBytes, rc_client_t* client);
    static void RC_CCONV ServerCall(const rc_api_request_t* request, rc_client_server_callback_t callback,
                                    void* callbackData, rc_client_t* client);
    static void RC_CCONV LoginCallback(int result, const char* errorMessage, rc_client_t* client, void* userdata);
    static void RC_CCONV LoadGameCallback(int result, const char* errorMessage, rc_client_t* client, void* userdata);
    static void RC_CCONV EventHandler(const rc_client_event_t* event, rc_client_t* client);
    static void RC_CCONV LogMessage(const char* message, const rc_client_t* client);

    void SetAuthState(RetroAchievementsAuthState state, std::string error = {});
    void SetGameState(RetroAchievementsGameState state, std::string error = {});

    void RecordEvent(std::string text);
    rc_client_t* mClient = nullptr;
    std::string mRomPath;
    bool mTestMode = true;
    bool mGameplayActive = false;
    uint64_t mEvaluatedFrames = 0;
    std::vector<std::pair<uint32_t, uint64_t>> mUnmappedAddresses;
    std::vector<std::string> mRecentEvents;
    std::unique_ptr<RetroAchievementsHttp> mHttp;

    mutable std::mutex mAuthMutex;
    RetroAchievementsAuthState mAuthState = RetroAchievementsAuthState::LoggedOut;
    std::string mLastAuthError;

    mutable std::mutex mGameMutex;
    RetroAchievementsGameState mGameState = RetroAchievementsGameState::Unloaded;
    std::string mLastGameError;
};

} // namespace ShipwrightRA
