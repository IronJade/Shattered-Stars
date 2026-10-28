#include <algorithm>
#include <array>
#include <utility>
#include <vector>

#include "psx/gte.h"
#include "runtime/libgte.h"
#include "test.h"

// GTE and libgte tests. Expected values are worked out by hand in the comments
// (fixed point: 0x1000 = 1.0 for matrices, 16.16 for the divide result), from
// psx-spx's GTE chapters. gte_matches_redux_sweep additionally pins every
// command to PCSX-Redux's independent GTE, bit for bit.

using namespace ss;
using G = psx::Gte;

namespace {

constexpr u32 xy(int x, int y) { return static_cast<u16>(x) | static_cast<u32>(static_cast<u16>(y)) << 16; }

void identity_rotation(G& g) {
  g.write_ctrl(G::R11R12, 0x1000);  // RT11 = 1.0, RT12 = 0
  g.write_ctrl(G::R13R21, 0);
  g.write_ctrl(G::R22R23, 0x1000);
  g.write_ctrl(G::R31R32, 0);
  g.write_ctrl(G::R33, 0x1000);
}

}  // namespace

// --- registers ---------------------------------------------------------------

TEST(gte_register_sign_and_zero_extension) {
  G g;
  g.write_data(G::VZ0, 0x12348000);  // 16-bit signed registers read back sign-extended
  CHECK_EQ(g.read_data(G::VZ0), 0xFFFF8000u);
  g.write_data(G::IR2, 0x00017FFF);
  CHECK_EQ(g.read_data(G::IR2), 0x7FFFu);
  g.write_data(G::OTZ, 0xFFFF8000);  // OTZ and SZ0-3 are unsigned: zero-extended
  CHECK_EQ(g.read_data(G::OTZ), 0x8000u);
  g.write_data(G::SZ1, 0xABCDFFFF);
  CHECK_EQ(g.read_data(G::SZ1), 0xFFFFu);
  g.write_ctrl(G::R33, 0x0000F000);
  CHECK_EQ(g.read_ctrl(G::R33), 0xFFFFF000u);
  g.write_ctrl(G::DQA, 0xEF9E);
  CHECK_EQ(g.read_ctrl(G::DQA), 0xFFFFEF9Eu);
  g.write_ctrl(G::ZSF3, 0x155);
  CHECK_EQ(g.read_ctrl(G::ZSF3), 0x155u);
  // Matrices pack two elements per register, row-major: R13R21 holds RT13 low, RT21 high.
  g.write_ctrl(G::R13R21, 0x12345678);
  CHECK_EQ(g.read_ctrl(G::R13R21), 0x12345678u);
  g.write_ctrl(G::LB3, 0xFFFF8001);
  CHECK_EQ(g.read_ctrl(G::LB3), 0xFFFF8001u);
}

TEST(gte_h_reads_back_sign_extended_but_divides_unsigned) {
  G g;
  g.write_ctrl(G::H, 0xFFFF);
  CHECK_EQ(g.read_ctrl(G::H), 0xFFFFFFFFu);
  // Yet the divide uses 0xFFFF: 0xFFFF < 2 * 0x8000, so no overflow, and
  // 0xFFFF / 0x8000 = 1.99997 = 0x1FFFE in 16.16.
  CHECK_EQ(g.divide(0xFFFF, 0x8000), 0x1FFFEu);
  CHECK_EQ(g.flag(), 0u);
}

TEST(gte_flag_register_write) {
  G g;
  g.write_ctrl(G::FLAG, 0xFFFFFFFF);  // bits 0-11 do not exist
  CHECK_EQ(g.read_ctrl(G::FLAG), 0xFFFFF000u);
  g.write_ctrl(G::FLAG, 0x80000000);  // bit 31 is derived, not stored
  CHECK_EQ(g.read_ctrl(G::FLAG), 0u);
  g.write_ctrl(G::FLAG, G::kFlagIr0 | G::kFlagIr3 | G::kFlagColorR);  // none of these raise bit 31
  CHECK_EQ(g.read_ctrl(G::FLAG), G::kFlagIr0 | G::kFlagIr3 | G::kFlagColorR);
  g.write_ctrl(G::FLAG, G::kFlagDivide);  // but this one does
  CHECK_EQ(g.read_ctrl(G::FLAG), G::kFlagDivide | G::kFlagError);
}

TEST(gte_irgb_orgb) {
  G g;
  // IRGB expands 5:5:5 into IR1-3, each component * 0x80.
  g.write_data(G::IRGB, 0x7C00 | 0x0200 | 0x0001);  // b = 31, g = 16, r = 1
  CHECK_EQ(g.ir(1), 0x0080);
  CHECK_EQ(g.ir(2), 0x0800);
  CHECK_EQ(g.ir(3), 0x0F80);
  CHECK_EQ(g.read_data(G::ORGB), 0x7E01u);
  CHECK_EQ(g.read_data(G::IRGB), 0x7E01u);  // IRGB reads as ORGB
  // ORGB is IR >> 7 saturated to 0..31: -1 >> 7 = -1 -> 0, 0x7FFF >> 7 = 255 -> 31.
  g.write_data(G::IR1, 0xFFFF);
  g.write_data(G::IR2, 0x7FFF);
  g.write_data(G::IR3, 0x017F);  // 383 >> 7 = 2
  CHECK_EQ(g.read_data(G::ORGB), (2u << 10) | (31u << 5));
  g.write_data(G::ORGB, 0);  // read-only
  CHECK_EQ(g.read_data(G::ORGB), (2u << 10) | (31u << 5));
}

TEST(gte_lzcs_lzcr) {
  G g;
  // LZCR counts leading bits equal to bit 31.
  const std::pair<u32, u32> cases[] = {{0, 32},          {0xFFFFFFFF, 32}, {1, 31},          {0x80000000, 1},
                                       {0x7FFFFFFF, 1},  {0x0000FFFF, 16}, {0xFFFF0000, 16}, {0xFFFFFFFE, 31},
                                       {0x00400000, 9}};
  for (const auto& [in, count] : cases) {
    g.write_data(G::LZCS, in);
    CHECK_EQ(g.read_data(G::LZCS), in);
    CHECK_EQ(g.read_data(G::LZCR), count);
  }
  g.write_data(G::LZCR, 5);  // read-only
  CHECK_EQ(g.read_data(G::LZCR), 9u);
}

TEST(gte_sxyp_pushes_the_screen_fifo) {
  G g;
  g.write_data(G::SXY0, xy(1, 1));
  g.write_data(G::SXY1, xy(2, 2));
  g.write_data(G::SXY2, xy(3, 3));
  g.write_data(G::SXYP, xy(-4, 4));  // SXY0 <- SXY1 <- SXY2 <- new
  CHECK_EQ(g.read_data(G::SXY0), xy(2, 2));
  CHECK_EQ(g.read_data(G::SXY1), xy(3, 3));
  CHECK_EQ(g.read_data(G::SXY2), xy(-4, 4));
  CHECK_EQ(g.read_data(G::SXYP), xy(-4, 4));  // reads the newest
}

// --- UNR divide ----------------------------------------------------------------

