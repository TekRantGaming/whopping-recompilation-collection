// The collection launcher's Achievements page shows each game's achievements:
// names, descriptions, gamerscore and icons. The launcher cannot read them from
// the encrypted default.xex, so on the first run after installing, the game
// writes them out of its own XDBF resource into <game folder>/achievements
// (achievements.toml and icons/<image>.png), as King Kong does
// (templates/src-examples/king_kong_app.h, ExportAchievementArt). Which ones are
// unlocked the launcher reads from the runtime's own unlock file
// (<user data>/achievements/<title id>.toml).

#include <filesystem>
#include <fstream>
#include <string>

#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/util/xdbf_utils.h>

namespace bk {

void ExportAchievements(rex::Runtime* runtime, const std::filesystem::path& game_data_root) {
  if (!runtime || game_data_root.empty()) return;
  const std::filesystem::path dir = game_data_root / "achievements";
  std::error_code ec;
  if (std::filesystem::exists(dir / "achievements.toml", ec)) return;
  auto* ks = runtime->kernel_state();
  if (!ks) return;
  const auto db = ks->title_xdbf();
  if (!db.is_valid()) return;
  std::filesystem::create_directories(dir / "icons", ec);
  const auto lang = db.GetExistingLanguage(rex::system::XLanguage::kEnglish);
  auto quote = [](const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
      if (c == '"' || c == '\\') out += '\\';
      if (static_cast<unsigned char>(c) >= 0x20) out += c;
    }
    return out + "\"";
  };
  const std::filesystem::path tmp = dir / "achievements.toml.new";
  {
    std::ofstream toml(tmp, std::ios::binary);
    for (const auto& a : db.GetAchievements()) {
      const uint32_t image = a.image_id;
      toml << "[[achievements]]\n"
           << "id = " << uint32_t(a.id) << "\n"
           << "label = " << quote(db.GetStringTableEntry(lang, a.label_id)) << "\n"
           << "description = " << quote(db.GetStringTableEntry(lang, a.description_id)) << "\n"
           << "unachieved_description = " << quote(db.GetStringTableEntry(lang, a.unachieved_id)) << "\n"
           << "gamerscore = " << uint32_t(a.gamerscore) << "\n"
           << "image_id = " << image << "\n"
           << "icon_path = \"icons/" << image << ".png\"\n\n";
      const auto block = db.GetEntry(rex::system::util::XdbfSection::kImage, image);
      if (block.buffer && block.size) {
        std::ofstream png(dir / "icons" / (std::to_string(image) + ".png"), std::ios::binary);
        png.write(reinterpret_cast<const char*>(block.buffer), std::streamsize(block.size));
      }
    }
  }
  std::filesystem::rename(tmp, dir / "achievements.toml", ec);
  REXLOG_INFO("Wrote the achievement list and icons for the launcher to {}", dir.string());
}

}  // namespace bk
