// A game's achievements for its Achievements page. The game writes the list and
// icons into games/<id>/game/achievements on its first run (its
// achievements_export.cpp); the runtime keeps the unlocks in
// Documents/<id>/achievements/<title id>.toml.
#pragma once

#include <string>
#include <vector>

#include <trg/launcher.h>

#include "art.h"
#include "games.h"

namespace whopping {

struct GameAchievements {
  bool loaded = false;
  bool list_found = false;  // false until the game has run once
  std::vector<trg::AchievementCard> cards;
  std::vector<Texture> icons;  // owned; same order as cards
  int unlocked = 0, points = 0, total_points = 0;
};

// Reads the list, the unlocks and the icons (uploading them with `upload`).
void LoadAchievements(const GameInfo& game, GameAchievements& out, UploadFn upload);

// PNG/JPEG bytes -> RGBA (art.cpp, stb_image).
bool DecodeImage(const std::string& bytes, int& width, int& height, std::vector<unsigned char>& rgba);

}  // namespace whopping