TEST(gte_unr_divide) {
  G g;
  // 1000 / 1000: normalised d = 0x8000 (shift 3), seed index 0, unr[0] = 0xFF, u = 0x200;
  // d1 = (0x2000080 - 0x8000 * 0x200) >> 8 = 0x10000, d2 = (0x80 + 0x10000 * 0x200) >> 8
  // = 0x20000; n = 1000 << 3 = 0x8000 * 0x20000 = 0x100000000, (+0x8000) >> 16 = 0x10000.
  CHECK_EQ(g.divide(1000, 1000), 0x10000u);
  // 1000 / 2000: shift 5, d = 0xFA00, index (0xFA00 - 0x7FC0) >> 7 = 244,
  // unr[244] = (0x40000 / 500 + 1) / 2 - 0x101 = 5, u = 262; d1 = (0x2000080 - 64000 * 262) >> 8
  // = 65572, d2 = (0x80 + 65572 * 262) >> 8 = 67109; (32000 * 67109 + 0x8000) >> 16 = 0x8000.
  CHECK_EQ(g.divide(1000, 2000), 0x8000u);
  // Not exact everywhere: 2 / 5 = 26214.4 in 16.16, but shift 13, d = 0xA000, index 64,
  // unr[64] = (0x40000 / 320 + 1) / 2 - 0x101 = 153, u = 410, d1 = 65472, d2 = 104858
  // (a reciprocal slightly above 0.4), and (16384 * 104858 + 0x8000) >> 16 = 26215.
  CHECK_EQ(g.divide(2, 5), 0x6667u);
  // Largest non-overflowing quotient: 0xFFF / 0x800 = 1.99951 = 0x1FFE0.
  CHECK_EQ(g.divide(0xFFF, 0x800), 0x1FFE0u);
  CHECK_EQ(g.flag(), 0u);
  // H >= SZ3 * 2 overflows: 0x1FFFF and FLAG bit 17. SZ3 = 0 always does.
  CHECK_EQ(g.divide(0x1000, 0x800), 0x1FFFFu);
  CHECK_EQ(g.flag(), G::kFlagDivide);
  CHECK_EQ(g.divide(0, 0), 0x1FFFFu);
}

// --- RTPS / RTPT -------------------------------------------------------------

TEST(gte_rtps_projects_a_vertex) {
  G g;
  // Rotate 90 degrees about Y: x' = z, y' = y, z' = -x. RT31 = -1.0 is 0xF000.
  g.write_ctrl(G::R11R12, 0);
  g.write_ctrl(G::R13R21, 0x1000);
  g.write_ctrl(G::R22R23, 0x1000);
  g.write_ctrl(G::R31R32, 0xF000);
  g.write_ctrl(G::R33, 0);
  g.write_ctrl(G::TRZ, 2000);
  g.write_ctrl(G::H, 850);
  g.write_ctrl(G::OFX, 160 << 16);
  g.write_ctrl(G::OFY, 120 << 16);
  g.write_ctrl(G::DQA, 0);
  g.write_ctrl(G::DQB, 0x800 << 12);
  g.write_data(G::VXY0, xy(300, -200));
  g.write_data(G::VZ0, 500);
  g.execute(G::kRtps);
  // MAC = (TR * 0x1000 + RT * V) >> 12 = (500, -200, 2000 - 300 = 1700); IR = MAC.
  CHECK_EQ(g.mac(1), 500);
  CHECK_EQ(g.mac(2), -200);
  CHECK_EQ(g.mac(3), 1700);
  CHECK_EQ(g.ir(1), 500);
  CHECK_EQ(g.ir(3), 1700);
  CHECK_EQ(g.sz(3), 1700);
  // H / SZ3 = 850 / 1700 = 0x8000 (exact through the UNR here), so
  // SX = (0x8000 * 500 + 160 << 16) >> 16 = 250 + 160, SY = -100 + 120.
  CHECK_EQ(g.sx(2), 410);
  CHECK_EQ(g.sy(2), 20);
  // Depth cue: MAC0 = DQA * 0x8000 + DQB = 0x800000, IR0 = MAC0 >> 12 = 0x800.
  CHECK_EQ(g.mac(0), 0x800000);
  CHECK_EQ(g.ir(0), 0x800);
  CHECK_EQ(g.flag(), 0u);
}

TEST(gte_rtpt_fills_the_fifos_in_order) {
  G g;
  identity_rotation(g);
  g.write_ctrl(G::H, 1000);
  for (int v = 0; v < 3; ++v) {
    g.write_data(G::VXY0 + 2 * v, xy(100 * (v + 1), 0));
    g.write_data(G::VZ0 + 2 * v, 1000 * (v + 1));  // z = 1000, 2000, 3000
  }
  g.write_data(G::SZ3, 7);
  g.execute(G::kRtpt);
  // The SZ FIFO shifts three times: the old SZ3 ends in SZ0.
  CHECK_EQ(g.sz(0), 7);
  CHECK_EQ(g.sz(1), 1000);
  CHECK_EQ(g.sz(2), 2000);
  CHECK_EQ(g.sz(3), 3000);
  // x * H / z: 100 * 1, 200 * 0.5, 300 * 0.333 (H / 3000 = 0x5555 -> 99.99, truncated).
  CHECK_EQ(g.sx(0), 100);
  CHECK_EQ(g.sx(1), 100);
  CHECK_EQ(g.sx(2), 99);
}

TEST(gte_rtps_behind_the_camera) {
  G g;
  identity_rotation(g);
  g.write_ctrl(G::TRZ, static_cast<u32>(-100));
  g.write_ctrl(G::H, 1000);
  g.write_data(G::VXY0, xy(100, 0));
  g.execute(G::kRtps);
  // z = -100 saturates SZ3 to 0 (bit 18); 1000 >= 0 * 2 is a divide overflow (bit 17),
  // so the quotient is 0x1FFFF and SX = 0x1FFFF * 100 >> 16 = 199.
  CHECK_EQ(g.sz(3), 0);
  CHECK_EQ(g.ir(3), -100);  // IR3 keeps the sign
  CHECK_EQ(g.sx(2), 199);
  CHECK_EQ(g.flag(), G::kFlagSz3Otz | G::kFlagDivide | G::kFlagError);
}

TEST(gte_rtps_screen_and_ir_saturation) {
  G g;
  identity_rotation(g);
  g.write_ctrl(G::TRZ, 100);
  g.write_ctrl(G::H, 199);  // 199 / 100 = 1.99
  g.write_data(G::VXY0, xy(0x2000, -0x2000));
  g.execute(G::kRtps);
  // SX = 1.99 * 0x2000 > 0x3FF saturates (bit 14), SY < -0x400 (bit 13).
  CHECK_EQ(g.sx(2), 0x3FF);
  CHECK_EQ(g.sy(2), -0x400);
  CHECK_EQ(g.flag(), G::kFlagSx2 | G::kFlagSy2 | G::kFlagError);

  // IR1 saturates at 0x7FFF (bit 24) when MAC1 = 0x7FFF + 0x7FFF; with lm = 1
  // a negative IR2 saturates at 0 (bit 23).
  identity_rotation(g);
  g.write_ctrl(G::TRX, 0x7FFF);
  g.write_data(G::VXY0, xy(0x7FFF, -5));
  g.execute(G::kRtps | 1 << 10);
  CHECK_EQ(g.mac(1), 0xFFFE);
  CHECK_EQ(g.ir(1), 0x7FFF);
  CHECK_EQ(g.mac(2), -5);
  CHECK_EQ(g.ir(2), 0);
  CHECK((g.flag() & (G::kFlagIr1 | G::kFlagIr2)) == (G::kFlagIr1 | G::kFlagIr2));
}

