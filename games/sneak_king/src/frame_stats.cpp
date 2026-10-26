// Frame rate and frame counter.
//
// Frame counter: counts the game's own frames (sub_820B7C40 is the one function that calls VdSwap), for the SDK's F3
// overlay ("Guest: N FPS") and, with bk_log_fps on, a line in the log each second (frames, average and slowest frame).
//
// Frame rate (bk_frame_rate, default 60, 0 for no cap): the engine's own cap is replaced by pacing in the swap
// hook (a high resolution waitable timer and a short spin) at every setting, 30 included, and the guest vertical blank
// becomes a fast timer (guest vsync off) so nothing else holds a frame back.
// Why at 30 too: the engine's frame timer (sub_82421CA0, object pointer at 0x82649D0C) caps the game by spinning until the
// frame rate drops to an entry of its rate table (30, 20, 15, 12...: 60 / n) when the flag at +72 is set, choosing the first entry below the last
// frame's rate. On PC a frame that runs a little late picks 20, the next one 15 and so on: Big Bumpin's attract demo
// ran at 10 to 11 frames a second in the release build (25 Oct 2026). Its time step is the real frame time either way
// (+88 is 0), so the game runs at the same speed whatever the rate (Sneak King: the running time advanced exactly 60
// per second at 30 and at 60).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ui/overlay/debug_overlay.h>

REXCVAR_DEFINE_BOOL(bk_log_fps, false, "Collection", "Log the game's frame rate once a second");
REXCVAR_DEFINE_INT32(bk_frame_rate, 60, "Collection", "Frame rate cap (30 is the original, 0 is no cap)");

namespace bk {
namespace {

using Clock = std::chrono::steady_clock;

struct Window {
  Clock::time_point start = Clock::now(), last = start;
  uint32_t frames = 0;
  double slowest_ms = 0.0;
};
Window g_window;  // only the game's render thread presents
std::atomic<uint64_t> g_total{0};
std::atomic<double> g_fps{0.0}, g_frame_ms{0.0};

void CountFrame() {
  const Clock::time_point now = Clock::now();
  Window& w = g_window;
  w.slowest_ms = std::max(w.slowest_ms, std::chrono::duration<double, std::milli>(now - w.last).count());
  w.last = now;
  ++w.frames;
  ++g_total;
  const double secs = std::chrono::duration<double>(now - w.start).count();
  if (secs < 1.0) return;
  g_fps = w.frames / secs;
  g_frame_ms = secs * 1000.0 / w.frames;
  if (REXCVAR_GET(bk_log_fps)) {
    REXLOG_INFO("BK fps: {:.1f} ({} frames, {:.2f} ms average, slowest {:.2f} ms)", g_fps.load(), w.frames,
                g_frame_ms.load(), w.slowest_ms);
  }
  w = Window();
  w.start = w.last = now;
}

constexpr uint32_t kEngineTimer = 0x82649D0C;  // guest address of the engine frame timer's pointer (0: none)

bool Unlocked() { return true; }  // see the top of the file: the engine's cap is never used

// Waits until this frame's turn at the chosen rate.
void Pace() {
  const int cap = REXCVAR_GET(bk_frame_rate);
  if (cap <= 0) return;
  static HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  static Clock::time_point next = Clock::now();
  const Clock::duration period = std::chrono::duration_cast<Clock::duration>(std::chrono::nanoseconds(1000000000LL / cap));
  next += period;
  Clock::time_point now = Clock::now();
  if (next < now - period) next = now;  // fell behind (loading): start again from here
  const auto sleep = next - now - std::chrono::milliseconds(1);
  if (timer && sleep > Clock::duration::zero()) {
    LARGE_INTEGER due;
    due.QuadPart = -std::max<LONGLONG>(1, std::chrono::duration_cast<std::chrono::nanoseconds>(sleep).count() / 100);
    if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(timer, INFINITE);
  }
  while (Clock::now() < next) YieldProcessor();
}

}  // namespace

rex::ui::FrameStats GuestFrameStats() {
  rex::ui::FrameStats s;
  s.fps = g_fps;
  s.frame_time_ms = g_frame_ms;
  s.frame_count = g_total;
  return s;
}

void ApplyFrameRate() {
  if (!Unlocked()) return;
  rex::cvar::SetFlagByName("vsync", "false");  // the guest vblank becomes a 1 ms tick: the game no longer waits on it
  // Submitting at the end of every primary buffer stalls the frame (King Kong turned it off for high frame rates too).
  rex::cvar::SetFlagByName("d3d12_submit_on_primary_buffer_end", "false");
  rex::cvar::SetFlagByName("vulkan_submit_on_primary_buffer_end", "false");
}

}  // namespace bk

REX_EXTERN(__imp__sub_820B7C40);
REX_HOOK_RAW(sub_820B7C40) {
  bk::Pace();
  __imp__sub_820B7C40(ctx, base);
  bk::CountFrame();
}

// The engine timer's own cap is turned off while another rate is chosen.
REX_EXTERN(__imp__sub_82421CA0);
REX_HOOK_RAW(sub_82421CA0) {
  if (bk::Unlocked()) {
    const uint32_t ptr = bk::kEngineTimer;
    uint32_t obj = uint32_t(base[ptr]) << 24 | uint32_t(base[ptr + 1]) << 16 | uint32_t(base[ptr + 2]) << 8 | base[ptr + 3];
    if (obj) {
      const uint32_t flag = obj + 72 + (obj >= 0xE0000000u ? 0x1000u : 0u);  // physical heap host offset
      base[flag] = base[flag + 1] = base[flag + 2] = base[flag + 3] = 0;
    }
  }
  __imp__sub_82421CA0(ctx, base);
}

