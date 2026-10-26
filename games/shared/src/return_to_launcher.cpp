// Back to the collection launcher: holding Back and Start
// together on the controller for a second, or holding Esc for a second and a
// half, closes the game; the launcher comes back when the game exits. Only
// when the launcher started the game (it passes --bk_from_launcher=true).
// Called from the XInputGetState hook in input_options.cpp, which the game
// polls many times a second. On Linux the game's window is SDL's: Esc comes from SDL's keyboard state and the
// game is closed with an SDL quit event.

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <SDL3/SDL.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(bk_from_launcher, false, "Collection",
                    "Started by the collection launcher: hold Back and Start, or Esc, to return to it");

namespace bk {
namespace {

using Clock = std::chrono::steady_clock;

#if defined(_WIN32)
BOOL CALLBACK CloseOwnWindow(HWND hwnd, LPARAM) {
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd)) PostMessageW(hwnd, WM_CLOSE, 0, 0);
  return TRUE;
}

bool OwnWindowFocused() {
  DWORD pid = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &pid);
  return pid == GetCurrentProcessId();
}

bool EscHeld() { return (GetAsyncKeyState(VK_ESCAPE) & 0x8000) && OwnWindowFocused(); }
void CloseGame() { EnumWindows(&CloseOwnWindow, 0); }
#else
bool EscHeld() {
  const bool* keys = SDL_GetKeyboardState(nullptr);
  return keys && keys[SDL_SCANCODE_ESCAPE] && SDL_GetKeyboardFocus();
}
void CloseGame() {
  SDL_Event e{};
  e.type = SDL_EVENT_QUIT;
  SDL_PushEvent(&e);
}
#endif

// How long a condition has held, or zero when it is not held.
struct Held {
  bool down = false;
  Clock::time_point since;
  double Seconds(bool now_down) {
    const Clock::time_point now = Clock::now();
    if (now_down && !down) since = now;
    down = now_down;
    return down ? std::chrono::duration<double>(now - since).count() : 0.0;
  }
};

}  // namespace

// `user` is the controller slot polled; `buttons` its physical buttons (before remapping), valid when `connected`.
void ReturnToLauncherTick(uint32_t user, bool connected, uint16_t buttons) {
  if (!REXCVAR_GET(bk_from_launcher)) return;
  static std::atomic<bool> closing{false};
  static Held pad[4], key;
  if (closing) return;
  bool go = false;
  if (user < 4) {
    constexpr uint16_t kStartBack = 0x0010 | 0x0020;  // XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_BACK
    go = pad[user].Seconds(connected && (buttons & kStartBack) == kStartBack) >= 1.0;
  }
  if (user == 0) go = go || key.Seconds(EscHeld()) >= 1.5;
  if (!go || closing.exchange(true)) return;
  REXLOG_INFO("BK: back to the launcher");
  CloseGame();
}

}  // namespace bk
