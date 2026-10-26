// A game's page, like a game hub on the Xbox dashboard: the art and logo, a
// row of buttons (Play, Settings, Saves and install) and cards underneath for
// achievements, what's new and save backups.

#include <algorithm>
#include <cstdio>
#include <string>

#include <trg/platform.h>

#include "app.h"
#include "sounds.h"

namespace whopping {
namespace {

using namespace dash;

struct HubState {
  int row = 0;  // 0 buttons, 1 cards
  int col = 0;
};
HubState hub;

const char* kBullet = "  \xC2\xB7  ";

std::string Percent(float f) {
  if (f < 0) return "";
  char b[16];
  std::snprintf(b, sizeof b, "  %.0f%%", double(f) * 100.0);
  return b;
}

void CardTitle(ImVec2 a, const std::string& small, const std::string& big) {
  const Frame& f = F();
  Text(f.regular, 13, ImVec2(a.x + 18 * f.s, a.y + 16 * f.s), White(0.62f), small);
  Text(f.semibold, 26, ImVec2(a.x + 18 * f.s, a.y + 34 * f.s), White(), big);
}

void AchievementsCard(App& app, int g, ImVec2 a, ImVec2 b) {
  const Frame& f = F();
  Card(a, b);
  if (app.state(g) != GameState::kInstalled) {
    CardTitle(a, "Achievements", "Not yet");
    Text(f.regular, 14, ImVec2(a.x + 18 * f.s, a.y + 80 * f.s), White(0.6f), "Install the game to see them.");
    return;
  }
  GameAchievements& ach = app.achievements(g);
  if (!ach.list_found) {
    CardTitle(a, "Achievements", "Not yet");
    Text(f.regular, 14, ImVec2(a.x + 18 * f.s, a.y + 80 * f.s), White(0.6f), "Start the game once to see them.");
    return;
  }
  const int total = int(ach.cards.size());
  CardTitle(a, "Achievements", std::to_string(ach.unlocked) + " of " + std::to_string(total));
  // Unlocked ones first, then the locked ones greyed out.
  std::vector<int> order;
  for (int pass = 0; pass < 2; ++pass)
    for (int i = 0; i < total; ++i)
      if (ach.cards[i].unlocked == (pass == 0)) order.push_back(i);
  float x = a.x + 18 * f.s;
  const float d = 44 * f.s, y = a.y + 84 * f.s;
  for (int k = 0; k < int(order.size()) && x + d < b.x - 18 * f.s; ++k) {
    const int i = order[k];
    const Texture& icon = ach.icons[i];
    if (icon)
      f.dl->AddImageRounded(ImTextureRef(icon.id), ImVec2(x, y), ImVec2(x + d, y + d), ImVec2(0, 0), ImVec2(1, 1),
                            ach.cards[i].unlocked ? White() : IM_COL32(80, 80, 80, 90), 6 * f.s);
    x += d + 8 * f.s;
  }
  Bar(ImVec2(a.x + 18 * f.s, b.y - 26 * f.s), ImVec2(b.x - 18 * f.s, b.y - 20 * f.s),
      total ? float(ach.unlocked) / float(total) : 0.0f, White());
}

void NewsCard(App& app, int g, ImVec2 a, ImVec2 b) {
  const Frame& f = F();
  f.dl->AddRectFilled(a, b, IM_COL32(20, 20, 24, 255), 10 * f.s);
  Cover(app.art().Get(g, ArtKind::kHero), a, b, 0.75f, 0.15f, 10 * f.s);
  f.dl->AddRectFilledMultiColor(ImVec2(a.x, a.y + (b.y - a.y) * 0.3f), b, Black(0.0f), Black(0.0f), Black(0.88f),
                                Black(0.88f));
  const std::string v = InstalledVersion(Games()[g]);
  Text(f.regular, 13, ImVec2(a.x + 18 * f.s, b.y - 66 * f.s), White(0.7f), "What's new");
  Text(f.semibold, 17, ImVec2(a.x + 18 * f.s, b.y - 46 * f.s), White(),
       v.empty() ? std::string("Release notes") : "Version " + v + " release notes");
}

void SavesCard(App& app, int g, ImVec2 a, ImVec2 b) {
  const Frame& f = F();
  Card(a, b);
  const std::vector<Backup>& backups = app.backups(g);
  const size_t n = backups.size();
  CardTitle(a, "Saves", n == 0 ? std::string("No backups") : std::to_string(n) + (n == 1 ? " backup" : " backups"));
  const std::string line = n == 0 ? "Back up your saves from here." : "Last backup " + backups.front().label;
  Text(f.regular, 14, ImVec2(a.x + 18 * f.s, a.y + 80 * f.s), White(0.6f), line, (b.x - a.x) / f.s - 36);
  GameUi& gu = app.game_ui(g);
  if (gu.manage.running())
    Text(f.regular, 14, ImVec2(a.x + 18 * f.s, a.y + 104 * f.s), White(0.85f), gu.manage.label());
}

}  // namespace

void FocusHubPlay() { hub = HubState(); }

void DrawHub(App& app, int g) {
  const Frame& f = F();
  const GameInfo& game = Games()[g];
  GameUi& gu = app.game_ui(g);

  if (Pressed(Btn::kB)) {
    app.OpenHome(g);
    return;
  }
  if (Pressed(Btn::kLB) || Pressed(Btn::kRB)) {
    app.OpenHub((g + (Pressed(Btn::kRB) ? 1 : kGameCount - 1)) % kGameCount);
    return;
  }
  if (Pressed(Btn::kUp)) hub.row = 0;
  if (Pressed(Btn::kDown)) hub.row = 1;
  if (Pressed(Btn::kLeft)) hub.col = std::max(0, hub.col - 1);
  if (Pressed(Btn::kRight)) hub.col = std::min(2, hub.col + 1);
  if (Pressed(Btn::kX) && gu.jobs.task.running() && gu.jobs.kind != GameJobs::Kind::kDisc) gu.jobs.task.Cancel();

  f.dl->AddRectFilled(f.o, f.end, IM_COL32(8, 8, 10, 255));
  Backdrop(app.art().Get(g, ArtKind::kHero), 1.0f, 0.72f);
  Shade();
  TopBar({"Home", game.title});

  // Logo.
  float y = f.Y(88);
  const Texture& logo = app.art().Get(g, ArtKind::kLogo);
  if (logo) {
    float w = 340 * f.s, h = w * float(logo.height) / float(logo.width);
    if (h > 180 * f.s) {
      h = 180 * f.s;
      w = h * float(logo.width) / float(logo.height);
    }
    f.dl->AddImage(ImTextureRef(logo.id), ImVec2(f.X(48), y), ImVec2(f.X(48) + w, y + h));
    y += h + 26 * f.s;
  } else {
    Text(f.bold, 54, ImVec2(f.X(48), y), White(), game.title);
    y += 80 * f.s;
  }

  // Buttons.
  int clicked = -1, hovered = -1;
  const std::string primary = app.PrimaryLabel(g);
  const std::string labels[3] = {primary, "Settings", "Saves and install"};
  float x = f.X(48);
  for (int i = 0; i < 3; ++i) {
    const bool focused = hub.row == 0 && hub.col == i;
    PillRect r = Pill(ImVec2(x, y), labels[i], i == 0, focused, i == 0 && primary == "Play");
    if (Hover(r.a, r.b)) hovered = i;
    if (Clicked(r.a, r.b)) clicked = i;
    x = r.b.x + 14 * f.s;
  }
  y += 48 * f.s + 24 * f.s;

  // Status.
  if (gu.jobs.task.running()) {
    const float fr = gu.jobs.task.fraction();
    Text(f.regular, 15, ImVec2(f.X(48), y), White(0.85f), gu.jobs.task.label() + Percent(fr));
    Bar(ImVec2(f.X(48), y + 28 * f.s), ImVec2(f.X(48 + 420), y + 34 * f.s), fr, White());
  } else if (gu.jobs.message_is_error && !gu.jobs.message.empty()) {
    Text(f.regular, 15, ImVec2(f.X(48), y), IM_COL32(255, 130, 120, 255), gu.jobs.message, 620);
  } else {
    const std::string v = InstalledVersion(game);
    std::string line;
    switch (app.state(g)) {
      case GameState::kNotDownloaded: line = "Not on this PC yet. Download gets the newest version."; break;
      case GameState::kDownloaded:
        line = "Downloaded" + (v.empty() ? std::string() : kBullet + ("Version " + v)) + kBullet +
               "Needs the files from your game disc";
        break;
      case GameState::kInstalled:
        line = "Installed" + (v.empty() ? std::string() : kBullet + ("Version " + v));
        if (!gu.jobs.update_found.empty()) line += kBullet + ("Version " + gu.jobs.update_found + " is out");
        break;
    }
    Text(f.regular, 15, ImVec2(f.X(48), y), White(0.66f), line);
  }

  // Cards.
  const float top = f.B(78 + 190), bottom = f.B(78);
  const float total = f.size.x - 96 * f.s - 28 * f.s;
  const float w0 = total * 1.1f / 3.1f, w1 = total / 3.1f;
  const ImVec2 ca[3] = {ImVec2(f.X(48), top), ImVec2(f.X(48) + w0 + 14 * f.s, top),
                        ImVec2(f.X(48) + w0 + w1 + 28 * f.s, top)};
  const ImVec2 cb[3] = {ImVec2(ca[0].x + w0, bottom), ImVec2(ca[1].x + w1, bottom), ImVec2(ca[2].x + w1, bottom)};
  AchievementsCard(app, g, ca[0], cb[0]);
  NewsCard(app, g, ca[1], cb[1]);
  SavesCard(app, g, ca[2], cb[2]);
  for (int i = 0; i < 3; ++i) {
    if (hub.row == 1 && hub.col == i) FocusRing(ca[i], cb[i], 10);
    if (Hover(ca[i], cb[i])) hovered = 3 + i;
    if (Clicked(ca[i], cb[i])) clicked = 3 + i;
  }
  if (hovered >= 0 && MouseMoved() && hub.row * 3 + hub.col != hovered) {
    hub.row = hovered / 3;
    hub.col = hovered % 3;
    sound::Play(sound::Sound::kMove);
  }

  int act = clicked;
  if (Pressed(Btn::kA)) act = hub.row * 3 + hub.col;
  if (Pressed(Btn::kStart)) act = 0;
  switch (act) {
    case 0: app.PrimaryAction(g); break;
    case 1: app.OpenGameSettings(g); break;
    case 2: app.OpenGameSettings(g, "Saves and install"); break;
    case 3: app.OpenGameSettings(g, "Achievements"); break;
    case 4: trg::OpenUrl(std::string("https://github.com/") + game.repo + "/releases"); break;
    case 5: app.OpenGameSettings(g, "Saves and install"); break;
    default: break;
  }

  std::vector<Hint> hints = {{Btn::kA, "Select"}, {Btn::kB, "Back"}};
  if (gu.jobs.task.running() && gu.jobs.kind != GameJobs::Kind::kDisc) hints.push_back({Btn::kX, "Cancel download"});
  hints.push_back({Btn::kLB, ""});
  hints.push_back({Btn::kRB, "Switch game"});
  HintBar(hints);
}

}  // namespace whopping
