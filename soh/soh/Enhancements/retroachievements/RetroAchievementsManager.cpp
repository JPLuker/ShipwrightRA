#include "RetroAchievementsManager.h"

#include "N64MemoryAdapter.h"
#include "RetroAchievementsHttp.h"

#include <string>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

#include <rc_hash.h>
#include <spdlog/spdlog.h>

namespace ShipwrightRA {
namespace {

std::string ResultErrorMessage(int result, const char* errorMessage, const char* fallbackMessage) {
    if (errorMessage != nullptr && errorMessage[0] != '\0') {
        return errorMessage;
    }

    const char* fallback = rc_error_str(result);
    if (fallback != nullptr && fallback[0] != '\0') {
        return fallback;
    }

    return fallbackMessage;
}

} // namespace

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

    mHttp = std::make_unique<RetroAchievementsHttp>();
    if (!mHttp->Initialize()) {
        spdlog::error("[RetroAchievements] Failed to initialize HTTP transport");
        mHttp.reset();
        return false;
    }

    mClient = rc_client_create(&RetroAchievementsManager::ReadMemory, &RetroAchievementsManager::ServerCall);
    if (mClient == nullptr) {
        mHttp.reset();
        spdlog::error("[RetroAchievements] Failed to create rcheevos client");
        return false;
    }

    rc_client_set_userdata(mClient, this);
    rc_client_enable_logging(mClient, RC_CLIENT_LOG_LEVEL_INFO, &RetroAchievementsManager::LogMessage);
    rc_client_set_event_handler(mClient, &RetroAchievementsManager::EventHandler);

    // Hardcore defaults to enabled in rc_client. Keep the experimental client
    // permissive until ShipwrightRA can enforce the full Hardcore restriction
    // contract (cheats, save states, speed controls, etc.).
    rc_client_set_hardcore_enabled(mClient, 0);
    rc_client_set_spectator_mode_enabled(mClient, mTestMode ? 1 : 0);

    SetAuthState(RetroAchievementsAuthState::LoggedOut);
    SetGameState(RetroAchievementsGameState::Unloaded);
    spdlog::info("[RetroAchievements] rcheevos client initialized (Hardcore disabled during bootstrap)");
    return true;
}

