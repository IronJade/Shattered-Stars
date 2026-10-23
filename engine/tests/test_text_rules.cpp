// The per-language text rules (game/common/text_rules): glyph numbers, the ASCII and number
// conversions, the line layout's word wrap on synthetic fonts, the name keyboards, the text
// VRAM allocator's choice and the moved layouts. No disc needed; ss_game_tests checks the NA
// layout against the ported field code on the disc's own text.

#include <array>
#include <string>
#include <vector>

#include "game/common/text_rules.h"
#include "runtime/region.h"
#include "test.h"

using namespace ss;
namespace text = ss::game::text;
using text::Lang;

namespace {

// A font of fixed widths (code -> width) with no names, items, numbers or ruby.
struct FakeHost : text::LayoutHost {
  std::array<u8, 0x200> widths{};
  explicit FakeHost(u8 w = 6) { widths.fill(w); }
  int glyph_width(u32 c) override { return c < widths.size() ? widths[c] : 0; }
  s32 message_variable(int) override { return 0; }
  int char_name_width(int, s32* n) override {
    *n = 0;
    return 0;
  }
  int item_name_width(int, s32* n) override {
    *n = 0;
    return 0;
  }
  int number_width(int, s32* n) override {
    *n = 0;
    return 0;
  }
  int measure_block(const u8**) override { return 0; }
  int fixed_space_width(int) override { return 0; }
};

void put(std::vector<u8>& out, u32 c) {
  if (c < 0x80) {
    out.push_back(static_cast<u8>(c));
  } else {
    out.push_back(static_cast<u8>((c & 0x7F) | 0x80));
    out.push_back(static_cast<u8>(c >> 7));
  }
}

// An ASCII string in a language's map-font codes; each '#' takes the next code of 'raw' as it is.
std::vector<u8> encode(Lang lang, const std::string& ascii, std::vector<u32> raw = {}) {
  std::vector<u8> out;
  size_t r = 0;
  for (char ch : ascii) {
    if (ch == '#' && r < raw.size()) {
      put(out, raw[r++]);
      continue;
    }
    s32 c = static_cast<u8>(ch);
    text::ascii_to_glyph(lang, &c, 0, 0);
    put(out, static_cast<u32>(c));
  }
  out.push_back(0);
  return out;
}

struct Line {
  int glyphs, width, height;
};
Line lay(Lang lang, FakeHost& host, const std::vector<u8>& s, int limit, bool continuing = false) {
  text::LayoutWindow w;
  w.w = limit + 0x10;
  w.continuing = continuing;
  std::array<s32, 64> out{};
  const int h = text::layout_line(lang, w, host, s.data(), out.data());
  return {out[0], out[1], h};
}

}  // namespace

TEST(text_rules_profiles_and_glyphs) {
  CHECK(region::profile(region::Id::kNorthAmerica).text == Lang::kNorthAmerica);
  CHECK(region::profile(region::Id::kEurope).text == Lang::kNorthAmerica);  // Europe kept NA's text
  CHECK(region::profile(region::Id::kFrance).text == Lang::kFrench);
  CHECK(region::profile(region::Id::kGermany).text == Lang::kGerman);
  const text::MapGlyphs& na = text::map_glyphs(Lang::kNorthAmerica);
  CHECK_EQ(na.digit0, 0xAA);
  CHECK_EQ(na.minus, 0xB4);
  CHECK_EQ(na.space, 0x105);
  CHECK_EQ(na.layout_skip, 0x107);
  for (Lang l : {Lang::kFrench, Lang::kGerman}) {
    const text::MapGlyphs& g = text::map_glyphs(l);
    CHECK_EQ(g.digit0, 1);
    CHECK_EQ(g.minus, 0x0B);
    CHECK_EQ(g.space, 0x72);
    CHECK_EQ(g.layout_skip, g.space + 2);  // the original's off-by-two, kept in every release
    CHECK_EQ(g.push_first, 0x69);
    CHECK_EQ(g.push_count, 4);
  }
}

