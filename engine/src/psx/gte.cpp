#include "psx/gte.h"

#include <algorithm>
#include <bit>

namespace ss::psx {
namespace {

constexpr std::array<s32, 3> kNoTranslation{};
constexpr s64 kMac44Max = (s64{1} << 43) - 1;
constexpr s64 kMac44Min = -(s64{1} << 43);

// MAC1-3 behave as 44-bit accumulators: every partial sum wraps at 44 bits.
constexpr s64 wrap44(s64 v) { return static_cast<s64>(static_cast<u64>(v) << 20) >> 20; }

// The reciprocal seed table of the UNR divide, 257 entries (psx-spx "GTE Division
// Inaccuracy"): unr[i] = max(0, (0x40000 / (i + 0x100) + 1) / 2 - 0x101).
constexpr std::array<u8, 257> kUnrTable = [] {
  std::array<u8, 257> t{};
  for (int i = 0; i < 257; ++i) t[i] = static_cast<u8>(std::max(0, (0x40000 / (i + 0x100) + 1) / 2 - 0x101));
  return t;
}();

u32 pack(s16 lo, s16 hi) { return static_cast<u16>(lo) | static_cast<u32>(static_cast<u16>(hi)) << 16; }
u32 sext(s16 v) { return static_cast<u32>(static_cast<s32>(v)); }

// Matrices are three packed registers of two elements plus a fifth with the last
// element alone, row-major: R11R12, R13R21, R22R23, R31R32, R33.
template <typename M>
u32 read_matrix(const M& m, int r) {
  if (r == 4) return sext(m[2][2]);
  const int k = r * 2;
  return pack(m[k / 3][k % 3], m[(k + 1) / 3][(k + 1) % 3]);
}

template <typename M>
void write_matrix(M& m, int r, u32 v) {
  if (r == 4) {
    m[2][2] = static_cast<s16>(v);
    return;
  }
  const int k = r * 2;
  m[k / 3][k % 3] = static_cast<s16>(v);
  m[(k + 1) / 3][(k + 1) % 3] = static_cast<s16>(v >> 16);
}

}  // namespace

void Gte::reset() { *this = Gte(); }

u32 Gte::read_data(int reg) const {
  reg &= 31;
  switch (reg) {
    case VXY0: case VXY1: case VXY2: return pack(v_[reg >> 1][0], v_[reg >> 1][1]);
    case VZ0: case VZ1: case VZ2: return sext(v_[reg >> 1][2]);
    case RGBC: return rgbc_[0] | rgbc_[1] << 8 | rgbc_[2] << 16 | static_cast<u32>(rgbc_[3]) << 24;
    case OTZ: return otz_;
    case IR0: case IR1: case IR2: case IR3: return sext(ir_[reg - IR0]);
    case SXY0: case SXY1: case SXY2: return pack(sxy_[reg - SXY0].x, sxy_[reg - SXY0].y);
    case SXYP: return pack(sxy_[2].x, sxy_[2].y);  // reads the newest entry
    case SZ0: case SZ1: case SZ2: case SZ3: return sz_[reg - SZ0];
    case RGB0: case RGB1: case RGB2: return rgb_[reg - RGB0];
    case RES1: return res1_;
    case MAC0: case MAC1: case MAC2: case MAC3: return static_cast<u32>(mac_[reg - MAC0]);
    case IRGB:
    case ORGB: {
      // Both read IR1-3 back as a 15-bit colour, each saturated to 0..0x1F.
      auto c = [](s16 v) { return static_cast<u32>(std::clamp(v >> 7, 0, 0x1F)); };
      return c(ir_[1]) | c(ir_[2]) << 5 | c(ir_[3]) << 10;
    }
    case LZCS: return lzcs_;
    default: return lzcr_;
  }
}

void Gte::write_data(int reg, u32 value) {
  const s16 lo = static_cast<s16>(value), hi = static_cast<s16>(value >> 16);
  reg &= 31;
  switch (reg) {
    case VXY0: case VXY1: case VXY2:
      v_[reg >> 1][0] = lo;
      v_[reg >> 1][1] = hi;
      break;
    case VZ0: case VZ1: case VZ2: v_[reg >> 1][2] = lo; break;
    case RGBC:
      for (int i = 0; i < 4; ++i) rgbc_[i] = static_cast<u8>(value >> (8 * i));
      break;
    case OTZ: otz_ = static_cast<u16>(value); break;
    case IR0: case IR1: case IR2: case IR3: ir_[reg - IR0] = lo; break;
    case SXY0: case SXY1: case SXY2: sxy_[reg - SXY0] = {lo, hi}; break;
    case SXYP:  // writing the FIFO's input pushes it
      sxy_[0] = sxy_[1];
      sxy_[1] = sxy_[2];
      sxy_[2] = {lo, hi};
      break;
    case SZ0: case SZ1: case SZ2: case SZ3: sz_[reg - SZ0] = static_cast<u16>(value); break;
    case RGB0: case RGB1: case RGB2: rgb_[reg - RGB0] = value; break;
    case RES1: res1_ = value; break;
    case MAC0: case MAC1: case MAC2: case MAC3: mac_[reg - MAC0] = static_cast<s32>(value); break;
    case IRGB:  // expands a 15-bit colour into IR1-3, 5 bits each scaled by 0x80
      ir_[1] = static_cast<s16>((value & 0x1F) << 7);
      ir_[2] = static_cast<s16>((value >> 5 & 0x1F) << 7);
      ir_[3] = static_cast<s16>((value >> 10 & 0x1F) << 7);
      break;
    case LZCS:
      // LZCR counts the leading bits equal to the sign bit (32 for 0 and -1).
      lzcs_ = value;
      lzcr_ = static_cast<u32>(std::countl_zero(value >> 31 ? ~value : value));
      break;
    default: break;  // ORGB and LZCR are read-only
  }
}

u32 Gte::read_ctrl(int reg) const {
  reg &= 31;
  if (reg <= R33) return read_matrix(rt_, reg);
  if (reg >= L11L12 && reg <= L33) return read_matrix(llm_, reg - L11L12);
  if (reg >= LR1LR2 && reg <= LB3) return read_matrix(lcm_, reg - LR1LR2);
  switch (reg) {
    case TRX: case TRY: case TRZ: return static_cast<u32>(tr_[reg - TRX]);
    case RBK: case GBK: case BBK: return static_cast<u32>(bk_[reg - RBK]);
    case RFC: case GFC: case BFC: return static_cast<u32>(fc_[reg - RFC]);
    case OFX: return static_cast<u32>(ofx_);
    case OFY: return static_cast<u32>(ofy_);
    case H: return sext(static_cast<s16>(h_));  // unsigned, yet read back sign-extended
    case DQA: return sext(dqa_);
    case DQB: return static_cast<u32>(dqb_);
    case ZSF3: return sext(zsf3_);
    case ZSF4: return sext(zsf4_);
    default: return flag_;
  }
}

void Gte::write_ctrl(int reg, u32 value) {
  reg &= 31;
  if (reg <= R33) return write_matrix(rt_, reg, value);
  if (reg >= L11L12 && reg <= L33) return write_matrix(llm_, reg - L11L12, value);
  if (reg >= LR1LR2 && reg <= LB3) return write_matrix(lcm_, reg - LR1LR2, value);
  switch (reg) {
    case TRX: case TRY: case TRZ: tr_[reg - TRX] = static_cast<s32>(value); break;
    case RBK: case GBK: case BBK: bk_[reg - RBK] = static_cast<s32>(value); break;
    case RFC: case GFC: case BFC: fc_[reg - RFC] = static_cast<s32>(value); break;
    case OFX: ofx_ = static_cast<s32>(value); break;
    case OFY: ofy_ = static_cast<s32>(value); break;
    case H: h_ = static_cast<u16>(value); break;
    case DQA: dqa_ = static_cast<s16>(value); break;
    case DQB: dqb_ = static_cast<s32>(value); break;
    case ZSF3: zsf3_ = static_cast<s16>(value); break;
    case ZSF4: zsf4_ = static_cast<s16>(value); break;
    default:  // FLAG: bits 0-11 do not exist and bit 31 is always derived
      flag_ = value & 0x7FFFF000;
      if (flag_ & kFlagErrorMask) flag_ |= kFlagError;
      break;
  }
}

// --- arithmetic steps --------------------------------------------------------

s64 Gte::mac_add(int i, s64 sum) {
  if (sum > kMac44Max) flag_ |= kFlagMac1Pos >> (i - 1);
  else if (sum < kMac44Min) flag_ |= kFlagMac1Neg >> (i - 1);
  return wrap44(sum);
}

void Gte::set_mac(int i, s64 value, int shift) { mac_[i] = static_cast<s32>(mac_add(i, value) >> shift); }

void Gte::set_ir(int i, s32 value, bool lm) {
  const s32 lo = lm ? 0 : -0x8000;
  if (value < lo || value > 0x7FFF) flag_ |= kFlagIr1 >> (i - 1);
  ir_[i] = static_cast<s16>(std::clamp(value, lo, 0x7FFF));
}

void Gte::set_mac_ir(int i, s64 value, int shift, bool lm) {
  set_mac(i, value, shift);
  set_ir(i, mac_[i], lm);
}

void Gte::set_mac0(s64 value) {
  if (value > INT32_MAX) flag_ |= kFlagMac0Pos;
  else if (value < INT32_MIN) flag_ |= kFlagMac0Neg;
  mac_[0] = static_cast<s32>(value);
}

void Gte::push_sz(s64 z) {
  if (z < 0 || z > 0xFFFF) flag_ |= kFlagSz3Otz;
  sz_[0] = sz_[1];
  sz_[1] = sz_[2];
  sz_[2] = sz_[3];
  sz_[3] = static_cast<u16>(std::clamp<s64>(z, 0, 0xFFFF));
}

void Gte::push_sxy(s32 x, s32 y) {
  if (x < -0x400 || x > 0x3FF) flag_ |= kFlagSx2;
  if (y < -0x400 || y > 0x3FF) flag_ |= kFlagSy2;
  sxy_[0] = sxy_[1];
  sxy_[1] = sxy_[2];
  sxy_[2] = {static_cast<s16>(std::clamp(x, -0x400, 0x3FF)), static_cast<s16>(std::clamp(y, -0x400, 0x3FF))};
}

// Colour FIFO entry from MAC1-3: each >> 4 and saturated to 0..255; CODE passes
// through. The shift is arithmetic: MAC -1..-15 gives -1, which saturates and
// flags, where a division by 16 would give 0.
void Gte::push_color() {
  u32 rgb = static_cast<u32>(rgbc_[3]) << 24;
  for (int i = 0; i < 3; ++i) {
    const s32 c = mac_[i + 1] >> 4;
    if (c < 0 || c > 0xFF) flag_ |= kFlagColorR >> i;
    rgb |= static_cast<u32>(std::clamp(c, 0, 0xFF)) << (8 * i);
  }
  rgb_[0] = rgb_[1];
  rgb_[1] = rgb_[2];
  rgb_[2] = rgb;
}

u32 Gte::divide(u16 numerator, u16 denominator) {
  if (numerator >= denominator * 2) {  // includes denominator 0
    flag_ |= kFlagDivide;
    return 0x1FFFF;
  }
  // Normalise the divisor to 0x8000..0xFFFF, take a seed reciprocal from the
  // table and refine it with one Newton-Raphson step, all in fixed point.
  const int z = std::countl_zero(denominator);
  const u64 n = static_cast<u64>(numerator) << z;
  const u32 d = static_cast<u32>(denominator) << z;
  const u32 u = kUnrTable[(d - 0x7FC0) >> 7] + 0x101u;
  const u32 d1 = (0x2000080u - d * u) >> 8;  // 2 - d * u, the Newton correction
  const u32 d2 = (0x80u + d1 * u) >> 8;      // refined reciprocal, 0x10000..0x20000
  return static_cast<u32>(std::min<u64>(0x1FFFF, (n * d2 + 0x8000) >> 16));
}

// T[i] * 0x1000 + row i of M times V, each partial sum checked and wrapped at 44 bits.
s64 Gte::row_sum(int i, const Matrix& m, const Vector& t, s16 x, s16 y, s16 z) {
  const s64 sum = mac_add(i + 1, s64{t[i]} * 0x1000 + s32{m[i][0]} * x);
  return mac_add(i + 1, mac_add(i + 1, sum + s32{m[i][1]} * y) + s32{m[i][2]} * z);
}

// [MAC1,MAC2,MAC3] = (T * 0x1000 + M * V) >> shift; [IR1,IR2,IR3] = MAC.
void Gte::mat_vec(const Matrix& m, const Vector& t, s16 x, s16 y, s16 z, int shift, bool lm) {
  for (int i = 0; i < 3; ++i) mac_[i + 1] = static_cast<s32>(row_sum(i, m, t, x, y, z) >> shift);
  for (int i = 1; i <= 3; ++i) set_ir(i, mac_[i], lm);
}

// MVMVA with the far-colour translation is broken in hardware: FC * 0x1000 plus
// the first column's product is computed and saturated (raising MAC and IR
// flags as if lm were 0), then thrown away; the result is only the other two
// columns' products.
void Gte::mat_vec_far_color(const Matrix& m, s16 x, s16 y, s16 z, int shift, bool lm) {
  for (int i = 0; i < 3; ++i) {
    const s64 dropped = mac_add(i + 1, s64{fc_[i]} * 0x1000 + s32{m[i][0]} * x);
    set_ir(i + 1, static_cast<s32>(dropped >> shift), false);
    s64 sum = mac_add(i + 1, s32{m[i][1]} * y);
    sum = mac_add(i + 1, sum + s32{m[i][2]} * z);
    mac_[i + 1] = static_cast<s32>(sum >> shift);
  }
  for (int i = 1; i <= 3; ++i) set_ir(i, mac_[i], lm);
}

// One vertex of RTPS/RTPT: rotate and translate V[v], push its depth, divide and
// push its screen position; depth cueing (IR0, MAC0) only for the last vertex.
void Gte::rtp(int v, int shift, bool lm, bool last) {
  s64 sum[3];
  for (int i = 0; i < 3; ++i) {
    sum[i] = row_sum(i, rt_, tr_, v_[v][0], v_[v][1], v_[v][2]);
    mac_[i + 1] = static_cast<s32>(sum[i] >> shift);
  }
  set_ir(1, mac_[1], lm);
  set_ir(2, mac_[2], lm);
  // IR3 is saturated from MAC3, but its flag is raised only when the depth
  // (sum >> 12) leaves -0x8000..0x7FFF, whatever sf and lm are.
  const s64 depth = sum[2] >> 12;
  if (depth < -0x8000 || depth > 0x7FFF) flag_ |= kFlagIr3;
  ir_[3] = static_cast<s16>(std::clamp(mac_[3], lm ? 0 : -0x8000, 0x7FFF));
  push_sz(depth);

  const s64 n = divide(h_, sz_[3]);
  const s64 x = n * ir_[1] + ofx_, y = n * ir_[2] + ofy_;
  set_mac0(x);
  set_mac0(y);
  push_sxy(static_cast<s32>(x >> 16), static_cast<s32>(y >> 16));
  if (last) {
    const s64 dq = n * dqa_ + dqb_;
    set_mac0(dq);
    if (dq >> 12 < 0 || dq >> 12 > 0x1000) flag_ |= kFlagIr0;
    ir_[0] = static_cast<s16>(std::clamp<s64>(dq >> 12, 0, 0x1000));  // 0..1.0
  }
}

void Gte::run_mvmva(u32 command, int shift, bool lm) {
  const u32 mx = command >> 17 & 3, vec = command >> 15 & 3, cv = command >> 13 & 3;
  Matrix m;
  if (mx == 0) m = rt_;
  else if (mx == 1) m = llm_;
  else if (mx == 2) m = lcm_;
  else {
    // mx = 3 selects no real matrix; the hardware reads a mix of other registers.
    const s16 r = static_cast<s16>(rgbc_[0] << 4);
    m = {{{static_cast<s16>(-r), r, ir_[0]},
          {rt_[0][2], rt_[0][2], rt_[0][2]},
          {rt_[1][1], rt_[1][1], rt_[1][1]}}};
  }
  const s16 x = vec == 3 ? ir_[1] : v_[vec][0];
  const s16 y = vec == 3 ? ir_[2] : v_[vec][1];
  const s16 z = vec == 3 ? ir_[3] : v_[vec][2];
  if (cv == 2) return mat_vec_far_color(m, x, y, z, shift, lm);
  mat_vec(m, cv == 0 ? tr_ : cv == 1 ? bk_ : kNoTranslation, x, y, z, shift, lm);
}

// [MAC1,MAC2,MAC3] = in + (FC * 0x1000 - in) * IR0, as two saturating steps:
// IR = (FC * 0x1000 - in) >> shift saturated as if lm were 0, then
// MAC = (IR * IR0 + in) >> shift and IR = MAC with the real lm.
void Gte::depth_cue(const std::array<s64, 3>& in, int shift, bool lm) {
  for (int i = 0; i < 3; ++i) set_mac_ir(i + 1, s64{fc_[i]} * 0x1000 - in[i], shift, false);
  for (int i = 0; i < 3; ++i) set_mac_ir(i + 1, s64{ir_[i + 1]} * ir_[0] + in[i], shift, lm);
}

// The lighting half of NCS/NCCS/NCDS and all of CC/CDP: IR = BK + LCM * IR, then
// optionally multiplied by RGBC (and depth cued), then pushed to the colour FIFO.
void Gte::light_color(int shift, bool lm, Shade shade) {
  mat_vec(lcm_, bk_, ir_[1], ir_[2], ir_[3], shift, lm);
  if (shade != Shade::None) {
    const std::array<s64, 3> in = {s64{rgbc_[0]} * ir_[1] * 16, s64{rgbc_[1]} * ir_[2] * 16,
                                   s64{rgbc_[2]} * ir_[3] * 16};
    if (shade == Shade::Depth) depth_cue(in, shift, lm);
    else
      for (int i = 0; i < 3; ++i) set_mac_ir(i + 1, in[i], shift, lm);
  }
  push_color();
}

void Gte::normal_color(int v, int shift, bool lm, Shade shade) {
  mat_vec(llm_, kNoTranslation, v_[v][0], v_[v][1], v_[v][2], shift, lm);
  light_color(shift, lm, shade);
}

void Gte::dpcs(u32 rgb, int shift, bool lm) {
  const std::array<s64, 3> in = {s64{rgb & 0xFF} << 16, s64{rgb >> 8 & 0xFF} << 16, s64{rgb >> 16 & 0xFF} << 16};
  depth_cue(in, shift, lm);
  push_color();
}

void Gte::avsz(s64 scale, s64 sum) {
  const s64 v = scale * sum;
  set_mac0(v);
  const s64 z = v >> 12;
  if (z < 0 || z > 0xFFFF) flag_ |= kFlagSz3Otz;
  otz_ = static_cast<u16>(std::clamp<s64>(z, 0, 0xFFFF));
}

void Gte::execute(u32 command) {
  const int shift = command >> 19 & 1 ? 12 : 0;
  const bool lm = command >> 10 & 1;
  flag_ = 0;
  switch (command & 0x3F) {
    case 0x01: rtp(0, shift, lm, true); break;  // RTPS
    case 0x30:                                   // RTPT
      for (int v = 0; v < 3; ++v) rtp(v, shift, lm, v == 2);
      break;
    case 0x06: {  // NCLIP: twice the signed area of the SXY triangle
      const s64 x0 = sxy_[0].x, y0 = sxy_[0].y, x1 = sxy_[1].x, y1 = sxy_[1].y, x2 = sxy_[2].x, y2 = sxy_[2].y;
      set_mac0(x0 * y1 + x1 * y2 + x2 * y0 - x0 * y2 - x1 * y0 - x2 * y1);
      break;
    }
    case 0x0C: {  // OP: cross product of the rotation matrix diagonal and IR
      const s64 d1 = rt_[0][0], d2 = rt_[1][1], d3 = rt_[2][2];
      const s64 i1 = ir_[1], i2 = ir_[2], i3 = ir_[3];
      set_mac(1, i3 * d2 - i2 * d3, shift);
      set_mac(2, i1 * d3 - i3 * d1, shift);
      set_mac(3, i2 * d1 - i1 * d2, shift);
      for (int i = 1; i <= 3; ++i) set_ir(i, mac_[i], lm);
      break;
    }
    case 0x10:  // DPCS
      dpcs(rgbc_[0] | rgbc_[1] << 8 | rgbc_[2] << 16, shift, lm);
      break;
    case 0x2A:  // DPCT: three times on RGB0, which each push moves along
      for (int i = 0; i < 3; ++i) dpcs(rgb_[0], shift, lm);
      break;
    case 0x11: {  // INTPL
      depth_cue({s64{ir_[1]} * 0x1000, s64{ir_[2]} * 0x1000, s64{ir_[3]} * 0x1000}, shift, lm);
      push_color();
      break;
    }
    case 0x29: {  // DCPL
      depth_cue({s64{rgbc_[0]} * ir_[1] * 16, s64{rgbc_[1]} * ir_[2] * 16, s64{rgbc_[2]} * ir_[3] * 16}, shift, lm);
      push_color();
      break;
    }
    case 0x12: run_mvmva(command, shift, lm); break;
    case 0x1E: normal_color(0, shift, lm, Shade::None); break;  // NCS
    case 0x20:                                                  // NCT
      for (int v = 0; v < 3; ++v) normal_color(v, shift, lm, Shade::None);
      break;
    case 0x1B: normal_color(0, shift, lm, Shade::Color); break;  // NCCS
    case 0x3F:                                                   // NCCT
      for (int v = 0; v < 3; ++v) normal_color(v, shift, lm, Shade::Color);
      break;
    case 0x13: normal_color(0, shift, lm, Shade::Depth); break;  // NCDS
    case 0x16:                                                   // NCDT
      for (int v = 0; v < 3; ++v) normal_color(v, shift, lm, Shade::Depth);
      break;
    case 0x1C: light_color(shift, lm, Shade::Color); break;  // CC
    case 0x14: light_color(shift, lm, Shade::Depth); break;  // CDP
    case 0x28:                                               // SQR
      for (int i = 1; i <= 3; ++i) set_mac_ir(i, s64{ir_[i]} * ir_[i], shift, lm);
      break;
    case 0x2D: avsz(zsf3_, s64{sz_[1]} + sz_[2] + sz_[3]); break;           // AVSZ3
    case 0x2E: avsz(zsf4_, s64{sz_[0]} + sz_[1] + sz_[2] + sz_[3]); break;  // AVSZ4
    case 0x3D:                                                              // GPF
      for (int i = 1; i <= 3; ++i) set_mac_ir(i, s64{ir_[i]} * ir_[0], shift, lm);
      push_color();
      break;
    case 0x3E:  // GPL: like GPF but accumulating onto MAC1-3
      for (int i = 1; i <= 3; ++i) set_mac_ir(i, s64{mac_[i]} * (s64{1} << shift) + s64{ir_[i]} * ir_[0], shift, lm);
      push_color();
      break;
    default: break;  // unused opcodes: nothing beyond clearing FLAG (the game never issues them)
  }
  if (flag_ & kFlagErrorMask) flag_ |= kFlagError;
}

}  // namespace ss::psx
