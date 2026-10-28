// Controller options set from The Whopping Recompilation Collection launcher
// (invert, sensitivity and button remap, the same in all three games).
//
// sub_820D3200 is the title's XInputGetState (r3 user, r4 XINPUT_STATE) and sub_820D3210 its
// XInputSetState (r3 user, r4 XINPUT_VIBRATION); both forward to the Xam
// imports. sub_820D31F8 (XInputGetCapabilities) only matters for the scripted dev pad. After the game's own read, the state is changed here:
//   bk_remap_<button>    the button (or trigger) a physical button acts as
//   bk_deadzone          percent of each stick's travel that is ignored
//   bk_invert_x / _y     right stick (camera) left/right and up/down turned round
//   bk_stick_sensitivity right stick (camera) speed in percent
//   bk_vibration         rumble strength in percent (the game's own values are
//                        put back after the call)
// With every option at its default the state is left exactly as the game read it.
//
// XINPUT_STATE (big endian): +0 packet number, +4 buttons, +6 left trigger,
// +7 right trigger, +8/+10 left stick x/y, +12/+14 right stick x/y.
//
// Every Xam input call goes through one lock (XamInputLock). The runtime's InputSystem rebuilds its device list on
// each call without a lock of its own, so two threads calling at once free it twice: Big Bumpin's matches crashed
// that way within seconds. The same lock keeps this game safe if it ever reads the pad from two threads.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(bk_invert_x, false, "Collection", "Turn the right stick round left and right");
REXCVAR_DEFINE_BOOL(bk_invert_y, false, "Collection", "Turn the right stick round up and down");
REXCVAR_DEFINE_INT32(bk_stick_sensitivity, 100, "Collection", "Right stick speed in percent");
REXCVAR_DEFINE_INT32(bk_deadzone, 0, "Collection", "Stick deadzone in percent");
REXCVAR_DEFINE_INT32(bk_vibration, 100, "Collection", "Vibration strength in percent");
REXCVAR_DEFINE_STRING(bk_remap_a, "a", "Collection", "A button acts as");
REXCVAR_DEFINE_STRING(bk_remap_b, "b", "Collection", "B button acts as");
REXCVAR_DEFINE_STRING(bk_remap_x, "x", "Collection", "X button acts as");
REXCVAR_DEFINE_STRING(bk_remap_y, "y", "Collection", "Y button acts as");
REXCVAR_DEFINE_STRING(bk_remap_lb, "lb", "Collection", "Left bumper acts as");
REXCVAR_DEFINE_STRING(bk_remap_rb, "rb", "Collection", "Right bumper acts as");
REXCVAR_DEFINE_STRING(bk_remap_lt, "lt", "Collection", "Left trigger acts as");
REXCVAR_DEFINE_STRING(bk_remap_rt, "rt", "Collection", "Right trigger acts as");
REXCVAR_DEFINE_STRING(bk_remap_start, "start", "Collection", "Start acts as");
REXCVAR_DEFINE_STRING(bk_remap_back, "back", "Collection", "Back acts as");
REXCVAR_DEFINE_STRING(bk_remap_ls, "ls", "Collection", "Left stick click acts as");
REXCVAR_DEFINE_STRING(bk_remap_rs, "rs", "Collection", "Right stick click acts as");

