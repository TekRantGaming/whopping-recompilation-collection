#include "saves.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>

#if defined(_WIN32)
#include <windows.h>
#else
#include <cstdlib>
#include <sys/wait.h>
#endif

namespace whopping {
namespace fs = std::filesystem;

namespace {

std::string Utf8(const fs::path& p) {
  auto s = p.generic_u8string();
  return std::string(s.begin(), s.end());
}

// Runs Windows' own tar (part of Windows 10 and 11), hidden; on Linux the system's tar (without the AppImage's
// library path). Returns its exit code.
int RunTar(const std::wstring& args) {
#if defined(_WIN32)
  wchar_t sysdir[MAX_PATH];
  GetSystemDirectoryW(sysdir, MAX_PATH);
  std::wstring cmd = L"\"" + std::wstring(sysdir) + L"\\tar.exe\" " + args;
  STARTUPINFOW si{};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    return -1;
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return int(code);
#else
  const fs::path a(args);
  const std::string cmd = "env -u LD_LIBRARY_PATH tar " + a.u8string() + " >/dev/null 2>&1";
  const int rc = std::system(cmd.c_str());
  return rc == -1 ? -1 : WEXITSTATUS(rc);
#endif
}

std::wstring W(const std::string& utf8) {
  fs::path p = fs::u8path(utf8);
  return p.make_preferred().wstring();
}

std::string Stamp() {
  std::time_t now = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &now);
#else
  localtime_r(&now, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%d_%H-%M-%S", &tm);
  return buf;
}

std::string LabelFromName(const std::string& name) {
  // <id>_YYYY-MM-DD_HH-MM-SS.zip -> "25 Oct 2026  14:03"
  static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  size_t at = name.rfind('_');
  if (at == std::string::npos || at < 11) return name;
  const std::string date = name.substr(at - 10, 10), time = name.substr(at + 1, 5);
  int y = 0, m = 0, d = 0;
  if (std::sscanf(date.c_str(), "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12) return name;
  char buf[48];
  std::snprintf(buf, sizeof buf, "%d %s %d  %c%c:%c%c", d, months[m - 1], y, time[0], time[1], time[3], time[4]);
  return buf;
}

}  // namespace

bool HasSaves(const GameInfo& game) {
  std::error_code ec;
  const fs::path dir = fs::u8path(SavesDir(game));
  return fs::is_directory(dir, ec) && !fs::is_empty(dir, ec);
}

std::vector<Backup> ListBackups(const GameInfo& game) {
  std::vector<Backup> out;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(fs::u8path(BackupsDir(game)), ec)) {
    if (!e.is_regular_file(ec) || e.path().extension() != ".zip") continue;
    Backup b;
    b.path = Utf8(e.path());
    b.label = LabelFromName(Utf8(e.path().stem()));
    b.size = static_cast<long long>(e.file_size(ec));
    out.push_back(b);
  }
  std::sort(out.begin(), out.end(), [](const Backup& a, const Backup& b) { return a.path > b.path; });
  return out;
}

std::string BackupSavesJob(trg::Task& task, const GameInfo& game, std::string* made) {
  if (!HasSaves(game)) return "There are no saves to back up yet.";
  task.SetLabel("Backing up your saves");
  std::error_code ec;
  fs::create_directories(fs::u8path(BackupsDir(game)), ec);
  const std::string zip = BackupsDir(game) + "/" + game.id + "_" + Stamp() + ".zip";
  const fs::path saves = fs::u8path(SavesDir(game));
  // Only the saves, not the shader cache the game also keeps there.
  std::wstring args = L"-a -c -f \"" + W(zip) + L"\" -C \"" + saves.wstring() + L"\" --exclude cache .";
  if (RunTar(args) != 0) {
    fs::remove(fs::u8path(zip), ec);
    return "The backup could not be written.";
  }
  if (made) *made = zip;
  return std::string();
}

std::string RestoreBackupJob(trg::Task& task, const GameInfo& game, const std::string& backup) {
  if (HasSaves(game)) {
    std::string err = BackupSavesJob(task, game);
    if (!err.empty()) return "Your current saves could not be backed up first, so nothing was changed. " + err;
  }
  task.SetLabel("Restoring the backup");
  std::error_code ec;
  const fs::path saves = fs::u8path(SavesDir(game));
  fs::create_directories(saves, ec);
  if (RunTar(L"-x -f \"" + W(backup) + L"\" -C \"" + saves.wstring() + L"\"") != 0)
    return "The backup could not be restored.";
  return std::string();
}

std::string DeleteSavesJob(trg::Task& task, const GameInfo& game) {
  if (!HasSaves(game)) return "There are no saves to delete.";
  std::string err = BackupSavesJob(task, game);
  if (!err.empty()) return "Your saves could not be backed up first, so nothing was deleted. " + err;
  task.SetLabel("Deleting your saves");
  std::error_code ec;
  const fs::path saves = fs::u8path(SavesDir(game));
  for (const auto& e : fs::directory_iterator(saves, ec)) {
    if (e.path().filename() == "cache") continue;
    fs::remove_all(e.path(), ec);
    if (ec) return "Some saves could not be deleted (is the game running?).";
  }
  return std::string();
}

std::string RemoveGameJob(trg::Task& task, const GameInfo& game) {
  task.SetLabel(std::string("Removing ") + game.title);
  std::error_code ec;
  fs::remove_all(fs::u8path(GameDir(game)), ec);
  if (ec) return "Some files could not be removed (is the game running?).";
  return std::string();
}

}  // namespace whopping
