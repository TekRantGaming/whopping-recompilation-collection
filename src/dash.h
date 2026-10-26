// The dashboard look the launcher is drawn in (after the Xbox Series home
// screen): full screen game art, big type, a white focus frame, and the
// controller's buttons along the bottom. Every screen is laid out on a 1280 x
// 720 grid and scaled to the window.
//
// Input is one set of console actions (Btn) fed by the controller, the
// keyboard and the mouse, so each screen handles A, B, LB... once. Dear ImGui's
// own navigation is not used: the screens keep their own focus.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <imgui.h>

#include <trg/launcher.h>
#include <trg/settings.h>

#include "art.h"

namespace whopping::dash {

// ------------------------------------------------------------- frame ------
struct Frame {
  ImVec2 o, size, end;  // the window, in screen pixels
  float s = 1.0f;       // pixels per grid unit (1 at 1280 x 720)
  ImDrawList* dl = nullptr;
  ImFont* regular = nullptr;
  ImFont* semibold = nullptr;
  ImFont* bold = nullptr;
  float X(float x) const { return o.x + x * s; }    // from the left edge
  float Y(float y) const { return o.y + y * s; }    // from the top edge
  float R(float x) const { return end.x - x * s; }  // from the right edge
  float B(float y) const { return end.y - y * s; }  // from the bottom edge
  ImVec2 P(float x, float y) const { return ImVec2(X(x), Y(y)); }
};
const Frame& F();
void BeginFrame(const trg::Fonts& fonts);  // opens the full window and reads input
void StartTransition();  // the next frames fade and slide in (a new screen)
float Ease(double since, double seconds);  // 0..1, eased out, for small animations
void EndFrame();                           // draws the modal (if any) and closes the window

// ------------------------------------------------------------- input ------
enum class Btn { kUp, kDown, kLeft, kRight, kA, kB, kX, kY, kLB, kRB, kStart, kCount };
bool Pressed(Btn b);  // this frame (directions repeat while held); false while a modal is open
void Consume(Btn b);  // so later code this frame does not see it
void Inject(Btn b);   // pressed next frame (testing, and clicked hints)
bool PadMode();       // the controller was used last: hints show its buttons, not keys
bool MouseMoved();
bool Hover(ImVec2 a, ImVec2 b);    // false while a modal is open
bool Clicked(ImVec2 a, ImVec2 b);  // left click inside, this frame
float Wheel();                     // mouse wheel this frame (rows)

// ------------------------------------------------------------ drawing -----
inline ImU32 White(float a = 1.0f) { return IM_COL32(255, 255, 255, int(255 * a)); }
inline ImU32 Black(float a = 1.0f) { return IM_COL32(0, 0, 0, int(255 * a)); }
ImU32 Accent(const ImVec4& c, float a = 1.0f);

// Sizes are in grid units (scaled here).
void Text(ImFont* font, float size, ImVec2 pos, ImU32 col, const std::string& text, float wrap = 0.0f);
ImVec2 TextSize(ImFont* font, float size, const std::string& text, float wrap = 0.0f);  // in pixels
void TextRight(ImFont* font, float size, ImVec2 right_top, ImU32 col, const std::string& text);

// Draws the texture over the rectangle, cropping to keep its shape; focus_x
// picks which part of a wide image stays in view.
void Cover(const Texture& tex, ImVec2 a, ImVec2 b, float alpha, float focus_x = 0.5f, float rounding = 0.0f);
// Draws the texture inside the rectangle whatever its shape: close to the rectangle's shape it fills it;
// otherwise it is fitted whole over a dimmed, filled copy of itself (art made for another tile shape).
void FitArt(const Texture& tex, ImVec2 a, ImVec2 b, float rounding = 0.0f);
// The full screen art with the dashboard's shade (dark on the left and bottom).
void Backdrop(const Texture& hero, float alpha, float focus_x);
void Shade();
// The art blurred and dimmed, behind settings.
void BlurBackdrop(const Texture& blur);

void FocusRing(ImVec2 a, ImVec2 b, float rounding);  // glides to each new place and pulses gently
void Card(ImVec2 a, ImVec2 b, float alpha = 0.08f);  // a frosted panel
void Bar(ImVec2 a, ImVec2 b, float fraction, ImU32 col);  // fraction < 0: busy, size unknown

// A rounded button with text (and a play triangle when `play`); returns its
// rectangle so the caller can test focus and clicks.
struct PillRect {
  ImVec2 a, b;
};
PillRect Pill(ImVec2 pos, const std::string& label, bool primary, bool focused, bool play = false,
              float height = 48.0f);
float PillWidth(const std::string& label, bool play = false);

// Top line: "Home  >  Sneak King" on the left, the clock on the right.
// `gear` adds a settings button there; returns true when it is clicked.
bool TopBar(const std::vector<std::string>& crumbs, bool gear = false);

// The controller buttons along the bottom right. Clicking one acts like
// pressing it (next frame).
struct Hint {
  Btn btn;
  std::string label;
};
void HintBar(const std::vector<Hint>& hints);
void Glyph(Btn b, ImVec2 pos, float size = 22.0f);  // A, B, LB... or the key that does it
float GlyphWidth(Btn b, float size = 22.0f);

void Toast(const std::string& text, bool error);  // shown for a few seconds
void DrawToast();

// ------------------------------------------------------------- modals -----
// One at a time, drawn over the screen; input goes to it until it closes.
void Confirm(std::string title, std::string text, std::string yes_label, std::function<void()> on_yes);
// Waits for a key or mouse button (Esc or the controller's B cancels); closes
// when on_key returns true (false: a key that cannot be bound, keep waiting).
void CaptureKey(std::string what, std::function<bool(ImGuiKey)> on_key);
// Two choices, both doing something (B or a click outside closes it doing neither); the first has the focus.
void Choose(std::string title, std::string text, std::string first_label, std::function<void()> on_first,
            std::string second_label, std::function<void()> on_second);
bool ModalOpen();

// An achievement notification like the games' own (achievement_toast.cpp in each game): the green orb
// with the icon grows in and unfolds into a bar. Plays the chime at `volume` percent when `sound`.
void ShowAchievement(const Texture& icon, std::string title, int points, bool sound, int volume);

// --------------------------------------------------------------- rows -----
// A scrolling list of setting rows, written fresh each frame (immediate mode):
// up and down move the focus, left and right change the focused value, A
// activates, X puts a setting back to its default. The focused row's help
// text is kept for the side panel.
struct RowsState {
  int focus = 0;
  float scroll = 0.0f;
  int count = 0;           // focusable rows last frame
  float content = 0.0f;    // height last frame
  float highlight = -1.0f; // where the focus highlight is drawn (slides to the focused row)
};

struct Option {
  std::string value, label;
};

class Rows {
 public:
  // `active`: the list has the controller's focus (else it only draws).
  Rows(RowsState& state, ImVec2 a, ImVec2 b, bool active, trg::SettingsStore* store = nullptr);
  ~Rows();

