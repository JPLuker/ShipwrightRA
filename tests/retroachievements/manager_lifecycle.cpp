#include "N64MemoryAdapter.h"
#include "RetroAchievementsManager.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <rc_api_request.h>
#include <thread>

// This test exercises real rc_client authentication, not N64 memory mapping.
uint32_t ShipwrightRA::N64MemoryAdapter::Read(uint32_t, uint8_t *, uint32_t) {
  assert(false && "Authentication must not read game memory");
  return 0;
}

using namespace ShipwrightRA;
static void WaitForLogin(RetroAchievementsManager &manager) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (manager.GetAuthState() == RetroAchievementsAuthState::Authenticating &&
         std::chrono::steady_clock::now() < deadline) {
    manager.Idle();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  assert(manager.GetAuthState() != RetroAchievementsAuthState::Authenticating);
}

int main(int argc, char **argv) {
  assert(argc == 2);
  auto &manager = RetroAchievementsManager::Instance();
  assert(manager.Initialize());
  rc_client_set_host(manager.GetClient(), argv[1]);
  assert(!rc_client_get_hardcore_enabled(manager.GetClient()));
  assert(manager.BeginLoginWithToken("first", "synthetic"));
  assert(manager.GetAuthState() == RetroAchievementsAuthState::Authenticating);
  assert(!manager.BeginLoginWithToken("duplicate", "synthetic"));
  assert(manager.GetAuthState() == RetroAchievementsAuthState::Authenticating);
  WaitForLogin(manager);
  assert(manager.GetAuthState() == RetroAchievementsAuthState::LoggedIn);
  assert(manager.GetUserInfo()->username == "first");
  manager.Logout();
  assert(!manager.GetUserInfo());
  assert(manager.GetGameState() == RetroAchievementsGameState::Unloaded);

  assert(manager.BeginLoginWithToken("stale", "synthetic"));
  manager.Idle(); // Transfer may be underway when logout occurs.
  manager.Logout();
  assert(manager.GetAuthState() == RetroAchievementsAuthState::LoggedOut);
  assert(manager.BeginLoginWithToken("replacement", "synthetic"));
  WaitForLogin(manager);
  assert(manager.GetAuthState() == RetroAchievementsAuthState::LoggedIn);
  assert(manager.GetUserInfo()->username == "replacement");
  manager.Logout();
  assert(manager.BeginLoginWithToken("invalid", "synthetic"));
  WaitForLogin(manager);
  assert(manager.GetAuthState() == RetroAchievementsAuthState::Failed);
  assert(!manager.GetLastAuthError().empty());
  manager.Logout();
  assert(manager.BeginLoginWithToken("shutdown", "synthetic"));
  manager.Shutdown();
  assert(!manager.IsInitialized());
  manager.Idle();
  manager.DoFrame();
  manager.Shutdown();
  assert(manager.Initialize());
  assert(manager.GetAuthState() == RetroAchievementsAuthState::LoggedOut);
  manager.Shutdown();
  std::cout << "Manager lifecycle tests passed\n";
}
