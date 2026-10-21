#pragma once

#include <array>

#include "core/types.h"

// The PS1 Geometry Transformation Engine (coprocessor 2), reproduced bit for bit:
// 32 data and 32 control registers with their read/write quirks, and every
// command with the hardware's 44-bit accumulators, saturation, FLAG bits and the
// Newton-Raphson (UNR) perspective divide. Ported code drives it the way the
// original drove cop2: load registers, execute a command word, read registers.
// Reference: psx-spx "Geometry Transformation Engine (GTE)" chapters.
namespace ss::psx {

class Gte {
 public:
  // Data registers (cop2r0-31, the mtc2/mfc2/lwc2/swc2 numbering).
  enum Data : int {
    VXY0, VZ0, VXY1, VZ1, VXY2, VZ2, RGBC, OTZ,
    IR0, IR1, IR2, IR3, SXY0, SXY1, SXY2, SXYP,
    SZ0, SZ1, SZ2, SZ3, RGB0, RGB1, RGB2, RES1,
    MAC0, MAC1, MAC2, MAC3, IRGB, ORGB, LZCS, LZCR,
  };
  // Control registers (cop2r32-63, the ctc2/cfc2 numbering).
  enum Ctrl : int {
    R11R12, R13R21, R22R23, R31R32, R33, TRX, TRY, TRZ,
    L11L12, L13L21, L22L23, L31L32, L33, RBK, GBK, BBK,
    LR1LR2, LR3LG1, LG2LG3, LB1LB2, LB3, RFC, GFC, BFC,
    OFX, OFY, H, DQA, DQB, ZSF3, ZSF4, FLAG,
  };

  // Command words as PsyQ's inline macros (gtemac.h / inline_c.h) issue them.
  // Bits 20-24 carry no meaning to the hardware; bit 19 is sf, bit 10 is lm.
  static constexpr u32 kRtps = 0x0180001;
  static constexpr u32 kRtpt = 0x0280030;
  static constexpr u32 kNclip = 0x1400006;
  static constexpr u32 kOp0 = 0x170000C, kOp12 = 0x178000C;
  static constexpr u32 kDpcs = 0x0780010;
  static constexpr u32 kIntpl = 0x0980011;
  static constexpr u32 kNcds = 0x0E80413;
  static constexpr u32 kCdp = 0x1280414;
  static constexpr u32 kNcdt = 0x0F80416;
  static constexpr u32 kNccs = 0x108041B;
  static constexpr u32 kCc = 0x138041C;
  static constexpr u32 kNcs = 0x0C8041E;
  static constexpr u32 kNct = 0x0D80420;
  static constexpr u32 kSqr0 = 0x0A00428, kSqr12 = 0x0A80428;
  static constexpr u32 kDcpl = 0x0680029;
  static constexpr u32 kDpct = 0x0F8002A;
  static constexpr u32 kAvsz3 = 0x158002D;
  static constexpr u32 kAvsz4 = 0x168002E;
  static constexpr u32 kGpf0 = 0x190003D, kGpf12 = 0x198003D;
  static constexpr u32 kGpl0 = 0x1A0003E, kGpl12 = 0x1A8003E;
  static constexpr u32 kNcct = 0x118043F;

  // MVMVA operands: matrix, vector and translation selectors.
  enum class Mx : u32 { Rotation = 0, Light = 1, Color = 2, Garbage = 3 };
  enum class Vec : u32 { V0 = 0, V1 = 1, V2 = 2, IR = 3 };
  enum class Tr : u32 { TR = 0, BK = 1, FC = 2, None = 3 };
  static constexpr u32 mvmva(bool sf, Mx mx, Vec v, Tr cv, bool lm) {
    return 0x0400012u | (sf ? 1u << 19 : 0) | static_cast<u32>(mx) << 17 | static_cast<u32>(v) << 15 |
           static_cast<u32>(cv) << 13 | (lm ? 1u << 10 : 0);
  }
  // A few MVMVA forms PsyQ names: gte_rtv0 (RT*V0), gte_rtv0tr (RT*V0+TR),
  // gte_rtir (RT*IR), gte_llv0 (LLM*V0), gte_lcir (LCM*IR+BK).
  static constexpr u32 kRtv0 = 0x0486012, kRtv0tr = 0x0480012, kRtir = 0x049E012, kRtirSf0 = 0x041E012;
  static constexpr u32 kLlv0 = 0x04A6012, kLcir = 0x04DA012;

  // FLAG bits.
  static constexpr u32 kFlagError = 1u << 31;
  static constexpr u32 kFlagMac1Pos = 1u << 30, kFlagMac2Pos = 1u << 29, kFlagMac3Pos = 1u << 28;
  static constexpr u32 kFlagMac1Neg = 1u << 27, kFlagMac2Neg = 1u << 26, kFlagMac3Neg = 1u << 25;
  static constexpr u32 kFlagIr1 = 1u << 24, kFlagIr2 = 1u << 23, kFlagIr3 = 1u << 22;
  static constexpr u32 kFlagColorR = 1u << 21, kFlagColorG = 1u << 20, kFlagColorB = 1u << 19;
  static constexpr u32 kFlagSz3Otz = 1u << 18;
  static constexpr u32 kFlagDivide = 1u << 17;
  static constexpr u32 kFlagMac0Pos = 1u << 16, kFlagMac0Neg = 1u << 15;
  static constexpr u32 kFlagSx2 = 1u << 14, kFlagSy2 = 1u << 13;
  static constexpr u32 kFlagIr0 = 1u << 12;
  // Bits that also raise the error bit 31 (30-23 and 18-13).
  static constexpr u32 kFlagErrorMask = 0x7F87E000;

