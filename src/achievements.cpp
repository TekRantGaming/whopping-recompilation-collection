#include "achievements.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

#include "game_settings.h"

namespace whopping {
namespace fs = std::filesystem;

namespace {

std::string ReadAll(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

std::string Trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

std::string Unquote(const std::string& v) {
  if (v.size() < 2 || v[0] != '"') return v;
  std::string out;
  for (size_t i = 1; i + 1 < v.size(); ++i) {
    if (v[i] == '\\' && i + 2 < v.size()) {
      out += v[++i];
    } else {
      out += v[i];
    }
  }
  return out;
}

}  // namespace

void LoadAchievements(const GameInfo& game, GameAchievements& out, UploadFn upload) {
  if (g_free_texture)
    for (Texture& icon : out.icons)
      if (icon) g_free_texture(icon);
  out = GameAchievements();
  out.loaded = true;
  const fs::path dir = fs::u8path(GameDataDir(game) + "/achievements");
  std::ifstream list(dir / "achievements.toml");
  if (!list) return;
  out.list_found = true;

  // Unlocked ids: "[unlocked.<id>]" sections of the runtime's unlock file.
  std::set<unsigned> unlocked;
  char name[32];
  std::snprintf(name, sizeof name, "%08X.toml", game.title_id);
  std::ifstream unlocks(fs::u8path(SavesDir(game)) / "achievements" / name);
  std::string line;
  while (std::getline(unlocks, line)) {
    unsigned id = 0;
    if (std::sscanf(Trim(line).c_str(), "[unlocked.%u]", &id) == 1) unlocked.insert(id);
  }

  // [[achievements]] blocks of `key = value` lines.
  std::vector<std::map<std::string, std::string>> entries;
  while (std::getline(list, line)) {
    line = Trim(line);
    if (line == "[[achievements]]") {
      entries.emplace_back();
    } else if (!entries.empty()) {
      size_t eq = line.find('=');
      if (eq != std::string::npos) entries.back()[Trim(line.substr(0, eq))] = Unquote(Trim(line.substr(eq + 1)));
    }
  }
  for (auto& e : entries) {
    trg::AchievementCard card;
    const unsigned id = unsigned(std::strtoul(e["id"].c_str(), nullptr, 10));
    card.title = e["label"];
    card.description = e["description"];
    card.locked_description = e["unachieved_description"];
    card.points = std::atoi(e["gamerscore"].c_str());
    card.unlocked = unlocked.count(id) != 0;
    Texture icon;
    if (!e["icon_path"].empty()) {
      int w = 0, h = 0;
      std::vector<unsigned char> rgba;
      if (DecodeImage(ReadAll(dir / fs::u8path(e["icon_path"])), w, h, rgba)) icon = upload(w, h, rgba.data());
    }
    card.icon = icon.id;
    out.icons.push_back(icon);
    out.total_points += card.points;
    if (card.unlocked) {
      ++out.unlocked;
      out.points += card.points;
    }
    out.cards.push_back(std::move(card));
  }
}

}  // namespace whopping
