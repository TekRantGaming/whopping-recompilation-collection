// Game art from SteamGridDB, cached in cache/art/<steamgriddb id>/ so the
// launcher only downloads it once; when a cached file is missing it is fetched
// again in the background. Until art arrives the launcher draws plain cards.
#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <imgui.h>

namespace whopping {

enum class ArtKind { kGrid, kHero, kLogo, kCount };  // portrait card, wide background, transparent title logo

struct Texture {
  ImTextureID id{};
  int width = 0, height = 0;
  explicit operator bool() const { return id != ImTextureID{}; }
};

// Uploads RGBA8 pixels; set by the window host.
using UploadFn = Texture (*)(int width, int height, const unsigned char* rgba);

class ArtCache {
 public:
  ArtCache(std::vector<int> steamgriddb_ids, UploadFn upload);
  ~ArtCache();

  // Loads what is cached and starts fetching the rest. Call once the GL
  // context exists.
  void Start();
  // Uploads images the worker decoded. Call once per frame on the UI thread.
  void Poll();

  const Texture& Get(int game_index, ArtKind kind) const { return textures_[game_index][int(kind)]; }
  // The wide art, small and blurred: stretched behind the settings screens.
  const Texture& Blur(int game_index) const { return blurred_[game_index]; }
  std::string status() const;  // "" or a short line about downloads

 private:
  struct Decoded {
    int game, kind, width, height;  // kind -1: the blurred hero
    std::vector<unsigned char> rgba;
  };
  void Worker();

  std::vector<int> ids_;
  UploadFn upload_;
  std::vector<std::array<Texture, int(ArtKind::kCount)>> textures_;
  std::vector<Texture> blurred_;
  std::mutex mu_;
  std::vector<Decoded> ready_;
  std::string status_;
  std::thread worker_;
  std::atomic<bool> stop_{false};
};

// Frees a texture made by UploadFn; set by the window host.
extern void (*g_free_texture)(Texture& tex);

// False: only cached art is used (offline runs and screenshots).
extern bool g_art_downloads_allowed;

// Where art is cached (folder per game) and the file base name of a kind.
std::string ArtFolder(int steamgriddb_id);
const char* ArtName(ArtKind kind);

}  // namespace whopping
