#include "RetroAchievementsHttp.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

using ShipwrightRA::RetroAchievementsHttp;

struct Result {
  int calls = 0;
  int status = 0;
  std::string body;
  RetroAchievementsHttp *transport = nullptr;
  std::string nextUrl;
  Result *next = nullptr;
  bool shutdown = false;
};

static void RC_CCONV Complete(const rc_api_server_response_t *response,
                              void *userdata) {
  auto &result = *static_cast<Result *>(userdata);
  ++result.calls;
  result.status = response->http_status_code;
  result.body.assign(response->body != nullptr ? response->body : "",
                     response->body_length);
  if (result.next != nullptr) {
    rc_api_request_t request{};
    request.url = result.nextUrl.c_str();
    result.transport->Enqueue(&request, Complete, result.next);
  }
  if (result.shutdown) {
    result.transport->Shutdown();
  }
}

static void Enqueue(RetroAchievementsHttp &transport, const std::string &url,
                    Result &result, const std::string &post = "") {
  rc_api_request_t request{};
  request.url = url.c_str();
  request.post_data = post.empty() ? nullptr : post.c_str();
  request.content_type = "application/x-www-form-urlencoded";
  transport.Enqueue(&request, Complete, &result);
}

[[maybe_unused]] static void Until(RetroAchievementsHttp &transport,
                                   Result &result) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (result.calls == 0 && std::chrono::steady_clock::now() < deadline) {
    transport.Pump();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  assert(result.calls == 1);
}

int main(int argc, char **argv) {
  assert(argc == 2);
  const std::string base = argv[1];
  RetroAchievementsHttp transport;
  assert(transport.Initialize());
  assert(transport.Initialize());
  Result invalid;
  transport.Enqueue(nullptr, Complete, &invalid);
  assert(invalid.calls == 1 &&
         invalid.status == RC_API_SERVER_RESPONSE_CLIENT_ERROR);
  transport.Enqueue(nullptr, nullptr, nullptr);

#ifdef SHIPWRIGHT_RA_HTTP_CURL
  Result slow;
  auto start = std::chrono::steady_clock::now();
  Enqueue(transport, base + "/slow", slow);
  assert(slow.calls == 0);
  assert(std::chrono::steady_clock::now() - start <
         std::chrono::milliseconds(100));
  // Enqueue's temporary URL and POST buffer are gone before curl uses them.
  Result post;
  Enqueue(transport, base + "/echo", post, "u=test&t=ephemeral");
  Until(transport, post);
  assert(post.status == 200 && post.body == "u=test&t=ephemeral");
  Until(transport, slow);
  assert(slow.status == 200 && slow.body == "slow");

  Result child;
  Result parent;
  parent.transport = &transport;
  parent.nextUrl = base + "/echo";
  parent.next = &child;
  Enqueue(transport, base + "/echo", parent);
  Until(transport, child);
  assert(parent.calls == 1 && child.status == 200);

  Result redirect;
  Enqueue(transport, base + "/redirect", redirect);
  Until(transport, redirect);
  assert(redirect.status == 302); // Never forward credentials on redirects.
  Result serverError;
  Enqueue(transport, base + "/error", serverError);
  Until(transport, serverError);
  assert(serverError.status == 503);
  Result oversized;
  Enqueue(transport, base + "/large", oversized);
  Until(transport, oversized);
  assert(oversized.status == RC_API_SERVER_RESPONSE_CLIENT_ERROR);

  Result pending;
  Enqueue(transport, base + "/slow", pending);
  start = std::chrono::steady_clock::now();
  transport.Shutdown();
  assert(pending.calls == 1 &&
         pending.status == RC_API_SERVER_RESPONSE_CLIENT_ERROR);
  assert(std::chrono::steady_clock::now() - start <
         std::chrono::milliseconds(100));
  transport.Pump();
  transport.Shutdown();
  assert(pending.calls == 1);
  assert(transport.Initialize());
  Result close;
  close.transport = &transport;
  close.shutdown = true;
  Enqueue(transport, base + "/echo", close);
  Until(transport, close);
  assert(close.status == 200);
#else
  Result unsupported;
  Enqueue(transport, base + "/echo", unsupported);
  assert(unsupported.calls == 1 &&
         unsupported.status == RC_API_SERVER_RESPONSE_CLIENT_ERROR);
#endif
  transport.Shutdown();
  std::cout << "HTTP transport tests passed\n";
}