TEST(text_rules_ascii_to_glyph) {
  struct Case {
    int ascii, na, latin;
  };
  const Case cases[] = {{'0', 0xAA, 0x01}, {'9', 0xB3, 0x0A}, {'A', 0xB6, 0x0D}, {'Z', 0xCF, 0x26},
                        {'a', 0xD0, 0x28}, {'z', 0xE9, 0x41}, {' ', 0xFE, 0x72}, {'!', 0xEF, 0x64},
                        {'?', 0xED, 0x63}, {',', 0x103, 0x70}, {'.', 0xB5, 0x0C}, {'/', 0x104, 0x71},
                        {':', 0x100, 0x6E}, {';', 0x101, 0x6F}, {'=', 0xFF, 0x6D},
                        // unmapped: NA 0xfe, FR / DE the byte itself
                        {'-', 0xFE, '-'}, {'\'', 0xFE, '\''}, {0xE9, 0xFE, 0xE9}, {0x7B, 0xFE, 0x7B}};
  for (const Case& k : cases) {
    for (Lang l : {Lang::kNorthAmerica, Lang::kFrench, Lang::kGerman}) {
      s32 c = k.ascii;
      CHECK_EQ(text::ascii_to_glyph(l, &c, 0, 0), 0);
      CHECK_EQ(c, l == Lang::kNorthAmerica ? k.na : k.latin);
    }
  }
  // NA's two extra punctuation bytes.
  s32 c = 0xA4;
  text::ascii_to_glyph(Lang::kNorthAmerica, &c, 0, 0);
  CHECK_EQ(c, 0x102);
  // The half-width kana path is the same everywhere, voiced marks included.
  for (int b = 0xA6; b < 0xE0; ++b) {
    for (int next : {0, 0xDE, 0xDF}) {
      for (int kana : {0, 1}) {
        s32 a = b, f = b, d = b;
        const int ra = text::ascii_to_glyph(Lang::kNorthAmerica, &a, next, kana);
        const int rf = text::ascii_to_glyph(Lang::kFrench, &f, next, kana);
        const int rd = text::ascii_to_glyph(Lang::kGerman, &d, next, kana);
        CHECK(a == f && f == d && ra == rf && rf == rd);
      }
    }
  }
  s32 ka = 0xB6;  // ka + the voiced mark: ga, and one byte consumed
  CHECK_EQ(text::ascii_to_glyph(Lang::kFrench, &ka, 0xDE, 1), 1);
}

TEST(text_rules_numbers) {
  u8 out[16];
  CHECK_EQ(text::format_number(Lang::kFrench, -105, out), 4);
  CHECK(out[0] == 0x0B && out[1] == 0x02 && out[2] == 0x01 && out[3] == 0x06 && out[4] == 0);
  CHECK_EQ(text::format_number(Lang::kNorthAmerica, 70, out), 2);
  CHECK(out[0] == 0xB1 && out[1] == 0xAA && out[2] == 0);
  CHECK_EQ(text::format_number(Lang::kGerman, 0, out), 1);
  CHECK(out[0] == 0x01 && out[1] == 0);
  // Widths come from font[code + 7].
  std::vector<u8> font(0x200, 0);
  for (int d = 0; d < 10; ++d) font[7 + 1 + d] = static_cast<u8>(4 + d);  // FR / DE digits
  font[7 + 0x0B] = 5;                                                      // '-'
  for (int d = 0; d < 10; ++d) font[7 + 0xAA + d] = 6;                     // NA digits
  font[7 + 0xB4] = 3;
  s32 n = 0;
  CHECK_EQ(text::number_width(Lang::kFrench, font.data(), -105, &n), (5 + 1) + (9 + 1) + (4 + 1) + (5 + 1));
  CHECK_EQ(n, 4);
  CHECK_EQ(text::number_width(Lang::kNorthAmerica, font.data(), -42, &n), (3 + 1) + 2 * (6 + 1));
  CHECK_EQ(n, 3);
}

