// Home's Discord and Steam buttons. Add to Steam works as in King Kong Recompiled: Steam reads its non Steam
// games only when it starts and writes its own list when it closes, so an open Steam closes first and opens
// again afterwards (steam_shortcut.h).

#include <algorithm>
#include <chrono>
#include <thread>

#include <trg/platform.h>

#include "app.h"

namespace whopping {
namespace {

constexpr const char* kDiscordUrl = "https://discord.gg/8eakYSvBZS";
constexpr const char* kSteamPhases[] = {"Getting the artwork", "Waiting for Steam to close", "Adding to your library",
                                        "Opening Steam again"};

}  // namespace

void App::OpenDiscord() { trg::OpenUrl(kDiscordUrl); }

void App::StartSteamCheck() {
  steam_ = std::make_shared<SteamState>();
  std::thread([s = steam_] {
    if (steam::StartedFromSteam()) {
      s->available = true;
      s->in_library = true;
    } else if (const auto where = steam::Locate()) {
      s->available = true;
      s->in_library = steam::HasShortcut(*where);
    }
    s->checked = true;
  }).detach();
}

std::string App::SteamLabel() const {
  if (steam_ && steam_->busy) return kSteamPhases[std::clamp(steam_->phase.load(), 0, 3)];
  if (steam_ && steam_->in_library) return "Open Steam";
  return "Add to Steam";
}

void App::SteamPressed() {
  if (!steam_ || !steam_->checked || steam_->busy) return;
  if (steam_->in_library) {
    steam::OpenSteam();
    return;
  }
  if (!steam_->available) {
    SetToast("Steam wasn't found on this PC, or nobody has signed in to it yet.", true);
    return;
  }
  const bool running = steam::SteamRunning();
  const std::string text =
      "Adds the collection to your Steam library as a non Steam game, with its artwork, so you can start it from "
      "Steam, Big Picture or a Steam Deck.";
  dash::Confirm("Add to Steam?",
                running ? text + "\n\nSteam reads its list of games only when it starts, so it closes for a moment "
                                 "and opens again afterwards."
                        : text,
                running ? "Close Steam and add" : "Add to Steam", [this] { StartAddToSteam(); });
}

void App::StartAddToSteam() {
  auto s = steam_;
  s->busy = true;
  s->done = false;
  s->phase = 0;
  std::thread([s] {
    auto finish = [&](steam::Result r) {
      s->result = std::move(r);
      s->busy = false;
      s->done = true;
    };
    const auto where = steam::Locate();
    if (!where) return finish({false, false, 0, "Steam wasn't found on this PC, or nobody has signed in to it yet."});
    const auto art = steam::DownloadArt();
    const bool running = steam::SteamRunning();
    if (running) {
      s->phase = 1;
      steam::CloseSteam();
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
      while (steam::SteamRunning()) {
        if (std::chrono::steady_clock::now() > deadline)
          return finish({false, false, 0, "Steam didn't close. Close Steam yourself, then choose Add to Steam again."});
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1500));  // Steam's last writes
    }
    s->phase = 2;
    steam::Result r = steam::AddShortcut(*where, art);
    if (running) {
      s->phase = 3;
      steam::OpenSteam();
    }
    finish(std::move(r));
  }).detach();
}

void App::TickSteam() {
  if (!steam_ || !steam_->done) return;
  steam_->done = false;
  const steam::Result& r = steam_->result;
  if (!r.ok) {
    SetToast(r.error, true);
    return;
  }
  steam_->in_library = true;
  SetToast(r.art == 5  ? "The collection is in your Steam library, with its artwork."
           : r.art > 0 ? "The collection is in your Steam library, with " + std::to_string(r.art) +
                             " of its 5 artwork pictures; the others didn't download."
                       : "The collection is in your Steam library, but its artwork didn't download.",
           false);
}

}  // namespace whopping