TEST(gte_rtps_ir3_flag_quirk) {
  // With sf = 0 the IR3 flag tests MAC3 >> 12 (the depth), not MAC3 itself.
  G g;
  identity_rotation(g);
  g.write_ctrl(G::TRZ, 10);
  g.write_ctrl(G::H, 1);
  g.execute(G::kRtps & ~(1u << 19));
  // MAC3 = 10 * 0x1000 = 40960 saturates IR3 to 0x7FFF, but 40960 >> 12 = 10 is in range.
  CHECK_EQ(g.mac(3), 40960);
  CHECK_EQ(g.ir(3), 0x7FFF);
  CHECK_EQ(g.sz(3), 10);
  CHECK_EQ(g.flag() & G::kFlagIr3, 0u);
  // And the other way round: depth 0x9000 flags IR3 although sf = 1 leaves MAC3 = 0x9000.
  g.write_ctrl(G::TRZ, 0x9000);
  g.execute(G::kRtps);
  CHECK_EQ(g.ir(3), 0x7FFF);
  CHECK_EQ(g.flag() & G::kFlagIr3, G::kFlagIr3);
}

// --- NCLIP, AVSZ -------------------------------------------------------------

TEST(gte_nclip_sign) {
  G g;
  // MAC0 = SX0*SY1 + SX1*SY2 + SX2*SY0 - SX0*SY2 - SX1*SY0 - SX2*SY1, twice the signed area.
  g.write_data(G::SXY0, xy(0, 0));
  g.write_data(G::SXY1, xy(10, 0));
  g.write_data(G::SXY2, xy(0, 10));
  g.execute(G::kNclip);
  CHECK_EQ(g.mac(0), 100);
  g.write_data(G::SXY1, xy(0, 10));
  g.write_data(G::SXY2, xy(10, 0));
  g.execute(G::kNclip);
  CHECK_EQ(g.mac(0), -100);
  g.write_data(G::SXY2, xy(0, 20));  // collinear
  g.execute(G::kNclip);
  CHECK_EQ(g.mac(0), 0);
  // (-32768,-32768), (32767,-32768), (-32768,32767): 2^30 + 32767^2 + 2 * 32767 * 32768
  // = 4294836225 > 2^31 - 1 sets bit 16; MAC0 keeps the low 32 bits, -131071.
  g.write_data(G::SXY0, xy(-32768, -32768));
  g.write_data(G::SXY1, xy(32767, -32768));
  g.write_data(G::SXY2, xy(-32768, 32767));
  g.execute(G::kNclip);
  CHECK_EQ(g.mac(0), -131071);
  CHECK_EQ(g.flag(), G::kFlagMac0Pos | G::kFlagError);
}

TEST(gte_avsz3_avsz4) {
  G g;
  g.write_ctrl(G::ZSF3, 0x155);  // InitGeom's 341 ~ 4096 / 12: the average, / 4
  g.write_ctrl(G::ZSF4, 0x100);  // 4096 / 16
  g.write_data(G::SZ0, 100);
  g.write_data(G::SZ1, 100);
  g.write_data(G::SZ2, 200);
  g.write_data(G::SZ3, 300);
  g.execute(G::kAvsz3);
  CHECK_EQ(g.mac(0), 341 * 600);  // 204600; OTZ = 204600 >> 12 = 49
  CHECK_EQ(g.otz(), 49);
  g.execute(G::kAvsz4);
  CHECK_EQ(g.mac(0), 256 * 700);  // 179200 >> 12 = 43
  CHECK_EQ(g.otz(), 43);
  CHECK_EQ(g.flag(), 0u);
  // A negative scale saturates OTZ at 0 (bit 18).
  g.write_ctrl(G::ZSF3, static_cast<u32>(-1));
  g.execute(G::kAvsz3);
  CHECK_EQ(g.otz(), 0);
  CHECK_EQ(g.flag(), G::kFlagSz3Otz | G::kFlagError);
  // 0x7FFF * 4 * 0xFFFF = 8589541380 overflows MAC0 (bit 16; low word -393212);
  // OTZ comes from the full sum and saturates at 0xFFFF.
  g.write_ctrl(G::ZSF4, 0x7FFF);
  for (int i = 0; i < 4; ++i) g.write_data(G::SZ0 + i, 0xFFFF);
  g.execute(G::kAvsz4);
  CHECK_EQ(g.mac(0), -393212);
  CHECK_EQ(g.otz(), 0xFFFF);
  CHECK_EQ(g.flag(), G::kFlagMac0Pos | G::kFlagSz3Otz | G::kFlagError);
}

// --- MVMVA -------------------------------------------------------------------

TEST(gte_mvmva_matrix_vector_translation) {
  G g;
  identity_rotation(g);
  g.write_ctrl(G::TRX, 1);
  g.write_ctrl(G::TRY, 2);
  g.write_ctrl(G::TRZ, 3);
  g.write_data(G::VXY1, xy(100, -200));
  g.write_data(G::VZ1, 300);
  g.execute(G::mvmva(true, G::Mx::Rotation, G::Vec::V1, G::Tr::TR, false));
  CHECK_EQ(g.mac(1), 101);
  CHECK_EQ(g.mac(2), -198);
  CHECK_EQ(g.mac(3), 303);
  // sf = 0 keeps the 12 fraction bits: (1 * 0x1000 + 0x1000 * 100) = 0x65000.
  g.execute(G::mvmva(false, G::Mx::Rotation, G::Vec::V1, G::Tr::TR, false));
  CHECK_EQ(g.mac(1), 0x65000);
  CHECK_EQ(g.ir(1), 0x7FFF);
  CHECK_EQ(g.ir(2), -0x8000);  // -198 * 0x1000 saturates low
  CHECK_EQ(g.flag(), G::kFlagIr1 | G::kFlagIr2 | G::kFlagIr3 | G::kFlagError);

  // Light matrix times IR plus the background colour: LLM row 0 = (0.5, 0.25, 0),
  // IR = (0x1000, 0x2000, 7), BK = (10, 0, 0): MAC1 = 10 + 0x800 + 0x800 = 0x100A.
  g.write_ctrl(G::L11L12, xy(0x800, 0x400));
  g.write_ctrl(G::RBK, 10);
  g.write_data(G::IR1, 0x1000);
  g.write_data(G::IR2, 0x2000);
  g.write_data(G::IR3, 7);
  g.execute(G::mvmva(true, G::Mx::Light, G::Vec::IR, G::Tr::BK, false));
  CHECK_EQ(g.mac(1), 0x100A);
  CHECK_EQ(g.flag(), 0u);
}

