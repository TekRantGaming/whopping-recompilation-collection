#include "projects.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>

#include <trg/download.h>

#include "games.h"
#include "install.h"

namespace whopping {
namespace fs = std::filesystem;

bool DecodeImage(const std::string& bytes, int& width, int& height, std::vector<unsigned char>& rgba);  // art.cpp

namespace {

// From King Kong Recompiled's Workshop (workshop_logic.h), King Kong first. Art links picked on steamgriddb.com:
// most are wide (460 x 215 or 920 x 430), the two South Park ones square; tiles fit any shape (dash::FitArt).
const std::vector<Project> kProjects = {
    {"King Kong Recompiled", "TekRantGaming", "king-kong-recompiled",
     "https://cdn2.steamgriddb.com/grid/3b7dbfa5b5fd2ad4cedaf1dbf2d1f693.png",  // wide 920 x 430, like the others
     "https://cdn2.steamgriddb.com/hero/e3fc72518f213ad1f39ed9306da322b0.png", true},
    {"Point Blank Recompiled", "TekRantGaming", "point-blank-recompiled",
     "https://cdn2.steamgriddb.com/grid/24f5d2bc2e14744b5bfd41a47c9d25f3.png", ""},
    {"Simpsons Wrestling Recompiled", "TekRantGaming", "simpsons-wrestling-recompiled",
     "https://cdn2.steamgriddb.com/grid/63d36bef6780ab6f8195f0803b728ac4.jpg", ""},
    {"Earthworm Jim HD Recompiled", "TekRantGaming", "earthworm-jim-hd-recompiled",
     "https://cdn2.steamgriddb.com/grid/48019033f9941955c6348a063eeb09c9.png", ""},
    {"Outpost Kaloki X Recompiled", "TekRantGaming", "outpost-kaloki-x-recompiled", "", ""},
    {"Super Mario Sunshine", "chasem-dev", "sms-launcher",
     "https://cdn2.steamgriddb.com/grid/e05d8eb9513ddafd8f410d874077d1b8.png", ""},
    {"Dig Dug Native", "TekRantGaming", "digdug-native",
     "https://cdn2.steamgriddb.com/grid/455a8d4a5cbf27ca5c8e480ea66c6a8f.png", ""},
    {"South Park: Tenorman's Revenge Recompiled", "TekRantGaming", "south-park-tenormans-revenge-recompiled",
     "https://cdn2.steamgriddb.com/grid/a22997b93b15692ee45f7a9fb6778c8d.jpg", ""},
    {"South Park: Let's Go Tower Defense Play! Recompiled", "TekRantGaming", "south-park-tower-defense-recompiled",
     "https://cdn2.steamgriddb.com/grid/e65e26a9a28ef7478d953cbec82be1b0.png", ""},
};

std::string Dir() { return CacheDir() + "/projects"; }

bool ReadFile(const fs::path& p, std::string& out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return !out.empty();
}

void WriteFile(const fs::path& p, const std::string& data) {
  std::error_code ec;
  fs::create_directories(p.parent_path(), ec);
  const fs::path part = p.string() + ".part";
  {
    std::ofstream f(part, std::ios::binary);
    f.write(data.data(), std::streamsize(data.size()));
  }
  fs::rename(part, p, ec);
}

bool Fresh(const fs::path& p, std::chrono::hours age) {
  std::error_code ec;
  const auto t = fs::last_write_time(p, ec);
  if (ec) return false;
  return fs::file_time_type::clock::now() - t < age;
}

// The value of "key" in a JSON object (a string with its escapes undone, or a number), "" when absent or null.
std::string JsonValue(const std::string& json, const std::string& key) {
  const std::string k = "\"" + key + "\"";
  size_t p = json.find(k);
  if (p == std::string::npos) return "";
  p = json.find(':', p + k.size());
  if (p == std::string::npos) return "";
  ++p;
  while (p < json.size() && (json[p] == ' ' || json[p] == '\n' || json[p] == '\r' || json[p] == '\t')) ++p;
  if (p >= json.size()) return "";
  if (json[p] != '"') {
    size_t e = p;
    while (e < json.size() && json[e] != ',' && json[e] != '}' && json[e] != '\n') ++e;
    std::string v = json.substr(p, e - p);
    return v == "null" ? "" : v;
  }
  std::string out;
  for (size_t i = p + 1; i < json.size() && json[i] != '"'; ++i) {
    if (json[i] != '\\' || i + 1 >= json.size()) {
      out += json[i];
      continue;
    }
    const char c = json[++i];
    if (c == 'n') out += ' ';
    else if (c == 't') out += ' ';
    else if (c == 'u' && i + 4 < json.size()) {
      const unsigned cp = unsigned(std::strtoul(json.substr(i + 1, 4).c_str(), nullptr, 16));
      i += 4;
      if (cp < 0x80) out += char(cp);
      else if (cp < 0x800) {
        out += char(0xC0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3F));
      } else {
        out += char(0xE0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
      }
    } else out += c;
  }
  return out;
}

// "2026-10-12T08:01:02Z" -> "12 Oct 2026".
std::string NiceDate(const std::string& iso) {
  int y = 0, m = 0, d = 0;
  if (std::sscanf(iso.c_str(), "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12) return "";
  static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  char buf[32];
  std::snprintf(buf, sizeof buf, "%d %s %d", d, months[m - 1], y);
  return buf;
}

std::string Ext(const std::string& url) {
  const size_t dot = url.rfind('.');
  return dot == std::string::npos ? ".img" : url.substr(dot);
}

trg::UpdaterConfig KingKongConfig() {
  trg::UpdaterConfig c;
  c.stable_repo = "TekRantGaming/king-kong-recompiled";
  c.current_version = "0.0.0";  // the collection only installs it; King Kong's own launcher updates it
  c.program_dir = LauncherDir() + "/games/king_kong";
  c.program_name = "king_kong.exe";
  c.files_list = "kk-files.txt";
  c.retired_list = "kk-retired.txt";
  // King Kong keeps its disc files, settings and user data beside the program; an install never touches them.
  c.protected_paths = {"game", "king_kong.toml", "logs", "cache", "saves", "sounds"};
  c.work_dir = LauncherDir() + "/cache/downloads/king_kong";
  if (!g_release_server.empty()) c.api_base = g_release_server;
  return c;
}

}  // namespace

Projects::Projects(UploadFn upload, bool downloads_allowed)
    : upload_(upload), downloads_allowed_(downloads_allowed), info_(kProjects.size()), text_(kProjects.size()) {}

Projects::~Projects() {
  stop_ = true;
  if (worker_.joinable()) worker_.join();
}

const std::vector<Project>& Projects::list() const { return kProjects; }

void Projects::Start() {
  if (!worker_.joinable()) worker_ = std::thread([this] { Worker(); });
}

void Projects::Worker() {
  for (size_t i = 0; i < kProjects.size() && !stop_; ++i) {
    const Project& p = kProjects[i];
    // GitHub facts, at most once a day (60 unauthenticated requests an hour per address).
    const fs::path info = fs::u8path(Dir() + "/" + p.repo + ".json");
    std::string json;
    const bool have = ReadFile(info, json);
    if ((!have || !Fresh(info, std::chrono::hours(24))) && downloads_allowed_) {
      std::string fresh;
      if (trg::HttpGet(std::string("https://api.github.com/repos/") + p.owner + "/" + p.repo, fresh).empty() &&
          fresh.find("\"full_name\"") != std::string::npos) {
        WriteFile(info, fresh);
        json = fresh;
      }
    }
    {
      std::lock_guard<std::mutex> lock(mu_);
      ProjectInfo& t = text_[i];
      if (json.empty()) {
        t.failed = true;
      } else {
        t.description = JsonValue(json, "description");
        t.updated = NiceDate(JsonValue(json, "pushed_at"));
        const std::string stars = JsonValue(json, "stargazers_count");
        t.stars = stars.empty() ? -1 : std::atoi(stars.c_str());
      }
    }
    // Art, cached for good once downloaded.
    for (int kind = 0; kind < 2 && !stop_; ++kind) {
      const std::string url = kind == 0 ? p.grid_url : p.hero_url;
      if (url.empty()) continue;
      const fs::path file = fs::u8path(Dir() + "/" + p.repo + (kind == 0 ? "_card" : "_hero") + Ext(url));
      std::string bytes;
      if (!ReadFile(file, bytes) && downloads_allowed_ && trg::HttpGet(url, bytes).empty()) WriteFile(file, bytes);
      Decoded d{i, kind == 1, 0, 0, {}};
      if (bytes.empty() || !DecodeImage(bytes, d.w, d.h, d.rgba)) continue;
      std::lock_guard<std::mutex> lock(mu_);
      ready_.push_back(std::move(d));
    }
  }
}

void Projects::Poll() {
  std::vector<Decoded> ready;
  {
    std::lock_guard<std::mutex> lock(mu_);
    ready.swap(ready_);
    for (size_t i = 0; i < info_.size(); ++i) {
      info_[i].description = text_[i].description;
      info_[i].updated = text_[i].updated;
      info_[i].stars = text_[i].stars;
      info_[i].failed = text_[i].failed;
    }
  }
  for (Decoded& d : ready) (d.hero ? info_[d.index].hero : info_[d.index].grid) = upload_(d.w, d.h, d.rgba.data());
}

ProjectInfo Projects::snapshot(size_t i) {
  std::lock_guard<std::mutex> lock(mu_);
  return text_[i];
}

std::string Projects::king_kong_dir() const { return LauncherDir() + "/games/king_kong"; }
std::string Projects::king_kong_exe() const { return king_kong_dir() + "/king_kong.exe"; }

bool Projects::king_kong_installed() const {
  std::error_code ec;
  return fs::exists(fs::u8path(king_kong_exe()), ec);
}

void Projects::StartKingKongInstall() {
  if (kk_task_.running()) return;
  kk_check_ = {};
  kk_report_ = {};
  const trg::UpdaterConfig config = KingKongConfig();
  trg::UpdateCheck* check = &kk_check_;
  trg::InstallReport* report = &kk_report_;
  kk_task_.Start("Downloading King Kong Recompiled", [config, check, report](trg::Task& task) -> std::string {
    std::error_code ec;
    fs::create_directories(fs::u8path(config.program_dir), ec);
    std::string err = trg::CheckForUpdate(task, config, "stable", false, check);
    if (!err.empty()) return err;
    if (!check->found) return "No release of King Kong Recompiled was found.";
    task.SetLabel("Downloading King Kong Recompiled " + check->found->version);
    return trg::InstallUpdate(task, config, *check->found, report);
  });
}

std::string Projects::PollKingKong() {
  std::string error;
  switch (kk_task_.Finish(&error)) {
    case trg::Task::State::kDone:
      kk_error_ = false;
      return "King Kong Recompiled " + (kk_check_.found ? kk_check_.found->version : std::string()) +
             " is installed. Its own launcher asks for your disc when it first starts.";
    case trg::Task::State::kFailed:
      kk_error_ = true;
      return error.empty() ? "King Kong Recompiled could not be installed." : error;
    case trg::Task::State::kCancelled:
      kk_error_ = false;
      return "Cancelled.";
    default:
      return "";
  }
}

}  // namespace whopping
