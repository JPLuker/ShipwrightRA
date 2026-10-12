#include "RetroAchievementsHttp.h"

#include <limits>
#include <string>
#include <unordered_map>
#include <utility>

#ifdef SHIPWRIGHT_RA_HTTP_CURL
#include <curl/curl.h>
#endif

namespace ShipwrightRA {
namespace {
void Fail(rc_client_server_callback_t callback, void* data) {
    const rc_api_server_response_t response{ nullptr, 0, RC_API_SERVER_RESPONSE_CLIENT_ERROR };
    callback(&response, data);
}

#ifdef SHIPWRIGHT_RA_HTTP_CURL
int FailureCode(CURLcode result) {
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

size_t WriteBody(char* data, size_t size, size_t count, void* userdata) {
    // Bound decompressed responses and never let allocation exceptions cross C.
    constexpr size_t maxResponseSize = 16 * 1024 * 1024;
    if (size != 0 && count > std::numeric_limits<size_t>::max() / size) {
        return 0;
    }
    const size_t bytes = size * count;
    auto& body = *static_cast<std::string*>(userdata);
    if (bytes > maxResponseSize - body.size()) {
        return 0;
    }
    try {
        body.append(data, bytes);
        return bytes;
    } catch (...) { return 0; }
}
#endif
} // namespace

struct RetroAchievementsHttp::Impl {
    bool stopping = true;
#ifdef SHIPWRIGHT_RA_HTTP_CURL
    struct Transfer {
        CURL* easy = nullptr;
        curl_slist* headers = nullptr;
        std::string body;
        rc_client_server_callback_t callback = nullptr;
        void* data = nullptr;
        ~Transfer() {
            if (easy != nullptr) {
                curl_easy_cleanup(easy);
            }
            curl_slist_free_all(headers);
        }
    };
    CURLM* multi = nullptr;
    std::unordered_map<CURL*, std::unique_ptr<Transfer>> pending;
#endif
};

RetroAchievementsHttp::RetroAchievementsHttp() : mImpl(std::make_unique<Impl>()) {
}

RetroAchievementsHttp::~RetroAchievementsHttp() {
    Shutdown();
}

bool RetroAchievementsHttp::Initialize() {
#ifdef SHIPWRIGHT_RA_HTTP_CURL
    if (mImpl->multi != nullptr) {
        return true;
    }
    // libcurl is process-global and may also be used by other Shipwright modules.
    static const bool initialized = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    if (!initialized || (mImpl->multi = curl_multi_init()) == nullptr) {
        return false;
    }
#endif
    mImpl->stopping = false;
    return true;
}

void RetroAchievementsHttp::Enqueue(const rc_api_request_t* request, rc_client_server_callback_t callback, void* data) {
    if (callback == nullptr) {
        return;
    }
#ifdef SHIPWRIGHT_RA_HTTP_CURL
    if (mImpl->stopping || mImpl->multi == nullptr || request == nullptr || request->url == nullptr) {
        Fail(callback, data);
        return;
    }
    auto transfer = std::make_unique<Impl::Transfer>();
    transfer->callback = callback;
    transfer->data = data;
    CURL* easy = transfer->easy = curl_easy_init();
    if (easy == nullptr) {
        Fail(callback, data);
        return;
    }

    bool configured = true;
    auto set = [&](CURLoption option, auto value) {
        configured = (curl_easy_setopt(easy, option, value) == CURLE_OK) && configured;
    };
    set(CURLOPT_URL, request->url);
    // Do not forward authentication data to a redirect destination.
    set(CURLOPT_FOLLOWLOCATION, 0L);
#if LIBCURL_VERSION_NUM >= 0x075500
    set(CURLOPT_PROTOCOLS_STR, "http,https");
#else
    set(CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
    set(CURLOPT_NOSIGNAL, 1L);
    set(CURLOPT_CONNECTTIMEOUT, 10L);
    set(CURLOPT_TIMEOUT, 30L);
    set(CURLOPT_USERAGENT, "ShipwrightRA/experimental");
    set(CURLOPT_ACCEPT_ENCODING, "");
    set(CURLOPT_WRITEFUNCTION, &WriteBody);
    set(CURLOPT_WRITEDATA, &transfer->body);
    if (request->post_data != nullptr) {
        // rc_client releases request buffers as soon as ServerCall returns.
        set(CURLOPT_COPYPOSTFIELDS, request->post_data);
        if (request->content_type != nullptr && request->content_type[0] != '\0') {
            const std::string header = std::string("Content-Type: ") + request->content_type;
            transfer->headers = curl_slist_append(nullptr, header.c_str());
            configured = (transfer->headers != nullptr) && configured;
            set(CURLOPT_HTTPHEADER, transfer->headers);
        }
    }
    if (!configured) {
        Fail(callback, data);
        return;
    }
    mImpl->pending.emplace(easy, std::move(transfer));
    if (curl_multi_add_handle(mImpl->multi, easy) != CURLM_OK) {
        mImpl->pending.erase(easy);
        Fail(callback, data);
    }
#else
    (void)request;
    Fail(callback, data);
#endif
}

void RetroAchievementsHttp::Pump() {
#ifdef SHIPWRIGHT_RA_HTTP_CURL
    if (mImpl->stopping || mImpl->multi == nullptr) {
        return;
    }
    int running = 0;
    if (curl_multi_perform(mImpl->multi, &running) != CURLM_OK) {
        Shutdown();
        return;
    }
    int remaining = 0;
    while (CURLMsg* message = curl_multi_info_read(mImpl->multi, &remaining)) {
        if (message->msg != CURLMSG_DONE) {
            continue;
        }
        const auto found = mImpl->pending.find(message->easy_handle);
        if (found == mImpl->pending.end()) {
            continue;
        }
        const CURLcode result = message->data.result;
        auto transfer = std::move(found->second);
        mImpl->pending.erase(found);
        long status = 0;
        curl_easy_getinfo(transfer->easy, CURLINFO_RESPONSE_CODE, &status);
        curl_multi_remove_handle(mImpl->multi, transfer->easy);
        const rc_api_server_response_t response{ transfer->body.data(), transfer->body.size(),
                                                 result == CURLE_OK ? static_cast<int>(status) : FailureCode(result) };
        // Remove before dispatch: callbacks may enqueue another request.
        transfer->callback(&response, transfer->data);
        if (mImpl->stopping || mImpl->multi == nullptr) {
            return;
        }
    }
#endif
}

void RetroAchievementsHttp::Shutdown() {
    mImpl->stopping = true;
#ifdef SHIPWRIGHT_RA_HTTP_CURL
    while (!mImpl->pending.empty()) {
        const auto found = mImpl->pending.begin();
        auto transfer = std::move(found->second);
        mImpl->pending.erase(found);
        curl_multi_remove_handle(mImpl->multi, transfer->easy);
        Fail(transfer->callback, transfer->data);
    }
    if (mImpl->multi != nullptr) {
        curl_multi_cleanup(mImpl->multi);
        mImpl->multi = nullptr;
    }
#endif
}

} // namespace ShipwrightRA