  void reset();  // all registers zero (LZCR 32, as LZCS is 0)

  u32 read_data(int reg) const;
  void write_data(int reg, u32 value);
  u32 read_ctrl(int reg) const;
  void write_ctrl(int reg, u32 value);

  // Runs one command: the 25-bit immediate of a cop2 instruction (bit 25 and
  // above are ignored, so a full 0x4Axxxxxx instruction word works as well).
  void execute(u32 command);

  // Typed views of the registers most read back by ported code.
  s16 ir(int i) const { return ir_[i]; }
  s32 mac(int i) const { return mac_[i]; }
  u16 sz(int i) const { return sz_[i]; }
  u16 otz() const { return otz_; }
  s16 sx(int i) const { return sxy_[i].x; }
  s16 sy(int i) const { return sxy_[i].y; }
  u32 flag() const { return flag_; }
  void set_vertex(int i, s16 x, s16 y, s16 z) { v_[i] = {x, y, z}; }

  // The UNR divide RTPS/RTPT use: min(0x1FFFF, h * 0x10000 / sz) with the
  // hardware's reciprocal-table rounding, or 0x1FFFF and the divide flag when
  // h >= sz * 2. Exposed for tests.
  u32 divide(u16 numerator, u16 denominator);

 private:
  struct Xy {
    s16 x = 0, y = 0;
  };
  using Matrix = std::array<std::array<s16, 3>, 3>;  // [row][column]
  using Vector = std::array<s32, 3>;
  enum class Shade { None, Color, Depth };  // NCS-like, NCCS-like, NCDS-like

  // Arithmetic steps shared by the commands; i is 1-3 for MAC/IR1-3.
  s64 mac_add(int i, s64 sum);                  // 44-bit overflow check and wrap
  void set_mac(int i, s64 value, int shift);    // check, shift, store 32 bits
  void set_ir(int i, s32 value, bool lm);       // saturate to IR range
  void set_mac_ir(int i, s64 value, int shift, bool lm);
  void set_mac0(s64 value);                     // 32-bit overflow check, store
  void push_sz(s64 z);
  void push_sxy(s32 x, s32 y);
  void push_color();

  s64 row_sum(int i, const Matrix& m, const Vector& t, s16 x, s16 y, s16 z);
  void mat_vec(const Matrix& m, const Vector& t, s16 x, s16 y, s16 z, int shift, bool lm);
  void mat_vec_far_color(const Matrix& m, s16 x, s16 y, s16 z, int shift, bool lm);
  void rtp(int v, int shift, bool lm, bool last);
  void run_mvmva(u32 command, int shift, bool lm);
  void normal_color(int v, int shift, bool lm, Shade shade);
  void light_color(int shift, bool lm, Shade shade);
  void depth_cue(const std::array<s64, 3>& in, int shift, bool lm);  // MAC + (FC - MAC) * IR0
  void dpcs(u32 rgb, int shift, bool lm);
  void avsz(s64 scale, s64 sum);

  // Data registers.
  std::array<std::array<s16, 3>, 3> v_ = {};  // V0-V2, x/y/z
  u8 rgbc_[4] = {};
  u16 otz_ = 0;
  s16 ir_[4] = {};
  Xy sxy_[3];
  u16 sz_[4] = {};
  u32 rgb_[3] = {};
  u32 res1_ = 0;
  s32 mac_[4] = {};
  u32 lzcs_ = 0, lzcr_ = 32;

  // Control registers.
  Matrix rt_ = {}, llm_ = {}, lcm_ = {};
  Vector tr_ = {}, bk_ = {}, fc_ = {};
  s32 ofx_ = 0, ofy_ = 0;
  u16 h_ = 0;
  s16 dqa_ = 0;
  s32 dqb_ = 0;
  s16 zsf3_ = 0, zsf4_ = 0;
  u32 flag_ = 0;
};

static_assert(Gte::kRtv0 == Gte::mvmva(true, Gte::Mx::Rotation, Gte::Vec::V0, Gte::Tr::None, false));
static_assert(Gte::kRtv0tr == Gte::mvmva(true, Gte::Mx::Rotation, Gte::Vec::V0, Gte::Tr::TR, false));
static_assert(Gte::kRtir == Gte::mvmva(true, Gte::Mx::Rotation, Gte::Vec::IR, Gte::Tr::None, false));
static_assert(Gte::kRtirSf0 == Gte::mvmva(false, Gte::Mx::Rotation, Gte::Vec::IR, Gte::Tr::None, false));
static_assert(Gte::kLlv0 == Gte::mvmva(true, Gte::Mx::Light, Gte::Vec::V0, Gte::Tr::None, false));
static_assert(Gte::kLcir == Gte::mvmva(true, Gte::Mx::Color, Gte::Vec::IR, Gte::Tr::BK, false));

}  // namespace ss::psx
