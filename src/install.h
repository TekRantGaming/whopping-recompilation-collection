// Getting a game onto the PC, in the two steps the home screen shows:
//   Download  the newest release of the game's recompilation from GitHub (the
//             TRG updater: checked download, every listed file installed, undo
//             on failure). The same job later installs updates automatically.
//   Install   the files from the player's own disc image, after checking the
//             image's title ID is the right game.
#pragma once

#include <string>

#include <trg/platform.h>
#include <trg/updater.h>

#include "games.h"

namespace whopping {

trg::UpdaterConfig GameUpdaterConfig(const GameInfo& game);

// Testing: where releases are asked for instead of GitHub ("" = GitHub). Set
// by --release-server or WHOPPING_RELEASE_SERVER; tools/fake_github.py serves
// test releases there.
extern std::string g_release_server;

struct GameJobs {
  enum class Kind { kNone, kDownload, kUpdateCheck, kDisc };
  Kind kind = Kind::kNone;
  std::string message;       // the last result, for the player
  bool message_is_error = false;
  bool quiet = false;        // the automatic startup update: no message unless something was installed
  std::string update_found;  // a newer version found by the startup check ("" when none)
  std::string from_version;  // what was installed when the download started ("" for a first download)
  trg::UpdateCheck check;
  trg::InstallReport report;
  trg::Task task;            // last, so it stops before the results above go away
};

// Download (first install) or update: checks GitHub and installs the newest
// release. `quiet_if_current` keeps the message empty when nothing is newer.
void StartDownload(const GameInfo& game, GameJobs& jobs, bool quiet = false);
// Startup check: finds out whether a newer release exists, installs nothing.
void StartUpdateCheck(const GameInfo& game, GameJobs& jobs);
// Extracts the disc image into the game folder after checking its title ID.
void StartDiscInstall(const GameInfo& game, const std::string& image, GameJobs& jobs);
// Call each frame: collects a finished job's result into jobs.message.
// Returns true on the frame a job finished.
bool PollJobs(const GameInfo& game, GameJobs& jobs);

// What the player's file is, in words, when its title ID is another game's.
std::string DescribeTitleId(uint32_t title_id);

}  // namespace whopping