  void Heading(const std::string& text);
  void Paragraph(const std::string& text, bool dim = true);
  void Info(const std::string& label, const std::string& value);
  void Space(float h) { y_ += h * s_; }
  bool Button(const std::string& label, const std::string& help, const std::string& value = "");
  bool Toggle(const std::string& label, const std::string& help, const char* key, bool def);
  bool ToggleValue(const std::string& label, const std::string& help, bool& value, bool def);
  bool Choice(const std::string& label, const std::string& help, const char* key, const std::string& def,
              const std::vector<Option>& options);
  bool ChoiceIndex(const std::string& label, const std::string& help, int& index,
                   const std::vector<std::string>& names, int def_index = -1);
  bool SliderInt(const std::string& label, const std::string& help, const char* key, int def, int min, int max,
                 int step, const char* fmt);
  bool SliderFloat(const std::string& label, const std::string& help, const char* key, float def, float min,
                   float max, float step, const char* fmt);
  bool KeyBind(const std::string& label, const std::string& value);  // true: start capturing
  void Progress(const std::string& label, float fraction);
  void Achievement(const Texture& icon, const trg::AchievementCard& card);

  // A Left press the focused row did not use (the screen moves focus out).
  bool left_unused() const { return left_unused_; }
  bool mouse_took_focus() const { return mouse_took_; }
  const std::string& help_title() const { return help_title_; }
  const std::string& help_text() const { return help_text_; }

 private:
  // Starts a focusable row of height h; returns whether it is focused.
  bool Begin(float h, const std::string& label, const std::string& help);
  void Saved();
  ImVec2 RowA() const { return ImVec2(a_.x, y_); }

  RowsState& st_;
  ImVec2 a_, b_;
  bool active_;
  trg::SettingsStore* store_;
  float s_, y_, row_top_ = 0, row_h_ = 0;
  int index_ = 0;
  bool focused_ = false, left_unused_ = false, mouse_took_ = false, row_clicked_ = false;
  bool up_ = false, down_ = false, left_ = false, right_ = false, a_btn_ = false, x_btn_ = false;
  float focus_top_ = -1, focus_bottom_ = -1;
  std::string help_title_, help_text_;
};

}  // namespace whopping::dash
