#include "game_settings.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace whopping {
namespace {

std::string Trim(std::string s) {
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

bool IsNumber(const std::string& s) {
  if (s.empty()) return false;
  size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
  bool digit = false, dot = false;
  for (; i < s.size(); ++i) {
    if (std::isdigit(static_cast<unsigned char>(s[i]))) {
      digit = true;
    } else if (s[i] == '.' && !dot) {
      dot = true;
    } else {
      return false;
    }
  }
  return digit;
}

// "name = value" -> name, unquoted value. False for comments, tables and blanks.
bool ParseLine(const std::string& line, std::string& key, std::string& value) {
  std::string t = Trim(line);
  if (t.empty() || t[0] == '#' || t[0] == '[') return false;
  size_t eq = t.find('=');
  if (eq == std::string::npos) return false;
  key = Trim(t.substr(0, eq));
  std::string v = Trim(t.substr(eq + 1));
  if (!v.empty() && v[0] == '"') {
    std::string out;
    for (size_t i = 1; i < v.size(); ++i) {
      if (v[i] == '\\' && i + 1 < v.size()) {
        out += v[++i];
      } else if (v[i] == '"') {
        break;
      } else {
        out += v[i];
      }
    }
    value = out;
  } else {
    size_t hash = v.find(" #");
    value = Trim(hash == std::string::npos ? v : v.substr(0, hash));
  }
  return !key.empty();
}

}  // namespace

std::string TomlValue(const std::string& value) {
  if (value == "true" || value == "false" || IsNumber(value)) return value;
  std::string out = "\"";
  for (char c : value) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out + "\"";
}

TomlSettings::TomlSettings(std::string path) : path_(std::move(path)) {
  true_value = "true";
  false_value = "false";
  Load();
}

void TomlSettings::Load() {
  lines_.clear();
  values_.clear();
  dirty_.clear();
  removed_.clear();
  std::ifstream in(std::filesystem::u8path(path_));
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines_.push_back(line);
    std::string k, v;
    if (ParseLine(line, k, v)) values_[k] = v;
  }
}

std::string TomlSettings::Get(std::string_view key, std::string_view def) const {
  auto it = values_.find(key);
  if (it == values_.end() || it->second.empty()) return std::string(def);
  return it->second;
}

void TomlSettings::Set(std::string_view key, std::string value) {
  values_[std::string(key)] = std::move(value);
  dirty_[std::string(key)] = true;
  removed_.erase(std::remove(removed_.begin(), removed_.end(), std::string(key)), removed_.end());
}

void TomlSettings::ResetToDefaults() {
  for (const auto& [k, v] : values_) removed_.push_back(k);
  values_.clear();
  dirty_.clear();
}

bool TomlSettings::Save() {
  std::vector<std::string> out;
  std::map<std::string, bool, std::less<>> written;
  for (const std::string& line : lines_) {
    std::string k, v;
    if (!ParseLine(line, k, v)) {
      out.push_back(line);
      continue;
    }
    if (std::find(removed_.begin(), removed_.end(), k) != removed_.end()) continue;
    auto it = values_.find(k);
    if (it != values_.end() && dirty_.count(k)) {
      out.push_back(k + " = " + TomlValue(it->second));
    } else {
      out.push_back(line);
    }
    written[k] = true;
  }
  bool header = false;
  for (const auto& [k, v] : values_) {
    if (written.count(k) || !dirty_.count(k)) continue;
    if (!header) {
      out.push_back("");
      out.push_back("# Set from The Whopping Recompilation Collection launcher");
      header = true;
    }
    out.push_back(k + " = " + TomlValue(v));
  }
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::u8path(path_).parent_path(), ec);
  const std::string tmp = path_ + ".new";
  {
    std::ofstream f(std::filesystem::u8path(tmp), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    for (const std::string& line : out) f << line << "\n";
    if (!f) return false;
  }
  std::filesystem::rename(std::filesystem::u8path(tmp), std::filesystem::u8path(path_), ec);
  if (ec) return false;
  Load();
  return true;
}

}  // namespace whopping
