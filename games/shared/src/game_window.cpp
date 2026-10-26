// The game's window:
//  * started by the collection launcher, it comes to the front over anything open (the launcher allows it with
//    AllowSetForegroundWindow before it steps aside);
//  * the mouse cursor hides while the controller is used and comes back when the mouse moves.
// The window is SDL's; its window procedure is wrapped to answer WM_SETCURSOR while the cursor is hidden.
// Driven from the XInputGetState hook in input_options.cpp, which the game polls many times a second.
// BK_TEST_BACKGROUND=1 (test runs) leaves the window where it opens.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <rex/cvar.h>

REXCVAR_DECLARE(bool, bk_from_launcher);  // return_to_launcher.cpp

namespace bk {
namespace {

std::atomic<HWND> g_window{nullptr};
WNDPROC g_old_proc = nullptr;
std::atomic<bool> g_hidden{false};
POINT g_last_mouse{-100000, -100000};

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_SETCURSOR && LOWORD(lp) == HTCLIENT && g_hidden) {
    SetCursor(nullptr);
    return TRUE;
  }
  if (msg == WM_MOUSEMOVE) {
    const POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    if (std::abs(p.x - g_last_mouse.x) + std::abs(p.y - g_last_mouse.y) > 2 && g_last_mouse.x > -100000)
      g_hidden = false;  // the mouse is used again: Windows shows the cursor at the next WM_SETCURSOR
    g_last_mouse = p;
  }
  return CallWindowProcW(g_old_proc, hwnd, msg, wp, lp);
}

BOOL CALLBACK FindOwnWindow(HWND hwnd, LPARAM out) {
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
  *reinterpret_cast<HWND*>(out) = hwnd;
  return FALSE;
}

void Attach() {
  HWND hwnd = nullptr;
  EnumWindows(&FindOwnWindow, LPARAM(&hwnd));
  if (!hwnd) return;
  g_old_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, LONG_PTR(&WindowProc)));
  g_window = hwnd;
  static const bool test_background = std::getenv("BK_TEST_BACKGROUND") != nullptr;
  if (REXCVAR_GET(bk_from_launcher) && !test_background) {
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
  }
}

}  // namespace

// `pad_used`: a connected controller's buttons or sticks changed since the last poll.
void GameWindowTick(bool pad_used) {
  static uint32_t polls = 0;
  if (!g_window) {
    if (++polls % 16 == 0) Attach();  // the window appears a little after the game starts polling
    return;
  }
  if (pad_used && !g_hidden) {
    g_hidden = true;
    POINT p;
    HWND hwnd = g_window;
    if (GetCursorPos(&p) && WindowFromPoint(p) == hwnd)  // refresh the cursor now, if it is over the game
      PostMessageW(hwnd, WM_SETCURSOR, WPARAM(hwnd), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
  }
}

// What the input hook passes: the state it saw last for slot `user`, and whether it changed.
bool PadChanged(uint32_t user, const uint8_t* state16) {
  static uint8_t last[4][12];
  static bool have[4];
  if (user >= 4) return false;
  // Buttons, triggers and both sticks (bytes 4..15 of XINPUT_STATE), ignoring small stick noise.
  bool changed = !have[user] || std::memcmp(last[user], state16 + 4, 4) != 0;
  for (int axis = 0; axis < 4 && !changed; ++axis) {
    const int16_t now = int16_t(state16[8 + axis * 2] << 8 | state16[9 + axis * 2]);
    const int16_t was = int16_t(last[user][4 + axis * 2] << 8 | last[user][5 + axis * 2]);
    changed = std::abs(int(now) - int(was)) > 6000;
  }
  std::memcpy(last[user], state16 + 4, 12);
  const bool first = !have[user];
  have[user] = true;
  return changed && !first;
}

}  // namespace bk