TEST(text_rules_wrap_classes) {
  // FR / DE: digits 1-10, letters 0x0d-0x61 (DE to 0x62) and 0x73, punctuation 0x63-0x6c / 0x0b-0x0c.
  CHECK_EQ(text::wrap_class(Lang::kFrench, 0x01, 0), 2);
  CHECK_EQ(text::wrap_class(Lang::kFrench, 0x0A, 0), 2);
  CHECK_EQ(text::wrap_class(Lang::kFrench, 0x0B, 0), -1);
  CHECK_EQ(text::wrap_class(Lang::kFrench, 0x0D, 0), 3);
  CHECK_EQ(text::wrap_class(Lang::kFrench, 0x61, 0), 3);
  CHECK_EQ(text::wrap_class(Lang::kFrench, 0x62, 0), 0);
  CHECK_EQ(text::wrap_class(Lang::kGerman, 0x62, 0), 3);
  CHECK_EQ(text::wrap_class(Lang::kFrench, 0x73, 0), 3);
  CHECK_EQ(text::wrap_class(Lang::kFrench, 0x63, 0), -1);
  CHECK_EQ(text::wrap_class(Lang::kFrench, 0x6C, 0), -1);
  CHECK_EQ(text::wrap_class(Lang::kFrench, 0x72, 0x28), 0);   // a space: a break opportunity
  CHECK_EQ(text::wrap_class(Lang::kFrench, 0x72, 0x64), -1);  // ... unless '!' / '?' follows
  CHECK_EQ(text::wrap_class(Lang::kFrench, 0x72, 0x63), -1);
  CHECK_EQ(text::wrap_class(Lang::kFrench, 0x400C, 0), 5);
  CHECK_EQ(text::wrap_class(Lang::kFrench, 0x4007, 0), -1);
  // NA: kana, digits, letters, codes >= 0x107.
  CHECK_EQ(text::wrap_class(Lang::kNorthAmerica, 0x55, 0), 1);
  CHECK_EQ(text::wrap_class(Lang::kNorthAmerica, 0xAA, 0), 2);
  CHECK_EQ(text::wrap_class(Lang::kNorthAmerica, 0xB6, 0), 3);
  CHECK_EQ(text::wrap_class(Lang::kNorthAmerica, 0x106, 0), 3);
  CHECK_EQ(text::wrap_class(Lang::kNorthAmerica, 0xEF, 0), -1);
  CHECK_EQ(text::wrap_class(Lang::kNorthAmerica, 0x105, 0), 0);
  CHECK_EQ(text::wrap_class(Lang::kNorthAmerica, 0x107, 0), 4);
}

TEST(text_rules_layout_french_word_wrap) {
  FakeHost host;            // every glyph 6 wide (7 with its gap)
  host.widths[0x72] = 4;    // the space (5)
  host.widths[0x64] = 3;    // '!' (4)
  // "abc def" in 40 pixels: 'f' overflows; back to the start of "def".
  Line l = lay(Lang::kFrench, host, encode(Lang::kFrench, "abc def"), 40);
  CHECK_EQ(l.glyphs, 4);
  CHECK_EQ(l.width, 33);  // the break's width includes the next word's first glyph, as in the original
  CHECK_EQ(l.height, 13);
  // "ab cd !": the space before '!' is no break and '!' cannot start a line, so "cd !" moves.
  l = lay(Lang::kFrench, host, encode(Lang::kFrench, "ab cd !"), 40);
  CHECK_EQ(l.glyphs, 3);
  CHECK_EQ(l.width, 26);
  // NA's rules keep "ab cd " (its space is always a break) and push '!' alone.
  host.widths[0x105] = 4;
  host.widths[0xEF] = 3;
  l = lay(Lang::kNorthAmerica, host, encode(Lang::kNorthAmerica, "ab#cd##", {0x105, 0x105, 0xEF}), 40);
  CHECK_EQ(l.glyphs, 5);
  CHECK_EQ(l.width, 38);
  // Everything fits.
  l = lay(Lang::kFrench, host, encode(Lang::kFrench, "ab cd !"), 100);
  CHECK_EQ(l.glyphs, 7);
  CHECK_EQ(l.width, 7 * 4 + 5 * 2 + 4);
}

TEST(text_rules_layout_apostrophe_digits_and_umlaut) {
  FakeHost host;
  host.widths[0x72] = 4;
  // "xy l'a": the apostrophe-like 0x73 never starts a line (overflow on it rewinds).
  const std::vector<u8> s = encode(Lang::kFrench, "xy l#a", {0x73});
  Line l = lay(Lang::kFrench, host, s, 32);  // overflows on 0x73 (x 33 > 32)
  CHECK_EQ(l.glyphs, 3);                    // back before "l'a"
  // Digits: "12 345" breaks before the number, not inside it.
  l = lay(Lang::kFrench, host, encode(Lang::kFrench, "12 345"), 30);
  CHECK_EQ(l.glyphs, 3);
  // German counts glyph 0x62 as a letter; French treats it as "other" (a break before it).
  const std::vector<u8> w = encode(Lang::kFrench, "x a#b", {0x62});
  const Line fr = lay(Lang::kFrench, host, w, 30);
  const Line de = lay(Lang::kGerman, host, w, 30);
  CHECK_EQ(fr.glyphs, 4);
  CHECK_EQ(fr.width, 26);
  CHECK_EQ(de.glyphs, 2);
  CHECK_EQ(de.width, 19);
}

