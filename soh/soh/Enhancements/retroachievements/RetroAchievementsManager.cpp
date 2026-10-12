#include "RetroAchievementsManager.h"

#include "N64MemoryAdapter.h"

#include <cstring>
#include <string>
#include <utility>

#include <rc_hash.h>
#include <spdlog/spdlog.h>

#ifdef SHIPWRIGHT_RA_HTTP_CURL
#include <curl/curl.h>
#endif

namespace ShipwrightRA {
namespace {

#ifdef SHIPWRIGHT_RA_HTTP_CURL
size_t CurlWriteCallback(char* data, size_t size, size_t count, void* userdata) {
    if (data == nullptr || userdata == nullptr) {
        return 0;
    }

    const size_t byteCount = size * count;
    static_cast<std::string*>(userdata)->append(data, byteCount);
    return byteCount;
}

bool EnsureCurlInitialized() {
    static const bool initialized = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    return initialized;
}

int CurlFailureResponseCode(CURLcode result) {
    switch (result) {
        case CURLE_COULDNT_RESOLVE_PROXY:
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_CONNECT:
        case CURLE_OPERATION_TIMEDOUT:
        case CURLE_SEND_ERROR:
        case CURLE_RECV_ERROR:
        case CURLE_GOT_NOTHING:
            return RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
        default:
            return RC_API_SERVER_RESPONSE_CLIENT_ERROR;
    }
}
#endif

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

#ifdef SHIPWRIGHT_RA_HTTP_CURL
    if (!EnsureCurlInitialized()) {
        spdlog::error("[RetroAchievements] Failed to initialize libcurl");
        return false;
    }
#endif

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

    SetAuthState(RetroAchievementsAuthState::LoggedOut);
    spdlog::info("[RetroAchievements] rcheevos client initialized (Hardcore disabled during bootstrap)");
    return true;
}

void RetroAchievementsManager::Shutdown() {
    if (mClient != nullptr) {
        rc_client_destroy(mClient);
        mClient = nullptr;
    }

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
    if (username.empty() || password.empty()) {
        SetAuthState(RetroAchievementsAuthState::Failed, "Username and password are required");
        return false;
    }

    SetAuthState(RetroAchievementsAuthState::Authenticating);
    const rc_client_async_handle_t* handle =
        rc_client_begin_login_with_password(mClient, username.c_str(), password.c_str(),
                                            &RetroAchievementsManager::LoginCallback, this);
    if (handle == nullptr) {
        if (GetAuthState() == RetroAchievementsAuthState::Authenticating) {
            SetAuthState(RetroAchievementsAuthState::Failed, "Failed to start RetroAchievements login");
        }
        return false;
    }

    return true;
}

bool RetroAchievementsManager::BeginLoginWithToken(const std::string& username, const std::string& token) {
    if (mClient == nullptr) {
        SetAuthState(RetroAchievementsAuthState::Failed, "RetroAchievements client is not initialized");
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
        return false;
    }

    return true;
}

void RetroAchievementsManager::Logout() {
    if (mClient != nullptr) {
        rc_client_logout(mClient);
    }
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

uint32_t RC_CCONV RetroAchievementsManager::ReadMemory(uint32_t address, uint8_t* buffer, uint32_t numBytes,
                                                       rc_client_t* client) {
    (void)client;
    return N64MemoryAdapter::Read(address, buffer, numBytes);
}

void RC_CCONV RetroAchievementsManager::ServerCall(const rc_api_request_t* request,
                                                   rc_client_server_callback_t callback, void* callbackData,
                                                   rc_client_t* client) {
    (void)client;

    if (callback == nullptr) {
        return;
    }

#ifdef SHIPWRIGHT_RA_HTTP_CURL
    if (request == nullptr || request->url == nullptr || !EnsureCurlInitialized()) {
        const rc_api_server_response_t response = {
            nullptr,
            0,
            RC_API_SERVER_RESPONSE_CLIENT_ERROR,
        };
        callback(&response, callbackData);
        return;
    }

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        spdlog::error("[RetroAchievements] Failed to create libcurl easy handle");
        const rc_api_server_response_t response = {
            nullptr,
            0,
            RC_API_SERVER_RESPONSE_CLIENT_ERROR,
        };
        callback(&response, callbackData);
        return;
    }

    std::string responseBody;
    curl_slist* headers = nullptr;

    curl_easy_setopt(curl, CURLOPT_URL, request->url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ShipwrightRA/experimental");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &CurlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);

    if (request->post_data != nullptr) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request->post_data);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                         static_cast<curl_off_t>(std::strlen(request->post_data)));

        if (request->content_type != nullptr && request->content_type[0] != '\0') {
            const std::string contentType = std::string("Content-Type: ") + request->content_type;
            headers = curl_slist_append(headers, contentType.c_str());
            if (headers != nullptr) {
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            }
        }
    }

    const CURLcode curlResult = curl_easy_perform(curl);
    long httpStatusCode = 0;
    if (curlResult == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpStatusCode);
    }

    if (headers != nullptr) {
        curl_slist_free_all(headers);
    }
    curl_easy_cleanup(curl);

    if (curlResult != CURLE_OK) {
        spdlog::warn("[RetroAchievements] HTTP request failed: {}", curl_easy_strerror(curlResult));
        const rc_api_server_response_t response = {
            nullptr,
            0,
            CurlFailureResponseCode(curlResult),
        };
        callback(&response, callbackData);
        return;
    }

    spdlog::debug("[RetroAchievements] HTTP {} returned {}", request->post_data != nullptr ? "POST" : "GET",
                  httpStatusCode);

    const rc_api_server_response_t response = {
        responseBody.data(),
        responseBody.size(),
        static_cast<int>(httpStatusCode),
    };
    callback(&response, callbackData);
#else
    (void)request;

    spdlog::warn("[RetroAchievements] Server request rejected because HTTP transport is unavailable on this platform");
    const rc_api_server_response_t response = {
        nullptr,
        0,
        RC_API_SERVER_RESPONSE_CLIENT_ERROR,
    };
    callback(&response, callbackData);
#endif
}

void RC_CCONV RetroAchievementsManager::LoginCallback(int result, const char* errorMessage, rc_client_t* client,
                                                      void* userdata) {
    auto* manager = static_cast<RetroAchievementsManager*>(userdata);
    if (manager == nullptr) {
        return;
    }

    if (result != RC_OK) {
        std::string error;
        if (errorMessage != nullptr && errorMessage[0] != '\0') {
            error = errorMessage;
        } else {
            const char* fallback = rc_error_str(result);
            error = fallback != nullptr ? fallback : "Unknown RetroAchievements authentication error";
        }

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

} // namespace ShipwrightRA
