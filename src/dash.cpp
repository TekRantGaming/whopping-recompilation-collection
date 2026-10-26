#include "dash.h"

#include <SDL.h>

#include "sounds.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace whopping::dash {
namespace {

Frame g_f;
bool g_raw[int(Btn::kCount)];      // this frame, before modals take it
bool g_pressed[int(Btn::kCount)];  // what screens see
bool g_pending[int(Btn::kCount)];  // clicked hints, pressed next frame
bool g_pad = false, g_pad_known = false;

struct Modal {
  enum class Kind { kNone, kConfirm, kKey } kind = Kind::kNone;
  std::string title, text, yes, no = "Cancel";
  std::function<void()> on_yes, on_no;
  std::function<bool(ImGuiKey)> on_key;
  int focus = 1;  // confirm: 0 yes, 1 cancel (the safe one first)
  int frames = 0;
  double since = 0.0;
};
Modal g_modal;

std::string g_toast;
bool g_toast_error = false;
double g_toast_until = 0.0;
double g_toast_since = 0.0;

double g_transition = -10.0;  // when the current screen appeared

struct AchievementNote {
  Texture icon;
  std::string title;
  int points = 0;
  double started = -1.0;
};
AchievementNote g_note;
// The focus ring's animated rectangle (slot 1 is the modal's own).
struct Ring {
  ImVec2 a, b;
  int last_frame = -10;
};
Ring g_rings[16];
int g_ring_slot = 0;   // 1 while the modal draws
int g_ring_count = 0;  // rings drawn this frame: each one in the frame's order glides on its own

struct Keys {
  ImGuiKey k[4];
};
// The controller button first, then the keys that do the same.
const Keys kMap[int(Btn::kCount)] = {
    {{ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadLStickUp, ImGuiKey_UpArrow, ImGuiKey_None}},
    {{ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadLStickDown, ImGuiKey_DownArrow, ImGuiKey_None}},
    {{ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadLStickLeft, ImGuiKey_LeftArrow, ImGuiKey_None}},
    {{ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadLStickRight, ImGuiKey_RightArrow, ImGuiKey_None}},
    {{ImGuiKey_GamepadFaceDown, ImGuiKey_Enter, ImGuiKey_KeypadEnter, ImGuiKey_Space}},
    {{ImGuiKey_GamepadFaceRight, ImGuiKey_Escape, ImGuiKey_Backspace, ImGuiKey_None}},
    {{ImGuiKey_GamepadFaceLeft, ImGuiKey_Delete, ImGuiKey_None, ImGuiKey_None}},
    {{ImGuiKey_GamepadFaceUp, ImGuiKey_Tab, ImGuiKey_None, ImGuiKey_None}},
    {{ImGuiKey_GamepadL1, ImGuiKey_Q, ImGuiKey_PageUp, ImGuiKey_None}},
    {{ImGuiKey_GamepadR1, ImGuiKey_E, ImGuiKey_PageDown, ImGuiKey_None}},
    {{ImGuiKey_GamepadStart, ImGuiKey_None, ImGuiKey_None, ImGuiKey_None}},
};
const char* kKeyCap[int(Btn::kCount)] = {"Up", "Down", "Left", "Right", "Enter", "Esc", "Del", "Tab", "Q", "E", "Enter"};

bool IsDirection(Btn b) { return b == Btn::kUp || b == Btn::kDown || b == Btn::kLeft || b == Btn::kRight; }

void ReadInput() {
  ImGuiIO& io = ImGui::GetIO();
  if (!g_pad_known) {
    g_pad = (io.BackendFlags & ImGuiBackendFlags_HasGamepad) != 0;
    g_pad_known = true;
  }
  for (int k = ImGuiKey_GamepadStart; k <= ImGuiKey_GamepadRStickDown; ++k)
    if (ImGui::IsKeyPressed(ImGuiKey(k), false)) g_pad = true;
  for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_GamepadStart; ++k)
    if (ImGui::IsKeyPressed(ImGuiKey(k), false)) g_pad = false;
  if (io.MouseDelta.x * io.MouseDelta.x + io.MouseDelta.y * io.MouseDelta.y > 4.0f || ImGui::IsMouseClicked(0))
    g_pad = false;

  for (int b = 0; b < int(Btn::kCount); ++b) {
    bool p = g_pending[b];
    g_pending[b] = false;
    for (ImGuiKey k : kMap[b].k)
      if (k != ImGuiKey_None && ImGui::IsKeyPressed(k, IsDirection(Btn(b)))) p = true;
    g_raw[b] = p;
    g_pressed[b] = p && g_modal.kind == Modal::Kind::kNone;
  }
  // Interface sounds for the controller and keyboard (the mouse plays its own where focus moves).
  using whopping::sound::Sound;
  if (g_raw[int(Btn::kA)]) whopping::sound::Play(Sound::kSelect);
  else if (g_raw[int(Btn::kB)]) whopping::sound::Play(Sound::kBack);
  else if (g_raw[int(Btn::kUp)] || g_raw[int(Btn::kDown)] || g_raw[int(Btn::kLeft)] || g_raw[int(Btn::kRight)] ||
           g_raw[int(Btn::kLB)] || g_raw[int(Btn::kRB)])
    whopping::sound::Play(Sound::kMove);
}

void Triangle(ImVec2 c, float r, int dir, ImU32 col) {  // dir: 0 left, 1 right
  ImDrawList* dl = g_f.dl;
  if (dir == 0)
    dl->AddTriangleFilled(ImVec2(c.x - r, c.y), ImVec2(c.x + r * 0.7f, c.y - r), ImVec2(c.x + r * 0.7f, c.y + r), col);
  else
    dl->AddTriangleFilled(ImVec2(c.x + r, c.y), ImVec2(c.x - r * 0.7f, c.y - r), ImVec2(c.x - r * 0.7f, c.y + r), col);
}

void Chevron(ImVec2 c, float r, ImU32 col) {
  g_f.dl->AddPolyline(std::array<ImVec2, 3>{ImVec2(c.x - r * 0.4f, c.y - r), ImVec2(c.x + r * 0.4f, c.y),
                                           ImVec2(c.x - r * 0.4f, c.y + r)}
                          .data(),
                      3, col, 0, 1.8f * g_f.s);
}

void Gear(ImVec2 c, float r, ImU32 col) {
  ImDrawList* dl = g_f.dl;
  for (int i = 0; i < 8; ++i) {
    const float a = float(i) * 3.14159265f / 4.0f;
    dl->AddLine(ImVec2(c.x + std::cos(a) * r * 0.62f, c.y + std::sin(a) * r * 0.62f),
                ImVec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r), col, 3.0f * g_f.s);
  }
  dl->AddCircle(c, r * 0.66f, col, 24, 1.8f * g_f.s);
  dl->AddCircle(c, r * 0.28f, col, 16, 1.8f * g_f.s);
}

