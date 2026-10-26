// Render resolution from the upscaler mode, and the guest video mode fix, as in
// King Kong Recompiled 1.9.x (settings.cpp ApplyRenderPreset and
// ApplyRuntimeOverrides).
//
// bk_render_quality: custom (resolution_scale as set, the default), native,
// quality, balanced or performance. A mode picks the whole number render
// scale (720p steps) from the output height: scale = round(height / ratio /
// 720), Performance one step below Quality where there is one. The scale is
// set as the DEFAULT of draw_resolution_scale_x/_y, so it is never saved, and
// a leftover resolution_scale is cleared. Runs in OnFinalizePaths: after the
// GPU plugin and the window exist, before the GPU starts.
//
// Video mode: the runtime reports window_width/height as the guest video mode
// whenever a window size is set (xboxkrnl_video.cpp), and the window size is in
// DPI scaled logical units, so a window under 720 lines made King Kong draw at
// 640 x 480. The guest is always told 1280 x 720 here.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/window.h>

REXCVAR_DEFINE_STRING(bk_render_quality, "custom", "Collection",
                      "Render resolution from the upscaler mode: custom, native, quality, balanced, performance");

namespace bk {

void SetCvarDefault(std::string_view name, std::string_view value);  // port_defaults.cpp

namespace {

double Ratio(std::string_view mode) {  // output height / render height, the usual upscaler names
  if (mode == "native") return 1.0;
  if (mode == "quality") return 1.5;
  if (mode == "balanced") return 1.7;
  if (mode == "performance") return 2.0;
  return 0.0;
}

int ScaleFor(std::string_view mode, int output_height) {
  const double ratio = Ratio(mode);
  if (ratio <= 0.0) return 0;
  const int scale = std::clamp(int(std::lround(std::max(1, output_height) / ratio / 720.0)), 1, 8);
  if (mode == "performance") return std::max(1, std::min(scale, ScaleFor("quality", output_height) - 1));
  return scale;
}

}  // namespace

void ApplyRenderQuality(rex::ui::Window* window) {
  // The guest video mode: always 1280 x 720. The default becomes 0 so that 1280 counts as set and wins over a
  // window size (HasNonDefaultValue compares with the default).
  for (const char* name : {"video_mode_width", "video_mode_height"}) SetCvarDefault(name, "0");
  rex::cvar::SetFlagByName("video_mode_width", "1280");
  rex::cvar::SetFlagByName("video_mode_height", "720");

  const std::string mode = REXCVAR_GET(bk_render_quality);
  // Output height in physical pixels: the screen in fullscreen, else the window.
  // The window is not sized yet here, so windowed uses the window height setting (logical units, times the
  // Windows display scale), 720 when it is automatic.
  int height = GetSystemMetrics(SM_CYSCREEN);
  if (!REXCVAR_QUERY(bool, fullscreen)) {
    const int logical = REXCVAR_QUERY(int32_t, window_height) > 0 ? REXCVAR_QUERY(int32_t, window_height) : 720;
    height = int(std::lround(logical * GetDpiForSystem() / 96.0));
  }
  (void)window;
  const int scale = ScaleFor(mode, height);
  if (scale <= 0) return;  // custom: the player's resolution_scale is used as is
  rex::cvar::ResetToDefault("resolution_scale");
  for (const char* axis : {"draw_resolution_scale_x", "draw_resolution_scale_y"}) {
    SetCvarDefault(axis, std::to_string(scale));
    rex::cvar::ResetToDefault(axis);
  }
  REXLOG_INFO("BK: render quality {} at {}p output: {}x ({}p)", mode, height, scale, scale * 720);
}

}  // namespace bk
