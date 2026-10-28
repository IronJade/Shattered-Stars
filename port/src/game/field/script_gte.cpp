// The libgte routines the VM calls, as the field program's own copies run them
// (SquareRoot0 0x8006ffc4, ratan2 0x80070a94, TransRot_32 0x80070cf4). The two
// lookup tables are read from the program image; TransRot_32 runs on the GTE.
#include "game/field/script_vm.h"
#include "psx/gte.h"
#include "runtime/libgte.h"

namespace ss::game::field {
namespace {

s16 table_s16(u32 address, s32 index) {
  const u8* p = field_data(address + 2 * static_cast<u32>(index));
  return p != nullptr ? vm_rd16(p) : 0;
}

// The GTE's leading-bit count (LZCS/LZCR): leading zeros of a, or leading ones when
// a is negative.
u32 leading_bits(s32 a) {
  u32 v = static_cast<u32>(a);
  if (a < 0) v = ~v;
  u32 n = 0;
  while (n < 32 && (v & 0x80000000u) == 0) {
    v <<= 1;
    ++n;
  }
  return n;
}

// Splits v into v >> 15 and v & 0x7fff with the sign kept on both halves.
void split15(s32 v, s32* hi, s32* lo) {
  if (v < 0) {
    const u32 m = 0u - static_cast<u32>(v);
    *hi = -static_cast<s32>(m >> 15);
    *lo = -static_cast<s32>(m & 0x7FFF);
  } else {
    *hi = v >> 15;
    *lo = v & 0x7FFF;
  }
}

}  // namespace

s32 SquareRoot0(s32 a) {
  const u32 lz = leading_bits(a);
  if (lz == 32) return 0;
  const u32 even = lz & ~1u;
  const u32 shift = (31 - even) >> 1;
  s32 index;
  if (static_cast<s32>(even) - 24 < 0) {
    index = a >> (24 - even);
  } else {
    index = static_cast<s32>(static_cast<u32>(a) << (even - 24));
  }
  const s32 root = table_s16(0x80077BAC, index - 0x40);
  return static_cast<s32>((static_cast<u32>(root) << shift) >> 12);
}

s32 ratan2(s32 y, s32 x) {
  const bool neg_x = x < 0;
  if (neg_x) x = -x;
  const bool neg_y = y < 0;
  if (neg_y) y = -y;
  if (x == 0 && y == 0) return 0;
  s32 v;
  if (y < x) {
    const s32 idx = (y & 0x7FE00000) != 0 ? y / (x >> 10) : (y << 10) / x;
    v = table_s16(0x8007803C, idx);
  } else {
    const s32 idx = (x & 0x7FE00000) != 0 ? x / (y >> 10) : (x << 10) / y;
    v = 0x400 - table_s16(0x8007803C, idx);
  }
  if (neg_x) v = 0x800 - v;
  if (neg_y) v = -v;
  return v;
}

s32 TransRot_32(const VmVector* v0, VmVector* v1, s32* flag) {
  psx::Gte& gte = psyq::gte();
  const s32 trx = static_cast<s32>(gte.read_ctrl(psx::Gte::TRX));
  const s32 try_ = static_cast<s32>(gte.read_ctrl(psx::Gte::TRY));
  const s32 trz = static_cast<s32>(gte.read_ctrl(psx::Gte::TRZ));
  gte.write_ctrl(psx::Gte::TRX, 0);
  gte.write_ctrl(psx::Gte::TRY, 0);
  gte.write_ctrl(psx::Gte::TRZ, 0);
  s32 hx, lx, hy, ly, hz, lz;
  split15(static_cast<s32>(static_cast<u32>(v0->vx) + static_cast<u32>(trx)), &hx, &lx);
  split15(static_cast<s32>(static_cast<u32>(v0->vy) + static_cast<u32>(try_)), &hy, &ly);
  split15(static_cast<s32>(static_cast<u32>(v0->vz) + static_cast<u32>(trz)), &hz, &lz);
  // High parts: RT * hi without the fraction shift; low parts: RT * lo >> 12.
  gte.write_data(psx::Gte::VXY0, (static_cast<u32>(hx) & 0xFFFF) | static_cast<u32>(hy) << 16);
  gte.write_data(psx::Gte::VZ0, static_cast<u32>(hz));
  gte.execute(0x0406012);  // MVMVA sf=0, RT * V0, no translation
  const s32 mx = static_cast<s32>(gte.read_data(psx::Gte::MAC1));
  const s32 my = static_cast<s32>(gte.read_data(psx::Gte::MAC2));
  const s32 mz = static_cast<s32>(gte.read_data(psx::Gte::MAC3));
  gte.write_data(psx::Gte::VXY0, (static_cast<u32>(lx) & 0xFFFF) | static_cast<u32>(ly) << 16);
  gte.write_data(psx::Gte::VZ0, static_cast<u32>(lz));
  gte.execute(psx::Gte::kRtv0);
  const u32 x = gte.read_data(psx::Gte::MAC1);
  const u32 y = gte.read_data(psx::Gte::MAC2);
  const u32 z = gte.read_data(psx::Gte::MAC3);
  const u32 f = gte.read_ctrl(psx::Gte::FLAG);
  v1->vx = static_cast<s32>(x + (static_cast<u32>(mx) << 3));
  v1->vy = static_cast<s32>(y + (static_cast<u32>(my) << 3));
  v1->vz = static_cast<s32>(z + (static_cast<u32>(mz) << 3));
  gte.write_ctrl(psx::Gte::TRX, static_cast<u32>(trx));
  gte.write_ctrl(psx::Gte::TRY, static_cast<u32>(try_));
  gte.write_ctrl(psx::Gte::TRZ, static_cast<u32>(trz));
  *flag = static_cast<s32>(f);
  return v1->vz >> 2;
}

}  // namespace ss::game::field
