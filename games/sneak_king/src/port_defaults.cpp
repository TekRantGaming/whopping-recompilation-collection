// The collection's defaults (they change only the default, so the
// settings file the launcher writes and the command line still win). GPU plugin settings exist only once the plugin is loaded, so this
// runs in OnPostSetup.
//   swap_post_effect = fxaa   anti aliasing on by default
// Fullscreen stays the SDK's default (on).

#include <string_view>

#include <rex/cvar.h>

namespace bk {

void SetCvarDefault(std::string_view name, std::string_view value) {
  for (auto& e : rex::cvar::GetRegistry()) {
    if (e.name != name) continue;
    e.default_value = value;
    if (e.source == rex::cvar::Source::kDefault) e.setter(value);  // config and command line still win
    return;
  }
}

void ApplyPortDefaults() { SetCvarDefault("swap_post_effect", "fxaa"); }

}  // namespace bk