void RetroAchievementsManager::Shutdown() {
    if (mHttp != nullptr) {
        mHttp->Shutdown();
        mHttp.reset();
    }
    if (mClient != nullptr) {
        rc_client_destroy(mClient);
        mClient = nullptr;
    }

    SetGameState(RetroAchievementsGameState::Unloaded);
    SetAuthState(RetroAchievementsAuthState::LoggedOut);
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

bool RetroAchievementsManager::BeginLoginWithPassword(const std::string& username, const std::string& password) {
    if (mClient == nullptr) {
        SetAuthState(RetroAchievementsAuthState::Failed, "RetroAchievements client is not initialized");
        return false;
    }
    if (GetAuthState() == RetroAchievementsAuthState::Authenticating) {
        return false;
    }
    if (username.empty() || password.empty()) {
        SetAuthState(RetroAchievementsAuthState::Failed, "Username and password are required");
        return false;
    }

    SetAuthState(RetroAchievementsAuthState::Authenticating);
    const rc_client_async_handle_t* handle = rc_client_begin_login_with_password(
        mClient, username.c_str(), password.c_str(), &RetroAchievementsManager::LoginCallback, this);
    if (handle == nullptr) {
        if (GetAuthState() == RetroAchievementsAuthState::Authenticating) {
            SetAuthState(RetroAchievementsAuthState::Failed, "Failed to start RetroAchievements login");
        }
        return GetAuthState() == RetroAchievementsAuthState::LoggedIn;
    }

    return true;
}

bool RetroAchievementsManager::BeginLoginWithToken(const std::string& username, const std::string& token) {
    if (mClient == nullptr) {
        SetAuthState(RetroAchievementsAuthState::Failed, "RetroAchievements client is not initialized");
        return false;
    }
    if (GetAuthState() == RetroAchievementsAuthState::Authenticating) {
        return false;
    }
    if (username.empty() || token.empty()) {
        SetAuthState(RetroAchievementsAuthState::Failed, "Username and token are required");
        return false;
    }

    SetAuthState(RetroAchievementsAuthState::Authenticating);
    const rc_client_async_handle_t* handle = rc_client_begin_login_with_token(
        mClient, username.c_str(), token.c_str(), &RetroAchievementsManager::LoginCallback, this);
    if (handle == nullptr) {
        if (GetAuthState() == RetroAchievementsAuthState::Authenticating) {
            SetAuthState(RetroAchievementsAuthState::Failed, "Failed to start RetroAchievements token login");
        }
        return GetAuthState() == RetroAchievementsAuthState::LoggedIn;
    }

    return true;
}

void RetroAchievementsManager::Logout() {
    if (mClient != nullptr) {
        rc_client_logout(mClient);
    }
    // Finish canceled callbacks before another login can begin. Otherwise a
    // late response from the previous account can overwrite the new session.
    if (mHttp != nullptr) {
        mHttp->Shutdown();
        mHttp->Initialize();
    }
    SetGameState(RetroAchievementsGameState::Unloaded);
    SetAuthState(RetroAchievementsAuthState::LoggedOut);
    spdlog::info("[RetroAchievements] Logged out");
}

RetroAchievementsAuthState RetroAchievementsManager::GetAuthState() const {
    std::lock_guard<std::mutex> lock(mAuthMutex);
    return mAuthState;
}

std::string RetroAchievementsManager::GetLastAuthError() const {
    std::lock_guard<std::mutex> lock(mAuthMutex);
    return mLastAuthError;
}

std::optional<RetroAchievementsUserInfo> RetroAchievementsManager::GetUserInfo() const {
    if (mClient == nullptr) {
        return std::nullopt;
    }

    const rc_client_user_t* user = rc_client_get_user_info(mClient);
    if (user == nullptr) {
        return std::nullopt;
    }

    RetroAchievementsUserInfo info;
    if (user->username != nullptr) {
        info.username = user->username;
    }
    if (user->display_name != nullptr) {
        info.displayName = user->display_name;
    }
    info.score = user->score;
    info.scoreSoftcore = user->score_softcore;
    info.unreadMessages = user->num_unread_messages;
    return info;
}

bool RetroAchievementsManager::BeginLoadGameFromRomPath(const std::string& romPath) {
    if (mClient == nullptr) {
        SetGameState(RetroAchievementsGameState::Failed, "RetroAchievements client is not initialized");
        return false;
    }
    if (romPath.empty()) {
        SetGameState(RetroAchievementsGameState::Failed, "ROM path is required");
        return false;
    }

    const auto hash = GenerateN64RomHash(romPath);
    if (!hash.has_value()) {
        SetGameState(RetroAchievementsGameState::Failed, "Failed to generate RetroAchievements N64 ROM hash");
        return false;
    }

    mEvaluatedFrames = 0;
    mUnmappedAddresses.clear();
    mRecentEvents.clear();
    spdlog::info("[RetroAchievements] Loading N64 game for hash {}", *hash);
    SetGameState(RetroAchievementsGameState::Loading);

    const rc_client_async_handle_t* handle =
        rc_client_begin_load_game(mClient, hash->c_str(), &RetroAchievementsManager::LoadGameCallback, this);
    if (handle == nullptr) {
        if (GetGameState() == RetroAchievementsGameState::Loading) {
            SetGameState(RetroAchievementsGameState::Failed, "Failed to start RetroAchievements game load");
        }
        return GetGameState() == RetroAchievementsGameState::Loaded;
    }

    return true;
}

void RetroAchievementsManager::UnloadGame() {
    if (mClient != nullptr) {
        rc_client_unload_game(mClient);
    }
    SetGameState(RetroAchievementsGameState::Unloaded);
}

RetroAchievementsGameState RetroAchievementsManager::GetGameState() const {
    std::lock_guard<std::mutex> lock(mGameMutex);
    return mGameState;
}

std::string RetroAchievementsManager::GetLastGameError() const {
    std::lock_guard<std::mutex> lock(mGameMutex);
    return mLastGameError;
}

std::optional<RetroAchievementsGameInfo> RetroAchievementsManager::GetGameInfo() const {
    if (mClient == nullptr || !rc_client_is_game_loaded(mClient)) {
        return std::nullopt;
    }

    const rc_client_game_t* game = rc_client_get_game_info(mClient);
    if (game == nullptr) {
        return std::nullopt;
    }

    RetroAchievementsGameInfo info;
    info.id = game->id;
    info.consoleId = game->console_id;
    if (game->title != nullptr) {
        info.title = game->title;
    }
    if (game->hash != nullptr) {
        info.hash = game->hash;
    }
    return info;
}

void RetroAchievementsManager::SetRomPath(std::string path) {
    UnloadGame();
    mRomPath = std::move(path);
}

const std::string& RetroAchievementsManager::GetRomPath() const {
    return mRomPath;
}

void RetroAchievementsManager::ProcessGameFrame(bool gameplayActive) {
    mGameplayActive = gameplayActive;
    // Activation validates memory while processing HTTP responses. If gameplay
    // disappears during a load, cancel before pumping those responses.
    if (!gameplayActive && GetGameState() == RetroAchievementsGameState::Loading) {
        UnloadGame();
    }
    Idle();
    if (gameplayActive) {
        if (GetAuthState() == RetroAchievementsAuthState::LoggedIn &&
            GetGameState() == RetroAchievementsGameState::Unloaded && !mRomPath.empty()) {
            BeginLoadGameFromRomPath(mRomPath);
        }
        DoFrame();
    }
}

bool RetroAchievementsManager::SetTestMode(bool enabled) {
    const auto state = GetGameState();
    if (state == RetroAchievementsGameState::Loading || state == RetroAchievementsGameState::Loaded) {
        return false;
    }
    mTestMode = enabled;
    if (mClient != nullptr) {
        rc_client_set_spectator_mode_enabled(mClient, enabled ? 1 : 0);
    }
    return true;
}

bool RetroAchievementsManager::IsTestMode() const {
    return mTestMode;
}
bool RetroAchievementsManager::IsGameplayActive() const {
    return mGameplayActive;
}
uint64_t RetroAchievementsManager::GetEvaluatedFrames() const {
    return mEvaluatedFrames;
}
const std::vector<std::string>& RetroAchievementsManager::GetRecentEvents() const {
    return mRecentEvents;
}

void RetroAchievementsManager::RecordEvent(std::string text) {
    if (mRecentEvents.size() == 32) {
        mRecentEvents.erase(mRecentEvents.begin());
    }
    mRecentEvents.push_back(std::move(text));
}

std::string RetroAchievementsManager::GetDiagnosticReport() const {
    std::ostringstream report;
    report << "ShipwrightRA gameplay test\nMode: " << (mTestMode ? "Test (no submissions)" : "Softcore submissions")
           << "\nHardcore: disabled\nGameplay active: " << (mGameplayActive ? "yes" : "no")
           << "\nEvaluated frames: " << mEvaluatedFrames << "\n";
    const auto game = GetGameInfo();
    if (game) {
        report << "Game: " << game->title << " (" << game->id << ")\nROM hash: " << game->hash << "\n";
        auto* list = rc_client_create_achievement_list(mClient, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,
                                                       RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
        if (list != nullptr) {
            for (uint32_t i = 0; i < list->num_buckets; ++i) {
                const auto& bucket = list->buckets[i];
                report << (bucket.label != nullptr ? bucket.label : "Achievements") << ": " << bucket.num_achievements
                       << "\n";
                if (bucket.bucket_type == RC_CLIENT_ACHIEVEMENT_BUCKET_UNSUPPORTED) {
                    for (uint32_t j = 0; j < bucket.num_achievements; ++j) {
                        const auto* achievement = bucket.achievements[j];
                        report << "  " << achievement->id << " "
                               << (achievement->title != nullptr ? achievement->title : "Unknown") << "\n";
                    }
                }
            }
            rc_client_destroy_achievement_list(list);
        }
    }
    report << "Unmapped RA addresses (first 64 unique):";
    for (const uint32_t address : mUnmappedAddresses) {
        report << " 0x" << std::hex << std::setw(6) << std::setfill('0') << address;
    }
    report << "\nRecent events:\n";
    for (const auto& event : mRecentEvents) {
        report << event << "\n";
    }
    return report.str();
}

void RetroAchievementsManager::Idle() {
    if (mClient == nullptr) {
        return;
    }
    mHttp->Pump();
    rc_client_idle(mClient);
}

void RetroAchievementsManager::ResetRuntime() {
    if (mClient != nullptr) {
        rc_client_reset(mClient);
    }
}

void RetroAchievementsManager::DoFrame() {
    if (mClient == nullptr || !rc_client_is_game_loaded(mClient)) {
        return;
    }

    rc_client_do_frame(mClient);
    ++mEvaluatedFrames;
}

uint32_t RC_CCONV RetroAchievementsManager::ReadMemory(uint32_t address, uint8_t* buffer, uint32_t numBytes,
                                                       rc_client_t* client) {
    const uint32_t count = N64MemoryAdapter::Read(address, buffer, numBytes);
    if (count != numBytes && client != nullptr) {
        auto* manager = static_cast<RetroAchievementsManager*>(rc_client_get_userdata(client));
        if (manager != nullptr && manager->mUnmappedAddresses.size() < 64) {
            const uint32_t failedAddress = address + count;
            auto& addresses = manager->mUnmappedAddresses;
            if (std::find(addresses.begin(), addresses.end(), failedAddress) == addresses.end()) {
                addresses.push_back(failedAddress);
            }
        }
    }
    return count;
}

void RC_CCONV RetroAchievementsManager::ServerCall(const rc_api_request_t* request,
                                                   rc_client_server_callback_t callback, void* callbackData,
                                                   rc_client_t* client) {
    if (callback == nullptr) {
        return;
    }
    auto* manager =
        client != nullptr ? static_cast<RetroAchievementsManager*>(rc_client_get_userdata(client)) : nullptr;
    if (manager == nullptr || manager->mHttp == nullptr) {
        const rc_api_server_response_t response{ nullptr, 0, RC_API_SERVER_RESPONSE_CLIENT_ERROR };
        callback(&response, callbackData);
        return;
    }
    manager->mHttp->Enqueue(request, callback, callbackData);
}

void RC_CCONV RetroAchievementsManager::LoginCallback(int result, const char* errorMessage, rc_client_t* client,
                                                      void* userdata) {
    auto* manager = static_cast<RetroAchievementsManager*>(userdata);
    if (manager == nullptr || manager->GetAuthState() != RetroAchievementsAuthState::Authenticating) {
        return;
    }

    if (result != RC_OK) {
        const std::string error =
            ResultErrorMessage(result, errorMessage, "Unknown RetroAchievements authentication error");
        manager->SetAuthState(RetroAchievementsAuthState::Failed, error);
        spdlog::warn("[RetroAchievements] Login failed: {}", error);
        return;
    }

    manager->SetAuthState(RetroAchievementsAuthState::LoggedIn);

    const rc_client_user_t* user = client != nullptr ? rc_client_get_user_info(client) : nullptr;
    if (user != nullptr && user->username != nullptr) {
        spdlog::info("[RetroAchievements] Logged in as {}", user->username);
    } else {
        spdlog::info("[RetroAchievements] Login succeeded");
    }
}

void RC_CCONV RetroAchievementsManager::LoadGameCallback(int result, const char* errorMessage, rc_client_t* client,
                                                         void* userdata) {
    auto* manager = static_cast<RetroAchievementsManager*>(userdata);
    if (manager == nullptr || result == RC_ABORTED) {
        return;
    }

    if (result != RC_OK) {
        const std::string error = ResultErrorMessage(result, errorMessage, "Unknown RetroAchievements game-load error");
        manager->SetGameState(RetroAchievementsGameState::Failed, error);
        spdlog::warn("[RetroAchievements] Game identification/load failed: {}", error);
        return;
    }

    const rc_client_game_t* game = client != nullptr ? rc_client_get_game_info(client) : nullptr;
    if (game == nullptr || game->id == 0) {
        manager->SetGameState(RetroAchievementsGameState::Failed,
                              "RetroAchievements returned no identified game for the supplied ROM hash");
        spdlog::warn("[RetroAchievements] Game load completed without an identified game");
        return;
    }

    manager->SetGameState(RetroAchievementsGameState::Loaded);
    spdlog::info("[RetroAchievements] Identified game {} ({}) [hash {}]",
                 game->title != nullptr ? game->title : "Unknown", game->id,
                 game->hash != nullptr ? game->hash : "unknown");
}

void RC_CCONV RetroAchievementsManager::EventHandler(const rc_client_event_t* event, rc_client_t* client) {
    auto* manager =
        client != nullptr ? static_cast<RetroAchievementsManager*>(rc_client_get_userdata(client)) : nullptr;

    if (event == nullptr) {
        return;
    }

    switch (event->type) {
        case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
            if (event->achievement != nullptr) {
                if (manager != nullptr) {
                    manager->RecordEvent(
                        std::string(manager->mTestMode ? "Test trigger: " : "Unlock trigger: ") +
                        std::to_string(event->achievement->id) + " - " +
                        (event->achievement->title != nullptr ? event->achievement->title : "Unknown"));
                }
                spdlog::info("[RetroAchievements] {}: {} ({}) - {} points",
                             manager != nullptr && manager->IsTestMode() ? "Test trigger" : "Unlock trigger",
                             event->achievement->title != nullptr ? event->achievement->title : "Unknown",
                             event->achievement->id, event->achievement->points);
            } else {
                spdlog::info("[RetroAchievements] Achievement unlocked");
            }
            break;
        case RC_CLIENT_EVENT_GAME_COMPLETED:
            spdlog::info("[RetroAchievements] Game completed");
            break;
        case RC_CLIENT_EVENT_SUBSET_COMPLETED:
            if (event->subset != nullptr) {
                spdlog::info("[RetroAchievements] Achievement subset completed: {}",
                             event->subset->title != nullptr ? event->subset->title : "Unknown");
            }
            break;
        case RC_CLIENT_EVENT_SERVER_ERROR:
            if (manager != nullptr) {
                manager->RecordEvent("Server error: inspect the application log for details");
            }
            if (event->server_error != nullptr) {
                spdlog::warn("[RetroAchievements] Server error in {}: {}",
                             event->server_error->api != nullptr ? event->server_error->api : "unknown API",
                             event->server_error->error_message != nullptr ? event->server_error->error_message
                                                                           : "unknown error");
            } else {
                spdlog::warn("[RetroAchievements] Server error");
            }
            break;
        case RC_CLIENT_EVENT_DISCONNECTED:
            if (manager != nullptr) {
                manager->RecordEvent("Disconnected: submissions pending");
            }
            spdlog::warn("[RetroAchievements] Disconnected; unlock submissions are pending");
            break;
        case RC_CLIENT_EVENT_RECONNECTED:
            if (manager != nullptr) {
                manager->RecordEvent("Reconnected: pending submissions completed");
            }
            spdlog::info("[RetroAchievements] Reconnected; pending submissions completed");
            break;
        default:
            break;
    }
}

void RC_CCONV RetroAchievementsManager::LogMessage(const char* message, const rc_client_t* client) {
    (void)client;

    if (message != nullptr) {
        spdlog::info("[RetroAchievements] {}", message);
    }
}

void RetroAchievementsManager::SetAuthState(RetroAchievementsAuthState state, std::string error) {
    std::lock_guard<std::mutex> lock(mAuthMutex);
    mAuthState = state;
    mLastAuthError = std::move(error);
}

void RetroAchievementsManager::SetGameState(RetroAchievementsGameState state, std::string error) {
    std::lock_guard<std::mutex> lock(mGameMutex);
    mGameState = state;
    mLastGameError = std::move(error);
}

} // namespace ShipwrightRA
