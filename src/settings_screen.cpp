// Settings, as on the Xbox: sections down the left, the section's settings in
// the middle, and help for the focused setting on the right. Each game has its
// own (written to its own games/<id>/<id>.toml, which the game reads at start;
// keys starting "bk_" are the collection's own, read by the input hook added
// to each game). The collection's settings use the same screen.
//
// Up and down move, Right or A enters a section, Left or Right change a value,
// A toggles or opens, X puts a setting back, B goes back, LB and RB change
// section.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <trg/platform.h>

#include "app.h"
#include "sounds.h"

namespace whopping {
namespace {

using namespace dash;

struct Section {
  const char* name;
  std::function<void(Rows&)> draw;
};

// ------------------------------------------------------------- screen -----
// Returns true when B is pressed on the section list (leave the screen).
bool SettingsScreen(SettingsView& v, const std::vector<std::string>& crumbs, const std::vector<Section>& sections,
                    trg::SettingsStore* store, const Texture& blur) {
  const Frame& f = F();
  const int n = int(sections.size());
  v.section = std::clamp(v.section, 0, n - 1);

  bool leave = false;
  if (Pressed(Btn::kLB)) v.section = (v.section + n - 1) % n;
  if (Pressed(Btn::kRB)) v.section = (v.section + 1) % n;
  if (!v.in_rows) {
    if (Pressed(Btn::kUp)) v.section = std::max(0, v.section - 1);
    if (Pressed(Btn::kDown)) v.section = std::min(n - 1, v.section + 1);
    if (Pressed(Btn::kRight) || Pressed(Btn::kA)) {
      v.in_rows = v.rows[v.section].count > 0;
      Consume(Btn::kRight);  // the rows must not see the press that entered them
      Consume(Btn::kA);
    }
    if (Pressed(Btn::kB)) leave = true;
  } else if (Pressed(Btn::kB)) {
    v.in_rows = false;
    Consume(Btn::kB);
  }

  BlurBackdrop(blur);
  TopBar(crumbs);

  // Sections.
  for (int i = 0; i < n; ++i) {
    const ImVec2 a(f.X(48), f.Y(96 + i * 54.0f)), b(f.X(48 + 280), f.Y(96 + i * 54.0f + 48));
    const bool sel = i == v.section;
    const bool hover = Hover(a, b);
    if (sel)
      f.dl->AddRectFilled(a, b, v.in_rows ? White(0.16f) : White(), 8 * f.s);
    else if (hover)
      f.dl->AddRectFilled(a, b, White(0.08f), 8 * f.s);
    const ImVec2 t = TextSize(sel && !v.in_rows ? f.semibold : f.regular, 17, sections[i].name);
    Text(sel && !v.in_rows ? f.semibold : f.regular, 17, ImVec2(a.x + 20 * f.s, a.y + (b.y - a.y - t.y) * 0.5f),
         sel && !v.in_rows ? IM_COL32(17, 17, 20, 255) : White(sel ? 1.0f : 0.72f), sections[i].name);
    if (hover && ImGui::IsMouseClicked(0)) {
      v.section = i;
      v.in_rows = false;
    }
  }

  // The section's rows.
  const float left = 372, right = 940;
  Text(f.semibold, 28, f.P(left + 24, 90), White(), sections[v.section].name);
  RowsState& rs = v.rows[v.section];
  std::string help_title, help_text;
  bool back_out = false;
  {
    Rows rows(rs, f.P(left, 146), ImVec2(f.X(right), f.B(70)), v.in_rows, store);
    sections[v.section].draw(rows);
    help_title = rows.help_title();
    help_text = rows.help_text();
    if (rows.mouse_took_focus()) v.in_rows = true;
    back_out = rows.left_unused();
  }
  if (back_out) v.in_rows = false;

  // Help for the focused row.
  if (v.in_rows && !help_text.empty() && !help_title.empty()) {
    const float hx = std::max(right + 32, 964.0f);
    const ImVec2 a(f.X(hx), f.Y(146));
    const float w = std::max(200.0f, (f.size.x / f.s) - hx - 48);
    const float th = TextSize(f.regular, 15, help_text, w - 36).y;
    const ImVec2 b(a.x + w * f.s, a.y + 64 * f.s + th);
    Card(a, b, 0.07f);
    Text(f.semibold, 16, ImVec2(a.x + 18 * f.s, a.y + 18 * f.s), White(), help_title, w - 36);
    Text(f.regular, 15, ImVec2(a.x + 18 * f.s, a.y + 46 * f.s), White(0.66f), help_text, w - 36);
  }

  if (v.in_rows)
    HintBar({{Btn::kA, "Change"}, {Btn::kX, "Default"}, {Btn::kB, "Back"}, {Btn::kLB, ""}, {Btn::kRB, "Section"}});
  else
    HintBar({{Btn::kA, "Select"}, {Btn::kB, "Back"}, {Btn::kLB, ""}, {Btn::kRB, "Section"}});
  return leave;
}

// --------------------------------------------------------------- keys ------
// ImGui keys -> the key names ReXGlue's keyboard settings use (ui/keybinds.cpp).
std::string KeyName(ImGuiKey k) {
  if (k >= ImGuiKey_A && k <= ImGuiKey_Z) return std::string(1, char('A' + (k - ImGuiKey_A)));
  if (k >= ImGuiKey_0 && k <= ImGuiKey_9) return std::string(1, char('0' + (k - ImGuiKey_0)));
  if (k >= ImGuiKey_F1 && k <= ImGuiKey_F12) return "F" + std::to_string(1 + (k - ImGuiKey_F1));
  if (k >= ImGuiKey_Keypad0 && k <= ImGuiKey_Keypad9) return "Numpad" + std::to_string(k - ImGuiKey_Keypad0);
  switch (k) {
    case ImGuiKey_GraveAccent: return "Backtick";
    case ImGuiKey_Minus: return "Minus";
    case ImGuiKey_Equal: return "Plus";
    case ImGuiKey_Comma: return "Comma";
    case ImGuiKey_Period: return "Period";
    case ImGuiKey_Semicolon: return "Semicolon";
    case ImGuiKey_Slash: return "Slash";
    case ImGuiKey_Backslash: return "Backslash";
    case ImGuiKey_LeftBracket: return "LBracket";
    case ImGuiKey_RightBracket: return "RBracket";
    case ImGuiKey_Apostrophe: return "Quote";
    case ImGuiKey_Enter: return "Return";
    case ImGuiKey_KeypadEnter: return "NumpadEnter";
    case ImGuiKey_Space: return "Space";
    case ImGuiKey_Tab: return "Tab";
    case ImGuiKey_Backspace: return "Backspace";
    case ImGuiKey_Delete: return "Delete";
    case ImGuiKey_Insert: return "Insert";
    case ImGuiKey_Home: return "Home";
    case ImGuiKey_End: return "End";
    case ImGuiKey_PageUp: return "PageUp";
    case ImGuiKey_PageDown: return "PageDown";
    case ImGuiKey_LeftArrow: return "Left";
    case ImGuiKey_RightArrow: return "Right";
    case ImGuiKey_UpArrow: return "Up";
    case ImGuiKey_DownArrow: return "Down";
    case ImGuiKey_KeypadAdd: return "NumpadPlus";
    case ImGuiKey_KeypadSubtract: return "NumpadMinus";
    case ImGuiKey_KeypadMultiply: return "NumpadStar";
    case ImGuiKey_KeypadDivide: return "NumpadSlash";
    case ImGuiKey_CapsLock: return "CapsLock";
    case ImGuiKey_MouseLeft: return "LMB";
    case ImGuiKey_MouseRight: return "RMB";
    case ImGuiKey_MouseMiddle: return "MMB";
    default: return "";
  }
}

// What the SDK binds by default (input/mnk/mnk_input_driver.cpp), so the
// screen shows the real keys before the player changes anything.
struct Bind {
  const char* key;
  const char* label;
  const char* def;
};
const Bind kBinds[] = {
    {"keybind_lstick_up", "Move forward", "W"},
    {"keybind_lstick_down", "Move back", "S"},
    {"keybind_lstick_left", "Move left", "A"},
    {"keybind_lstick_right", "Move right", "D"},
    {"keybind_rstick_up", "Look up", "Up"},
    {"keybind_rstick_down", "Look down", "Down"},
    {"keybind_rstick_left", "Look left", "Left"},
    {"keybind_rstick_right", "Look right", "Right"},
    {"keybind_a", "A button", "Semicolon,Space"},
    {"keybind_b", "B button", "Quote,Backspace"},
    {"keybind_x", "X button", "L"},
    {"keybind_y", "Y button", "P"},
    {"keybind_left_trigger", "Left trigger", "Q,I"},
    {"keybind_right_trigger", "Right trigger", "E,O"},
    {"keybind_left_shoulder", "Left bumper", "1"},
    {"keybind_right_shoulder", "Right bumper", "3"},
    {"keybind_lstick_press", "Left stick click", "F"},
    {"keybind_rstick_press", "Right stick click", "K"},
    {"keybind_dpad_up", "D pad up", "Shift+Up"},
    {"keybind_dpad_down", "D pad down", "Shift+Down"},
    {"keybind_dpad_left", "D pad left", "Shift+Left"},
    {"keybind_dpad_right", "D pad right", "Shift+Right"},
    {"keybind_start", "Start", "X,Return"},
    {"keybind_back", "Back", "Z,Tab"},
};

// Controller buttons for remapping (bk_remap_<button> = the button it acts as).
const char* kButtons[] = {"a", "b", "x", "y", "lb", "rb", "lt", "rt", "start", "back", "ls", "rs"};
const char* kButtonNames[] = {"A",     "B",    "X",                "Y",
                              "Left bumper", "Right bumper", "Left trigger", "Right trigger",
                              "Start", "Back", "Left stick click", "Right stick click"};

std::vector<Option> ButtonOptions() {
  std::vector<Option> o;
  for (size_t i = 0; i < std::size(kButtons); ++i) o.push_back({kButtons[i], kButtonNames[i]});
  o.push_back({"none", "Nothing"});
  return o;
}

std::string Pretty(const std::string& bind) {
  std::string out;
  for (char c : bind) out += c == ',' ? std::string("  or  ") : std::string(1, c);
  return out;
}

// The game's render scale for an upscaler mode, as the game works it out (render_quality.cpp in each game).
int RenderScaleFor(const std::string& mode, int output_height) {
  double ratio = 0.0;
  if (mode == "native") ratio = 1.0;
  if (mode == "quality") ratio = 1.5;
  if (mode == "balanced") ratio = 1.7;
  if (mode == "performance") ratio = 2.0;
  if (ratio <= 0.0) return 0;
  const int scale = std::clamp(int(std::lround(std::max(1, output_height) / ratio / 720.0)), 1, 8);
  if (mode == "performance") return std::max(1, std::min(scale, RenderScaleFor("quality", output_height) - 1));
  return scale;
}

// --------------------------------------------------------- game sections ---
std::vector<Section> GameSections(App& app, int g) {
  trg::SettingsStore& st = *app.game_ui(g).settings;
  std::vector<Section> s;

  s.push_back({"Display", [&st](Rows& r) {
    r.Choice("Window mode", "Fullscreen fills your screen. Windowed opens a window you can move and resize.",
             "fullscreen", "true", {{"true", "Fullscreen"}, {"false", "Windowed"}});
    // The windowed size: window_width and window_height together (logical pixels, so Windows display scaling
    // still applies). Not the SDK's "resolution" preset, which also changes the game's own video mode
    // (the guest stays at 1280 x 720).
    {
      static const int sizes[][2] = {{0, 0}, {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
      static const std::vector<std::string> names = {"Automatic", "1280 \xC3\x97 720", "1600 \xC3\x97 900",
                                                     "1920 \xC3\x97 1080", "2560 \xC3\x97 1440", "3840 \xC3\x97 2160"};
      const int w = st.GetInt("window_width", 0), h = st.GetInt("window_height", 0);
      int cur = -1;
      for (int i = 0; i < int(std::size(sizes)); ++i)
        if (sizes[i][0] == w && sizes[i][1] == h) cur = i;
      if (r.ChoiceIndex("Window size", "The size of the window when the game is windowed.", cur, names, 0)) {
        st.SetInt("window_width", sizes[cur][0]);
        st.SetInt("window_height", sizes[cur][1]);
        st.Save();
      }
    }
    r.Choice("Monitor", "Which screen the game opens on.", "monitor", "0",
             {{"0", "Main"}, {"1", "Second"}, {"2", "Third"}, {"3", "Fourth"}});
    // VSync is the presenter's tearing setting (as in King Kong), not the game side "vsync", which paces the
    // game itself.
    {
      bool on = !st.GetBool("d3d12_allow_variable_refresh_rate_and_tearing", true);
      if (r.ToggleValue("Vertical sync", "Matches the picture to your screen and stops tearing.", on, false)) {
        st.SetBool("d3d12_allow_variable_refresh_rate_and_tearing", !on);
        st.SetBool("vulkan_allow_present_mode_immediate", !on);
        st.Save();
      }
    }
    // bk_frame_rate: each game paces itself to this in its frame_stats.cpp (0 is no limit); 60 by default.
    r.Choice("Frame rate", "30 is how the game ran on the Xbox 360. Higher is smoother and the game still runs at the "
             "right speed. No limit runs as fast as your PC allows.", "bk_frame_rate", "60",
             {{"30", "30"}, {"60", "60"}, {"120", "120"}, {"144", "144"}, {"165", "165"}, {"240", "240"},
              {"0", "No limit"}});
    r.Toggle("Keep the original shape", "Keeps the picture 16:9 with black bars instead of stretching it to fill "
             "a screen of another shape.", "present_letterbox", true);
  }});

  s.push_back({"Graphics", [&st](Rows& r) {
    // Upscaler and its mode as in King Kong Recompiled 1.9.x: the mode picks
    // the render scale from the screen when the game starts (render_quality.cpp in each game); with the upscaler
    // off the render resolution is chosen directly.
    if (r.Choice("Upscaler", "Scales the picture up to your screen. AMD FSR 1 works on any graphics card, NVIDIA NIS "
                 "suits NVIDIA cards. Off draws at the render resolution you choose.", "present_effect", "bilinear",
                 {{"bilinear", "Off"}, {"fsr", "AMD FSR 1"}, {"nis", "NVIDIA NIS"}})) {
      const bool on = st.Get("present_effect", "bilinear") != "bilinear";
      if (on && st.Get("bk_render_quality", "custom") == "custom") st.Set("bk_render_quality", "quality");
      if (!on) st.Set("bk_render_quality", "custom");
      st.Save();
    }
    if (st.Get("present_effect", "bilinear") != "bilinear") {
      SDL_DisplayMode dm{};
      const int screen_h = SDL_GetDesktopDisplayMode(0, &dm) == 0 ? dm.h : 1080;
      const int scale = RenderScaleFor(st.Get("bk_render_quality", "quality"), screen_h);
      const std::string draws = scale > 0 ? "On your " + std::to_string(screen_h) + "p screen the game draws at " +
                                                std::to_string(1280 * scale) + " \xC3\x97 " +
                                                std::to_string(720 * scale) + "."
                                          : std::string();
      r.Choice("Upscaler mode", "Quality draws closest to your screen, Performance draws smaller for speed. The "
               "game draws in steps of its original 720p, so two modes can give the same size. " + draws,
               "bk_render_quality", "quality",
               {{"native", "Native"}, {"quality", "Quality"}, {"balanced", "Balanced"}, {"performance", "Performance"}});
    } else {
      r.Choice("Render resolution", "Draws the game at a higher resolution for a sharper picture. Higher settings "
               "need a faster graphics card.", "resolution_scale", "1",
               {{"1", "Original (720p)"}, {"2", "2x (1440p)"}, {"3", "3x (4K)"}, {"4", "4x"}, {"5", "5x"},
                {"6", "6x"}});
    }
    r.Choice("Anti aliasing", "Smooths jagged edges.", "swap_post_effect", "fxaa",
             {{"none", "Off"}, {"fxaa", "FXAA"}, {"fxaa_extreme", "FXAA strong"}});
    r.Choice("Texture filtering", "Keeps ground and wall textures sharp when seen at an angle.",
             "anisotropic_override", "3",
             {{"-1", "Original"}, {"2", "2x"}, {"3", "4x"}, {"4", "8x"}, {"5", "16x"}});
  }});

  s.push_back({"Controller", [&st](Rows& r) {
    r.Toggle("Invert horizontal", "Turns the camera the other way left and right.", "bk_invert_x", false);
    r.Toggle("Invert vertical", "Turns the camera the other way up and down.", "bk_invert_y", false);
    r.SliderInt("Camera sensitivity", "How fast the right stick turns the camera.", "bk_stick_sensitivity", 100, 25,
                200, 5, "%d%%");
    r.SliderInt("Stick deadzone", "How far a stick moves before the game notices.", "bk_deadzone", 0, 0, 40, 1,
                "%d%%");
    r.SliderInt("Vibration", "How strongly the controller rumbles.", "bk_vibration", 100, 0, 100, 5, "%d%%");
    r.Heading("BUTTON REMAPPING");
    static const std::vector<Option> options = ButtonOptions();
    for (size_t i = 0; i < std::size(kButtons); ++i) {
      const std::string key = std::string("bk_remap_") + kButtons[i];
      r.Choice(std::string(kButtonNames[i]) + " acts as", "What the game sees when you press this button.",
               key.c_str(), kButtons[i], options);
    }
    (void)st;
  }});

  s.push_back({"Keyboard and mouse", [&st, g](Rows& r) {
    r.Toggle("Keyboard and mouse", "Play with the keyboard and mouse instead of a controller.", "mnk_mode", false);
    r.Toggle("Mouse look", "The mouse turns the camera. Off: the look keys do.", "mnk_mouse", false);
    // Sneak King's mouse turns its camera directly (its mouse_look.cpp), with its own setting; the other games
    // turn the mouse into a right stick push (the runtime's mnk_sensitivity).
    if (std::string_view(Games()[g].id) == "sneak_king") {
      r.SliderFloat("Mouse sensitivity", "How fast the mouse turns the camera. The same mouse movement always turns "
                    "it the same amount, at any speed.", "bk_mouse_sensitivity", 1.0f, 0.1f, 5.0f, 0.1f, "%.1f");
      r.Toggle("Invert mouse up and down", "Moving the mouse up looks down.", "bk_mouse_invert_y", false);
    } else {
      r.SliderFloat("Mouse sensitivity", "How fast the mouse turns the camera.", "mnk_sensitivity", 1.0f, 0.1f, 5.0f,
                    0.1f, "%.1f");
    }
    r.Heading("KEYS");
    for (const Bind& b : kBinds) {
      const std::string cur = st.Get(b.key, b.def);
      if (r.KeyBind(b.label, Pretty(cur))) {
        const char* key = b.key;
        trg::SettingsStore* store = &st;
        CaptureKey(b.label, [store, key](ImGuiKey k) {
          const std::string name = KeyName(k);
          if (name.empty()) return false;
          store->Set(key, name);
          store->Save();
          return true;
        });
      }
    }
    if (r.Button("Reset keys", "Puts every key back to how it started.")) {
      trg::SettingsStore* store = &st;
      Confirm("Reset keys?", "Every key goes back to how it started.", "Reset", [store] {
        for (const Bind& b : kBinds) store->Set(b.key, b.def);
        store->Save();
      });
    }
  }});

  s.push_back({"Audio", [](Rows& r) {
    r.Toggle("Mute", "Turns all game sound off.", "audio_mute", false);
  }});

  s.push_back({"Achievements", [&app, g](Rows& r) {
    if (app.state(g) != GameState::kInstalled) {
      r.Paragraph("Install the game to see its achievements.");
      return;
    }
    GameAchievements& ach = app.achievements(g);
    // Notifications in game (achievement_toast.cpp in each game), as in the TRG ports.
    trg::SettingsStore& st = *app.game_ui(g).settings;
    r.Heading("NOTIFICATIONS");
    r.Toggle("Notifications", "A pop up when you unlock an achievement in game.", "bk_achievement_toasts", true);
    r.Toggle("Sound", "The chime that plays with each pop up.", "bk_achievement_sound", true);
    r.SliderInt("Volume", "How loud the chime is.", "bk_achievement_volume", 80, 0, 100, 5, "%d%%");
    if (r.Button("Test notification", "Shows a sample notification right now, with the chime.")) {
      if (!st.GetBool("bk_achievement_toasts", true)) {
        app.SetToast("Notifications are off. Switch them on to see the test.");
      } else {
        static int next = 0;
        const Texture icon = ach.icons.empty() ? app.art().Get(g, ArtKind::kGrid) : ach.icons[size_t(next++) % ach.icons.size()];
        dash::ShowAchievement(icon, "Test achievement", 10, st.GetBool("bk_achievement_sound", true),
                              st.GetInt("bk_achievement_volume", 80));
      }
    }
    if (!ach.list_found) {
      r.Paragraph("Start the game once to see its achievements.");
      return;
    }
    r.Heading("ACHIEVEMENTS");
    char line[96];
    std::snprintf(line, sizeof line, "%d of %d unlocked   \xC2\xB7   %d of %d G", ach.unlocked, int(ach.cards.size()),
                  ach.points, ach.total_points);
    r.Paragraph(line, false);
    for (size_t i = 0; i < ach.cards.size(); ++i) r.Achievement(ach.icons[i], ach.cards[i]);
  }});

  s.push_back({"Saves and install", [&app, g](Rows& r) {
    const GameInfo& game = Games()[g];
    GameUi& gu = app.game_ui(g);
    const GameState st = app.state(g);
    const bool busy = gu.manage.running();
    if (busy) r.Progress(gu.manage.label(), gu.manage.fraction());
    r.Heading("SAVES");
    if (r.Button("Back up saves", "Makes a zip of your saves in the backups folder.", "Back up now") && !busy)
      gu.manage.Start("Backing up", [&game](trg::Task& t) { return BackupSavesJob(t, game); });
    for (const Backup& b : app.backups(g)) {
      if (r.Button("Backup from " + b.label, "Puts this backup in place of your saves. Your current saves are "
                   "backed up first.", "Restore") && !busy) {
        const std::string path = b.path;
        GameUi* u = &gu;
        Confirm("Restore this backup?", "Your current saves are backed up first, so you can go back.", "Restore",
                [u, &game, path] {
                  u->manage.Start("Restoring", [&game, path](trg::Task& t) {
                    return RestoreBackupJob(t, game, path);
                  });
                });
      }
    }
    if (r.Button("Open the save folder", "Where the game keeps your profile, saves and achievements."))
      trg::OpenInFileManager(SavesDir(game));
    if (r.Button("Open the backups folder", "Your save backups.")) trg::OpenInFileManager(BackupsDir(game));
    if (r.Button("Delete saves", "Removes your saves. A backup is made first.") && !busy) {
      GameUi* u = &gu;
      Confirm("Delete your saves?", "A backup is made first, so you can restore them later.", "Delete",
              [u, &game] { u->manage.Start("Deleting saves", [&game](trg::Task& t) { return DeleteSavesJob(t, game); }); });
    }
    r.Heading("INSTALL");
    if (r.Button("Open the game folder", "The game's program files and the files from your disc."))
      trg::OpenInFileManager(GameDir(game));
    if (st == GameState::kInstalled &&
        r.Button("Reinstall from disc", "Copies the game files from your disc image again."))
      app.OpenInstall(g);
    if (st != GameState::kNotDownloaded &&
        r.Button("Remove game", "Deletes the game from this PC. Your saves and backups stay.") && !busy &&
        !app.game_running()) {
      GameUi* u = &gu;
      App* a = &app;
      Confirm("Remove " + std::string(game.title) + "?", "The game is deleted from this PC. Your saves and backups "
              "are kept.", "Remove", [u, a, &game] {
                u->manage.Start("Removing", [&game](trg::Task& t) { return RemoveGameJob(t, game); });
                (void)a;
              });
    }
  }});

  s.push_back({"Updates", [&app, g](Rows& r) {
    const GameInfo& game = Games()[g];
    GameJobs& jobs = app.game_ui(g).jobs;
    const std::string v = InstalledVersion(game);
    r.Info("Version", v.empty() ? "Not downloaded" : v);
    if (jobs.task.running()) r.Progress(jobs.task.label(), jobs.task.fraction());
    else if (r.Button("Check for updates", "Looks for a new version now and installs it."))
      StartDownload(game, jobs);
    if (!jobs.message.empty() && !jobs.task.running()) r.Paragraph(jobs.message);
    if (r.Button("Release notes", "What is new in each version, on GitHub."))
      trg::OpenUrl(std::string("https://github.com/") + game.repo + "/releases");
    r.Info("Releases from", game.repo);
  }});

  s.push_back({"About", [&app, g](Rows& r) {
    const GameInfo& game = Games()[g];
    char id[16];
    std::snprintf(id, sizeof id, "%08X", game.title_id);
    r.Paragraph(std::string(game.title) + " for PC, made by static recompilation of the Xbox 360 game. You need "
                "your own disc.");
    r.Info("Game", game.title);
    r.Info("Xbox 360 title ID", id);
    r.Info("Media ID", game.media_id);
    r.Info("Disc version", game.disc_version);
    r.Info("Disc image", std::string(game.image_name) + ", " + game.disc_size);
    r.Info("SHA1", game.disc_sha1);
    r.Info("MD5", game.disc_md5);
    r.Info("CRC32", game.disc_crc32);
    r.Info("Settings file", GameSettingsPath(game));
    if (r.Button("Reset all settings", "Puts every setting of this game back to how it started.")) {
      trg::SettingsStore* store = app.game_ui(g).settings.get();
      Confirm("Reset all settings?", "Every setting of " + std::string(game.title) + " goes back to how it started.",
              "Reset", [store] {
                store->ResetToDefaults();
                store->Save();
              });
    }
  }});
  return s;
}

const char* kGameSectionNames[] = {"Display",      "Graphics",          "Controller", "Keyboard and mouse", "Audio",
                                   "Achievements", "Saves and install", "Updates",    "About"};

}  // namespace

int FindGameSection(const char* name) {
  for (int i = 0; i < int(std::size(kGameSectionNames)); ++i)
    if (std::string(kGameSectionNames[i]) == name) return i;
  return -1;
}

void DrawGameSettings(App& app, int g) {
  GameUi& gu = app.game_ui(g);
  const std::vector<Section> sections = GameSections(app, g);
  if (SettingsScreen(gu.settings_view, {Games()[g].title, "Settings"}, sections, gu.settings.get(),
                     app.art().Blur(g)))
    app.OpenHub(g);
}

// ---------------------------------------------------- collection screen ---
void DrawCollection(App& app) {
  trg::UpdatesState& up = app.self_updates();
  const trg::UpdaterConfig& cfg = app.self_updater();
  std::vector<Section> s;
  s.push_back({"General", [&app](Rows& r) {
    r.Toggle("Install game updates by themselves", "New versions of the games download and install when the "
             "launcher starts.", "auto_update", true);
    r.Toggle("Install launcher updates by themselves", "A new version of this launcher downloads when it starts, "
             "then it restarts into it.", "update_check", true);
    if (r.Toggle("Interface sounds", "Soft sounds as you move around the launcher.", "ui_sounds", true))
      sound::SetEnabled(app.launcher_settings().GetBool("ui_sounds", true));
    if (r.Toggle("Fullscreen", "The launcher fills the screen. F11 switches at any time.", "fullscreen", true)) {
      const bool want = app.launcher_settings().GetBool("fullscreen", true);
      if (want != IsFullscreen(app.window())) SetFullscreen(app.window(), want);
    }
  }});
  s.push_back({"Updates", [&app, &up, &cfg](Rows& r) {
    // The launcher's own updates (the TRG updater's state, kept by App::PollSelfUpdate).
    r.Info("Version", WHOPPING_VERSION);
    if (up.task.running()) {
      r.Progress(up.task.label(), up.task.fraction());
    } else if (up.restart_ready) {
      if (r.Button("Restart now", "Starts the new version of the launcher.")) {
        if (trg::RestartProgram()) app.Quit();
        else up.message = "Could not restart by itself. Close the launcher and start it again.";
      }
    } else if (trg::UpdateWaiting(up)) {
      const trg::Release rel = *up.check.found;
      if (r.Button("Install version " + rel.version, "Downloads the new launcher and puts it in place. Your games, "
                   "saves and settings stay as they are.")) {
        up.installing = true;
        up.message.clear();
        trg::InstallReport* out = &up.report;
        const trg::UpdaterConfig c = cfg;
        up.task.Start("Downloading version " + rel.version,
                      [c, rel, out](trg::Task& t) { return trg::InstallUpdate(t, c, rel, out); });
      }
    } else if (r.Button("Check for updates", "Looks for a new version of the launcher now.")) {
      trg::StartUpdateCheck(up, cfg, "stable");
    }
    if (!up.message.empty()) r.Paragraph(up.message);
  }});
  s.push_back({"About", [](Rows& r) {
    r.Paragraph("One launcher for the three Burger King Xbox 360 games: Sneak King, Big Bumpin' and PocketBike "
                "Racer. Each game is a native PC recompilation and needs your own disc.");
    r.Info("Version", WHOPPING_VERSION);
    r.Info("Game art", "SteamGridDB");
    if (r.Button("Open the launcher folder", "Where the games, backups and cached art are kept."))
      trg::OpenInFileManager(LauncherDir());
  }});
  if (SettingsScreen(app.collection_view(), {"Home", "Settings"}, s, &app.launcher_settings(), app.art().Blur(0)))
    app.OpenHome();
}

}  // namespace whopping