std::string Clock() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char buf[16];
  std::strftime(buf, sizeof buf, "%I:%M %p", &tm);
  return buf[0] == '0' ? std::string(buf + 1) : std::string(buf);
}

void DrawModal() {
  if (g_modal.kind == Modal::Kind::kNone) return;
  const Frame& f = g_f;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  ImDrawList* saved = g_f.dl;
  g_f.dl = dl;
  g_ring_slot = 1;
  const int ring_count = g_ring_count;
  g_ring_count = 0;
  struct RingBack {
    int count;
    ~RingBack() {
      g_ring_slot = 0;
      g_ring_count = count;
    }
  } ring_back{ring_count};
  const float pop = Ease(g_modal.since, 0.18);
  dl->AddRectFilled(f.o, f.end, Black(0.66f * pop));
  const float w = 540 * f.s;
  const float wrap = 540 - 64;
  const float th = TextSize(f.regular, 16, g_modal.text, wrap).y;
  const float h = 110 * f.s + th + (g_modal.kind == Modal::Kind::kConfirm ? 64 * f.s : 0);
  const ImVec2 a(f.o.x + (f.size.x - w) * 0.5f, f.o.y + (f.size.y - h) * 0.5f + (1.0f - pop) * 24 * f.s);
  const ImVec2 b(a.x + w, a.y + h);
  dl->AddRectFilled(a, b, IM_COL32(32, 33, 38, 255), 12 * f.s);
  Text(f.semibold, 22, ImVec2(a.x + 32 * f.s, a.y + 28 * f.s), White(), g_modal.title);
  Text(f.regular, 16, ImVec2(a.x + 32 * f.s, a.y + 70 * f.s), White(0.7f), g_modal.text, wrap);
  ++g_modal.frames;

  if (g_modal.kind == Modal::Kind::kKey) {
    if (g_modal.frames > 2) {
      if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
        g_modal = Modal();
      } else {
        for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
          if (k >= ImGuiKey_GamepadStart && k <= ImGuiKey_GamepadRStickDown) continue;
          if (!ImGui::IsKeyPressed(ImGuiKey(k), false)) continue;
          auto on_key = g_modal.on_key;
          if (on_key && on_key(ImGuiKey(k))) {
            g_modal = Modal();
            break;
          }
        }
      }
    }
    g_f.dl = saved;
    return;
  }

  // Confirm: [yes] [Cancel] at the bottom right.
  const float y = b.y - 72 * f.s;
  const float cw = PillWidth(g_modal.no), yw = PillWidth(g_modal.yes);
  const ImVec2 cpos(b.x - 32 * f.s - cw * f.s, y), ypos(cpos.x - 14 * f.s - yw * f.s, y);
  if (g_raw[int(Btn::kLeft)] || g_raw[int(Btn::kRight)]) g_modal.focus ^= 1;
  const bool mouse = ImGui::GetIO().MouseDelta.x != 0 || ImGui::GetIO().MouseDelta.y != 0;
  PillRect yes = Pill(ypos, g_modal.yes, g_modal.focus == 0, g_modal.focus == 0);
  PillRect no = Pill(cpos, g_modal.no, g_modal.focus == 1, g_modal.focus == 1);
  const auto in = [](const PillRect& r) { return ImGui::IsMouseHoveringRect(r.a, r.b); };
  if (mouse && in(yes) && g_modal.focus != 0) {
    g_modal.focus = 0;
    whopping::sound::Play(whopping::sound::Sound::kMove);
  }
  if (mouse && in(no) && g_modal.focus != 1) {
    g_modal.focus = 1;
    whopping::sound::Play(whopping::sound::Sound::kMove);
  }
  bool do_yes = false, do_no = false, close = false;
  if (g_modal.frames > 1) {
    if (g_raw[int(Btn::kA)]) (g_modal.focus == 0 ? do_yes : do_no) = true;
    if (g_raw[int(Btn::kB)]) close = true;
    if (ImGui::IsMouseClicked(0)) {
      if (in(yes) || in(no)) whopping::sound::Play(whopping::sound::Sound::kSelect);
      if (in(yes)) do_yes = true;
      else if (in(no)) do_no = true;
      else if (!ImGui::IsMouseHoveringRect(a, b)) close = true;
    }
  }
  g_f.dl = saved;
  if (do_yes || do_no) {
    auto fn = do_yes ? g_modal.on_yes : g_modal.on_no;
    g_modal = Modal();
    if (fn) fn();
  } else if (close) {
    g_modal = Modal();
  }
}

}  // namespace

const Frame& F() { return g_f; }

void BeginFrame(const trg::Fonts& fonts) {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->Pos);
  ImGui::SetNextWindowSize(vp->Size);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::Begin("##dash", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav |
                   ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar(2);
  g_f.o = vp->Pos;
  g_f.size = vp->Size;
  g_f.end = ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);
  g_f.s = std::min(vp->Size.x / 1280.0f, vp->Size.y / 720.0f);
  // A new screen slides in a little from the right while it fades in (EndFrame).
  const float slide = (1.0f - Ease(g_transition, 0.28)) * 36.0f * g_f.s;
  g_f.o.x += slide;
  g_f.end.x += slide;
  g_f.dl = ImGui::GetWindowDrawList();
  g_ring_count = 0;
  g_f.regular = fonts.regular ? fonts.regular : ImGui::GetFont();
  g_f.semibold = fonts.semibold ? fonts.semibold : g_f.regular;
  g_f.bold = fonts.bold ? fonts.bold : g_f.semibold;
  ReadInput();
  // The mouse cursor hides while the controller is used and comes back when the mouse moves. ImGui's
  // backend would show it again every frame, so it is told not to touch the cursor meanwhile.
  static bool cursor_hidden = false;
  if (g_pad != cursor_hidden) {
    cursor_hidden = g_pad;
    SDL_ShowCursor(cursor_hidden ? SDL_DISABLE : SDL_ENABLE);
    if (cursor_hidden)
      ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    else
      ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
  }
}

