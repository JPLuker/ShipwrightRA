#include "N64MemoryAdapter.h"
#include "RetroAchievementsManager.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <rc_api_request.h>
#include <thread>

// Isolated runtime fixture; the native adapter has its own real-structure test.
static uint8_t testValue = 0;
static uint32_t memoryReads = 0;
uint32_t ShipwrightRA::N64MemoryAdapter::Read(uint32_t address, uint8_t *buffer,
                                              uint32_t count) {
  ++memoryReads;
  if (address != 0 || count != 1) {
    return 0;
  }
  *buffer = testValue;
  return 1;
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
  assert(argc == 3);
  auto &manager = RetroAchievementsManager::Instance();
  assert(manager.Initialize());
  rc_client_set_host(manager.GetClient(), argv[1]);
  assert(!rc_client_get_hardcore_enabled(manager.GetClient()));
  assert(manager.BeginLoginWithToken("ra_test_account", "synthetic"));
  assert(manager.GetAuthState() == RetroAchievementsAuthState::Authenticating);
  assert(!manager.BeginLoginWithToken("duplicate", "synthetic"));
  assert(!manager.BeginLoginWithToken("", ""));
  assert(manager.GetAuthState() == RetroAchievementsAuthState::Authenticating);
  WaitForLogin(manager);
  assert(manager.GetAuthState() == RetroAchievementsAuthState::LoggedIn);
  assert(manager.GetUserInfo()->username == "ra_test_account");
  assert(memoryReads == 0);
  assert(manager.IsTestMode());
  manager.SetRomPath(argv[2]);
  manager.ProcessGameFrame(false);
  assert(manager.GetGameState() == RetroAchievementsGameState::Unloaded);
  assert(memoryReads == 0);

  manager.ProcessGameFrame(true);
  assert(manager.GetGameState() == RetroAchievementsGameState::Loading);
  manager.ProcessGameFrame(false);
  assert(manager.GetGameState() == RetroAchievementsGameState::Unloaded);
  assert(memoryReads == 0);

  const auto load = [&]() {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do {
      manager.ProcessGameFrame(true);
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (manager.GetGameState() == RetroAchievementsGameState::Loading &&
             std::chrono::steady_clock::now() < deadline);
    assert(manager.GetGameState() == RetroAchievementsGameState::Loaded);
    assert(manager.GetGameInfo()->id == 1234);
  };
  load();
  assert(!manager.SetTestMode(
      false)); // Cannot turn a test session into live submissions mid-game.
  const auto frames = manager.GetEvaluatedFrames();
  const auto reads = memoryReads;
  manager.ProcessGameFrame(false);
  assert(manager.GetEvaluatedFrames() == frames && memoryReads == reads);
  testValue = 1;
  manager.ProcessGameFrame(true);
  assert(!manager.GetRecentEvents().empty());
  assert(manager.GetRecentEvents().back().find("Test trigger: 5501") !=
         std::string::npos);
  const auto report = manager.GetDiagnosticReport();
  assert(report.find("0x000100") != std::string::npos);
  assert(report.find("5502 Unsupported") != std::string::npos);
  assert(report.find("synthetic.z64") == std::string::npos);
  assert(report.find("ra_test_account") == std::string::npos);

  manager.UnloadGame();
  assert(manager.SetTestMode(false));
  testValue = 0;
  load();
  testValue = 1;
  manager.ProcessGameFrame(true);
  assert(manager.GetRecentEvents().back().find("Unlock trigger: 5501") !=
         std::string::npos);
  // Flush the single deliberate softcore submission to the loopback server.
  for (int i = 0; i < 40; ++i) {
    manager.Idle();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  manager.SetRomPath({});
  assert(manager.SetTestMode(true));
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