TEST(text_rules_layout_continuing_line) {
  FakeHost host;
  host.widths[0x74] = 7;
  host.widths[0x72] = 4;
  // A wrapped line's leading glyph 0x74 (the layout's skip glyph) costs nothing ...
  Line l = lay(Lang::kFrench, host, encode(Lang::kFrench, "#a", {0x74}), 100, true);
  CHECK_EQ(l.glyphs, 2);
  CHECK_EQ(l.width, 7);
  // ... the real space (0x72) does (the renderer drops it, the layout measures it).
  l = lay(Lang::kFrench, host, encode(Lang::kFrench, " a"), 100, true);
  CHECK_EQ(l.glyphs, 2);
  CHECK_EQ(l.width, 5 + 7);
  // NA: the same with 0x107.
  host.widths[0x107] = 9;
  std::vector<u8> na;
  put(na, 0x107);
  put(na, 0xD0);
  na.push_back(0);
  l = lay(Lang::kNorthAmerica, host, na, 100, true);
  CHECK_EQ(l.width, 7);
  l = lay(Lang::kNorthAmerica, host, na, 100, false);
  CHECK_EQ(l.width, 10 + 7);
}

TEST(text_rules_layout_na_only_rules) {
  FakeHost host;
  // Glyph 1 ends an NA line that has glyphs (its width is counted); in FR / DE glyph 1 is '0'.
  std::vector<u8> s;
  put(s, 0xB6);
  put(s, 1);
  put(s, 0xB7);
  s.push_back(0);
  Line l = lay(Lang::kNorthAmerica, host, s, 200);
  CHECK_EQ(l.glyphs, 1);
  CHECK_EQ(l.width, 14);
  l = lay(Lang::kFrench, host, s, 200);
  CHECK_EQ(l.glyphs, 3);
  // NA squeezes glyphs 2 / 3 into the last two pixels; FR / DE do not.
  std::vector<u8> q;
  for (int i = 0; i < 4; ++i) put(q, 0xB6);
  put(q, 2);
  q.push_back(0);
  host.widths[2] = 3;
  l = lay(Lang::kNorthAmerica, host, q, 4 * 7 + 2);  // glyph 2 (4) overflows by 2
  CHECK_EQ(l.glyphs, 5);
  CHECK_EQ(l.width, 4 * 7 + 4);
  std::vector<u8> q2;
  for (int i = 0; i < 4; ++i) put(q2, 0x28);
  put(q2, 2);
  q2.push_back(0);
  l = lay(Lang::kFrench, host, q2, 4 * 7 + 2);
  CHECK_EQ(l.glyphs, 4);
}

TEST(text_rules_layout_controls) {
  FakeHost host;
  // A new-line code ends the line; 0x4005 centres; 0x4003 / 0x4004 take a byte.
  std::vector<u8> s;
  put(s, 0x4005);
  put(s, 0x28);
  put(s, 0x4004);
  s.push_back(2);
  put(s, 0x29);
  put(s, 0x4000);
  put(s, 0x2A);
  s.push_back(0);
  text::LayoutWindow w;
  w.w = 200;
  std::array<s32, 64> out{};
  CHECK_EQ(text::layout_line(Lang::kFrench, w, host, s.data(), out.data()), 13);
  CHECK_EQ(out[0], 2);
  CHECK_EQ(out[1], 14);
  CHECK_EQ(out[4], 1);
  CHECK_EQ(out[6], 1);
  // No word wrap: the line just stops at the glyph that does not fit.
  w.w = 0x10 + 20;
  w.word_wrap = false;
  const std::vector<u8> t = encode(Lang::kFrench, "ab cd");
  CHECK_EQ(text::layout_line(Lang::kGerman, w, host, t.data(), out.data()), 13);
  CHECK_EQ(out[0], 2);  // the space (7 here) is the glyph that does not fit
  CHECK_EQ(out[1], 14);
}

