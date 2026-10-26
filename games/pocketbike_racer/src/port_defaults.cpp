// The collection's defaults (they change only the default, so the
// settings file the launcher writes and the command line still win). GPU plugin settings exist only once the plugin is loaded, so this
// runs in OnPostSetup.
//   swap_post_effect = fxaa   anti aliasing on by default
//   render_target_path_vulkan = fbo   Linux: host render targets (faster on handheld graphics)
//   readback_resolve = fast   the washed out tracks (see ApplyPortDefaults)
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

void ApplyPortDefaults() {
  SetCvarDefault("swap_post_effect", "fxaa");
#if defined(__linux__)
  // Linux (Vulkan): host render targets; the default, fragment shader interlock, ran Sneak King at about 19 frames a
  // second on the ROG Ally against 30 (26 Oct 2026).
  SetCvarDefault("render_target_path_vulkan", "fbo");
#endif
  // The game measures the scene's brightness on the CPU from a resolved render target; without the resolve read
  // back to guest memory it reads nothing and turns the exposure all the way up (the fantasy track and others were
  // washed out white; pausing, which stops the measurement, looked right). "fast" reads the previous frame's
  // resolve and kept 60 frames a second; "full" stalls the GPU (11).
  SetCvarDefault("readback_resolve", "fast");
}

}  // namespace bk
