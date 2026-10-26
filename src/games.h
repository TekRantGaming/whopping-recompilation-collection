// The three games of the collection and where each one lives on disk.
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <imgui.h>

namespace whopping {

// Where the collection is released: one GitHub repo for the launcher and the three games, each release holding
// WhoppingCollection-vX-windows-x64.zip (the launcher), SneakKing-, BigBumpin- and PocketBikeRacer-vX-windows-x64.zip
// and SHA256SUMS.txt.
inline constexpr const char* kCollectionRepo = "TekRantGaming/whopping-recompilation-collection";

struct GameInfo {
  const char* id;           // folder and settings name: "sneak_king"
  const char* title;        // "Sneak King"
  const char* exe;          // "sneak_king.exe" (also the ReXGlue app name, so the settings file is sneak_king.toml)
  const char* repo;         // GitHub "owner/name" its releases come from (kCollectionRepo)
  const char* zip_folder;   // the folder inside its release zip, and the zip's name start ("SneakKing")
  uint32_t title_id;        // the Xbox 360 title ID its disc must have
  int steamgriddb_id;       // https://www.steamgriddb.com/game/<id>
  ImVec4 accent;            // the game's colour in the launcher
  const char* tagline;      // one line under the title on the home screen
  // The supported disc, shown on the install page (only the title ID is checked, as in King Kong).
  const char* disc_version;
  const char* disc_size;    // "7,027,228,672 bytes"
  const char* disc_sha1;
  const char* installed_size;
  // More about the disc, for the install screen (from the dumps the ports were made with and their default.xex).
  const char* media_id;     // the execution info's media ID
  const char* disc_md5;
  const char* disc_crc32;
  const char* image_name;   // the file name of the image the port was made and tested with
};

enum class GameState {
  kNotDownloaded,  // no program files yet: the first button says Download
  kDownloaded,     // program files, but no game files from a disc: Install
  kInstalled,      // ready: Play
};

constexpr int kGameCount = 3;
const std::array<GameInfo, kGameCount>& Games();

// Folders (UTF-8, forward slashes). Everything lives beside the launcher, so the
// collection can be moved or deleted as one folder; saves stay where the games
// keep them (Documents/<id>).
std::string LauncherDir();
std::string GameDir(const GameInfo& game);       // games/<id>: the program files from the release
std::string GameDataDir(const GameInfo& game);   // games/<id>/game: the files from the player's disc
std::string GameExePath(const GameInfo& game);
std::string GameSettingsPath(const GameInfo& game);  // games/<id>/<id>.toml, read by the game at start
std::string SavesDir(const GameInfo& game);      // Documents/<id>
std::string BackupsDir(const GameInfo& game);    // save-backups/<id>
std::string CacheDir();                          // cache (art)

GameState DetectState(const GameInfo& game);
std::string InstalledVersion(const GameInfo& game);  // from update-files.txt; "" when unknown

}  // namespace whopping
