// Keyboard and mouse: the mouse turns the camera directly (as King Kong Recompiled does), instead of the runtime
// turning mouse movement into a right stick push.
//
// The game's right stick goes through a 30% deadzone (sub_82462628), an action that builds its value up a little
// each frame (sub_82464C88) and a stick filter that smooths it (sub_8242CE20), so small mouse moves did nothing and
// the camera lagged. The camera itself is simple: its update (sub_8253AD80) asks for the right stick's actions 2
// and 3 (sub_824FEBF8, called from 0x8253B100 and 0x8253B124), and when either is pushed it calls sub_8253A090,
// which turns the camera's yaw (+304, radians, pi a second at a full push) and pitch (+308, degrees, 180 a
// second) by stick x frame time x speed; the update then uses those angles as they are.
//
// So the window's mouse movement is added up here, the runtime's own mouse to stick part is kept at its lowest
// (mnk_sensitivity 0.01), and each camera update gets the angle the mouse moved: a fixed angle per count, at any
// speed and frame rate, with no deadzone, build up or smoothing. While the mouse moves, the camera's "is the stick
// pushed" check is nudged so it takes its stick path, and the turn is added after sub_8253A090's own (so a pushed
// controller stick still works too). Movement that piles up while the camera isn't reading (menus, cutscenes) is
// dropped, not turned all at once.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string_view>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

REXCVAR_DEFINE_DOUBLE(bk_mouse_sensitivity, 1.0, "Collection", "Keyboard and mouse: how fast the mouse turns the camera");
REXCVAR_DEFINE_BOOL(bk_mouse_invert_y, false, "Collection", "Keyboard and mouse: mouse up looks down");

namespace bk {
void SetCvarDefault(std::string_view name, std::string_view value);  // port_defaults.cpp

namespace {

using Clock = std::chrono::steady_clock;

// Degrees per count at sensitivity 1 (King Kong's: the Source engine's yaw at sensitivity 3, about 34 cm of a
// 1600 dpi mouse for a full turn).
constexpr double kDegreesPerCount = 0.066;
constexpr double kPi = 3.14159265358979;
constexpr uint32_t kStickCheckX = 0x8253B104;  // return addresses of the camera's "is the stick pushed" reads
constexpr uint32_t kStickCheckY = 0x8253B128;

rex::ui::Window* g_window = nullptr;
std::atomic<bool> g_on{false};  // mouse look, as of the last camera update

std::mutex g_mutex;
double g_dx = 0, g_dy = 0;  // counts since the camera last read them

// This camera update (game thread): the turn still to give, yaw in radians and pitch in degrees.
double g_yaw = 0, g_pitch = 0;
Clock::time_point g_last_read{};

bool MouseCamera() {
  return rex::cvar::GetFlagByName("mnk_mode") == "true" && rex::cvar::GetFlagByName("mnk_mouse") == "true";
}

// The window reports movement in pixels (counts times the display's pixel density); a sensitivity means counts.
double Density() {
  if (!g_window || !g_window->GetMediumDpi()) return 1.0;
  const double d = double(g_window->GetDpi()) / g_window->GetMediumDpi();
  return d > 0.25 ? d : 1.0;
}

class Listener : public rex::ui::WindowInputListener {
 public:
  void OnMouseMove(rex::ui::MouseEvent& e) override {
    // Only reported while the pointer is locked (mouse look).
    if (!g_on.load(std::memory_order_relaxed) || (e.dx() == 0.0f && e.dy() == 0.0f)) return;
    std::lock_guard lock(g_mutex);
    g_dx += e.dx();
    g_dy += e.dy();
  }
};
Listener g_listener;

// Once a camera update: takes the movement since the last one and works out this update's turn.
void ReadMouse() {
  const bool on = MouseCamera();
  g_on.store(on, std::memory_order_relaxed);
  const auto now = Clock::now();
  const double since = std::chrono::duration<double>(now - g_last_read).count();
  g_last_read = now;
  double dx, dy;
  {
    std::lock_guard lock(g_mutex);
    dx = g_dx;
    dy = g_dy;
    g_dx = g_dy = 0;
  }
  g_yaw = g_pitch = 0;
  if (!on || since > 0.25) return;  // stale: the camera wasn't reading
  const double degrees = kDegreesPerCount * std::clamp(REXCVAR_GET(bk_mouse_sensitivity), 0.05, 10.0) / Density();
  // Right turns like the stick pushed right (yaw down); up looks up (the camera lowers, pitch down).
  g_yaw = -dx * degrees * kPi / 180.0;
  g_pitch = dy * degrees * (REXCVAR_GET(bk_mouse_invert_y) ? -1.0 : 1.0);
}

}  // namespace

// The window's mouse movement, and the runtime's own mouse to stick part kept at its lowest (as the setting writes
// it, so it is not saved).
void InstallMouseLook(rex::ui::Window* window) {
  if (!window || g_window) return;
  g_window = window;
  window->AddInputListener(&g_listener, 1000);  // ahead of the runtime's, which still gets every event
  SetCvarDefault("mnk_sensitivity", "0.010000");
  rex::cvar::SetFlagByName("mnk_sensitivity", "0.010000");
}

}  // namespace bk

// The camera's right stick reads: while the mouse moves, a still stick reads as just pushed, so the camera takes
// its stick path this update.
REX_EXTERN(__imp__sub_824FEBF8);
REX_HOOK_RAW(sub_824FEBF8) {
  const uint32_t lr = uint32_t(ctx.lr);
  if (lr == bk::kStickCheckX) bk::ReadMouse();
  __imp__sub_824FEBF8(ctx, base);
  if ((lr == bk::kStickCheckX || lr == bk::kStickCheckY) && ctx.f1.f64 == 0.0 && (bk::g_yaw != 0 || bk::g_pitch != 0))
    ctx.f1.f64 = 0.001;
}

// The camera's stick turn (r3 the camera), then the mouse's.
REX_EXTERN(__imp__sub_8253A090);
REX_HOOK_RAW(sub_8253A090) {
  const uint32_t cam = ctx.r3.u32;
  __imp__sub_8253A090(ctx, base);
  if (bk::g_yaw == 0 && bk::g_pitch == 0) return;
  auto turn = [&](uint32_t field, double by) {
    const uint32_t a = cam + field;
    uint32_t* p = reinterpret_cast<uint32_t*>(base + a + (a >= 0xE0000000u ? 0x1000u : 0u));  // the runtime's physical view offset
    uint32_t bits = __builtin_bswap32(*p);
    float v;
    std::memcpy(&v, &bits, 4);
    v = float(v + by);
    std::memcpy(&bits, &v, 4);
    *p = __builtin_bswap32(bits);
  };
  turn(304, bk::g_yaw);
  turn(308, bk::g_pitch);
  bk::g_yaw = bk::g_pitch = 0;
}