TEST(gte_mvmva_far_color_bug) {
  G g;
  identity_rotation(g);
  g.write_ctrl(G::RFC, 1000);
  g.write_data(G::VXY0, xy(100, 200));
  g.write_data(G::VZ0, 300);
  // Intended: FC + RT * V = (1100, 200, 300). The hardware drops FC and the first
  // column: row i is RT[i][1] * VY + RT[i][2] * VZ, so (0, 200, 300).
  g.execute(G::mvmva(true, G::Mx::Rotation, G::Vec::V0, G::Tr::FC, false));
  CHECK_EQ(g.mac(1), 0);
  CHECK_EQ(g.mac(2), 200);
  CHECK_EQ(g.mac(3), 300);
  CHECK_EQ(g.flag(), 0u);
  // The dropped part still raises flags: (0x10000 * 0x1000 + 0x1000 * 100) >> 12 = 65636
  // exceeds 0x7FFF, so bit 24 is set although IR1 ends up 0.
  g.write_ctrl(G::RFC, 0x10000);
  g.execute(G::mvmva(true, G::Mx::Rotation, G::Vec::V0, G::Tr::FC, false));
  CHECK_EQ(g.ir(1), 0);
  CHECK_EQ(g.flag(), G::kFlagIr1 | G::kFlagError);
}

TEST(gte_mvmva_garbage_matrix) {
  G g;
  // mx = 3 reads [-R*16, R*16, IR0; RT13 x3; RT22 x3] with R from RGBC.
  g.write_data(G::RGBC, 6);  // R = 6: -0x60, 0x60
  g.write_data(G::IR0, 0x100);
  g.write_ctrl(G::R13R21, 5);    // RT13 = 5
  g.write_ctrl(G::R22R23, 0xFFFE);  // RT22 = -2
  g.write_data(G::VXY0, xy(1, 2));
  g.write_data(G::VZ0, 3);
  g.execute(G::mvmva(false, G::Mx::Garbage, G::Vec::V0, G::Tr::None, false));
  CHECK_EQ(g.mac(1), -0x60 + 0x60 * 2 + 0x100 * 3);  // 864
  CHECK_EQ(g.mac(2), 5 * 6);
  CHECK_EQ(g.mac(3), -2 * 6);
}

TEST(gte_mac_44bit_overflow) {
  G g;
  // TRX * 0x1000 = 2^43 - 0x1000, plus 0x7FFF^2 crosses 2^43 - 1: bit 30, and the
  // sum wraps to 44-bit negative: (2^43 - 4096 + 1073676289 - 2^44) >> 12 = -2147221521.
  g.write_ctrl(G::TRX, 0x7FFFFFFF);
  g.write_ctrl(G::R11R12, 0x7FFF);
  g.write_data(G::VXY0, 0x7FFF);
  g.execute(G::mvmva(true, G::Mx::Rotation, G::Vec::V0, G::Tr::TR, false));
  CHECK_EQ(g.mac(1), -2147221521);
  CHECK_EQ(g.ir(1), -0x8000);
  CHECK_EQ(g.flag(), G::kFlagMac1Pos | G::kFlagIr1 | G::kFlagError);
  // The negative side, on MAC3: -2^43 - 0x7FFF * 0x7FFF.
  g.write_ctrl(G::TRX, 0);
  g.write_ctrl(G::TRZ, 0x80000000);
  g.write_ctrl(G::R31R32, 0x7FFF);
  g.write_data(G::VXY0, xy(-0x7FFF, 0));
  g.execute(G::mvmva(true, G::Mx::Rotation, G::Vec::V0, G::Tr::TR, false));
  CHECK_EQ(g.flag() & (G::kFlagMac3Neg | G::kFlagMac3Pos), G::kFlagMac3Neg);
}

// --- colour commands -----------------------------------------------------------

namespace {

// One light straight down -z, coloured (1.0, 0.5, 0.25); ambient 16/255 (SetBackColor
// scale: 16 << 4 = 0x100); far colour pure red (255 << 4); material grey 128.
void colour_scene(G& g) {
  g.write_ctrl(G::L13L21, 0xF000);  // LLM row 0 = (0, 0, -1.0)
  g.write_ctrl(G::LR1LR2, 0x1000);  // LCM column 0 = (1.0, 0.5, 0.25)
  g.write_ctrl(G::LR3LG1, 0x800u << 16);
  g.write_ctrl(G::LB1LB2, 0x400);
  for (int i = 0; i < 3; ++i) g.write_ctrl(G::RBK + i, 0x100);
  g.write_ctrl(G::RFC, 0xFF0);
  g.write_data(G::VXY0, 0);
  g.write_data(G::VZ0, 0xF000);  // normal (0, 0, -1.0): facing the light
  g.write_data(G::RGBC, 0x30808080);
  g.write_data(G::IR0, 0x800);  // depth cue halfway
}

}  // namespace

TEST(gte_ncds_colour_pipeline) {
  G g;
  colour_scene(g);
  g.execute(G::kNcds);
  // 1. IR = LLM * N = (-1.0 * -1.0) = (0x1000, 0, 0).
  // 2. IR = BK + LCM * IR = (0x100 + 0x1000, 0x100 + 0x800, 0x100 + 0x400) = (4352, 2304, 1280).
  // 3. MAC = RGB * IR << 4 = (8912896, 4718592, 2621440).
  // 4. IR = (FC << 12 - MAC) >> 12 = (1904, -1152, -640) (as if lm = 0), then
  //    MAC = (IR * IR0 + MAC) >> 12 = (3128, 576, 320).
  // 5. Colour = MAC >> 4 = (195, 36, 20): grey 128 lit to (136, 72, 40), halfway to red.
  CHECK_EQ(g.ir(1), 3128);
  CHECK_EQ(g.ir(2), 576);
  CHECK_EQ(g.ir(3), 320);
  CHECK_EQ(g.read_data(G::RGB2), 0x301424C3u);
  CHECK_EQ(g.flag(), 0u);
}

TEST(gte_nccs_ncs_cc) {
  G g;
  colour_scene(g);
  g.execute(G::kNccs);  // steps 1-3 then >> 12: (2176, 1152, 640) -> (136, 72, 40)
  CHECK_EQ(g.read_data(G::RGB2), 0x30284888u);
  g.execute(G::kNcs);  // no material: (4352, 2304, 1280) >> 4 -> red 272 saturates (bit 21)
  CHECK_EQ(g.read_data(G::RGB2), 0x305090FFu);
  CHECK_EQ(g.flag(), G::kFlagColorR);  // colour saturation does not raise bit 31
  // CC starts at step 2 from IR: IR = (0x1000, 0, 0) gives the NCCS result again.
  g.write_data(G::IR1, 0x1000);
  g.write_data(G::IR2, 0);
  g.write_data(G::IR3, 0);
  g.execute(G::kCc);
  CHECK_EQ(g.read_data(G::RGB2), 0x30284888u);
  // The colour FIFO moved along with each command.
  CHECK_EQ(g.read_data(G::RGB0), 0x30284888u);
  CHECK_EQ(g.read_data(G::RGB1), 0x305090FFu);
}

