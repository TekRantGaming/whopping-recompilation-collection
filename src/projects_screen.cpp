// The Other TRG Projects screen: the projects as a grid of wide tiles on the
// right, the focused one's name, GitHub description and facts on the left with
// its buttons (Download or Play for King Kong Recompiled, the release page for the
// others, View on GitHub).

#include <algorithm>
#include <cstdio>
#include <string>

#include <trg/platform.h>

#include "app.h"
#include "sounds.h"

namespace whopping {
namespace {

using namespace dash;

struct ProjectsState {
  int focus = 0;
  int button = 0;  // 0 the main button, 1 View on GitHub (mouse and keyboard only: A always does the main one)
  float grow[16] = {};
};
ProjectsState ps;

// King Kong installs from here on Windows; on Linux its button opens its download page (its own AppImage).
bool Installable(const Project& p) {
#if defined(_WIN32)
  return p.installable;
#else
  (void)p;
  return false;
#endif
}

std::string MainLabel(App& app, const Project& p) {
  if (!Installable(p)) return "Download page";
  Projects& pr = app.projects();
  if (pr.king_kong_task().running()) return "Please wait";
  if (app.running_game() == App::kKingKong) return "Playing";
  return pr.king_kong_installed() ? "Play" : "Download";
}

void MainAction(App& app, const Project& p) {
  if (!Installable(p)) {
    trg::OpenUrl(std::string("https://github.com/") + p.owner + "/" + p.repo + "/releases/latest");
    return;
  }
  Projects& pr = app.projects();
  if (pr.king_kong_task().running()) return;
  if (pr.king_kong_installed())
    app.LaunchKingKong();
  else
    pr.StartKingKongInstall();
}

}  // namespace

void DrawProjects(App& app) {
  const Frame& f = F();
  Projects& pr = app.projects();
  const std::vector<Project>& list = pr.list();
  const int n = int(list.size());
  constexpr int kCols = 3;
  const float dt = std::min(0.1f, ImGui::GetIO().DeltaTime);
  const bool still = app.options().frames < 60;

  const std::string done = pr.PollKingKong();
  if (!done.empty()) app.SetToast(done, pr.king_kong_error());

  if (Pressed(Btn::kB)) {
    app.OpenHome(-2);
    return;
  }
  // A grid of wide tiles: the arrows move in it, LB and RB step through it in order.
  if (Pressed(Btn::kLeft) && ps.focus % kCols > 0) --ps.focus;
  if (Pressed(Btn::kRight) && ps.focus % kCols < kCols - 1 && ps.focus + 1 < n) ++ps.focus;
  if (Pressed(Btn::kUp) && ps.focus >= kCols) ps.focus -= kCols;
  if (Pressed(Btn::kDown) && ps.focus + kCols < n) ps.focus += kCols;
  if (Pressed(Btn::kLB)) ps.focus = (ps.focus + n - 1) % n;
  if (Pressed(Btn::kRB)) ps.focus = (ps.focus + 1) % n;
  const Project& p = list[size_t(ps.focus)];
  const ProjectInfo& info = pr.info(size_t(ps.focus));

  // Background: the project's wide art, darkened.
  f.dl->AddRectFilled(f.o, f.end, IM_COL32(8, 8, 10, 255));
  if (info.hero)
    Backdrop(info.hero, 1.0f, 0.6f);
  else if (info.grid)
    Backdrop(info.grid, 0.5f, 0.5f);
  Shade();
  f.dl->AddRectFilledMultiColor(f.o, f.end, Black(0.0f), Black(0.55f), Black(0.55f), Black(0.0f));
  TopBar({"Home", "Other TRG Projects"});

  // The focused project, on the left.
  float y = f.Y(96);
  const float wrap = 470;
  Text(f.bold, 32, ImVec2(f.X(48), y), White(), p.name, wrap);
  y += TextSize(f.bold, 32, p.name, wrap).y + 6 * f.s;
  Text(f.regular, 15, ImVec2(f.X(48), y), White(0.55f), std::string("github.com/") + p.owner + "/" + p.repo, wrap);
  y += TextSize(f.regular, 15, std::string("github.com/") + p.owner + "/" + p.repo, wrap).y + 14 * f.s;
  std::string desc = info.description;
  if (desc.empty()) desc = info.failed ? "Could not reach GitHub. Check your internet connection." : "Loading...";
  Text(f.regular, 16, ImVec2(f.X(48), y), White(0.85f), desc, wrap);
  y += TextSize(f.regular, 16, desc, wrap).y + 12 * f.s;
  std::string facts;
  if (info.stars >= 0) facts = std::to_string(info.stars) + (info.stars == 1 ? " star" : " stars");
  if (!info.updated.empty()) facts += (facts.empty() ? "" : "  \xC2\xB7  ") + ("Updated " + info.updated);
  if (p.installable) facts += (facts.empty() ? "" : "  \xC2\xB7  ") + std::string("Stable channel");
  Text(f.regular, 14, ImVec2(f.X(48), y), White(0.55f), facts);
  y += 36 * f.s;

  const std::string main = MainLabel(app, p);
  PillRect a = Pill(ImVec2(f.X(48), y), main, true, ps.button == 0, main == "Play");
  PillRect b = Pill(ImVec2(a.b.x + 14 * f.s, y), "View on GitHub", false, ps.button == 1);
  if (Hover(a.a, a.b) && MouseMoved()) ps.button = 0;
  if (Hover(b.a, b.b) && MouseMoved()) ps.button = 1;
  const bool click_main = Clicked(a.a, a.b), click_git = Clicked(b.a, b.b);
  y += 48 * f.s + 20 * f.s;
  if (Installable(p) && pr.king_kong_task().running()) {
    trg::Task& t = pr.king_kong_task();
    const float fr = t.fraction();
    char pct[16] = "";
    if (fr >= 0) std::snprintf(pct, sizeof pct, "  %.0f%%", double(fr) * 100.0);
    Text(f.regular, 15, ImVec2(f.X(48), y), White(0.85f), t.label() + pct, wrap);
    Bar(ImVec2(f.X(48), y + 28 * f.s), ImVec2(f.X(48 + 420), y + 34 * f.s), fr, White());
  } else if (Installable(p)) {
    Text(f.regular, 14, ImVec2(f.X(48), y), White(0.5f),
         pr.king_kong_installed() ? "Installed in this collection. Its own launcher keeps it up to date."
                                  : "Installs the newest stable version into this collection.",
         wrap);
  } else {
    Text(f.regular, 14, ImVec2(f.X(48), y), White(0.5f), "Download page opens the project's newest release on GitHub.",
         wrap);
  }

  // The grid of wide tiles, on the right.
  const float tw = 216, th = tw * 430.0f / 920.0f, gap = 14;
  const float gx = 1280 - 48 - (tw * kCols + gap * (kCols - 1));
  const float gy = 150;
  int hovered = -1;
  for (int i = 0; i < n; ++i) {
    const float target = i == ps.focus ? 1.0f : 0.0f;
    ps.grow[i] = still ? target : ps.grow[i] + (target - ps.grow[i]) * std::min(1.0f, dt * 14.0f);
    const float lift = 6.0f * ps.grow[i];
    const float x = gx + (i % kCols) * (tw + gap), yy = gy + (i / kCols) * (th + gap);
    const ImVec2 ta(f.X(x - lift * 0.5f), f.Y(yy - lift * 0.5f)), tb(f.X(x + tw + lift * 0.5f), f.Y(yy + th + lift * 0.5f));
    const ProjectInfo& pi = pr.info(size_t(i));
    f.dl->AddRectFilled(ImVec2(ta.x + 4 * f.s, ta.y + 6 * f.s), ImVec2(tb.x + 4 * f.s, tb.y + 6 * f.s), Black(0.45f),
                        10 * f.s);
    f.dl->AddRectFilled(ta, tb, IM_COL32(44, 46, 54, 255), 8 * f.s);
    if (pi.grid) {
      f.dl->PushClipRect(ta, tb, true);
      FitArt(pi.grid, ta, tb, 8 * f.s);
      f.dl->PopClipRect();
    } else {  // no art: the name on a plain card
      Text(f.semibold, 15, ImVec2(ta.x + 12 * f.s, ta.y + 12 * f.s), White(), list[size_t(i)].name,
           (tb.x - ta.x) / f.s - 24);
    }
    if (Installable(list[size_t(i)])) {
      const std::string badge = pr.king_kong_installed() ? "Installed" : "Can be installed";
      const ImVec2 t = TextSize(f.semibold, 12, badge);
      const ImVec2 ba(ta.x + 8 * f.s, tb.y - 8 * f.s - t.y - 6 * f.s), bb(ba.x + t.x + 14 * f.s, tb.y - 8 * f.s);
      f.dl->AddRectFilled(ba, bb, Black(0.75f), 4 * f.s);
      Text(f.semibold, 12, ImVec2(ba.x + 7 * f.s, ba.y + 3 * f.s), White(), badge);
    }
    if (i == ps.focus) FocusRing(ta, tb, 8);
    if (Hover(ta, tb)) hovered = i;
  }
  if (hovered >= 0 && MouseMoved() && hovered != ps.focus) {
    ps.focus = hovered;
    sound::Play(sound::Sound::kMove);
  }
  if (hovered >= 0 && ImGui::IsMouseClicked(0) && !ModalOpen()) {
    ps.focus = hovered;
    sound::Play(sound::Sound::kSelect);
  }

  if (Pressed(Btn::kA) || click_main) MainAction(app, list[size_t(ps.focus)]);
  if (Pressed(Btn::kY) || click_git)
    trg::OpenUrl(std::string("https://github.com/") + list[size_t(ps.focus)].owner + "/" + list[size_t(ps.focus)].repo);
  HintBar({{Btn::kA, main}, {Btn::kY, "View on GitHub"}, {Btn::kB, "Back"}});
}

}  // namespace whopping
