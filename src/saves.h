// Save games and removing a game, for the Manage install page. Saves are the
// game's user data folder (Documents/<id>: profile, saves, achievements);
// backups are zip files in save-backups/<id>, named by date and time.
#pragma once

#include <string>
#include <vector>

#include <trg/platform.h>

#include "games.h"

namespace whopping {

struct Backup {
  std::string path;   // UTF-8
  std::string label;  // "25 Oct 2026  14:03"
  long long size = 0;
};

bool HasSaves(const GameInfo& game);
std::vector<Backup> ListBackups(const GameInfo& game);  // newest first

// Each runs on a Task and returns "" on success, else a message for the player.
std::string BackupSavesJob(trg::Task& task, const GameInfo& game, std::string* made = nullptr);
// Makes a backup of the current saves first (when there are any), then puts
// the chosen backup in their place.
std::string RestoreBackupJob(trg::Task& task, const GameInfo& game, const std::string& backup);
std::string DeleteSavesJob(trg::Task& task, const GameInfo& game);  // backs up first, then deletes
// Deletes the game's program and disc files (games/<id>). Saves and backups stay.
std::string RemoveGameJob(trg::Task& task, const GameInfo& game);

}  // namespace whopping