void DrawAchievement() {
  if (g_note.started < 0) return;
  constexpr double kOrbIn = 0.30, kUnfold = 0.40, kHold = 4.6, kFold = 0.35, kOrbOut = 0.25;
  constexpr double kTotal = kOrbIn + kUnfold + kHold + kFold + kOrbOut;
  const float t = float(ImGui::GetTime() - g_note.started);
  if (t > kTotal) {
    g_note.started = -1.0;
    return;
  }
  auto ease_back = [](float x) {
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    return 1.0f + c3 * std::pow(x - 1.0f, 3.0f) + c1 * std::pow(x - 1.0f, 2.0f);
  };
  auto ease_io = [](float x) { return x < 0.5f ? 4 * x * x * x : 1 - std::pow(-2 * x + 2, 3.0f) / 2; };
  float orb = 1.0f, unfold = 1.0f;
  if (t < kOrbIn) {
    orb = ease_back(t / float(kOrbIn));
    unfold = 0.0f;
  } else if (t < kOrbIn + kUnfold) {
    unfold = ease_io((t - float(kOrbIn)) / float(kUnfold));
  } else if (t > kTotal - kOrbOut) {
    unfold = 0.0f;
    orb = 1.0f - ease_io((t - float(kTotal - kOrbOut)) / float(kOrbOut));
  } else if (t > kTotal - kOrbOut - kFold) {
    unfold = 1.0f - ease_io((t - float(kTotal - kOrbOut - kFold)) / float(kFold));
  }
  const Frame& f = g_f;
  const float s = f.s;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const std::string line1 = "Achievement unlocked";
  const std::string line2 = g_note.points ? std::to_string(g_note.points) + "G   " + g_note.title : g_note.title;
  const float h = 70 * s, r = h * 0.5f;
  const float text_w = std::max(f.regular->CalcTextSizeA(16 * s, FLT_MAX, 0, line1.c_str()).x,
                                f.semibold->CalcTextSizeA(20 * s, FLT_MAX, 0, line2.c_str()).x);
  const float full_w = h + 22 * s + text_w + 34 * s;
  const float w = h + (full_w - h) * unfold;
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float x0 = vp->Pos.x + (vp->Size.x - full_w) * 0.5f, y0 = vp->Pos.y + vp->Size.y * 0.86f - r;
  if (unfold > 0.0f) {
    const ImVec2 b0(x0, y0), b1(x0 + w, y0 + h);
    dl->AddRectFilled(ImVec2(b0.x + 3 * s, b0.y + 5 * s), ImVec2(b1.x + 3 * s, b1.y + 5 * s), Black(0.35f), r);
    dl->AddRectFilled(b0, b1, IM_COL32(30, 30, 34, 245), r);
    dl->AddRectFilled(ImVec2(b0.x + 2 * s, b0.y + 2 * s), ImVec2(b1.x - 2 * s, b0.y + h * 0.5f), White(0.06f),
                      r - 2 * s, ImDrawFlags_RoundCornersTop);
    dl->AddRect(b0, b1, White(0.16f), r, 0, 1.2f * s);
    const float alpha = std::clamp((unfold - 0.6f) / 0.4f, 0.0f, 1.0f);
    if (alpha > 0.0f) {
      const float tx = x0 + h + 18 * s;
      dl->PushClipRect(b0, ImVec2(b1.x - 14 * s, b1.y), true);
      dl->AddText(f.regular, 16 * s, ImVec2(tx, y0 + 12 * s), IM_COL32(200, 205, 210, int(255 * alpha)), line1.c_str());
      dl->AddText(f.semibold, 20 * s, ImVec2(tx, y0 + 33 * s), White(alpha), line2.c_str());
      dl->PopClipRect();
    }
  }
  if (orb > 0.0f) {
    const ImVec2 c(x0 + r, y0 + r);
    const float rr = (r + 4 * s) * orb;
    dl->AddCircleFilled(c, rr + 2 * s, Black(0.31f), 48);
    dl->AddCircleFilled(c, rr, IM_COL32(124, 194, 66, 255), 48);
    dl->AddCircleFilled(c, rr * 0.86f, IM_COL32(18, 22, 18, 255), 48);
    if (g_note.icon) {
      const float ir = rr * 0.80f;
      dl->AddImageRounded(ImTextureRef(g_note.icon.id), ImVec2(c.x - ir, c.y - ir), ImVec2(c.x + ir, c.y + ir),
                          ImVec2(0, 0), ImVec2(1, 1), White(), ir);
    }
    dl->AddCircle(c, rr, IM_COL32(190, 240, 140, 180), 48, 1.5f * s);
  }
}

void EndFrame() {
  const float fade = 1.0f - Ease(g_transition, 0.22);
  if (fade > 0.0f) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    g_f.dl->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), Black(fade));
  }
  DrawToast();
  DrawAchievement();
  DrawModal();
  ImGui::End();
}

void StartTransition() { g_transition = ImGui::GetTime(); }

float Ease(double since, double seconds) {
  const float t = std::clamp(float((ImGui::GetTime() - since) / seconds), 0.0f, 1.0f);
  return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

bool Pressed(Btn b) { return g_pressed[int(b)]; }
void Consume(Btn b) { g_pressed[int(b)] = false; }
void Inject(Btn b) { g_pending[int(b)] = true; }
bool PadMode() { return g_pad; }
bool MouseMoved() {
  const ImVec2 d = ImGui::GetIO().MouseDelta;
  return g_modal.kind == Modal::Kind::kNone && (d.x != 0.0f || d.y != 0.0f);
}
bool Hover(ImVec2 a, ImVec2 b) { return g_modal.kind == Modal::Kind::kNone && ImGui::IsMouseHoveringRect(a, b); }
bool Clicked(ImVec2 a, ImVec2 b) {
  const bool c = Hover(a, b) && ImGui::IsMouseClicked(0);
  if (c) whopping::sound::Play(whopping::sound::Sound::kSelect);
  return c;
}
float Wheel() { return g_modal.kind == Modal::Kind::kNone ? ImGui::GetIO().MouseWheel : 0.0f; }

ImU32 Accent(const ImVec4& c, float a) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * a)); }

void Text(ImFont* font, float size, ImVec2 pos, ImU32 col, const std::string& text, float wrap) {
  g_f.dl->AddText(font, size * g_f.s, pos, col, text.c_str(), text.c_str() + text.size(),
                  wrap > 0 ? wrap * g_f.s : 0.0f);
}

ImVec2 TextSize(ImFont* font, float size, const std::string& text, float wrap) {
  return font->CalcTextSizeA(size * g_f.s, FLT_MAX, wrap > 0 ? wrap * g_f.s : 0.0f, text.c_str(),
                             text.c_str() + text.size());
}

void TextRight(ImFont* font, float size, ImVec2 right_top, ImU32 col, const std::string& text) {
  const ImVec2 t = TextSize(font, size, text);
  Text(font, size, ImVec2(right_top.x - t.x, right_top.y), col, text);
}

void Cover(const Texture& tex, ImVec2 a, ImVec2 b, float alpha, float focus_x, float rounding) {
  if (!tex || alpha <= 0.0f) return;
  const float rw = b.x - a.x, rh = b.y - a.y;
  const float ta = float(tex.width) / float(tex.height), ra = rw / rh;
  ImVec2 uv0(0, 0), uv1(1, 1);
  if (ta > ra) {
    const float keep = ra / ta;
    uv0.x = (1 - keep) * focus_x;
    uv1.x = uv0.x + keep;
  } else {
    const float keep = ta / ra;
    uv0.y = (1 - keep) * 0.5f;
    uv1.y = uv0.y + keep;
  }
  g_f.dl->AddImageRounded(ImTextureRef(tex.id), a, b, uv0, uv1, White(alpha), rounding);
}