TEST(gte_dpcs_dpct_intpl) {
  G g;
  g.write_ctrl(G::BFC, 0xFF0);
  g.write_data(G::RGBC, 0x003264C8);  // (200, 100, 50)
  g.write_data(G::IR0, 0x1000);       // fully far
  g.execute(G::kDpcs);
  // MAC = RGB << 16; IR = (FC << 12 - MAC) >> 12, e.g. blue (0xFF0000 - 0x320000) >> 12 = 3280;
  // MAC = (IR * 0x1000 + MAC) >> 12 = FC exactly, so the colour is the far colour.
  CHECK_EQ(g.read_data(G::RGB2), 0x00FF0000u);
  g.write_data(G::IR0, 0);  // not at all far: the colour itself
  g.execute(G::kDpcs);
  CHECK_EQ(g.read_data(G::RGB2), 0x003264C8u);

  // DPCT runs DPCS three times on RGB0, which each push replaces with the next entry.
  g.write_data(G::RGBC, 0x7F000000);  // CODE comes from RGBC each time
  g.write_data(G::RGB0, 0x00000010);
  g.write_data(G::RGB1, 0x00000020);
  g.write_data(G::RGB2, 0x00000030);
  g.execute(G::kDpct);
  CHECK_EQ(g.read_data(G::RGB0), 0x7F000010u);
  CHECK_EQ(g.read_data(G::RGB1), 0x7F000020u);
  CHECK_EQ(g.read_data(G::RGB2), 0x7F000030u);

  // INTPL interpolates IR towards FC: IR = (1.0, 0.5, 0), FC = (0, 1.0, 0), IR0 = 0.5
  // gives (0.5, 0.75, 0), i.e. MAC (0x800, 0xC00, 0) and colour (0x80, 0xC0, 0).
  g.write_ctrl(G::RFC, 0);
  g.write_ctrl(G::GFC, 0x1000);
  g.write_ctrl(G::BFC, 0);
  g.write_data(G::IR1, 0x1000);
  g.write_data(G::IR2, 0x800);
  g.write_data(G::IR3, 0);
  g.write_data(G::IR0, 0x800);
  g.execute(G::kIntpl);
  CHECK_EQ(g.mac(1), 0x800);
  CHECK_EQ(g.mac(2), 0xC00);
  CHECK_EQ(g.read_data(G::RGB2) & 0xFFFFFF, 0x00C080u);
}

TEST(gte_sqr_op_gpf_gpl) {
  G g;
  g.write_data(G::IR1, 3);
  g.write_data(G::IR2, static_cast<u32>(-4));
  g.write_data(G::IR3, 0x7FFF);
  g.execute(G::kSqr0);  // squares, unshifted; 0x7FFF^2 = 0x3FFF0001 saturates IR3 (bit 22)
  CHECK_EQ(g.mac(1), 9);
  CHECK_EQ(g.mac(2), 16);
  CHECK_EQ(g.mac(3), 0x3FFF0001);
  CHECK_EQ(g.ir(3), 0x7FFF);
  CHECK_EQ(g.flag(), G::kFlagIr3);

  // OP: D x IR with D = (RT11, RT22, RT33) = (1, 2, 3), IR = (4, 5, 6) -> (-3, 6, -3).
  g.write_ctrl(G::R11R12, 1);
  g.write_ctrl(G::R22R23, 2);
  g.write_ctrl(G::R33, 3);
  g.write_data(G::IR1, 4);
  g.write_data(G::IR2, 5);
  g.write_data(G::IR3, 6);
  g.execute(G::kOp0);
  CHECK_EQ(g.ir(1), -3);
  CHECK_EQ(g.ir(2), 6);
  CHECK_EQ(g.ir(3), -3);

  // GPF: MAC = IR * IR0 >> 12 = (0x800, 0x1000, -0x800); colour >> 4 saturates G
  // high (bit 20) and B low (bit 19).
  g.write_data(G::IR0, 0x800);
  g.write_data(G::IR1, 0x1000);
  g.write_data(G::IR2, 0x2000);
  g.write_data(G::IR3, static_cast<u32>(-0x1000));
  g.write_data(G::RGBC, 0x11000000);
  g.execute(G::kGpf12);
  CHECK_EQ(g.mac(3), -0x800);
  CHECK_EQ(g.read_data(G::RGB2), 0x1100FF80u);
  CHECK_EQ(g.flag(), G::kFlagColorG | G::kFlagColorB);
  // GPL adds onto MAC: MAC1 = (0x800 << 12 + 0x800 * 0x800) >> 12 = 0xC00.
  g.write_data(G::IR1, 0x800);
  g.execute(G::kGpl12);
  CHECK_EQ(g.mac(1), 0xC00);
}

// --- reference sweep ---------------------------------------------------------