namespace bk {

namespace {

std::mutex xam_input_mutex;
using XamInputLock = std::lock_guard<std::mutex>;

uint16_t Load16(const uint8_t* base, uint32_t a) { return uint16_t(base[a] << 8 | base[a + 1]); }
void Store16(uint8_t* base, uint32_t a, uint16_t v) {
  base[a] = uint8_t(v >> 8);
  base[a + 1] = uint8_t(v);
}

struct Button {
  const char* name;
  uint16_t mask;  // 0 for the triggers
  int trigger;    // -1, or 0 left / 1 right
};
const Button kButtons[] = {
    {"a", 0x1000, -1},     {"b", 0x2000, -1},    {"x", 0x4000, -1},  {"y", 0x8000, -1},
    {"lb", 0x0100, -1},    {"rb", 0x0200, -1},   {"lt", 0, 0},       {"rt", 0, 1},
    {"start", 0x0010, -1}, {"back", 0x0020, -1}, {"ls", 0x0040, -1}, {"rs", 0x0080, -1},
};

const std::string& Remap(int i) {
  switch (i) {
    case 0: return REXCVAR_GET(bk_remap_a);
    case 1: return REXCVAR_GET(bk_remap_b);
    case 2: return REXCVAR_GET(bk_remap_x);
    case 3: return REXCVAR_GET(bk_remap_y);
    case 4: return REXCVAR_GET(bk_remap_lb);
    case 5: return REXCVAR_GET(bk_remap_rb);
    case 6: return REXCVAR_GET(bk_remap_lt);
    case 7: return REXCVAR_GET(bk_remap_rt);
    case 8: return REXCVAR_GET(bk_remap_start);
    case 9: return REXCVAR_GET(bk_remap_back);
    case 10: return REXCVAR_GET(bk_remap_ls);
    default: return REXCVAR_GET(bk_remap_rs);
  }
}

void ApplyRemap(uint8_t* base, uint32_t state) {
  bool identity = true;
  for (int i = 0; i < 12; ++i) identity = identity && Remap(i) == kButtons[i].name;
  if (identity) return;
  const uint16_t in_buttons = Load16(base, state + 4);
  const uint8_t in_trig[2] = {base[state + 6], base[state + 7]};
  uint16_t out_buttons = in_buttons & 0x000F;  // the D pad is not remapped
  uint8_t out_trig[2] = {0, 0};
  for (int i = 0; i < 12; ++i) {
    const Button& src = kButtons[i];
    const uint8_t amount = src.trigger >= 0 ? in_trig[src.trigger] : ((in_buttons & src.mask) ? 255 : 0);
    if (!amount) continue;
    for (const Button& dst : kButtons) {
      if (Remap(i) != dst.name) continue;
      if (dst.trigger >= 0) {
        out_trig[dst.trigger] = std::max(out_trig[dst.trigger], amount);
      } else if (amount > 30) {  // XINPUT_GAMEPAD_TRIGGER_THRESHOLD
        out_buttons |= dst.mask;
      }
    }
  }
  Store16(base, state + 4, out_buttons);
  base[state + 6] = out_trig[0];
  base[state + 7] = out_trig[1];
}

void ApplyDeadzone(uint8_t* base, uint32_t x_addr, int percent) {
  if (percent <= 0) return;
  const float x = int16_t(Load16(base, x_addr)), y = int16_t(Load16(base, x_addr + 2));
  const float mag = std::sqrt(x * x + y * y) / 32767.0f, dz = std::min(percent, 90) / 100.0f;
  float scale = 0.0f;
  if (mag > dz) scale = std::min(1.0f, (mag - dz) / (1.0f - dz)) / mag;
  Store16(base, x_addr, uint16_t(int16_t(std::clamp(x * scale, -32768.0f, 32767.0f))));
  Store16(base, x_addr + 2, uint16_t(int16_t(std::clamp(y * scale, -32768.0f, 32767.0f))));
}

void ApplyRightStick(uint8_t* base, uint32_t state) {
  const int sens = REXCVAR_GET(bk_stick_sensitivity);
  const bool ix = REXCVAR_GET(bk_invert_x), iy = REXCVAR_GET(bk_invert_y);
  if (sens == 100 && !ix && !iy) return;
  for (int axis = 0; axis < 2; ++axis) {
    const uint32_t a = state + 12 + axis * 2;
    float v = int16_t(Load16(base, a)) * (sens / 100.0f);
    if ((axis == 0 && ix) || (axis == 1 && iy)) v = -v;
    Store16(base, a, uint16_t(int16_t(std::clamp(v, -32767.0f, 32767.0f))));
  }
}

}  // namespace
}  // namespace bk

namespace bk {
void ReturnToLauncherTick(uint32_t user, bool connected, uint16_t buttons);
void GameWindowTick(bool pad_used);
bool PadChanged(uint32_t user, const uint8_t* state16);
}  // namespace bk

REX_EXTERN(__imp__sub_820D31F8);
REX_HOOK_RAW(sub_820D31F8) {
  bk::XamInputLock lock(bk::xam_input_mutex);
  __imp__sub_820D31F8(ctx, base);
}
REX_EXTERN(__imp__sub_820D3200);
REX_HOOK_RAW(sub_820D3200) {
  const uint32_t state = ctx.r4.u32;
  const uint32_t user = ctx.r3.u32;
  {
    bk::XamInputLock lock(bk::xam_input_mutex);
    __imp__sub_820D3200(ctx, base);
  }
  const bool connected = ctx.r3.u32 == 0 && state;
  bk::ReturnToLauncherTick(user, connected, connected ? bk::Load16(base, state + 4) : 0);  // return_to_launcher.cpp
  bk::GameWindowTick(connected && bk::PadChanged(user, base + state));  // game_window.cpp: front, cursor
  if (!connected) return;  // no pad in this slot
  bk::ApplyRemap(base, state);
  bk::ApplyDeadzone(base, state + 8, REXCVAR_GET(bk_deadzone));
  bk::ApplyDeadzone(base, state + 12, REXCVAR_GET(bk_deadzone));
  bk::ApplyRightStick(base, state);
}

REX_EXTERN(__imp__sub_820D3210);
REX_HOOK_RAW(sub_820D3210) {
  const uint32_t vib = ctx.r4.u32;
  const int percent = REXCVAR_GET(bk_vibration);
  bk::XamInputLock lock(bk::xam_input_mutex);
  if (!vib || percent == 100) {
    __imp__sub_820D3210(ctx, base);
    return;
  }
  const uint16_t left = bk::Load16(base, vib), right = bk::Load16(base, vib + 2);
  const float k = std::clamp(percent, 0, 100) / 100.0f;
  bk::Store16(base, vib, uint16_t(left * k));
  bk::Store16(base, vib + 2, uint16_t(right * k));
  __imp__sub_820D3210(ctx, base);
  bk::Store16(base, vib, left);  // the game's own values back
  bk::Store16(base, vib + 2, right);
}
