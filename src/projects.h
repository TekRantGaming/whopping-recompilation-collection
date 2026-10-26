// Other TRG Projects: the other TekRantGaming ports, as on King Kong Recompiled's
// Workshop page. Each project shows its
// SteamGridDB art and its GitHub description, stars and last update, cached
// in cache/projects and read again at most once a day. Download opens the
// project's latest release page; King Kong Recompiled itself can be installed
// here from its stable channel and started (its own launcher then asks for the
// disc and keeps it up to date).
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <trg/platform.h>
#include <trg/updater.h>

#include "art.h"

namespace whopping {

struct Project {
  const char* name;
  const char* owner;
  const char* repo;
  const char* grid_url;  // the tile's SteamGridDB art, wide where there is one ("" when there is none)
  const char* hero_url;  // wide art ("" when there is none)
  bool installable = false;  // King Kong Recompiled: Download / Play here
};

struct ProjectInfo {
  std::string description, updated;  // from GitHub ("" until read)
  int stars = -1;
  bool failed = false;  // GitHub could not be reached and nothing was cached
  Texture grid, hero;
};

class Projects {
 public:
  Projects(UploadFn upload, bool downloads_allowed);
  ~Projects();
  void Start();
  void Poll();  // UI thread: uploads art the worker decoded

  const std::vector<Project>& list() const;
  const ProjectInfo& info(size_t i) const { return info_[i]; }
  ProjectInfo snapshot(size_t i);  // the text fields, read safely from the worker's results

  // King Kong Recompiled (the installable project).
  bool king_kong_installed() const;
  std::string king_kong_exe() const;
  std::string king_kong_dir() const;
  void StartKingKongInstall();
  trg::Task& king_kong_task() { return kk_task_; }
  std::string PollKingKong();  // "" or the finished job's message for a toast
  bool king_kong_error() const { return kk_error_; }

 private:
  struct Decoded {
    size_t index;
    bool hero;
    int w, h;
    std::vector<unsigned char> rgba;
  };
  void Worker();

  UploadFn upload_;
  bool downloads_allowed_;
  std::vector<ProjectInfo> info_;
  std::mutex mu_;
  std::vector<Decoded> ready_;
  std::vector<ProjectInfo> text_;  // worker results, guarded by mu_
  std::thread worker_;
  std::atomic<bool> stop_{false};
  trg::UpdateCheck kk_check_;
  trg::InstallReport kk_report_;
  bool kk_error_ = false;
  trg::Task kk_task_;  // last, so it stops before the results above go away
};

}  // namespace whopping