void FitArt(const Texture& tex, ImVec2 a, ImVec2 b, float rounding) {
  if (!tex) return;
  const float rw = b.x - a.x, rh = b.y - a.y;
  const float ta = float(tex.width) / float(tex.height), ra = rw / rh;
  if (std::fabs(ta / ra - 1.0f) < 0.15f) {
    Cover(tex, a, b, 1.0f, 0.5f, rounding);
    return;
  }
  Cover(tex, a, b, 1.0f, 0.5f, rounding);
  g_f.dl->AddRectFilled(a, b, Black(0.62f), rounding);
  float w = rw, h = rw / ta;
  if (h > rh) {
    h = rh;
    w = rh * ta;
  }
  const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
  g_f.dl->AddImage(ImTextureRef(tex.id), ImVec2(c.x - w * 0.5f, c.y - h * 0.5f), ImVec2(c.x + w * 0.5f, c.y + h * 0.5f));
}

void Shade() {
  ImDrawList* dl = g_f.dl;
  const float w = g_f.size.x, h = g_f.size.y, x = g_f.o.x, y = g_f.o.y;
  const ImU32 c0 = Black(0.92f), c1 = Black(0.55f), c2 = Black(0.0f);
  dl->AddRectFilledMultiColor(ImVec2(x, y), ImVec2(x + w * 0.38f, y + h), c0, c1, c1, c0);
  dl->AddRectFilledMultiColor(ImVec2(x + w * 0.38f, y), ImVec2(x + w * 0.70f, y + h), c1, c2, c2, c1);
  const ImU32 b1 = Black(0.6f), b2 = Black(1.0f);
  dl->AddRectFilledMultiColor(ImVec2(x, y + h * 0.40f), ImVec2(x + w, y + h * 0.66f), c2, c2, b1, b1);
  dl->AddRectFilledMultiColor(ImVec2(x, y + h * 0.66f), ImVec2(x + w, y + h), b1, b1, b2, b2);
  // A little at the top so the top line reads on bright art.
  dl->AddRectFilledMultiColor(ImVec2(x, y), ImVec2(x + w, y + 110 * g_f.s), Black(0.7f), Black(0.7f), c2, c2);
}

void Backdrop(const Texture& hero, float alpha, float focus_x) { Cover(hero, g_f.o, g_f.end, alpha, focus_x); }

void BlurBackdrop(const Texture& blur) {
  g_f.dl->AddRectFilled(g_f.o, g_f.end, IM_COL32(10, 10, 12, 255));
  if (blur) {
    const float rw = g_f.size.x, rh = g_f.size.y;
    const float ta = float(blur.width) / float(blur.height), ra = rw / rh;
    ImVec2 uv0(0, 0), uv1(1, 1);
    if (ta > ra) {
      uv0.x = (1 - ra / ta) * 0.5f;
      uv1.x = 1 - uv0.x;
    }
    g_f.dl->AddImage(ImTextureRef(blur.id), g_f.o, g_f.end, uv0, uv1, IM_COL32(95, 95, 100, 255));
  }
  g_f.dl->AddRectFilledMultiColor(g_f.o, g_f.end, Black(0.35f), Black(0.1f), Black(0.25f), Black(0.55f));
}

void FocusRing(ImVec2 a, ImVec2 b, float rounding) {
  const float s = g_f.s;
  Ring& r = g_rings[(g_ring_slot * 8 + g_ring_count++) % 16];
  const int frame = ImGui::GetFrameCount();
  if (r.last_frame < frame - 1 || ImGui::GetTime() - g_transition < 0.05) {
    r.a = a;
    r.b = b;
  } else {  // glide towards the new place
    const float k = std::min(1.0f, ImGui::GetIO().DeltaTime * 20.0f);
    r.a = ImVec2(r.a.x + (a.x - r.a.x) * k, r.a.y + (a.y - r.a.y) * k);
    r.b = ImVec2(r.b.x + (b.x - r.b.x) * k, r.b.y + (b.y - r.b.y) * k);
  }
  r.last_frame = frame;
  const float pulse = 0.82f + 0.18f * (0.5f + 0.5f * std::sin(float(ImGui::GetTime()) * 3.2f));
  const ImVec2 ra(r.a.x - 4 * s, r.a.y - 4 * s), rb(r.b.x + 4 * s, r.b.y + 4 * s);
  g_f.dl->AddRect(ImVec2(ra.x - 3 * s, ra.y - 3 * s), ImVec2(rb.x + 3 * s, rb.y + 3 * s), White(0.12f * pulse),
                  (rounding + 7) * s, 0, 3.0f * s);  // a soft glow outside
  g_f.dl->AddRect(ra, rb, White(pulse), (rounding + 4) * s, 0, 3.0f * s);
}

void Card(ImVec2 a, ImVec2 b, float alpha) { g_f.dl->AddRectFilled(a, b, White(alpha), 10 * g_f.s); }

void Bar(ImVec2 a, ImVec2 b, float fraction, ImU32 col) {
  const float r = (b.y - a.y) * 0.5f;
  g_f.dl->AddRectFilled(a, b, White(0.14f), r);
  if (fraction < 0.0f) {  // unknown size: a moving piece
    const float w = (b.x - a.x) * 0.3f;
    const float t = float(std::fmod(ImGui::GetTime() * 0.6, 1.0));
    const float x = a.x + (b.x - a.x + w) * t - w;
    g_f.dl->AddRectFilled(ImVec2(std::max(a.x, x), a.y), ImVec2(std::min(b.x, x + w), b.y), col, r);
  } else if (fraction > 0.0f) {
    g_f.dl->AddRectFilled(a, ImVec2(a.x + (b.x - a.x) * std::min(1.0f, fraction), b.y), col, r);
  }
}

float PillWidth(const std::string& label, bool play) {
  return TextSize(g_f.semibold, 16, label).x / g_f.s + 52 + (play ? 22 : 0);
}

PillRect Pill(ImVec2 pos, const std::string& label, bool primary, bool focused, bool play, float height) {
  const float s = g_f.s;
  const float w = PillWidth(label, play) * s, h = height * s;
  PillRect r{pos, ImVec2(pos.x + w, pos.y + h)};
  const bool hover = Hover(r.a, r.b);
  g_f.dl->AddRectFilled(r.a, r.b, primary ? White(hover ? 0.92f : 1.0f) : White(hover ? 0.2f : 0.13f), h * 0.5f);
  if (focused) FocusRing(r.a, r.b, height * 0.5f);
  const ImU32 tc = primary ? IM_COL32(17, 17, 20, 255) : White();
  float x = pos.x + 26 * s;
  const float ty = pos.y + (h - TextSize(g_f.semibold, 16, label).y) * 0.5f;
  if (play) {
    const float cy = pos.y + h * 0.5f;
    g_f.dl->AddTriangleFilled(ImVec2(x, cy - 7 * s), ImVec2(x, cy + 7 * s), ImVec2(x + 12 * s, cy), tc);
    x += 22 * s;
  }
  Text(g_f.semibold, 16, ImVec2(x, ty), tc, label);
  return r;
}

