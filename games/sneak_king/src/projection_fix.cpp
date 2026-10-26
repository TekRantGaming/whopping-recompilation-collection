// Title, menu and cutscene videos showed black.
//
// sub_8245EBC8 is the game's 4x4 matrix multiply (r3 = destination, r4 and r5 =
// sources, VMX128 dot products). Its result at 0x82643B90 is the main camera
// projection, which the game copies into vertex shader constants c0 to c3.
// Found with a write watch on that matrix .
//
// The projection uses reversed depth with near 15 and far 500000, so
// m22 = f / (f - n) = 1.0000300009, stored as the float 1.0000300407
// (0x3F8000FC). The video is drawn as a full screen quad at z = 500000, exactly
// on the far plane, and with that rounding its depth lands at 1.00000004: the
// PC GPU clips the quad away and it also fails the game's "greater" depth test,
// so nothing of the video reaches the screen. The console evidently ends up
// just inside the far plane.
//
// Fix: after the multiply writes the projection, lower m22 by one float step
// (0x3F8000FB) for a perspective matrix (m23 = 1, m33 = 0). That moves the far
// plane out by about 0.4 percent, which nothing on screen shows, and puts the
// far plane quad just inside it.

#include <cstdint>
#include <cstring>

#include <rex/hook.h>

namespace bk {
namespace {

constexpr uint32_t kProjectionMatrix = 0x82643B90;
constexpr uint32_t kOne = 0x3F800000;

uint32_t Load32(const uint8_t* base, uint32_t addr) {
  const uint8_t* p = base + addr;
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

void Store32(uint8_t* base, uint32_t addr, uint32_t value) {
  uint8_t* p = base + addr;
  p[0] = uint8_t(value >> 24);
  p[1] = uint8_t(value >> 16);
  p[2] = uint8_t(value >> 8);
  p[3] = uint8_t(value);
}

}  // namespace
}  // namespace bk

REX_EXTERN(__imp__sub_8245EBC8);
REX_HOOK_RAW(sub_8245EBC8) {
  const uint32_t dest = ctx.r3.u32;
  __imp__sub_8245EBC8(ctx, base);
  if (dest != bk::kProjectionMatrix) return;
  const uint32_t m22 = bk::Load32(base, dest + 40);
  const uint32_t m23 = bk::Load32(base, dest + 44);
  const uint32_t m33 = bk::Load32(base, dest + 60);
  // Positive m22 just above 1 (a perspective depth term), w = z, no w offset.
  if (m23 == bk::kOne && (m33 & 0x7FFFFFFF) == 0 && m22 > bk::kOne && m22 < 0x40000000) {
    bk::Store32(base, dest + 40, m22 - 1);
  }
}
