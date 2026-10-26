// sneak_king - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include <rex/audio/sdl/sdl_audio_system.h>
#include <rex/cvar.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/ui/overlay/achievement_notification.h>
#include <rex/ui/overlay/debug_overlay.h>

namespace rex::ui {
class Window;
class WindowedAppContext;
}

namespace bk {
void InstallFpeGuard();  // fpe_guard.cpp
void ApplyPortDefaults();  // port_defaults.cpp
rex::ui::FrameStats GuestFrameStats();  // frame_stats.cpp
void ApplyFrameRate();  // frame_stats.cpp
void ApplyRenderQuality(rex::ui::Window* window);  // render_quality.cpp
std::unique_ptr<rex::ui::AchievementNotificationDialog> CreateAchievementToast(
    rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate, rex::Runtime* runtime);  // achievement_toast.cpp
void ExportAchievements(rex::Runtime* runtime, const std::filesystem::path& game_data_root);  // achievements_export.cpp
}  // namespace bk

class BigBumpinApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<BigBumpinApp>(new BigBumpinApp(ctx, "big_bumpin",
        PPCImageConfig));
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (!config.graphics && config.gpu_plugin.empty()) config.gpu_plugin = "xenos";
    if (!config.audio_factory)
      config.audio_factory = REX_AUDIO_BACKEND(rex::audio::sdl::SDLAudioSystem);
  }

  // After the GPU plugin and the window exist, before the GPU starts: the render scale from the upscaler mode and
  // the 1280 x 720 guest video mode (render_quality.cpp).
  std::optional<rex::PathConfig> OnFinalizePaths(const rex::PathConfig& defaults,
                                                 std::function<void(rex::PathConfig)> resume) override {
    (void)resume;
    bk::ApplyRenderQuality(window());
    return defaults;
  }

#if defined(__linux__)
  // On Linux the program sits in the read only AppImage: its settings file (which the launcher writes) and its logs
  // go in the game's own user folder (XDG_DATA_HOME/<game>, where its saves are).
  void OnConfigurePaths(rex::PathConfig& paths) override {
    paths.config_path = paths.user_data_root / (std::string(GetName()) + ".toml");
    std::error_code ec;
    std::filesystem::create_directories(paths.user_data_root / "logs", ec);
    if (rex::cvar::GetFlagByName("log_file").empty())
      rex::cvar::SetFlagByName("log_file", (paths.user_data_root / "logs" / (std::string(GetName()) + ".log")).string());
  }
#endif

  void OnPostSetup() override {
    bk::InstallFpeGuard();  // after the runtime's own signal handlers
    bk::ApplyPortDefaults();
    SetGuestFrameStats(bk::GuestFrameStats);  // the F3 overlay's frame rate
    bk::ApplyFrameRate();
    bk::ExportAchievements(runtime(), game_data_root());
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // Achievement notifications in the TRG ports' style (achievement_toast.cpp).
  std::unique_ptr<rex::ui::AchievementNotificationDialog> CreateAchievementNotificationDialog() override {
    return bk::CreateAchievementToast(imgui_drawer(), immediate_drawer(), runtime());
  }
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};