namespace {

struct SweepCase {
  std::array<u32, 32> ctrl{}, data{};
  u32 command = 0;
};

// splitmix64; every draw is its own statement so the sequence does not depend
// on argument evaluation order.
struct Rng {
  u64 state;
  u64 next() {
    u64 z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }
  u32 bits() { return static_cast<u32>(next() >> 32); }
  u32 range(s32 lo, s32 hi) { return static_cast<u32>(lo + static_cast<s32>(next() % static_cast<u64>(hi - lo + 1))); }
  bool chance(int percent) { return static_cast<int>(next() % 100) < percent; }
};

// Tame: plausible game values. Mixed: some registers fully random. Wild: all
// random. Edge: translations, colours and MAC near +-2^31 with full-range
// matrices and vectors, so the 44-bit accumulators overflow.
enum class Mode { Tame, Mixed, Wild, Edge };

SweepCase make_case(Rng& rng, Mode mode, u32 command) {
  SweepCase c;
  c.command = command;
  auto pick = [&](u32 tame) {
    if (mode == Mode::Wild || (mode == Mode::Mixed && rng.chance(30))) return rng.bits();
    return tame;
  };
  auto pair = [&](s32 limit) {
    const u32 lo = rng.range(-limit, limit);
    const u32 hi = rng.range(-limit, limit);
    return (lo & 0xFFFF) | hi << 16;
  };
  auto mostly = [&](s32 lo, s32 hi, s32 rare_lo, s32 rare_hi) {
    return rng.chance(80) ? rng.range(lo, hi) : rng.range(rare_lo, rare_hi);
  };
  const s32 limit = rng.chance(30) ? 0x7FFF : 0x1000;
  for (int base : {G::R11R12, G::L11L12, G::LR1LR2}) {
    for (int k = 0; k < 4; ++k) {
      const u32 v = pair(limit);
      c.ctrl[base + k] = pick(v);
    }
    const u32 v = rng.range(-limit, limit) & 0xFFFF;
    c.ctrl[base + 4] = pick(v);
  }
  for (int r : {G::TRX, G::TRY}) {
    const u32 v = rng.range(-0x8000, 0x8000);
    c.ctrl[r] = pick(v);
  }
  {
    const u32 v = rng.range(-0x400, 0x8000);
    c.ctrl[G::TRZ] = pick(v);
  }
  for (int r : {G::RBK, G::GBK, G::BBK, G::RFC, G::GFC, G::BFC}) {
    const u32 v = mostly(0, 0x1000, -0x100000, 0x100000);
    c.ctrl[r] = pick(v);
  }
  for (int r : {G::OFX, G::OFY, G::DQB}) {
    const u32 v = rng.range(-0x2000000, 0x2000000);
    c.ctrl[r] = pick(v);
  }
  {
    const u32 v = mostly(1, 0x1000, 0, 0xFFFF);
    c.ctrl[G::H] = pick(v);
  }
  {
    const u32 v = rng.range(-0x8000, 0x7FFF);
    c.ctrl[G::DQA] = pick(v);
  }
  for (int r : {G::ZSF3, G::ZSF4}) {
    const u32 v = mostly(0, 0x7FFF, -0x8000, 0x7FFF);
    c.ctrl[r] = pick(v);
  }
  c.ctrl[G::FLAG] = rng.bits();

  for (int v = 0; v < 3; ++v) {
    const u32 vxy = pair(limit);
    c.data[G::VXY0 + 2 * v] = pick(vxy);
    const u32 vz = rng.range(-limit, limit);
    c.data[G::VZ0 + 2 * v] = pick(vz);
  }
  c.data[G::RGBC] = rng.bits();
  c.data[G::OTZ] = rng.bits();
  {
    const u32 v = mostly(0, 0x1000, -0x8000, 0x7FFF);
    c.data[G::IR0] = pick(v);
  }
  for (int r : {G::IR1, G::IR2, G::IR3}) {
    const u32 v = rng.chance(60) ? rng.range(-0x1000, 0x1000) : rng.range(-0x8000, 0x7FFF);
    c.data[r] = pick(v);
  }
  for (int r : {G::SXY0, G::SXY1, G::SXY2}) {
    const u32 lo = rng.range(-0x400, 0x3FF);
    const u32 hi = rng.range(-0x400, 0x3FF);
    c.data[r] = pick((lo & 0xFFFF) | hi << 16);
  }
  for (int r : {G::SZ0, G::SZ1, G::SZ2, G::SZ3}) {
    const u32 v = rng.range(0, 0xFFFF);
    c.data[r] = pick(v);
  }
  for (int r : {G::RGB0, G::RGB1, G::RGB2, G::RES1}) c.data[r] = rng.bits();
  for (int r : {G::MAC0, G::MAC1, G::MAC2, G::MAC3}) {
    if (rng.chance(70)) {
      const u32 v = rng.range(-0x100000, 0x100000);
      c.data[r] = pick(v);
    } else {
      c.data[r] = rng.bits();
    }
  }
  c.data[G::LZCS] = rng.bits();

  if (mode == Mode::Edge) {
    auto near_limit = [&] {
      const u32 off = rng.range(0, 0xFFFFF);
      return rng.chance(50) ? 0x7FFFFFFFu - off : 0x80000000u + off;
    };
    for (int r : {G::TRX, G::TRY, G::TRZ, G::RBK, G::GBK, G::BBK, G::RFC, G::GFC, G::BFC})
      if (rng.chance(70)) c.ctrl[r] = near_limit();
    for (int r : {G::MAC1, G::MAC2, G::MAC3})
      if (rng.chance(70)) c.data[r] = near_limit();
    for (int base : {G::R11R12, G::L11L12, G::LR1LR2})
      for (int k = 0; k < 5; ++k) c.ctrl[base + k] = rng.bits();
    for (int r = G::VXY0; r <= G::VZ2; ++r) c.data[r] = rng.bits();
    for (int r = G::IR0; r <= G::IR3; ++r) c.data[r] = rng.bits();
  }
  return c;
}

constexpr u32 kSweepOps[] = {0x01, 0x06, 0x0C, 0x10, 0x11, 0x13, 0x14, 0x16, 0x1B, 0x1C, 0x1E,
                             0x20, 0x28, 0x29, 0x2A, 0x2D, 0x2E, 0x30, 0x3D, 0x3E, 0x3F};

std::vector<SweepCase> sweep_cases() {
  Rng rng{0x5EED};
  std::vector<SweepCase> cases;
  constexpr Mode kModes[] = {Mode::Tame, Mode::Mixed, Mode::Wild, Mode::Edge};
  for (u32 op : kSweepOps)
    for (u32 sf = 0; sf < 2; ++sf)
      for (u32 lm = 0; lm < 2; ++lm)
        for (Mode mode : kModes)
          for (int n = 0; n < (mode == Mode::Tame ? 6 : 4); ++n)
            cases.push_back(make_case(rng, mode, op | sf << 19 | lm << 10));
  for (u32 mx = 0; mx < 4; ++mx)
    for (u32 v = 0; v < 4; ++v)
      for (u32 cv = 0; cv < 4; ++cv)
        for (u32 sf = 0; sf < 2; ++sf)
          for (u32 lm = 0; lm < 2; ++lm)
            for (Mode mode : kModes)
              cases.push_back(make_case(rng, mode, 0x12 | sf << 19 | mx << 17 | v << 15 | cv << 13 | lm << 10));
  // The divide around its overflow boundary: identity rotation, depth TRZ near H / 2.
  for (int n = 0; n < 200; ++n) {
    SweepCase c = make_case(rng, Mode::Tame, 0x01);
    const u32 h = rng.range(0, 0xFFFF);
    const u32 lm = rng.range(0, 1);
    c.ctrl[G::H] = h;
    c.ctrl[G::R11R12] = 0x1000;
    c.ctrl[G::R13R21] = 0;
    c.ctrl[G::R22R23] = 0x1000;
    c.ctrl[G::R31R32] = 0;
    c.ctrl[G::R33] = 0x1000;
    c.data[G::VZ0] = 0;
    if (rng.chance(50)) {
      const s32 d = static_cast<s32>(rng.range(-3, 3));
      c.ctrl[G::TRZ] = static_cast<u32>(std::max(0, static_cast<s32>(h / 2) + d));
    } else {
      c.ctrl[G::TRZ] = rng.range(0, 0xFFFF);
    }
    c.command = 0x01 | 1u << 19 | lm << 10;
    cases.push_back(c);
  }
  return cases;
}

// Loads a case the way the reference program did (ctc2 0-31, then mtc2 of the
// writable data registers in order, skipping SXYP, IRGB, ORGB and LZCR), runs it
// and reads all 64 registers back.
std::array<u32, 64> run_case(const SweepCase& c) {
  G g;
  for (int r = 0; r < 32; ++r) g.write_ctrl(r, c.ctrl[r]);
  for (int r = 0; r < 32; ++r)
    if (r != G::SXYP && r != G::IRGB && r != G::ORGB && r != G::LZCR) g.write_data(r, c.data[r]);
  g.execute(c.command);
  std::array<u32, 64> out{};
  for (int r = 0; r < 32; ++r) out[r] = g.read_data(r);
  for (int r = 0; r < 32; ++r) out[32 + r] = g.read_ctrl(r);
  return out;
}

// FNV-1a over the 64 registers of every case of one command (opcode & 0x3F).
u64 digest(const std::vector<SweepCase>& cases, u32 op) {
  u64 h = 0xCBF29CE484222325ull;
  for (const auto& c : cases) {
    if ((c.command & 0x3F) != op) continue;
    for (u32 w : run_case(c))
      for (int b = 0; b < 4; ++b) h = (h ^ (w >> (8 * b) & 0xFF)) * 0x100000001B3ull;
  }
  return h;
}

}  // namespace

