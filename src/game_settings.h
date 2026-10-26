// Each game's own settings file (games/<id>/<id>.toml), which the game reads at
// start: flat `name = value` TOML lines, one per ReXGlue setting. The launcher
// edits it in place and keeps every line it does not know.
#pragma once

#include <map>
#include <string>
#include <vector>

#include <trg/settings.h>

namespace whopping {

class TomlSettings : public trg::SettingsStore {
 public:
  explicit TomlSettings(std::string path);

  void Load();
  std::string Get(std::string_view key, std::string_view def = {}) const override;
  void Set(std::string_view key, std::string value) override;
  bool Save() override;
  void ResetToDefaults() override;  // removes the launcher's settings, so the game's defaults apply

  const std::string& path() const { return path_; }

 private:
  std::string path_;
  std::vector<std::string> lines_;
  std::map<std::string, std::string, std::less<>> values_;   // unquoted values
  std::map<std::string, bool, std::less<>> dirty_;
  std::vector<std::string> removed_;
};

// TOML text for a value: true/false and numbers bare, anything else quoted.
std::string TomlValue(const std::string& value);

}  // namespace whopping