TEST(text_rules_name_keyboards) {
  const text::NameKeyboard& na = text::name_keyboard(Lang::kNorthAmerica);
  CHECK_EQ(na.buttons, 5);
  CHECK_EQ(na.button_step, 0x2A);
  CHECK_EQ(na.first_column, 2);
  CHECK(na.charset_switch);
  CHECK(text::keyboard_button(Lang::kNorthAmerica, na.first_column) == text::KeyAction::kOk);
  CHECK(text::keyboard_button(Lang::kNorthAmerica, 0) == text::KeyAction::kCapital);
  CHECK(text::keyboard_button(Lang::kNorthAmerica, 4) == text::KeyAction::kReturn);
  for (Lang l : {Lang::kFrench, Lang::kGerman}) {
    const text::NameKeyboard& k = text::name_keyboard(l);
    CHECK_EQ(k.buttons, 3);
    CHECK_EQ(k.button_step, 0x4C);
    CHECK_EQ(k.first_column, 1);
    CHECK(!k.charset_switch);
    CHECK(text::keyboard_button(l, k.ok_column) == text::KeyAction::kOk);
    CHECK(text::keyboard_button(l, 1) == text::KeyAction::kDelete);
    CHECK(text::keyboard_button(l, 2) == text::KeyAction::kReturn);
    CHECK(text::keyboard_button(l, 3) == text::KeyAction::kNone);
  }
  CHECK_EQ(text::ascii_high_bias(Lang::kNorthAmerica, text::Program::kTitle), -1);
  CHECK_EQ(text::ascii_high_bias(Lang::kFrench, text::Program::kTitle), 4);
  CHECK_EQ(text::ascii_high_bias(Lang::kGerman, text::Program::kTitle), -1);
  CHECK_EQ(text::ascii_high_bias(Lang::kFrench, text::Program::kBattle), 6);
  CHECK_EQ(text::ascii_high_bias(Lang::kGerman, text::Program::kBattle), 0xC);
  CHECK_EQ(text::ascii_high_bias(Lang::kGerman, text::Program::kRename), -1);
}

TEST(text_rules_cvram_choice) {
  // Free blocks (w, h): NA takes the lowest one that fits (first on ties); FR / DE the last
  // block no wider and no taller than the best so far.
  const text::FreeBlock blocks[] = {{true, 0x3F, 0x40}, {true, 0x10, 0x7F}, {false, 0x3F, 0x10},
                                    {true, 0x20, 0x40}, {true, 0x30, 0x20}, {true, 0x08, 0x08}};
  CHECK_EQ(text::cvram_choose(Lang::kNorthAmerica, blocks, 0x10, 0x10), 5);
  CHECK_EQ(text::cvram_choose(Lang::kFrench, blocks, 0x10, 0x10), 4);
  CHECK_EQ(text::cvram_choose(Lang::kGerman, blocks, 0x10, 0x10), 4);
  CHECK_EQ(text::cvram_choose(Lang::kNorthAmerica, blocks, 0x40, 0x10), 0);
  CHECK_EQ(text::cvram_choose(Lang::kFrench, blocks, 0x3F, 0x40), 1);
}

TEST(text_rules_layouts) {
  const text::Layout& na = text::layout(Lang::kNorthAmerica);
  const text::Layout& fr = text::layout(Lang::kFrench);
  const text::Layout& de = text::layout(Lang::kGerman);
  CHECK(&text::layout(Lang::kJapanese) == &na);  // not supported yet: NA's
  CHECK_EQ(na.check_dialog_w, 0xC8);
  CHECK_EQ(fr.check_dialog_w, 0x100);
  CHECK_EQ(de.check_dialog_w, 0x100);
  CHECK(!na.result_prefix_order && fr.result_prefix_order && !de.result_prefix_order);
  CHECK(na.scan_first_page == 0 && fr.scan_first_page == 0 && de.scan_first_page == 1);
  CHECK(fr.skills_heading_message && de.skills_heading_message && !na.skills_heading_message);
  CHECK(na.save_slot_x == fr.save_slot_x && de.save_slot_x == 0x88);  // German only
  CHECK(na.vibration_heading_x == de.vibration_heading_x && fr.vibration_heading_x == 0x30);
  CHECK_EQ(de.sound_lines_stereo, 6);
  CHECK(fr.end_logo3_x1 == 0x205 && de.end_logo3_x1 == 0x235 && na.end_logo3_x1 == 0x26C);
}