TEST(gte_matches_redux_sweep) {
  // Digests of PCSX-Redux's own GTE output for the same 2736 register states.
  struct Expected {
    u32 op;
    u64 digest;
  };
  constexpr Expected kExpected[] = {
      {0x01, 0x6C20D31CC6D9ADF2ull},
      {0x06, 0x51E8EAD6237331F5ull},
      {0x0C, 0xFB0904EA4C802BB8ull},
      {0x10, 0xACCE3CF387060A9Full},
      {0x11, 0x71AD954933BC86DEull},
      {0x13, 0x032EA77A6698E09Eull},
      {0x14, 0x83AB1CB41F9916E7ull},
      {0x16, 0xFC09472D04138A1Cull},
      {0x1B, 0x0D1D8F37B60E302Bull},
      {0x1C, 0xDDE5888C1B4FB34Full},
      {0x1E, 0x96F9BE5519389507ull},
      {0x20, 0x12C8EC7D733DF708ull},
      {0x28, 0x0330ED57E9FA001Dull},
      {0x29, 0xBC65950223085DBAull},
      {0x2A, 0xDDB099D33A0F6373ull},
      {0x2D, 0x663D6D455A0C3243ull},
      {0x2E, 0x71B594DCE996DE0Eull},
      {0x30, 0xAC3FDFC255A0F326ull},
      {0x3D, 0x66EE86FC2545FAFEull},
      {0x3E, 0x1A2A8BD5A4E0B061ull},
      {0x3F, 0x49B688F47C01586Cull},
      {0x12, 0x775324875F54FFBFull},
  };
  const auto cases = sweep_cases();
  CHECK_EQ(cases.size(), size_t{2736});
  for (const auto& e : kExpected) {
    const u64 got = digest(cases, e.op);
    if (got != e.digest) std::fprintf(stderr, "  GTE command 0x%02X differs from the reference\n", e.op);
    CHECK(got == e.digest);
  }
}

// --- libgte ------------------------------------------------------------------

namespace {

psyq::MATRIX diagonal(s16 d, s32 tx = 0, s32 ty = 0, s32 tz = 0) {
  psyq::MATRIX m{};
  m.m[0][0] = m.m[1][1] = m.m[2][2] = d;
  m.t[0] = tx;
  m.t[1] = ty;
  m.t[2] = tz;
  return m;
}

bool same_rotation(const psyq::MATRIX& m, const std::array<s16, 9>& e) {
  for (int k = 0; k < 9; ++k)
    if (m.m[k / 3][k % 3] != e[k]) return false;
  return true;
}

}  // namespace

TEST(libgte_rot_matrix) {
  psyq::MATRIX m{};
  m.t[0] = 7;
  psyq::SVECTOR r{0, 0, 0, 0};
  psyq::RotMatrix(&r, &m);
  CHECK(same_rotation(m, {4096, 0, 0, 0, 4096, 0, 0, 0, 4096}));
  CHECK_EQ(m.t[0], 7);  // translation untouched
  r = {1024, 0, 0, 0};  // 90 degrees about X: y' = -z, z' = y
  psyq::RotMatrix(&r, &m);
  CHECK(same_rotation(m, {4096, 0, 0, 0, 0, -4096, 0, 4096, 0}));
  // 45 degrees about X and Y, sin = cos = round(4096 * 0.70711) = 2896. PsyQ computes
  // m12 = (-(cy * sx)) >> 12 = -8386816 >> 12 = -2048 but m22 = (cy * cx) >> 12 = 2047:
  // the shifts floor, so the order of negation and rounding shows in the low bits.
  // m10 = (sz*cx >> 12) - ((cz*-sy >> 12) * sx >> 12) = 0 - (-2896 * 2896 >> 12) = 2048.
  r = {512, 512, 0, 0};
  psyq::RotMatrix(&r, &m);
  CHECK(same_rotation(m, {2896, 0, 2896, 2048, 2896, -2048, -2048, 2896, 2047}));
  // Results of PsyQ 4.2's own RotMatrix code, run in PCSX-Redux.
  const std::pair<psyq::SVECTOR, std::array<s16, 9>> sony[] = {
      {{-2010, 4470, 6605, 0}, {-2616, 2234, 2223, 2755, 3023, 200, -1533, 1623, -3435}},
      {{-9173, -2048, -23, 0}, {-4093, -144, 0, -10, 269, -4087, 143, -4085, -270}},
      {{8134, -8395, -9120, 0}, {572, 3857, -1255, -4020, 708, 346, 543, 1184, 3883}},
  };
  for (const auto& [angles, expected] : sony) {
    psyq::RotMatrix(&angles, &m);
    CHECK(same_rotation(m, expected));
  }
}

TEST(libgte_matrix_products) {
  psyq::gte().reset();
  psyq::MATRIX rx{}, out{};
  const psyq::SVECTOR r{1024, 0, 0, 0};
  psyq::RotMatrix(&r, &rx);
  // Rx(90) * Rx(90) = Rx(180) = diag(1, -1, -1); as in PsyQ, m0 is left in RT.
  psyq::MulMatrix0(&rx, &rx, &out);
  CHECK(same_rotation(out, {4096, 0, 0, 0, -4096, 0, 0, 0, -4096}));
  CHECK_EQ(psyq::gte().read_ctrl(G::R22R23), 0xF0000000u);  // RT22 = 0, RT23 = -1.0
  // CompMatrix adds translation: Rx(90) * (100, 200, 300) + (10, 20, 30) = (110, -280, 230).
  rx.t[0] = 10;
  rx.t[1] = 20;
  rx.t[2] = 30;
  const psyq::MATRIX moved = diagonal(4096, 100, 200, 300);
  psyq::CompMatrix(&rx, &moved, &out);
  CHECK(same_rotation(out, {4096, 0, 0, 0, 0, -4096, 0, 4096, 0}));
  CHECK_EQ(out.t[0], 110);
  CHECK_EQ(out.t[1], -280);
  CHECK_EQ(out.t[2], 230);
  // Products go through IR, so they saturate: 2.0 * 7.9998 = 0xFFFE -> 0x7FFF.
  psyq::MATRIX a = diagonal(0x2000);
  const psyq::MATRIX b = diagonal(0x7FFF);
  psyq::MulMatrix(&a, &b);
  CHECK(same_rotation(a, {0x7FFF, 0, 0, 0, 0x7FFF, 0, 0, 0, 0x7FFF}));
}