bool TopBar(const std::vector<std::string>& crumbs, bool gear) {
  const Frame& f = g_f;
  float x = f.X(48);
  const float y = f.Y(22);
  for (size_t i = 0; i < crumbs.size(); ++i) {
    const bool last = i + 1 == crumbs.size();
    ImFont* font = last ? f.semibold : f.regular;
    Text(font, 16, ImVec2(x + 1, y + 1.5f * f.s), Black(0.6f), crumbs[i]);
    Text(font, 16, ImVec2(x, y), last ? White() : White(0.6f), crumbs[i]);
    x += TextSize(font, 16, crumbs[i]).x;
    if (!last) {
      Chevron(ImVec2(x + 14 * f.s, y + 11 * f.s), 5 * f.s, White(0.5f));
      x += 28 * f.s;
    }
  }
  float right = f.R(48);
  bool clicked = false;
  if (gear) {
    const ImVec2 c(right - 11 * f.s, y + 11 * f.s);
    const ImVec2 a(c.x - 18 * f.s, c.y - 18 * f.s), b(c.x + 18 * f.s, c.y + 18 * f.s);
    const bool hover = Hover(a, b);
    if (hover) f.dl->AddCircleFilled(c, 18 * f.s, White(0.15f));
    Gear(c, 10 * f.s, White(0.85f));
    clicked = Clicked(a, b);
    right -= 44 * f.s;
  }
  TextRight(f.regular, 16, ImVec2(right + 1, y + 1.5f * f.s), Black(0.6f), Clock());
  TextRight(f.regular, 16, ImVec2(right, y), White(0.85f), Clock());
  return clicked;
}

float GlyphWidth(Btn b, float size) {
  if (g_pad) {
    if (b == Btn::kLB || b == Btn::kRB) return size * 1.45f;
    return size;
  }
  return TextSize(g_f.semibold, size * 0.55f, kKeyCap[int(b)]).x / g_f.s + size * 0.6f;
}

void Glyph(Btn b, ImVec2 pos, float size) {
  const Frame& f = g_f;
  const float s = f.s, d = size * s;
  ImDrawList* dl = f.dl;
  if (!g_pad) {
    const float w = GlyphWidth(b, size) * s;
    dl->AddRect(pos, ImVec2(pos.x + w, pos.y + d), White(0.8f), 5 * s, 0, 1.5f * s);
    const ImVec2 t = TextSize(f.semibold, size * 0.55f, kKeyCap[int(b)]);
    Text(f.semibold, size * 0.55f, ImVec2(pos.x + (w - t.x) * 0.5f, pos.y + (d - t.y) * 0.5f), White(0.9f),
         kKeyCap[int(b)]);
    return;
  }
  const ImVec2 c(pos.x + d * 0.5f, pos.y + d * 0.5f);
  auto letter = [&](const char* l, ImU32 bg, ImU32 fg) {
    dl->AddCircleFilled(c, d * 0.5f, bg, 24);
    const ImVec2 t = TextSize(f.bold, size * 0.55f, l);
    Text(f.bold, size * 0.55f, ImVec2(c.x - t.x * 0.5f, c.y - t.y * 0.5f), fg, l);
  };
  switch (b) {
    case Btn::kA: letter("A", IM_COL32(93, 194, 30, 255), IM_COL32(17, 17, 17, 255)); break;
    case Btn::kB: letter("B", IM_COL32(227, 53, 43, 255), White()); break;
    case Btn::kX: letter("X", IM_COL32(42, 127, 219, 255), White()); break;
    case Btn::kY: letter("Y", IM_COL32(242, 183, 5, 255), IM_COL32(17, 17, 17, 255)); break;
    case Btn::kLB:
    case Btn::kRB: {
      const float w = GlyphWidth(b, size) * s;
      dl->AddRect(ImVec2(pos.x, pos.y + d * 0.1f), ImVec2(pos.x + w, pos.y + d * 0.9f), White(0.8f), 5 * s, 0,
                  1.5f * s);
      const char* l = b == Btn::kLB ? "LB" : "RB";
      const ImVec2 t = TextSize(f.semibold, size * 0.5f, l);
      Text(f.semibold, size * 0.5f, ImVec2(pos.x + (w - t.x) * 0.5f, c.y - t.y * 0.5f), White(0.9f), l);
      break;
    }
    case Btn::kStart:
      dl->AddCircle(c, d * 0.5f, White(0.8f), 24, 1.5f * s);
      for (int i = -1; i <= 1; ++i)
        dl->AddLine(ImVec2(c.x - d * 0.2f, c.y + i * d * 0.15f), ImVec2(c.x + d * 0.2f, c.y + i * d * 0.15f),
                    White(0.9f), 1.5f * s);
      break;
    default: break;
  }
}

void HintBar(const std::vector<Hint>& hints) {
  const Frame& f = g_f;
  float x = f.R(48);
  const float y = f.B(37);
  for (auto it = hints.rbegin(); it != hints.rend(); ++it) {
    const float tw = TextSize(f.regular, 15, it->label).x;
    const float gw = GlyphWidth(it->btn) * f.s;
    // LB and RB share one label ("Switch game"): drawn as LB RB.
    const bool pair = it->btn == Btn::kRB && it + 1 != hints.rend() && (it + 1)->btn == Btn::kLB &&
                      (it + 1)->label.empty();
    const float pw = pair ? GlyphWidth(Btn::kLB) * f.s + 6 * f.s : 0.0f;
    const float start = x - tw - 8 * f.s - gw - pw;
    const ImVec2 a(start - 6 * f.s, y - 6 * f.s), b(x + 6 * f.s, y + 28 * f.s);
    if (it->label.empty()) continue;
    if (Hover(a, b)) f.dl->AddRectFilled(a, b, White(0.1f), 8 * f.s);
    if (pair) Glyph(Btn::kLB, ImVec2(start, y));
    Glyph(it->btn, ImVec2(start + pw, y));
    Text(f.regular, 15, ImVec2(x - tw, y + 2 * f.s), White(0.88f), it->label);
    if (Clicked(a, b)) {
      g_pending[int(it->btn)] = true;  // a click on LB RB means "next"
    }
    x = start - 30 * f.s;
  }
}

void Toast(const std::string& text, bool error) {
  if (error) whopping::sound::Play(whopping::sound::Sound::kError);
  g_toast_since = ImGui::GetTime();
  g_toast = text;
  g_toast_error = error;
  g_toast_until = ImGui::GetTime() + 5.0;
}

