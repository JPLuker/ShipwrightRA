#include "RetroAchievementsManager.h"

#include <array>
#include <cstdio>
#include <memory>

#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>
#include <ship/window/gui/GuiWindow.h>

#include "z64.h"
#include "soh/SohGui/SohGui.hpp"
#include "soh/SohGui/SohMenu.h"
#include "soh/cvar_prefixes.h"

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
#include "soh/Extractor/portable-file-dialogs.h"
#define RA_HAS_FILE_DIALOG 1
#endif

namespace SohGui {
extern std::shared_ptr<SohMenu> mSohMenu;
}

namespace ShipwrightRA {
namespace {

class RetroAchievementsWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void InitElement() override {
    }
    void UpdateElement() override {
    }

    void DrawElement() override {
        auto& manager = RetroAchievementsManager::Instance();
        if (!manager.IsInitialized()) {
            ImGui::TextWrapped("RetroAchievements could not initialize. Check the application log.");
            return;
        }
        ImGui::TextUnformatted("RetroAchievements gameplay test");
        ImGui::TextWrapped(
            "Experimental memory support. Hardcore is disabled. Use a normal, unmodified OoT save for parity testing.");
        ImGui::Separator();

        const auto auth = manager.GetAuthState();
        if (auth == RetroAchievementsAuthState::LoggedIn) {
            const auto user = manager.GetUserInfo();
            ImGui::Text("Signed in: %s", user ? user->displayName.c_str() : "Connected");
            if (ImGui::Button("Sign out")) {
                manager.Logout();
            }
        } else {
            ImGui::BeginDisabled(auth == RetroAchievementsAuthState::Authenticating);
            ImGui::InputText("Username", mUsername.data(), mUsername.size());
            ImGui::InputText("Password", mPassword.data(), mPassword.size(), ImGuiInputTextFlags_Password);
            if (ImGui::Button("Sign in")) {
                manager.BeginLoginWithPassword(mUsername.data(), mPassword.data());
                // Credentials are session-only. Clear the input as soon as rc_client
                // has built and handed off its request (the transport owns a copy).
                mPassword.fill('\0');
            }
            ImGui::EndDisabled();
            if (auth == RetroAchievementsAuthState::Authenticating) {
                ImGui::TextUnformatted("Signing in...");
            }
            const std::string error = manager.GetLastAuthError();
            if (!error.empty()) {
                ImGui::TextWrapped("Login: %s", error.c_str());
            }
            ImGui::TextWrapped("The password is not saved. Sign-in lasts for this application session.");
        }

        ImGui::Separator();
        if (!mPathInitialized) {
            std::snprintf(mRomPath.data(), mRomPath.size(), "%s", manager.GetRomPath().c_str());
            mPathInitialized = true;
        }
        ImGui::TextWrapped("Select the same original OoT ROM used for this installation. This only identifies the "
                           "achievement set; it does not change the game assets.");
        ImGui::InputText("ROM path", mRomPath.data(), mRomPath.size());
#ifdef RA_HAS_FILE_DIALOG
        ImGui::BeginDisabled(mFileDialog != nullptr);
        if (ImGui::Button("Browse...")) {
            mFileDialog = std::make_unique<pfd::open_file>(
                "Select your Ocarina of Time ROM", "",
                std::vector<std::string>{ "N64 ROM", "*.z64 *.n64 *.v64", "All files", "*" });
        }
        ImGui::EndDisabled();
        if (mFileDialog != nullptr && mFileDialog->ready(0)) {
            const auto selection = mFileDialog->result();
            if (!selection.empty()) {
                std::snprintf(mRomPath.data(), mRomPath.size(), "%s", selection.front().c_str());
            }
            mFileDialog.reset();
        }
        ImGui::SameLine();
#endif
        if (ImGui::Button("Use ROM / reload set")) {
            manager.SetRomPath(mRomPath.data());
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop session")) {
            manager.SetRomPath({});
        }

        const auto gameState = manager.GetGameState();
        const bool busy =
            gameState == RetroAchievementsGameState::Loading || gameState == RetroAchievementsGameState::Loaded;
        bool testMode = manager.IsTestMode();
        ImGui::BeginDisabled(busy);
        if (ImGui::Checkbox("Test mode - do not submit unlocks", &testMode)) {
            manager.SetTestMode(testMode);
        }
        ImGui::EndDisabled();
        if (busy) {
            ImGui::TextWrapped("Stop the session before changing test mode.");
        }
        ImGui::TextWrapped(manager.IsTestMode()
                               ? "Triggers are local test results. Your account will not receive unlocks."
                               : "Softcore unlock submissions are enabled for this session.");

        if (gameState == RetroAchievementsGameState::Loading) {
            ImGui::TextUnformatted("Loading achievement set...");
        } else if (gameState == RetroAchievementsGameState::Failed) {
            ImGui::TextWrapped("Game load: %s", manager.GetLastGameError().c_str());
        } else if (gameState == RetroAchievementsGameState::Unloaded) {
            ImGui::TextWrapped("To start: sign in, apply a ROM path, then enter a normal save. The set loads once "
                               "gameplay is active.");
        }
        const auto game = manager.GetGameInfo();
        if (game) {
            ImGui::Text("Game: %s (#%u)", game->title.c_str(), game->id);
        }
        ImGui::Text("Gameplay: %s | Evaluated frames: %llu", manager.IsGameplayActive() ? "active" : "waiting",
                    static_cast<unsigned long long>(manager.GetEvaluatedFrames()));
        if (ImGui::Button("Copy diagnostic report")) {
            ImGui::SetClipboardText(manager.GetDiagnosticReport().c_str());
        }
        ImGui::TextWrapped("Report includes set ID/hash, unsupported achievements, memory addresses, and recent "
                           "triggers. It excludes your password, token, username, and ROM path.");

        if (game && ImGui::CollapsingHeader("Achievements", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto* list = rc_client_create_achievement_list(manager.GetClient(), RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,
                                                           RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
            if (list != nullptr) {
                for (uint32_t i = 0; i < list->num_buckets; ++i) {
                    const auto& bucket = list->buckets[i];
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::TreeNode("bucket", "%s (%u)", bucket.label != nullptr ? bucket.label : "Achievements",
                                        bucket.num_achievements)) {
                        for (uint32_t j = 0; j < bucket.num_achievements; ++j) {
                            const auto* achievement = bucket.achievements[j];
                            ImGui::TextWrapped("#%u %s (%u points)", achievement->id,
                                               achievement->title != nullptr ? achievement->title : "Unknown",
                                               achievement->points);
                            if (achievement->description != nullptr) {
                                ImGui::TextWrapped("%s", achievement->description);
                            }
                            if (achievement->measured_progress[0] != '\0') {
                                ImGui::Text("Progress: %s", achievement->measured_progress);
                            }
                            ImGui::Separator();
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                rc_client_destroy_achievement_list(list);
            }
        }
        if (ImGui::CollapsingHeader("Recent events", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (const auto& event : manager.GetRecentEvents()) {
                ImGui::TextWrapped("%s", event.c_str());
            }
        }
    }

  private:
    std::array<char, 128> mUsername{};
    std::array<char, 256> mPassword{};
    std::array<char, 4096> mRomPath{};
    bool mPathInitialized = false;
#ifdef RA_HAS_FILE_DIALOG
    std::unique_ptr<pfd::open_file> mFileDialog;
#endif
};

void RegisterRetroAchievementsWindow() {
    using namespace UIWidgets;
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->AddGuiWindow(std::make_shared<RetroAchievementsWindow>(
        CVAR_WINDOW("RetroAchievements"), "RetroAchievements", ImVec2(740, 700)));
    SohGui::mSohMenu->AddSidebarEntry("Settings", "RetroAchievements", 1);
    WidgetPath path = { "Settings", "RetroAchievements", SECTION_COLUMN_1 };
    SohGui::mSohMenu->AddWidget(path, "Open gameplay test panel", WIDGET_WINDOW_BUTTON)
        .CVar(CVAR_WINDOW("RetroAchievements"))
        .RaceDisable(false)
        .WindowName("RetroAchievements")
        .Options(WindowButtonOptions()
                     .Tooltip("Sign in, choose an OoT ROM, and inspect achievement test results.")
                     .EmbedWindow(false));
}

static RegisterMenuInitFunc registerMenu(RegisterRetroAchievementsWindow);
} // namespace
} // namespace ShipwrightRA
