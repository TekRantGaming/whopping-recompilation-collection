#include "games.h"

#include <filesystem>

#include <trg/updater.h>

#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h>
#endif

namespace whopping {
namespace fs = std::filesystem;

namespace {

std::string Utf8(const fs::path& p) {
  auto s = p.generic_u8string();
  return std::string(s.begin(), s.end());
}

fs::path PathOf(const std::string& utf8) { return fs::u8path(utf8); }

}  // namespace

const std::array<GameInfo, kGameCount>& Games() {
  static const std::array<GameInfo, kGameCount> games = {{
      {"sneak_king", "Sneak King", "sneak_king.exe",  kCollectionRepo, "SneakKing",
       0x4D538802, 5315547, ImVec4(0.93f, 0.62f, 0.10f, 1.0f), "Deliver the goods without being seen.", "0.0.0.70",
       "7,027,228,672 bytes", "BDE18507EBF74060D43625BE8CE2B20C604DAF3E", "about 1.6 GB", "5D787B9D",
       "74C91928178C93D81C9077D74331FBFD", "D1C0F3FE", "Burger King Sneak King.iso"},
      {"big_bumpin", "Big Bumpin'", "big_bumpin.exe",  kCollectionRepo, "BigBumpin",
       0x4D538801, 5315548, ImVec4(0.90f, 0.22f, 0.20f, 1.0f), "Bumper car battles at the fair.", "0.0.0.70",
       "7,825,162,240 bytes", "51FBF8E78A25D466295568FBE9752C7070961779", "about 2.0 GB", "56F9F46F",
       "9BA7CD5F4B589A80A6C5E876F4A2B26E", "0D2B2716", "Big Bumpin' (USA).iso"},
      {"pocketbike_racer", "PocketBike Racer", "pocketbike_racer.exe",
       kCollectionRepo, "PocketBikeRacer", 0x4D538803, 5315546,
       ImVec4(0.18f, 0.48f, 0.90f, 1.0f), "Tiny bikes, big races.", "0.0.0.70", "7,825,162,240 bytes",
       "6A801254B1691717445253A1F4E068115F16F7B9", "about 1.2 GB", "346E875D", "FA5A17171280CE21AFD58B7EB5F7035A",
       "A2F832A7", "PocketBike Racer (USA).iso"},
  }};
  return games;
}

std::string LauncherDir() {
  static const std::string dir = [] {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
    if (n > 0 && n < std::size(buf)) return Utf8(fs::path(buf).parent_path());
#endif
    return Utf8(fs::current_path());
  }();
  return dir;
}

std::string GameDir(const GameInfo& game) { return LauncherDir() + "/games/" + game.id; }
std::string GameDataDir(const GameInfo& game) { return GameDir(game) + "/game"; }
std::string GameExePath(const GameInfo& game) { return GameDir(game) + "/" + game.exe; }
std::string GameSettingsPath(const GameInfo& game) { return GameDir(game) + "/" + game.id + ".toml"; }
std::string BackupsDir(const GameInfo& game) { return LauncherDir() + "/save-backups/" + game.id; }
std::string CacheDir() { return LauncherDir() + "/cache"; }

std::string SavesDir(const GameInfo& game) {
#if defined(_WIN32)
  PWSTR docs = nullptr;
  std::string out;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &docs))) {
    out = Utf8(fs::path(docs) / game.id);
  }
  CoTaskMemFree(docs);
  return out;
#else
  return std::string(getenv("HOME") ? getenv("HOME") : ".") + "/.local/share/" + game.id;
#endif
}

GameState DetectState(const GameInfo& game) {
  std::error_code ec;
  if (!fs::exists(PathOf(GameExePath(game)), ec)) return GameState::kNotDownloaded;
  if (!fs::exists(PathOf(GameDataDir(game) + "/default.xex"), ec)) return GameState::kDownloaded;
  return GameState::kInstalled;
}

std::string InstalledVersion(const GameInfo& game) {
  auto manifest = trg::update::ReadManifest(GameDir(game) + "/update-files.txt");
  return manifest ? manifest->version : std::string();
}

}  // namespace whopping