void DrawToast() {
  if (g_toast.empty() || ImGui::GetTime() > g_toast_until) return;
  const Frame& f = g_f;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const ImVec2 t = f.regular->CalcTextSizeA(16 * f.s, FLT_MAX, 0, g_toast.c_str());
  const ImVec2 pad(20 * f.s, 12 * f.s);
  const float drop = (1.0f - Ease(g_toast_since, 0.25)) * 30 * f.s;  // slides down into place, under the top line
  const ImVec2 a(f.o.x + (f.size.x - t.x) * 0.5f - pad.x, f.Y(64) - drop);
  const ImVec2 b(a.x + t.x + pad.x * 2, a.y + t.y + pad.y * 2);
  dl->AddRectFilled(a, b, g_toast_error ? IM_COL32(150, 40, 40, 240) : IM_COL32(36, 37, 42, 240), 10 * f.s);
  dl->AddText(f.regular, 16 * f.s, ImVec2(a.x + pad.x, a.y + pad.y), White(), g_toast.c_str());
}

void Confirm(std::string title, std::string text, std::string yes_label, std::function<void()> on_yes) {
  g_modal = Modal();
  g_modal.kind = Modal::Kind::kConfirm;
  g_modal.title = std::move(title);
  g_modal.text = std::move(text);
  g_modal.yes = std::move(yes_label);
  g_modal.on_yes = std::move(on_yes);
  g_modal.since = ImGui::GetTime();
}

void Choose(std::string title, std::string text, std::string first_label, std::function<void()> on_first,
            std::string second_label, std::function<void()> on_second) {
  Confirm(std::move(title), std::move(text), std::move(first_label), std::move(on_first));
  g_modal.no = std::move(second_label);
  g_modal.on_no = std::move(on_second);
  g_modal.focus = 0;
}

void CaptureKey(std::string what, std::function<bool(ImGuiKey)> on_key) {
  g_modal = Modal();
  g_modal.kind = Modal::Kind::kKey;
  g_modal.title = "Press a key for " + what;
  g_modal.text = "Press the key or mouse button to use. Esc, or B on the controller, cancels.";
  g_modal.on_key = std::move(on_key);
  g_modal.since = ImGui::GetTime();
}

bool ModalOpen() { return g_modal.kind != Modal::Kind::kNone; }

void ShowAchievement(const Texture& icon, std::string title, int points, bool sound, int volume) {
  g_note.icon = icon;
  g_note.title = std::move(title);
  g_note.points = points;
  g_note.started = ImGui::GetTime();
  if (sound) whopping::sound::Play(whopping::sound::Sound::kAchievement, std::clamp(volume, 0, 100) / 100.0f);
}

// ------------------------------------------------------------------ rows ---
namespace {
constexpr float kRowH = 60.0f;
const ImU32 kGreen = IM_COL32(93, 194, 30, 255);
}  // namespace

Rows::Rows(RowsState& state, ImVec2 a, ImVec2 b, bool active, trg::SettingsStore* store)
    : st_(state), a_(a), b_(b), active_(active), store_(store), s_(g_f.s) {
  if (active_) {
    up_ = Pressed(Btn::kUp);
    down_ = Pressed(Btn::kDown);
    left_ = Pressed(Btn::kLeft);
    right_ = Pressed(Btn::kRight);
    a_btn_ = Pressed(Btn::kA);
    x_btn_ = Pressed(Btn::kX);
    for (Btn k : {Btn::kUp, Btn::kDown, Btn::kLeft, Btn::kRight, Btn::kA, Btn::kX}) Consume(k);
    if (up_) st_.focus = std::max(0, st_.focus - 1);
    if (down_) st_.focus = std::min(std::max(0, st_.count - 1), st_.focus + 1);
    left_unused_ = left_;
  }
  st_.focus = std::clamp(st_.focus, 0, std::max(0, st_.count - 1));
  if (Hover(a_, b_) && Wheel() != 0.0f) st_.scroll -= Wheel() * 90.0f * s_;
  y_ = a_.y - st_.scroll;
  g_f.dl->PushClipRect(ImVec2(a_.x - 8 * s_, a_.y - 8 * s_), ImVec2(b_.x + 8 * s_, b_.y + 8 * s_), true);
}

Rows::~Rows() {
  g_f.dl->PopClipRect();
  st_.count = index_;
  st_.content = y_ - (a_.y - st_.scroll);
  const float view = b_.y - a_.y;
  if ((up_ || down_) && focus_top_ >= 0) {
    if (focus_top_ < st_.scroll) st_.scroll = focus_top_ - 8 * s_;
    if (focus_bottom_ > st_.scroll + view) st_.scroll = focus_bottom_ - view + 8 * s_;
  }
  st_.scroll = std::clamp(st_.scroll, 0.0f, std::max(0.0f, st_.content - view));
  if (st_.content > view + 1) {  // where the list is: a thin bar on the right
    const float h = view * view / st_.content;
    const float t = st_.scroll / (st_.content - view);
    const ImVec2 a(b_.x + 14 * s_, a_.y + (view - h) * t);
    g_f.dl->AddRectFilled(a, ImVec2(a.x + 3 * s_, a.y + h), White(0.35f), 2 * s_);
  }
}

bool Rows::Begin(float h, const std::string& label, const std::string& help) {
  const int idx = index_++;
  row_top_ = y_;
  row_h_ = h * s_;
  const ImVec2 a(a_.x, y_), b(b_.x, y_ + row_h_);
  const bool visible = b.y > a_.y && a.y < b_.y;
  const bool hover = visible && Hover(ImVec2(a.x, std::max(a.y, a_.y)), ImVec2(b.x, std::min(b.y, b_.y)));
  if (hover && MouseMoved() && st_.focus != idx) {
    st_.focus = idx;
    mouse_took_ = true;
    whopping::sound::Play(whopping::sound::Sound::kMove);
  }
  row_clicked_ = hover && ImGui::IsMouseClicked(0) && !ModalOpen();
  if (row_clicked_) {
    whopping::sound::Play(whopping::sound::Sound::kSelect);
    st_.focus = idx;
    mouse_took_ = true;
  }
  focused_ = idx == st_.focus;
  if (focused_) {
    help_title_ = label;
    help_text_ = help;
    focus_top_ = y_ - (a_.y - st_.scroll);
    focus_bottom_ = focus_top_ + row_h_;
  }
  const bool lit = focused_ && (active_ || mouse_took_);
  if (lit) {
    // The highlight slides from the last focused row to this one.
    const float target = y_ - (a_.y - st_.scroll);
    if (st_.highlight < 0.0f) st_.highlight = target;
    st_.highlight += (target - st_.highlight) * std::min(1.0f, ImGui::GetIO().DeltaTime * 22.0f);
    if (std::fabs(target - st_.highlight) < 0.5f) st_.highlight = target;
    const float hy = a_.y - st_.scroll + st_.highlight;
    const ImVec2 ha(a.x, hy), hb(b.x, hy + row_h_);
    g_f.dl->AddRectFilled(ha, hb, White(0.14f), 8 * s_);
    g_f.dl->AddRect(ha, hb, White(), 8 * s_, 0, 2.0f * s_);
  } else {
    g_f.dl->AddLine(ImVec2(a.x + 4 * s_, b.y - 0.5f), ImVec2(b.x - 4 * s_, b.y - 0.5f), White(0.07f), 1.0f);
  }
  if (!label.empty()) {
    const float th = TextSize(g_f.regular, 17, label).y;
    Text(g_f.regular, 17, ImVec2(a.x + 24 * s_, a.y + (row_h_ - th) * 0.5f), White(), label);
  }
  return focused_ && active_;
}