TEST(libgte_apply_scale_trans) {
  psyq::gte().reset();
  const psyq::MATRIX twice = diagonal(0x2000);
  const psyq::SVECTOR v{0x6000, 1, -0x6000, 0};
  psyq::VECTOR lv{};
  psyq::SVECTOR sv{};
  psyq::ApplyMatrix(&twice, &v, &lv);  // MAC: (0xC000, 2, -0xC000)
  CHECK_EQ(lv.vx, 0xC000);
  CHECK_EQ(lv.vy, 2);
  CHECK_EQ(lv.vz, -0xC000);
  psyq::ApplyMatrixSV(&twice, &v, &sv);  // IR: saturated to (0x7FFF, 2, -0x8000)
  CHECK_EQ(sv.vx, 0x7FFF);
  CHECK_EQ(sv.vz, -0x8000);
  // ApplyMatrixLV splits each 32-bit component by magnitude, x = hi * 0x8000 + lo.
  // m = 0.5, x = 100001 = 3 * 0x8000 + 1697: 0x800 * 3 unshifted = 6144, * 8 = 49152, plus
  // (0x800 * 1697) >> 12 = 848: 50000. For -100001 both parts are negative and the low part
  // floors to -849: -50001.
  const psyq::MATRIX half = diagonal(0x800);
  const psyq::VECTOR big{100001, -100001, 0x12345678, 0};
  psyq::ApplyMatrixLV(&half, &big, &lv);
  CHECK_EQ(lv.vx, 50000);
  CHECK_EQ(lv.vy, -50001);
  CHECK_EQ(lv.vz, 0x12345678 / 2);
  // ScaleMatrix scales column j by v[j] / 4096.
  psyq::MATRIX m = diagonal(4096);
  m.m[0][1] = m.m[0][2] = 4096;
  const psyq::VECTOR scale{2048, 4096, 8192, 0};
  psyq::ScaleMatrix(&m, &scale);
  CHECK(same_rotation(m, {2048, 4096, 8192, 0, 4096, 0, 0, 0, 8192}));
  const psyq::VECTOR t{-1, 2, 3, 0};
  psyq::TransMatrix(&m, &t);
  CHECK_EQ(m.t[0], -1);
  CHECK_EQ(m.t[2], 3);
}

TEST(libgte_perspective_helpers) {
  psyq::gte().reset();
  psyq::InitGeom();  // H = 1000, ZSF3 = 0x155, ZSF4 = 0x100, DQA = -0x1062, DQB = 0x1400000
  const psyq::MATRIX camera = diagonal(4096, 0, 0, 1000);
  psyq::SetRotMatrix(&camera);
  psyq::SetTransMatrix(&camera);
  psyq::SetGeomOffset(160, 120);
  CHECK_EQ(psyq::ReadGeomScreen(), 1000);
  const psyq::SVECTOR v{100, -50, 0, 0};
  s32 sxy = 0, p = 0, flag = 0;
  // z = 1000 = H, scale 1.0: (100 + 160, -50 + 120); returns SZ3 / 4.
  CHECK_EQ(psyq::RotTransPers(&v, &sxy, &p, &flag), 250);
  CHECK_EQ(static_cast<u32>(sxy), xy(260, 70));
  // IR0 = (DQA * 0x10000 + DQB) >> 12 is negative, so it saturates to 0: FLAG bit 12,
  // which does not raise the error bit.
  CHECK_EQ(p, 0);
  CHECK_EQ(flag, 0x1000);
  psyq::VECTOR moved{};
  psyq::RotTrans(&v, &moved, &flag);
  CHECK_EQ(moved.vz, 1000);
  CHECK_EQ(flag, 0);

  const psyq::SVECTOR a{0, 0, 0, 0}, b{0, 0, 1000, 0}, c{0, 0, 2000, 0}, d{0, 0, 3000, 0};
  s32 s0 = 0, s1 = 0, s2 = 0, s3 = 0;
  CHECK_EQ(psyq::RotTransPers3(&a, &b, &c, &s0, &s1, &s2, &p, &flag), 3000 / 4);
  // RotAverage3: OTZ = 0x155 * (1000 + 2000 + 3000) >> 12 = 2046000 >> 12 = 499; the flag
  // is RTPT's (IR0 saturated), read before AVSZ3 clears FLAG.
  CHECK_EQ(psyq::RotAverage3(&a, &b, &c, &s0, &s1, &s2, &p, &flag), 499);
  CHECK_EQ(flag, 0x1000);
  CHECK_EQ(psyq::gte().flag(), 0u);
  // RotAverage4: 0x100 * (1000 + 2000 + 3000 + 4000) >> 12 = 625.
  CHECK_EQ(psyq::RotAverage4(&a, &b, &c, &d, &s0, &s1, &s2, &s3, &p, &flag), 625);
  CHECK_EQ(psyq::NormalClip(static_cast<s32>(xy(0, 0)), static_cast<s32>(xy(10, 0)), static_cast<s32>(xy(0, 10))),
           100);
  CHECK_EQ(psyq::AverageZ3(100, 200, 300), 49);       // 341 * 600 >> 12
  CHECK_EQ(psyq::AverageZ4(100, 200, 300, 400), 62);  // 256 * 1000 >> 12
}

TEST(libgte_lighting_helpers) {
  psyq::gte().reset();
  // The scene of gte_ncds_colour_pipeline, set up through the library.
  psyq::MATRIX light{}, colour{};
  light.m[0][2] = -4096;
  colour.m[0][0] = 4096;
  colour.m[1][0] = 2048;
  colour.m[2][0] = 1024;
  psyq::SetLightMatrix(&light);
  psyq::SetColorMatrix(&colour);
  psyq::SetBackColor(16, 16, 16);  // BK = 16 << 4
  psyq::SetFarColor(255, 0, 0);    // FC = 255 << 4
  const psyq::SVECTOR normal{0, 0, -4096, 0};
  const psyq::CVECTOR grey{128, 128, 128, 0x30};
  psyq::CVECTOR out{};
  psyq::NormalColorDpq(&normal, &grey, 0x800, &out);
  CHECK(out.r == 195 && out.g == 36 && out.b == 20 && out.cd == 0x30);
  psyq::NormalColorCol(&normal, &grey, &out);
  CHECK(out.r == 136 && out.g == 72 && out.b == 40 && out.cd == 0x30);
}

TEST(libgte_fog_and_matrix_stack) {
  psyq::gte().reset();
  // SetFogNearFar(1000, 5000, 1000): DQA = ((-1000 * 5000 / 4000) << 8) / 1000 = -320,
  // DQB = ((5000 << 12) / 4000) << 12 = 0x1400000, so IR0 is 0 at z = 1000 (scale 1.0).
  psyq::SetFogNearFar(1000, 5000, 1000);
  CHECK_EQ(static_cast<s32>(psyq::gte().read_ctrl(G::DQA)), -320);
  CHECK_EQ(psyq::gte().read_ctrl(G::DQB), 0x1400000u);
  psyq::SetFogNearFar(0, 99, 1000);  // a range under 100 changes nothing
  CHECK_EQ(static_cast<s32>(psyq::gte().read_ctrl(G::DQA)), -320);
  psyq::SetFogNear(2000, 1000);  // DQA = -320 * 2000 / 1000
  CHECK_EQ(static_cast<s32>(psyq::gte().read_ctrl(G::DQA)), -640);

  // PushMatrix / PopMatrix save and restore RT and TR.
  const psyq::MATRIX first = diagonal(4096, 10, 20, 30), second = diagonal(1, -1, -2, -3);
  psyq::SetRotMatrix(&first);
  psyq::SetTransMatrix(&first);
  psyq::PushMatrix();
  psyq::SetRotMatrix(&second);
  psyq::SetTransMatrix(&second);
  CHECK_EQ(psyq::gte().read_ctrl(G::R33), 1u);
  psyq::PopMatrix();
  CHECK_EQ(psyq::gte().read_ctrl(G::R33), 4096u);
  CHECK_EQ(psyq::gte().read_ctrl(G::TRY), 20u);
}
