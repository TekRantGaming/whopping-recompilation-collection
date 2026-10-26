#include "app.h"

#include "discord_icon.inc"
#include "logo_png.h"
#include "steam_icon.inc"
#include "sounds.h"

#include <filesystem>

#if defined(_WIN32)
#include <windows.h>
#else
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace whopping {
namespace fs = std::filesystem;

namespace {

std::wstring Wide(const std::string& utf8) { return fs::u8path(utf8).make_preferred().wstring(); }

}  // namespace

App::App(AppOptions options, UploadFn upload, SDL_Window* window)
    : options_(std::move(options)), upload_(upload), window_(window) {}

App::~App() { Stop(); }

void App::Start() {
  std::error_code ec;
  fs::create_directories(fs::u8path(LauncherDir() + "/games"), ec);
  launcher_settings_ = std::make_unique<trg::TextFileSettings>(LauncherDir() + "/whopping.txt");

#if defined(_WIN32)
  fonts_ = trg::LoadSystemFonts(18.0f);
#else
  // Linux: Noto Sans, from the AppImage (usr/share/fonts beside usr/bin) or the system; the TRG launcher's DejaVu
  // Sans when neither has it.
  {
    auto load = [](const char* name) -> ImFont* {
      for (const std::string dir : {ProgramDir() + "/../share/fonts/", std::string("/usr/share/fonts/google-noto/"),
                                    std::string("/usr/share/fonts/truetype/noto/")}) {
        std::error_code e;
        if (fs::exists(fs::u8path(dir + name), e)) return ImGui::GetIO().Fonts->AddFontFromFileTTF((dir + name).c_str(), 18.0f);
      }
      return nullptr;
    };
    if (ImFont* regular = load("NotoSans-Regular.ttf")) {
      fonts_.size = 18.0f;
      fonts_.regular = regular;
      fonts_.semibold = load("NotoSans-SemiBold.ttf");
      fonts_.bold = load("NotoSans-Bold.ttf");
      if (!fonts_.bold) fonts_.bold = regular;
      if (!fonts_.semibold) fonts_.semibold = fonts_.bold;
      ImGui::GetIO().FontDefault = regular;
    } else {
      fonts_ = trg::LoadSystemFonts(18.0f);
    }
  }
#endif
  sound::SetEnabled(launcher_settings_->GetBool("ui_sounds", true));

  self_updater_.stable_repo = kCollectionRepo;
#if defined(_WIN32)
  self_updater_.asset = "WhoppingCollection-*-windows-x64.zip";  // the launcher's own zip in the packages release
#else
  // The AppImage (the launcher and the games together) is replaced as one file.
  self_updater_.asset = "WhoppingCollection-*-linux-x86_64.AppImage";
#endif
  self_updater_.tag_prefix = kPackagesTagPrefix;
  self_updater_.current_version = WHOPPING_VERSION;
  self_updater_.program_dir = ProgramDir();
#if defined(_WIN32)
  self_updater_.program_name = "whopping_launcher.exe";
#else
  self_updater_.program_name = "whopping_launcher";
#endif
  self_updater_.protected_paths = {"games", "save-backups", "cache", "whopping.txt"};
  self_updater_.state_file = LauncherDir() + "/update-state.txt";
  if (!g_release_server.empty()) self_updater_.api_base = g_release_server;
  trg::CleanUpAfterUpdate(self_updater_);

  std::vector<int> ids;
  for (const GameInfo& g : Games()) ids.push_back(g.steamgriddb_id);
  art_ = std::make_unique<ArtCache>(ids, upload_);
  {
    int w = 0, h = 0;
    std::vector<unsigned char> rgba;
    const std::string png(reinterpret_cast<const char*>(kWhoppingLogoPng), sizeof kWhoppingLogoPng);
    if (DecodeImage(png, w, h, rgba)) logo_ = upload_(w, h, rgba.data());
  }
  {  // the brand marks, white with their alpha, tinted when drawn
    auto mark = [this](const unsigned char* mask, int size) {
      std::vector<unsigned char> rgba(size_t(size) * size * 4, 255);
      for (int i = 0; i < size * size; ++i) rgba[size_t(i) * 4 + 3] = mask[i];
      return upload_(size, size, rgba.data());
    };
    discord_icon_ = mark(kDiscordIcon, kDiscordIconSize);
    steam_icon_ = mark(kSteamIcon, kSteamIconSize);
  }
  StartSteamCheck();
  projects_ = std::make_unique<Projects>(upload_, !options_.offline);
  projects_->Start();

  for (int i = 0; i < kGameCount; ++i) {
    games_[i] = std::make_unique<GameUi>();
    games_[i]->index = i;
    games_[i]->settings = std::make_unique<TomlSettings>(GameSettingsPath(Games()[i]));
  }
  RefreshStates();

  if (!options_.offline) {
    art_->Start();
    // Updates install by themselves ("update them automatically when a new
    // version has been released"); the launcher checks once at startup.
#if defined(_WIN32)  // on Linux the games come inside the launcher's AppImage and update with it
    if (launcher_settings_->GetBool("auto_update", true)) {
      for (int i = 0; i < kGameCount; ++i) {
        if (states_[i] != GameState::kNotDownloaded) StartDownload(Games()[i], games_[i]->jobs, true);
      }
    }
#endif
    if (launcher_settings_->GetBool("update_check", true)) trg::StartUpdateCheck(self_updates_, self_updater_, "stable");
  } else {
    g_art_downloads_allowed = false;
    art_->Start();  // still loads what is cached; downloads are skipped while offline
  }

  if (options_.hover >= 0 && options_.hover < kGameCount) FocusGameOnHome(options_.hover);
  if (options_.test_play >= 0 && options_.test_play < kGameCount) LaunchGame(options_.test_play);
  if (options_.test_download >= 0 && options_.test_download < kGameCount)
    StartDownload(Games()[options_.test_download], games_[options_.test_download]->jobs);
  if (options_.test_install >= 0 && options_.test_install < kGameCount) {
    OpenInstall(options_.test_install);
    StartDiscInstall(Games()[options_.test_install], options_.test_iso, games_[options_.test_install]->jobs);
  }

  // Testing views.
  const std::string& v = options_.view;
  if (v.rfind("hub:", 0) == 0) {
    OpenHub(std::atoi(v.c_str() + 4));
  } else if (v.rfind("game:", 0) == 0) {
    const int g = std::atoi(v.c_str() + 5);
    const size_t colon = v.find(':', 5);
    OpenGameSettings(g, colon == std::string::npos ? nullptr : v.c_str() + colon + 1);
    if (colon != std::string::npos) games_[g]->settings_view.in_rows = true;
  } else if (v.rfind("install:", 0) == 0) {
    OpenInstall(std::atoi(v.c_str() + 8));
  } else if (v == "projects") {
    OpenProjects();
  } else if (v == "settings") {
    OpenCollection();
  }
}

void App::Stop() {
  for (auto& g : games_) {
    if (g) g->jobs.task.Cancel();
  }
  games_ = {};
  projects_.reset();
  art_.reset();
#if defined(_WIN32)
  if (process_) CloseHandle(static_cast<HANDLE>(process_));
#endif
  process_ = nullptr;
}

void App::RefreshStates() {
  for (int i = 0; i < kGameCount; ++i) {
    const GameState was = states_[i];
    states_[i] = DetectState(Games()[i]);
    if (games_[i] && was != states_[i]) games_[i]->achievements.loaded = false;
  }
}

ImVec4 App::clear_color() const { return ImVec4(0.0f, 0.0f, 0.0f, 1.0f); }

void App::OpenHome(int focus_game) {
  RefreshStates();
  if (focus_game >= 0) FocusGameOnHome(focus_game);
  if (focus_game == -2) FocusProjectsOnHome();
  view_ = View::kHome;
}

void App::OpenHub(int game) {
  if (game < 0 || game >= kGameCount) return;
  current_ = game;
  RefreshStates();
  FocusHubPlay();
  view_ = View::kHub;
}

void App::OpenGameSettings(int game, const char* section) {
  if (game < 0 || game >= kGameCount) return;
  current_ = game;
  GameUi& gu = *games_[game];
  gu.settings->Load();
  gu.achievements.loaded = false;  // read again: the game may have unlocked some since
  gu.settings_view.in_rows = false;
  if (section) {
    const int s = FindGameSection(section);
    if (s >= 0) {
      gu.settings_view.section = s;
      gu.settings_view.in_rows = true;
    }
  }
  view_ = View::kSettings;
}

void App::OpenInstall(int game) {
  if (game < 0 || game >= kGameCount) return;
  current_ = game;
  view_ = View::kInstall;
}

void App::OpenProjects() { view_ = View::kProjects; }

void App::OpenCollection() {
  collection_view_.in_rows = false;
  view_ = View::kCollection;
}

const char* App::PrimaryLabel(int game) const {
  if (games_[game]->jobs.task.running()) return "Please wait";
  if (running_game_ == game) return "Playing";
  switch (states_[game]) {
    case GameState::kNotDownloaded: return "Download";
    case GameState::kDownloaded: return "Play";  // asks for the disc first (the games come in the zip)
    case GameState::kInstalled: return "Play";
  }
  return "";
}

void App::PrimaryAction(int game) {
  if (games_[game]->jobs.task.running()) {
    SetToast("Please wait until " + games_[game]->jobs.task.label() + " finishes.");
    return;
  }
  switch (states_[game]) {
    case GameState::kNotDownloaded:
      StartDownload(Games()[game], games_[game]->jobs);
      break;
    case GameState::kDownloaded:
      OpenInstall(game);
      break;
    case GameState::kInstalled:
      Play(game);
      break;
  }
}

void App::Play(int game) {
  if (launcher_settings_->GetBool("return_note_seen", false)) {
    LaunchGame(game);
    return;
  }
  // The first time: how to come back to the launcher while playing.
  dash::Choose("Coming back here",
               "To come back to the launcher while you play, hold Back and Start together on the controller for a "
               "second, or hold Esc on the keyboard.",
               "Play", [this, game] { LaunchGame(game); }, "Play, and don't show this again", [this, game] {
                 launcher_settings_->SetBool("return_note_seen", true);
                 launcher_settings_->Save();
                 LaunchGame(game);
               });
}

GameAchievements& App::achievements(int game) {
  GameUi& gu = *games_[game];
  if (!gu.achievements.loaded && states_[game] == GameState::kInstalled)
    LoadAchievements(Games()[game], gu.achievements, upload_);
  return gu.achievements;
}

const std::vector<Backup>& App::backups(int game) {
  GameUi& gu = *games_[game];
  if (ImGui::GetTime() - gu.backups_read > 3.0) {
    gu.backups = ListBackups(Games()[game]);
    gu.backups_read = ImGui::GetTime();
  }
  return gu.backups;
}

void App::PollManage(int game) {
  GameUi& gu = *games_[game];
  std::string err;
  switch (gu.manage.Finish(&err)) {
    case trg::Task::State::kDone:
      gu.manage_result = "Done.";
      gu.manage_error = false;
      gu.backups_read = -10.0;
      RefreshStates();
      SetToast(gu.manage_result);
      break;
    case trg::Task::State::kFailed:
      gu.manage_result = err;
      gu.manage_error = true;
      SetToast(err, true);
      break;
    default:
      break;
  }
}

bool App::game_running() const { return process_ != nullptr; }

bool App::LaunchGame(int game) {
  if (game_running()) {
    SetToast(running_game_ < kGameCount ? std::string(Games()[running_game_].title) + " is already running."
                                        : std::string("A game is already running."));
    return false;
  }
  if (games_[game]->jobs.task.running()) {
    SetToast("Please wait until the download finishes.");
    return false;
  }
  const GameInfo& g = Games()[game];
  std::vector<std::string> args = {"--game_data_root=" + GameDataDir(g)};
  args.push_back("--bk_from_launcher=true");  // Back and Start, or holding Esc, closes the game (return_to_launcher.cpp)
  // Testing (--test-play): never the player's own saves in Documents, a private folder instead.
  if (options_.test_play >= 0) args.push_back("--user_data_root=" + LauncherDir() + "/test-userdata");
  return Launch(GameExePath(g), GameDir(g), args, g.title, game);
}

bool App::LaunchKingKong() {
  if (game_running()) {
    SetToast("A game is already running.");
    return false;
  }
  return Launch(projects_->king_kong_exe(), projects_->king_kong_dir(), {}, "King Kong Recompiled", kKingKong);
}

bool App::Launch(const std::string& exe, const std::string& dir_utf8, const std::vector<std::string>& args,
                 const std::string& title, int index) {
#if defined(_WIN32)
  std::wstring cmd = L"\"" + Wide(exe) + L"\"";
  for (const std::string& a : args) {  // --name=value: the value in quotes (paths with spaces)
    const size_t eq = a.find('=');
    cmd += eq == std::string::npos ? L" " + Wide(a) : L" " + Wide(a.substr(0, eq + 1)) + L"\"" + Wide(a.substr(eq + 1)) + L"\"";
  }
  std::wstring dir = Wide(dir_utf8);
  STARTUPINFOW si{};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
    SetToast(title + " could not be started.", true);
    return false;
  }
  CloseHandle(pi.hThread);
  AllowSetForegroundWindow(pi.dwProcessId);  // the game brings its window to the front (game_window.cpp)
  sound::Play(sound::Sound::kLaunch);
  process_ = pi.hProcess;
  running_game_ = index;
  // The launcher steps aside while the game runs and comes back when it closes.
  SDL_MinimizeWindow(window_);
  return true;
#else
  // The game starts with the launcher's environment (the AppImage's library path) in its own folder; the launcher
  // stays alive while it runs (Steam and the AppImage mount follow the launcher's process).
  std::error_code ec;
  fs::create_directories(fs::u8path(dir_utf8), ec);
  std::vector<std::string> argv_s = {exe};
  argv_s.insert(argv_s.end(), args.begin(), args.end());
  std::vector<char*> argv;
  for (std::string& a : argv_s) argv.push_back(a.data());
  argv.push_back(nullptr);
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addchdir_np(&actions, dir_utf8.c_str());
  pid_t pid = 0;
  const int err = posix_spawn(&pid, exe.c_str(), &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  if (err != 0) {
    SetToast(title + " could not be started.", true);
    return false;
  }
  sound::Play(sound::Sound::kLaunch);
  process_ = reinterpret_cast<void*>(intptr_t(pid));
  running_game_ = index;
  SDL_MinimizeWindow(window_);
  return true;
#endif
}

void App::Idle() {
#if defined(_WIN32)
  const bool ended = process_ && WaitForSingleObject(static_cast<HANDLE>(process_), 0) == WAIT_OBJECT_0;
  if (ended) CloseHandle(static_cast<HANDLE>(process_));
#else
  int wstatus = 0;
  const bool ended = process_ && waitpid(pid_t(reinterpret_cast<intptr_t>(process_)), &wstatus, WNOHANG) > 0;
#endif
  if (ended) {
    process_ = nullptr;
    if (running_game_ >= 0 && running_game_ < kGameCount) {
      games_[running_game_]->achievements.loaded = false;  // it may have unlocked some
      games_[running_game_]->backups_read = -10.0;
    }
    running_game_ = -1;
    if (options_.test_play < 0) {
      SDL_RestoreWindow(window_);
      SDL_RaiseWindow(window_);
    } else {
      quit_ = true;  // testing: done when the game closes
    }
    RefreshStates();
  }
  // Downloads and update checks finish here, and disc installs the player left
  // running; the install screen finishes its own (then goes to the game's page).
  for (int i = 0; i < kGameCount; ++i) {
    GameJobs& jobs = games_[i]->jobs;
    if (jobs.kind == GameJobs::Kind::kDisc && view_ == View::kInstall && current_ == i) continue;
    if (PollJobs(Games()[i], jobs)) {
      RefreshStates();
      // The startup check stays quiet unless it installed something (no release yet, offline...).
      if (jobs.quiet && !jobs.report.installed) {
        jobs.message.clear();
        jobs.message_is_error = false;
      }
      if (!jobs.message.empty()) SetToast(jobs.message, jobs.message_is_error);
      jobs.quiet = false;
    }
  }
}

void App::DropFile(const std::string& path) {
  if (view_ != View::kInstall) return;
  GameUi& u = *games_[current_];
  u.disc_path = path;
  StartDiscInstall(Games()[current_], path, u.jobs);
}

// The launcher's own updates: the result of a check or install (shown on the Updates page), and, when updates are
// on, the new version found at startup installs by itself and the launcher restarts into it as soon as nothing
// else is going on (no game running, no download, no install, nothing open).
void App::PollSelfUpdate() {
  trg::UpdatesState& up = self_updates_;
  std::string error;
  if (const trg::Task::State st = up.task.Finish(&error); st != trg::Task::State::kIdle) {
    if (up.checking) {
      up.checking = false;
      up.message = st == trg::Task::State::kDone ? up.check.message : error.empty() ? "Cancelled." : error;
    } else if (up.installing) {
      up.installing = false;
      up.restart_ready = up.report.installed;
      up.message = up.report.installed ? up.report.message : error.empty() ? "Cancelled." : error;
      if (up.report.installed) up.check.found.reset();
    }
  }
  if (!launcher_settings_->GetBool("update_check", true)) return;
  if (!self_update_tried_ && !up.task.running() && trg::UpdateWaiting(up)) {
    self_update_tried_ = true;
    const trg::Release rel = *up.check.found;
    up.installing = true;
    up.message.clear();
    trg::InstallReport* out = &up.report;
    const trg::UpdaterConfig c = self_updater_;
    up.task.Start("Downloading version " + rel.version,
                  [c, rel, out](trg::Task& t) { return trg::InstallUpdate(t, c, rel, out); });
  }
  if (up.restart_ready && self_update_tried_ && !game_running() && !dash::ModalOpen()) {
    for (const auto& g : games_)
      if (g->jobs.task.running()) return;
    if (trg::RestartProgram()) Quit();
    else up.message = "Could not restart by itself. Close the launcher and start it again.";
    self_update_tried_ = false;  // once
  }
}

bool App::Frame() {
  if (options_.test_play >= 0 && !game_running()) return false;  // testing: the game closed
  // Testing: report the install or download result in install-test.txt or
  // download-test.txt and quit.
  for (int test : {options_.test_install, options_.test_download}) {
    if (test < 0 || test >= kGameCount) continue;
    GameJobs& jobs = games_[test]->jobs;
    if (jobs.task.running()) continue;
    PollJobs(Games()[test], jobs);
    const char* name = test == options_.test_install ? "/install-test.txt" : "/download-test.txt";
    FILE* f = std::fopen((LauncherDir() + name).c_str(), "w");
    if (f) {
      std::fprintf(f, "%s\nstate %d\nversion %s\n", jobs.message.c_str(), int(DetectState(Games()[test])),
                   InstalledVersion(Games()[test]).c_str());
      std::fclose(f);
    }
    return false;
  }
  art_->Poll();
  TickSteam();
  PollSelfUpdate();
  projects_->Poll();
  for (int i = 0; i < kGameCount; ++i) PollManage(i);
  if (view_ != drawn_view_) {
    dash::StartTransition();
    drawn_view_ = view_;
  }
  dash::BeginFrame(fonts_);
  switch (view_) {
    case View::kHome: DrawHome(*this); break;
    case View::kHub: DrawHub(*this, current_); break;
    case View::kSettings: DrawGameSettings(*this, current_); break;
    case View::kInstall: DrawInstall(*this, current_); break;
    case View::kCollection: DrawCollection(*this); break;
    case View::kProjects: DrawProjects(*this); break;
  }
  dash::EndFrame();
  Idle();
  return !quit_;
}

}  // namespace whopping