void Rows::Saved() {
  if (store_) store_->Save();
}

void Rows::Heading(const std::string& text) {
  y_ += 14 * s_;
  Text(g_f.semibold, 14, ImVec2(a_.x + 24 * s_, y_), White(0.55f), text);
  y_ += 32 * s_;
}

void Rows::Paragraph(const std::string& text, bool dim) {
  const float wrap = (b_.x - a_.x) / s_ - 48;
  const float h = TextSize(g_f.regular, 15, text, wrap).y;
  Text(g_f.regular, 15, ImVec2(a_.x + 24 * s_, y_ + 6 * s_), dim ? White(0.62f) : White(0.9f), text, wrap);
  y_ += h + 18 * s_;
}

void Rows::Info(const std::string& label, const std::string& value) {
  const float h = 44 * s_;
  const float th = TextSize(g_f.regular, 15, label).y;
  Text(g_f.regular, 15, ImVec2(a_.x + 24 * s_, y_ + (h - th) * 0.5f), White(0.6f), label);
  // Long values (paths, hashes) shrink to fit beside the label.
  float size = 15;
  const float room = (b_.x - a_.x) - 220 * s_;
  while (size > 10 && TextSize(g_f.regular, size, value).x > room) size -= 1;
  TextRight(g_f.regular, size, ImVec2(b_.x - 24 * s_, y_ + (h - TextSize(g_f.regular, size, value).y) * 0.5f),
            White(0.92f), value);
  y_ += h;
}

bool Rows::Button(const std::string& label, const std::string& help, const std::string& value) {
  const bool f = Begin(kRowH, label, help);
  const bool go = (f && a_btn_) || row_clicked_;
  const float cy = row_top_ + row_h_ * 0.5f;
  Chevron(ImVec2(b_.x - 26 * s_, cy), 6 * s_, White(0.6f));
  if (!value.empty()) {
    const float th = TextSize(g_f.regular, 16, value).y;
    TextRight(g_f.regular, 16, ImVec2(b_.x - 44 * s_, cy - th * 0.5f), White(0.72f), value);
  }
  y_ += row_h_;
  return go;
}

bool Rows::ToggleValue(const std::string& label, const std::string& help, bool& value, bool def) {
  const bool f = Begin(kRowH, label, help);
  bool changed = false;
  if ((f && a_btn_) || row_clicked_) {
    value = !value;
    changed = true;
  }
  if (f && right_ && !value) {
    value = true;
    changed = true;
  }
  if (f && x_btn_ && value != def) {
    value = def;
    changed = true;
  }
  const float cy = row_top_ + row_h_ * 0.5f;
  const ImVec2 ta(b_.x - 24 * s_ - 44 * s_, cy - 12 * s_), tb(b_.x - 24 * s_, cy + 12 * s_);
  g_f.dl->AddRectFilled(ta, tb, value ? kGreen : White(0.25f), 12 * s_);
  g_f.dl->AddCircleFilled(ImVec2(value ? tb.x - 12 * s_ : ta.x + 12 * s_, cy), 9 * s_, White(), 20);
  y_ += row_h_;
  return changed;
}

bool Rows::Toggle(const std::string& label, const std::string& help, const char* key, bool def) {
  bool v = store_ ? store_->GetBool(key, def) : def;
  if (!ToggleValue(label, help, v, def)) return false;
  store_->SetBool(key, v);
  Saved();
  return true;
}

bool Rows::ChoiceIndex(const std::string& label, const std::string& help, int& index,
                       const std::vector<std::string>& names, int def_index) {
  const bool f = Begin(kRowH, label, help);
  const int n = int(names.size());
  int next = index;
  const float cy = row_top_ + row_h_ * 0.5f;
  const std::string shown = index >= 0 && index < n ? names[index] : std::string("Custom");
  const float tw = TextSize(g_f.regular, 16, shown).x;
  const float right = b_.x - 24 * s_;
  const float lx = right - 36 * s_ - tw - 14 * s_;  // left arrow centre
  if (f) {
    if (left_) next = std::max(0, index - 1);
    if (right_) next = std::min(n - 1, index + 1);
    if (a_btn_) next = (index + 1) % n;
    if (x_btn_ && def_index >= 0) next = def_index;
    left_unused_ = false;
  }
  if (row_clicked_) {
    const float mx = ImGui::GetIO().MousePos.x;
    next = mx < lx + 16 * s_ ? std::max(0, index - 1) : (index + 1) % n;
  }
  const bool can_l = index > 0, can_r = index < n - 1;
  Triangle(ImVec2(lx, cy), 5 * s_, 0, White(can_l ? 0.75f : 0.2f));
  Triangle(ImVec2(right - 6 * s_, cy), 5 * s_, 1, White(can_r ? 0.75f : 0.2f));
  const float th = TextSize(g_f.regular, 16, shown).y;
  Text(g_f.regular, 16, ImVec2(right - 24 * s_ - tw, cy - th * 0.5f), White(0.9f), shown);
  y_ += row_h_;
  if (next == index || next < 0) return false;
  index = next;
  return true;
}

bool Rows::Choice(const std::string& label, const std::string& help, const char* key, const std::string& def,
                  const std::vector<Option>& options) {
  const std::string cur = store_ ? store_->Get(key, def) : def;
  int index = -1, def_index = -1;
  std::vector<std::string> names;
  for (size_t i = 0; i < options.size(); ++i) {
    names.push_back(options[i].label);
    if (options[i].value == cur) index = int(i);
    if (options[i].value == def) def_index = int(i);
  }
  int was = index;
  if (!ChoiceIndex(label, help, index, names, def_index)) return false;
  if (was == -1 && index < 0) return false;
  store_->Set(key, options[std::max(0, index)].value);
  Saved();
  return true;
}

