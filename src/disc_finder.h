// Finds the player's disc images, so the install page can offer them as
// buttons: the Windows file dialog cannot be used with a controller. Looks
// through the usual folders (Desktop, Downloads, Documents, Videos) and every
// fixed drive a few folders deep for .iso files, and keeps those whose title
// ID is one of the collection's games.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace whopping {

struct FoundDisc {
  std::string path;  // UTF-8
  uint32_t title_id = 0;
  long long size = 0;
};

class DiscFinder {
 public:
  ~DiscFinder();
  void Start();  // once; later calls do nothing while a scan runs or after it finished
  void Rescan();
  bool running() const { return running_; }
  std::vector<FoundDisc> Found(uint32_t title_id) const;

 private:
  void Run();
  std::thread worker_;
  std::atomic<bool> running_{false}, stop_{false}, done_{false};
  mutable std::mutex mu_;
  std::vector<FoundDisc> found_;
};

}  // namespace whopping
