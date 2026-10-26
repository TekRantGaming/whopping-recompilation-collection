// The collection launcher, drawn as a console style dashboard (dash.h):
//   Home      the three games as tiles over the focused game's art
//   Game      one game's page: Play, its settings, achievements, saves
//   Settings  one game's own settings, in sections (each game has its own file)
//   Install   the disc image step of a game's install
//   Collection settings for the launcher itself
//   Projects  Other TRG Projects (projects.h)
#pragma once

#include <array>
#include <memory>
#include <string>

#include <SDL.h>

#include <trg/launcher.h>
#include <trg/settings.h>
#include <trg/updater.h>

#include "achievements.h"
#include "art.h"
#include "dash.h"
#include "disc_finder.h"
#include "game_settings.h"
#include "games.h"
#include "install.h"
#include "projects.h"
#include "saves.h"

namespace whopping {

struct AppOptions {
  std::string view = "home";  // testing: home, hub:<n>, game:<n>:<section>, install:<n>, settings
  int hover = -1;             // testing: start with this game focused on the home screen
  bool offline = false;       // no update checks or art downloads
  int frames = 45;            // testing: frames drawn before --screenshot saves
  int test_install = -1;      // testing: run the disc install of this game, then quit
  int test_play = -1;         // testing: start this game as Play does, quit when it closes
  int test_download = -1;     // testing: download (or update) this game, then quit
  std::string test_iso;
};

// What a settings screen remembers: the section and where each list is.
struct SettingsView {
  int section = 0;
  bool in_rows = false;
  std::array<dash::RowsState, 12> rows;
};

// One game's settings, jobs and screen state.
struct GameUi {
  int index = 0;
  std::unique_ptr<TomlSettings> settings;
  GameJobs jobs;
  std::string disc_path;          // chosen on the install screen
  GameAchievements achievements;  // loaded when first shown, again after the game closes
  SettingsView settings_view;
  dash::RowsState install_rows;
  // Saves and install work (back up, restore, delete, remove).
  trg::Task manage;
  std::string manage_result;
  bool manage_error = false;
  // The backups list, read again every few seconds while shown.
  std::vector<Backup> backups;
  double backups_read = -10.0;
};

class App {
 public:
  App(AppOptions options, UploadFn upload, SDL_Window* window);
  ~App();

  void Start();
  void Stop();
  bool Frame();  // false: quit
  void Idle();   // also while minimized (the game is running)
  void DropFile(const std::string& path);
  ImVec4 clear_color() const;

  // Used by the screens.
  GameState state(int game) const { return states_[game]; }
  void RefreshStates();
  void OpenHome(int focus_game = -1);
  void OpenHub(int game);
  void OpenGameSettings(int game, const char* section = nullptr);
  void OpenInstall(int game);
  void OpenCollection();
  void OpenProjects();
  static constexpr int kKingKong = 100;  // running_game() while King Kong Recompiled runs
  bool LaunchKingKong();
  Projects& projects() { return *projects_; }
  const Texture& logo() const { return logo_; }  // the collection's wordmark (tools/logo)
  void PrimaryAction(int game);  // Download / Install / Play
  const char* PrimaryLabel(int game) const;
  bool LaunchGame(int game);
  void Play(int game);  // shows the "how to come back" note first, unless the player turned it off
  bool game_running() const;
  int running_game() const { return running_game_; }
  void Quit() { quit_ = true; }
  GameUi& game_ui(int game) { return *games_[game]; }
  GameAchievements& achievements(int game);  // loads them the first time
  const std::vector<Backup>& backups(int game);
  void PollManage(int game);
  ArtCache& art() { return *art_; }
  UploadFn upload() const { return upload_; }
  DiscFinder& discs() { return discs_; }
  trg::SettingsStore& launcher_settings() { return *launcher_settings_; }
  const trg::Fonts& fonts() const { return fonts_; }
  const AppOptions& options() const { return options_; }
  void SetToast(std::string text, bool error = false) { dash::Toast(text, error); }
  trg::UpdatesState& self_updates() { return self_updates_; }
  const trg::UpdaterConfig& self_updater() const { return self_updater_; }
  SettingsView& collection_view() { return collection_view_; }
  SDL_Window* window() const { return window_; }

 private:
  enum class View { kHome, kHub, kSettings, kInstall, kCollection, kProjects };
  bool Launch(const std::string& exe, const std::string& dir, const std::wstring& args, const std::string& title,
              int index);
  AppOptions options_;
  UploadFn upload_;
  SDL_Window* window_;
  View view_ = View::kHome;
  View drawn_view_ = View::kHome;  // the screen drawn last frame (a change starts a transition)
  int current_ = 0;
  bool quit_ = false;
  std::array<GameState, kGameCount> states_{};
  std::array<std::unique_ptr<GameUi>, kGameCount> games_;
  std::unique_ptr<trg::TextFileSettings> launcher_settings_;
  std::unique_ptr<ArtCache> art_;
  std::unique_ptr<Projects> projects_;
  Texture logo_;
  DiscFinder discs_;
  trg::Fonts fonts_;
  trg::UpdaterConfig self_updater_;
  trg::UpdatesState self_updates_;
  SettingsView collection_view_;
  // The running game.
  void* process_ = nullptr;
  int running_game_ = -1;
};

void ToggleFullscreen(SDL_Window* win);  // main.cpp
bool IsFullscreen(SDL_Window* win);
void SetFullscreen(SDL_Window* win, bool on);

// The screens (home.cpp, hub.cpp, settings_screen.cpp, install_screen.cpp).
void DrawHome(App& app);
void FocusGameOnHome(int game);
void FocusProjectsOnHome();
void DrawProjects(App& app);  // projects_screen.cpp
void DrawHub(App& app, int game);
void FocusHubPlay();
void DrawGameSettings(App& app, int game);
int FindGameSection(const char* name);  // -1 when there is no such section
void DrawInstall(App& app, int game);
void DrawCollection(App& app);

}  // namespace whopping