namespace {

bool Slider(Rows& rows, float& v, float min, float max, float step, bool f, bool left, bool right, bool clicked,
            ImVec2 track_a, ImVec2 track_b) {
  float next = v;
  if (f && left) next = std::max(min, v - step);
  if (f && right) next = std::min(max, v + step);
  if (clicked) {
    const float mx = ImGui::GetIO().MousePos.x;
    if (mx >= track_a.x - 10 * g_f.s && mx <= track_b.x + 10 * g_f.s) {
      const float t = std::clamp((mx - track_a.x) / (track_b.x - track_a.x), 0.0f, 1.0f);
      next = min + std::round(t * (max - min) / step) * step;
    }
  }
  (void)rows;
  if (next == v) return false;
  v = next;
  return true;
}

}  // namespace

bool Rows::SliderFloat(const std::string& label, const std::string& help, const char* key, float def, float min,
                       float max, float step, const char* fmt) {
  float v = store_ ? store_->GetFloat(key, def) : def;
  const bool f = Begin(kRowH, label, help);
  const float cy = row_top_ + row_h_ * 0.5f;
  const float right = b_.x - 24 * s_;
  const ImVec2 ta(right - 64 * s_ - 170 * s_, cy - 3 * s_), tb(right - 64 * s_, cy + 3 * s_);
  bool changed = Slider(*this, v, min, max, step, f, left_, right_, row_clicked_, ta, tb);
  if (f) left_unused_ = false;
  if (f && x_btn_ && v != def) {
    v = def;
    changed = true;
  }
  const float t = (v - min) / (max - min);
  Bar(ta, tb, t, White());
  g_f.dl->AddCircleFilled(ImVec2(ta.x + (tb.x - ta.x) * t, cy), 8 * s_, White(), 20);
  char buf[32];
  std::snprintf(buf, sizeof buf, fmt, double(v));
  const float th = TextSize(g_f.regular, 16, buf).y;
  TextRight(g_f.regular, 16, ImVec2(right, cy - th * 0.5f), White(0.9f), buf);
  y_ += row_h_;
  if (!changed) return false;
  store_->SetFloat(key, v, step < 0.1f ? 2 : 1);
  Saved();
  return true;
}

bool Rows::SliderInt(const std::string& label, const std::string& help, const char* key, int def, int min, int max,
                     int step, const char* fmt) {
  float v = float(store_ ? store_->GetInt(key, def) : def);
  const bool f = Begin(kRowH, label, help);
  const float cy = row_top_ + row_h_ * 0.5f;
  const float right = b_.x - 24 * s_;
  const ImVec2 ta(right - 64 * s_ - 170 * s_, cy - 3 * s_), tb(right - 64 * s_, cy + 3 * s_);
  bool changed = Slider(*this, v, float(min), float(max), float(step), f, left_, right_, row_clicked_, ta, tb);
  if (f) left_unused_ = false;
  if (f && x_btn_ && int(v) != def) {
    v = float(def);
    changed = true;
  }
  const float t = (v - float(min)) / float(max - min);
  Bar(ta, tb, t, White());
  g_f.dl->AddCircleFilled(ImVec2(ta.x + (tb.x - ta.x) * t, cy), 8 * s_, White(), 20);
  char buf[32];
  std::snprintf(buf, sizeof buf, fmt, int(v));
  const float th = TextSize(g_f.regular, 16, buf).y;
  TextRight(g_f.regular, 16, ImVec2(right, cy - th * 0.5f), White(0.9f), buf);
  y_ += row_h_;
  if (!changed) return false;
  store_->SetInt(key, int(v));
  Saved();
  return true;
}

bool Rows::KeyBind(const std::string& label, const std::string& value) {
  const bool f = Begin(kRowH, label, "Press A, or click, then press the key to use.");
  const bool go = (f && a_btn_) || row_clicked_;
  const float cy = row_top_ + row_h_ * 0.5f;
  const ImVec2 t = TextSize(g_f.semibold, 14, value);
  const ImVec2 kb(b_.x - 24 * s_, cy + 15 * s_), ka(kb.x - t.x - 24 * s_, cy - 15 * s_);
  g_f.dl->AddRectFilled(ka, kb, White(0.1f), 6 * s_);
  g_f.dl->AddRect(ka, kb, White(0.35f), 6 * s_, 0, 1.2f * s_);
  Text(g_f.semibold, 14, ImVec2(ka.x + 12 * s_, cy - t.y * 0.5f), White(0.92f), value);
  y_ += row_h_;
  return go;
}

void Rows::Progress(const std::string& label, float fraction) {
  Text(g_f.regular, 15, ImVec2(a_.x + 24 * s_, y_ + 8 * s_), White(0.85f), label);
  if (fraction >= 0) {
    char pct[16];
    std::snprintf(pct, sizeof pct, "%.0f%%", double(fraction) * 100.0);
    TextRight(g_f.regular, 15, ImVec2(b_.x - 24 * s_, y_ + 8 * s_), White(0.6f), pct);
  }
  Bar(ImVec2(a_.x + 24 * s_, y_ + 36 * s_), ImVec2(b_.x - 24 * s_, y_ + 42 * s_), fraction, White());
  y_ += 58 * s_;
}

void Rows::Achievement(const Texture& icon, const trg::AchievementCard& card) {
  Begin(76, "", card.unlocked ? card.description : (card.locked_description.empty() ? card.description
                                                                                      : card.locked_description));
  const float x = a_.x + 16 * s_, y = row_top_ + 12 * s_, d = 52 * s_;
  if (icon)
    g_f.dl->AddImageRounded(ImTextureRef(icon.id), ImVec2(x, y), ImVec2(x + d, y + d), ImVec2(0, 0), ImVec2(1, 1),
                            card.unlocked ? White() : IM_COL32(80, 80, 80, 110), 6 * s_);
  else
    g_f.dl->AddRectFilled(ImVec2(x, y), ImVec2(x + d, y + d), White(0.1f), 6 * s_);
  const float tx = x + d + 16 * s_;
  const float wrap = (b_.x - tx) / s_ - 110;
  Text(g_f.semibold, 16, ImVec2(tx, y + 2 * s_), card.unlocked ? White() : White(0.6f), card.title);
  const std::string& desc = card.unlocked || card.locked_description.empty() ? card.description
                                                                             : card.locked_description;
  Text(g_f.regular, 14, ImVec2(tx, y + 26 * s_), White(0.55f), desc, wrap);
  if (card.points > 0) {
    char pts[24];
    std::snprintf(pts, sizeof pts, "%d G", card.points);
    TextRight(g_f.semibold, 15, ImVec2(b_.x - 24 * s_, y + 16 * s_), card.unlocked ? White(0.9f) : White(0.4f),
              pts);
  }
  if (card.unlocked) g_f.dl->AddCircleFilled(ImVec2(x + d - 2 * s_, y + d - 2 * s_), 8 * s_, kGreen, 16);
  y_ += row_h_;
}

}  // namespace whopping::dash
