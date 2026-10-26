// Videos showed black (the same bug as Sneak King, same Blitz engine).
//
// sub_8248EDE0 is the game's 4x4 matrix multiply (r3 = destination, r4 and r5 =
// sources, VMX128 dot products); instruction for instruction the same as
// Sneak King's sub_8245EBC8. The camera projection it produces is copied into
// vertex shader constants c0 to c3.
//
// The projection uses reversed depth, and videos are drawn as a full screen
// quad exactly on the far plane. With float rounding m22 = f / (f - n) comes
// out one step high, the quad's depth lands just past 1, the PC GPU clips it
// and it fails the game's "greater" depth test, so the video never shows.
//
// Fix: after the multiply, lower m22 by one float step for a perspective
// projection (m23 = 1, m33 = 0, m22 just above 1, m32 negative). That moves the
// far plane out by a fraction of a percent and puts the far plane quad just
// inside it.

#include <cstdint>

#include <rex/hook.h>

namespace bk {
namespace {

constexpr uint32_t kOne = 0x3F800000;
constexpr uint32_t kTwo = 0x40000000;

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

REX_EXTERN(__imp__sub_8248EDE0);
REX_HOOK_RAW(sub_8248EDE0) {
  const uint32_t dest = ctx.r3.u32;
  __imp__sub_8248EDE0(ctx, base);
  const uint32_t m22 = bk::Load32(base, dest + 40);
  const uint32_t m23 = bk::Load32(base, dest + 44);
  const uint32_t m32 = bk::Load32(base, dest + 56);
  const uint32_t m33 = bk::Load32(base, dest + 60);
  if (m23 == bk::kOne && (m33 & 0x7FFFFFFF) == 0 && m22 > bk::kOne && m22 < bk::kTwo && (m32 & 0x80000000)) {
    bk::Store32(base, dest + 40, m22 - 1);
  }
}
