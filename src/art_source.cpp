// Where the game art is downloaded from: SteamGridDB's image server
// (cdn2.steamgriddb.com), which serves each image to anyone without an API key.
// The images were picked from the games' SteamGridDB pages;
// change a line below to use another one. The result is cached (art.h), so
// each image is downloaded once.
//
//   Sneak King        https://www.steamgriddb.com/game/5315547
//   Big Bumpin'       https://www.steamgriddb.com/game/5315548
//   PocketBike Racer  https://www.steamgriddb.com/game/5315546
//
// Other images on those pages (for swapping):
//   Sneak King grids   b6b28486cb0a1675344ad86a6f7158ed.jpg, ac5e37a014b907541cc5964917631b0b.png,
//                      c1a95432b61cbb80a437ae43e2851c68.jpg; hero 24648affebbc9bd26c25ca2204036b60.jpg
//   Big Bumpin' grids  9614aed0f74fc5c205ab85c5819c0d88.png, f339455964a1d9e602cebb370b310783.png,
//                      8569199a7cbd506df106d3bfda2e21e3.png; hero 5dd808ef0d37652165522bd8b73b795e.jpg
//   PocketBike grids   9c530a4028d8196f9b88e305b978a50c.png, 628612ad1d84ed962e68713df78c81d7.jpg;
//                      hero 6648cb0e60b6ebcb33f638831097eb8b.jpg

#include <string>

#include <trg/download.h>

#include "art.h"

namespace whopping {
namespace {

struct ArtSet {
  int steamgriddb_id;
  const char* grid;  // 600 x 900 card
  const char* hero;  // 1920 x 620 background
  const char* logo;  // transparent title
};

const ArtSet kArt[] = {
    {5315547, "grid/1acc3f304807556a4aa25c38b681210b.png", "hero/77161957d9238f826b23a955f6a0b5ce.png",
     "logo/27c3025bb610524e6fc37870205dd0e7.png"},
    {5315548, "grid/2030310c6edc8298f0c3765834d0636e.png", "hero/ccfc2d538ddff519d893a6b966a1c4f1.png",
     "logo/1c555091db0a110c5f994d8f1866571e.png"},
    {5315546, "grid/a738df772803bc3aa4c117a55b197b71.jpg", "hero/bed402ffececc1f61e200943dc38b89d.png",
     "logo/59ff15c69ad138635326c3f8267af16e.png"},
};

}  // namespace

bool g_art_downloads_allowed = true;

std::string FetchArt(int steamgriddb_id, ArtKind kind, std::string& bytes, std::string& ext) {
  bytes.clear();
  if (!g_art_downloads_allowed) return "offline";
  for (const ArtSet& a : kArt) {
    if (a.steamgriddb_id != steamgriddb_id) continue;
    const char* path = kind == ArtKind::kGrid ? a.grid : kind == ArtKind::kHero ? a.hero : a.logo;
    const std::string p = path;
    ext = p.substr(p.rfind('.'));
    return trg::HttpGet("https://cdn2.steamgriddb.com/" + p, bytes);
  }
  return "no art for this game";
}

}  // namespace whopping
