#include "disc_finder.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include <trg/xbox360.h>

#include "games.h"

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

bool Skip(const fs::path& dir) {
  std::string n = Utf8(dir.filename());
  std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return n.empty() || n[0] == '$' || n[0] == '.' || n == "windows" || n == "program files" ||
         n == "program files (x86)" || n == "programdata" || n == "appdata" || n == "system volume information" ||
         n == "recovery" || n == "node_modules";
}

}  // namespace

DiscFinder::~DiscFinder() {
  stop_ = true;
  if (worker_.joinable()) worker_.join();
}

void DiscFinder::Start() {
  if (running_ || done_) return;
  if (worker_.joinable()) worker_.join();
  running_ = true;
  worker_ = std::thread([this] { Run(); });
}

void DiscFinder::Rescan() {
  if (running_) return;
  done_ = false;
  Start();
}

std::vector<FoundDisc> DiscFinder::Found(uint32_t title_id) const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<FoundDisc> out;
  for (const FoundDisc& d : found_)
    if (d.title_id == title_id) out.push_back(d);
  return out;
}

void DiscFinder::Run() {
  std::vector<std::pair<fs::path, int>> roots;  // folder, depth to search
#if defined(_WIN32)
  const KNOWNFOLDERID* known[] = {&FOLDERID_Desktop, &FOLDERID_Downloads, &FOLDERID_Documents, &FOLDERID_Videos};
  for (const KNOWNFOLDERID* id : known) {
    PWSTR p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(*id, KF_FLAG_DEFAULT, nullptr, &p))) roots.push_back({fs::path(p), 3});
    CoTaskMemFree(p);
  }
  const DWORD drives = GetLogicalDrives();
  for (int i = 0; i < 26; ++i) {
    if (!(drives & (1u << i))) continue;
    wchar_t root[] = {wchar_t(L'A' + i), L':', L'\\', 0};
    if (GetDriveTypeW(root) != DRIVE_FIXED) continue;
    roots.push_back({fs::path(root), 3});
  }
#endif
  std::vector<FoundDisc> found;
  std::vector<std::string> seen;
  for (const auto& [root, depth] : roots) {
    if (stop_) break;
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end && !stop_; it.increment(ec)) {
      if (it.depth() > depth) {
        it.disable_recursion_pending();
        continue;
      }
      const fs::directory_entry& e = *it;
      std::error_code ec2;
      if (e.is_directory(ec2)) {
        if (Skip(e.path()) || it.depth() >= depth) it.disable_recursion_pending();
        continue;
      }
      std::string ext = Utf8(e.path().extension());
      std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
      if (ext != ".iso") continue;
      const std::string path = Utf8(e.path());
      if (std::find(seen.begin(), seen.end(), path) != seen.end()) continue;
      seen.push_back(path);
      const uint32_t id = trg::ReadXbox360TitleId(path);
      for (const GameInfo& g : Games()) {
        if (g.title_id == id) found.push_back({path, id, static_cast<long long>(e.file_size(ec2))});
      }
      std::lock_guard<std::mutex> lock(mu_);
      found_ = found;
    }
  }
  std::lock_guard<std::mutex> lock(mu_);
  found_ = found;
  running_ = false;
  done_ = true;
}

}  // namespace whopping
