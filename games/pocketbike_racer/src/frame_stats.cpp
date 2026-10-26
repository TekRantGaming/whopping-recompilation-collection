// Frame rate and frame counter.
//
// Frame counter: counts the game's own frames (sub_82219080 is the one function that calls VdSwap), for the SDK's F3
// overlay ("Guest: N FPS") and, with bk_log_fps on, a line in the log each second (frames, average and slowest frame).
//
// Frame rate (bk_frame_rate, default 60): 30 is the original (the game paces itself, nothing here changes). Any other value
// turns the guest vertical blank interrupt into a fast timer (guest vsync off) and paces each frame in the swap hook
// with a high resolution waitable timer and a short spin; 0 means no cap.
// The game paces itself only by the guest vertical blank (two per frame, so 30); its race runs on real time
// (opponents covered the same ground at 30 and about 100 frames a second), so the guest vblank is sped up and this
// paces instead.

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <thread>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
  if (REXCVAR_GET(bk_log_fps))
    REXLOG_INFO("BK fps: {:.1f} ({} frames, {:.2f} ms average, slowest {:.2f} ms)", g_fps.load(), w.frames,
                g_frame_ms.load(), w.slowest_ms);
  w = Window();
  w.start = w.last = now;
}

constexpr uint32_t kEngineTimer = 0;  // guest address of the engine frame timer's pointer (0: none)

bool Unlocked() {
  const int cap = REXCVAR_GET(bk_frame_rate);
  return cap != 30;
}

// Waits until this frame's turn at the chosen rate.
void Pace() {
  const int cap = REXCVAR_GET(bk_frame_rate);
  if (cap == 30 || cap <= 0) return;
#if defined(_WIN32)
  static HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
#endif
  static Clock::time_point next = Clock::now();
  const Clock::duration period = std::chrono::duration_cast<Clock::duration>(std::chrono::nanoseconds(1000000000LL / cap));
  next += period;
  Clock::time_point now = Clock::now();
  if (next < now - period) next = now;  // fell behind (loading): start again from here
  const auto sleep = next - now - std::chrono::milliseconds(1);
#if defined(_WIN32)
  if (timer && sleep > Clock::duration::zero()) {
    LARGE_INTEGER due;
    due.QuadPart = -std::max<LONGLONG>(1, std::chrono::duration_cast<std::chrono::nanoseconds>(sleep).count() / 100);
    if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(timer, INFINITE);
  }
  while (Clock::now() < next) YieldProcessor();
#else
  if (sleep > Clock::duration::zero()) std::this_thread::sleep_for(sleep);  // Linux sleeps to the microsecond
  while (Clock::now() < next) __builtin_ia32_pause();
#endif
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

REX_EXTERN(__imp__sub_82219080);
REX_HOOK_RAW(sub_82219080) {
  bk::Pace();
  __imp__sub_82219080(ctx, base);
  bk::CountFrame();
}

