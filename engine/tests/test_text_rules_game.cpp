// The per-language text rules (game/common/text_rules) on the discs' own text.
//
// - NA (SS_DISC1): layout_line(kNorthAmerica) gives the same lines as the ported field
//   text_layout_line for every string of a sample of maps, from every code boundary, at
//   several window widths, continuing or not (names, numbers, items, ruby included).
// - France / Germany (SS_DISC1_FR / SS_DISC1_DE, skipped when unset): the fonts are the
//   132-glyph Latin layout the rules assume, and every string lays out without running away.
// - SS_TEXT_REDUX_LOG=<file> (with the disc of that language): lines the original laid out in
//   PCSX-Redux (scratchpad/regions_text/redux, the text_layout_line arguments and results)
//   replayed through layout_line; every line must match.

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "disc/archive.h"
#include "disc/disc_image.h"
#include "disc/slz.h"
#include "game/boot/pad.h"
#include "game/boot/system.h"
#include "game/common/text_rules.h"
#include "game/field/field.h"
#include "game/field/message.h"
#include "game/title/title_program.h"
#include "test.h"

using namespace ss;
namespace text = ss::game::text;
using text::Lang;

namespace {

u32 rd32(const u8* p) {
  u32 v;
  std::memcpy(&v, p, 4);
  return v;
}
u16 rd16(const u8* p) {
  u16 v;
  std::memcpy(&v, p, 2);
  return v;
}

// One map's section 1: its script (strings) and font.
struct MapText {
  std::vector<u8> script, font;
  u32 count() const { return rd32(script.data() + 0xC); }
  const u8* string(u32 i) const {
    const u32 code = rd32(script.data());
    const u8* table = script.data() + 0x1C + code;
    return table + count() * 2 + rd16(table + i * 2);
  }
  bool valid(u32 i) const {
    const u32 code = rd32(script.data());
    const size_t at = 0x1C + code + count() * 2 + rd16(script.data() + 0x1C + code + i * 2);
    return at < script.size();
  }
};

std::optional<MapText> map_text(const std::vector<u8>& entry) {
  if (entry.size() < 8) return std::nullopt;
  const u32 sections = rd32(entry.data());
  if (sections == 0 || sections > 64) return std::nullopt;
  u32 offset = 0;
  bool found = false;
  for (u32 i = 0; i < sections; ++i) {
    if (rd32(entry.data() + 4 + i * 8) == 1) {
      offset = rd32(entry.data() + 8 + i * 8);
      found = true;
    }
  }
  if (!found || offset >= entry.size()) return std::nullopt;
  auto script = disc::slz::unpack_block(entry, offset, 0, nullptr);
  auto font = disc::slz::unpack_block(entry, offset, 1, nullptr);
  if (!script || !font || script->size() < 0x20 || font->size() < 16) return std::nullopt;
  MapText m{std::move(*script), std::move(*font)};
  if (0x1C + rd32(m.script.data()) + m.count() * 2 > m.script.size()) return std::nullopt;
  return m;
}

// A disc of another release, opened on its own (the game keeps running on SS_DISC1).
struct OtherDisc {
  std::unique_ptr<disc::DiscImage> image;
  disc::Archive archive;
  std::vector<u8> entry(int n) {
    auto e = archive.read_entry(static_cast<u32>(n));
    return e ? std::move(*e) : std::vector<u8>{};
  }
};
OtherDisc* other_disc(const char* env) {
  const char* path = std::getenv(env);
  if (!path || !*path) return nullptr;
  auto* d = new OtherDisc;
  std::string error;
  d->image = disc::DiscImage::open(path, &error);
  if (!d->image || !d->archive.open(*d->image, &error)) {
    std::fprintf(stderr, "  cannot open %s: %s\n", path, error.c_str());
    ++test::failures;
    delete d;
    return nullptr;
  }
  return d;
}

// The field's message state as the field program leaves it (as test_field_message.cpp).
bool field_ready() {
  if (game::get_system() == nullptr || game::get_system()->loader.archive() == nullptr) {
    static std::unique_ptr<disc::DiscImage> image;
    static disc::Archive archive;
    const char* path = std::getenv("SS_DISC1");
    if (!path || !*path) {
      std::printf("  (skipped: SS_DISC1 not set)\n");
      return false;
    }
    std::string error;
    image = disc::DiscImage::open(path, &error);
    if (!image || !archive.open(*image, &error)) {
      std::fprintf(stderr, "  cannot open %s: %s\n", path, error.c_str());
      ++test::failures;
      return false;
    }
    game::boot_main(0, &archive);
    game::pad_set_port_mask(&game::get_system()->pad, 3);
  }
  game::create_game_state();
  game::field::field_bind_system(nullptr);
  if (game::handoff_get(game::field::globals().handoff, 9) == nullptr) game::field::field_create_state_block();
  game::field::field_bind_state_block(nullptr);
  return true;
}

// LayoutHost over the ported field functions and a font.
struct FieldHost : text::LayoutHost {
  game::field::MsgWindow* w;
  const u8* font;
  int glyph_width(u32 c) override { return font[c + 7]; }
  s32 message_variable(int var) override {
    return static_cast<s32>(rd32(game::field::globals().script_globals + 0x38 + var * 4));
  }
  int char_name_width(int ch, s32* n) override { return game::field::text_char_name_width(ch, n); }
  int item_name_width(int var, s32* n) override { return game::field::text_item_name_width(w, var, n); }
  int number_width(int var, s32* n) override { return game::field::text_number_width(w, var, n); }
  int measure_block(const u8** p) override { return game::field::text_measure_block(w, p); }
  int fixed_space_width(int i) override { return game::field::field_data(0x80076318)[i]; }
};

// A host for another release's text: its font, character names from the log or defaults,
// message variables from the log (no item names: the 0xfc2 strings are not loaded).
struct LatinHost : text::LayoutHost {
  Lang lang = Lang::kFrench;
  const u8* font = nullptr;
  std::array<std::string, 16> names{"Claude", "Rena", "Celine", "Bowman", "Ashton", "Precis", "Opera",
                                    "Ernest", "Leon", "Noel", "Dias", "Chisato", "", "", "", ""};
  std::array<s32, 16> vars{};
  int glyph_width(u32 c) override { return font[c + 7]; }
  s32 message_variable(int var) override { return vars[static_cast<size_t>(var & 15)]; }
  int char_name_width(int ch, s32* count) override {
    const std::string& s = names[static_cast<size_t>(ch & 15)];
    int width = 0, n = 0, kana = 1;
    for (size_t i = 0; i < s.size(); ++i) {
      if (s[i] == '@') {
        kana ^= 1;
        continue;
      }
      ++n;
      s32 g = static_cast<u8>(s[i]);
      i += static_cast<size_t>(text::ascii_to_glyph(lang, &g, i + 1 < s.size() ? static_cast<u8>(s[i + 1]) : 0, kana));
      width += 1 + font[g + 7];
    }
    if (count) *count = n;
    return width;
  }
  int item_name_width(int, s32* count) override {
    if (count) *count = 0;
    return 0;
  }
  int number_width(int var, s32* count) override { return text::number_width(lang, font, vars[var & 15], count); }
  int measure_block(const u8** p) override {
    // Ruby blocks do not occur in the European text; skip to 0x400b.
    const u8* q = *p;
    int n = 0;
    while (*q != 0) {
      u32 c = *q++;
      if (c > 0x7F) c = (c & 0x7F) | static_cast<u32>(*q++) << 7;
      if (c == 0x400B) break;
      ++n;
    }
    *p = q;
    return n;
  }
  int fixed_space_width(int) override { return 0x10; }
};

// The code boundaries of a string (where a line may start), the terminator excluded.
std::vector<const u8*> boundaries(const u8* s, size_t cap) {
  std::vector<const u8*> out;
  const u8* p = s;
  while (*p != 0 && out.size() < cap) {
    out.push_back(p);
    u32 c = *p++;
    if (c > 0x7F) c = (c & 0x7F) | static_cast<u32>(*p++) << 7;
    switch (c) {
      case 0x4003: case 0x4004: case 0x4007: case 0x4008: case 0x400C: case 0x400D: case 0x400E: case 0x400F:
        if (*p != 0) ++p;
        break;
      case 0x4006:
        for (int k = 0; k < 2 && *p != 0; ++k) ++p;
        break;
      default:
        break;
    }
  }
  return out;
}

}  // namespace

TEST(text_rules_na_layout_matches_field) {
  if (!field_ready()) return;
  using namespace game::field;
  FieldGlobals& g = globals();
  CHECK(g.script_globals != nullptr);
  if (g.script_globals == nullptr) return;
  // Message variables for 0x4007 / 0x400e: characters and a spread of numbers.
  for (int v = 0; v < 16; ++v) {
    const s32 value = v < 4 ? v : (v * 7919 - 50000) * (v & 1 ? 1 : -1);
    std::memcpy(g.script_globals + 0x38 + v * 4, &value, 4);
  }
  game::Loader* loader = g.loader;
  int maps = 0, strings = 0, lines = 0, mismatches = 0;
  for (int map = 0; map < 1115; map += 7) {
    const int entry = 3207 + map;
    const s32 size = loader->entry_size(entry);
    if (size <= 0) continue;
    std::vector<u8> raw(static_cast<size_t>(size));
    loader->load_sync(raw.data(), entry, 0);
    auto m = map_text(raw);
    if (!m) continue;
    ++maps;
    msg_set_map_font(m->font.data());
    MsgWindow win;
    FieldHost host;
    host.w = &win;
    host.font = m->font.data();
    for (u32 i = 0; i < m->count(); ++i) {
      if (!m->valid(i)) continue;
      ++strings;
      for (const u8* start : boundaries(m->string(i), 160)) {
        for (int width : {0x58, 0xA0, 0xD8, 0x120}) {
          for (int cont : {0, 1}) {
            win.w = static_cast<s16>(width);
            win.word_wrap = 1;
            win.continuing = static_cast<u8>(cont);
            win.line_height = 13;
            TextLine port;
            const int h1 = text_layout_line(&win, start, &port);
            std::array<s32, 64> out{};
            text::LayoutWindow lw;
            lw.w = width;
            lw.continuing = cont != 0;
            const int h2 = text::layout_line(Lang::kNorthAmerica, lw, host, start, out.data());
            ++lines;
            bool same = h1 == h2;
            for (int k = 0; k < 7; ++k) same = same && port.v[k] == out[static_cast<size_t>(k)];
            if (!same && ++mismatches <= 5) {
              std::fprintf(stderr, "  map %d string %u +%d width %d cont %d: port %d/%d/%d h %d, rules %d/%d/%d h %d\n",
                           map, i, static_cast<int>(start - m->string(i)), width, cont, port.v[0], port.v[1],
                           port.v[3], h1, out[0], out[1], out[3], h2);
            }
          }
        }
      }
    }
  }
  std::printf("  %d maps, %d strings, %d layouts compared, %d differ\n", maps, strings, lines, mismatches);
  CHECK(maps > 100);
  CHECK_EQ(mismatches, 0);
}

namespace {

// The European fonts: 132 glyphs (a few maps add special glyphs), FR == DE but for 0x62.
void check_latin_disc(const char* env, Lang lang) {
  OtherDisc* d = other_disc(env);
  if (d == nullptr) {
    std::printf("  (skipped: %s not set)\n", env);
    return;
  }
  std::unique_ptr<OtherDisc> owner(d);
  LatinHost host;
  host.lang = lang;
  for (int v = 0; v < 16; ++v) host.vars[static_cast<size_t>(v)] = v * 1234 - 9000;
  int maps = 0, latin_fonts = 0, strings = 0, lines = 0, stuck = 0;
  for (int map = 0; map < 1115; ++map) {
    auto m = map_text(d->entry(3207 + map));
    if (!m) continue;
    ++maps;
    const s32 count = static_cast<s32>(rd32(m->font.data()));
    if (count < 132 || count > 140) continue;  // maps 1113 / 1114 carry a 492-glyph font
    ++latin_fonts;
    const u8* w = m->font.data() + 8;
    // Digits share one width, '.' and ',' are narrow, the space is 4; 0x62 is German only.
    for (int dgt = 1; dgt <= 10; ++dgt) CHECK_EQ(w[dgt - 1], w[0]);
    CHECK_EQ(w[0x72 - 1], 4);
    if (map % 97 == 0) CHECK_EQ(w[0x62 - 1] != 0, lang == Lang::kGerman);
    host.font = m->font.data();
    for (u32 i = 0; i < m->count(); ++i) {
      if (!m->valid(i)) continue;
      ++strings;
      // Lay the string out line by line from its start, as the typewriter would.
      const u8* p = m->string(i);
      bool cont = false;
      for (int guard = 0; *p != 0 && guard < 400; ++guard) {
        text::LayoutWindow lw;
        lw.w = 0xD8;
        lw.continuing = cont;
        std::array<s32, 64> out{};
        text::layout_line(lang, lw, host, p, out.data());
        ++lines;
        // Skip the laid-out glyphs (and the codes between them) to the next line.
        int left = out[0];
        bool ended = false;
        while (*p != 0) {
          const u8* q = p;
          u32 c = *q++;
          if (c > 0x7F) c = (c & 0x7F) | static_cast<u32>(*q++) << 7;
          s32 k = 0;  // glyphs this code puts on the line
          if (c < 0x4000) {
            k = 1;
          } else if (c == 0x4007 || c == 0x400C) {
            host.char_name_width(c == 0x400C ? *q : host.message_variable(*q), &k);
          } else if (c == 0x400E) {
            host.number_width(*q, &k);
          } else if (c == 0x400D) {
            k = 1;
          }
          if (k > left) break;
          left -= k;
          const std::vector<const u8*> b = boundaries(p, 2);
          p = b.size() > 1 ? b[1] : p + std::strlen(reinterpret_cast<const char*>(p));
          if (c == 0x4000 || c == 0x4001 || c == 0x4002 || c == 0x4006) {
            ended = true;
            break;
          }
        }
        cont = !ended;
        if (out[0] == 0 && !ended && *p != 0) {
          ++stuck;  // nothing fits and no code ended the line: the typewriter would loop
          break;
        }
      }
    }
  }
  std::printf("  %d maps (%d with the Latin font), %d strings, %d lines laid out, %d stuck\n", maps, latin_fonts, strings,
              lines, stuck);
  CHECK(latin_fonts > 900);
  CHECK_EQ(stuck, 0);
  // The title (#14 block 1) and battle (#1980 block 1) fonts: 132 glyphs, ASCII order.
  for (int e : {14, 1980}) {
    const std::vector<u8> raw = d->entry(e);
    auto font = disc::slz::unpack_block(raw, 0, 1, nullptr);
    CHECK(font.has_value() && font->size() > 8 && rd32(font->data()) == 132);
  }
}

}  // namespace

TEST(text_rules_french_disc) { check_latin_disc("SS_DISC1_FR", Lang::kFrench); }
TEST(text_rules_german_disc) { check_latin_disc("SS_DISC1_DE", Lang::kGerman); }

namespace {

std::vector<u8> from_hex(const std::string& h) {
  std::vector<u8> out;
  for (size_t i = 0; i + 1 < h.size(); i += 2) out.push_back(static_cast<u8>(std::stoul(h.substr(i, 2), nullptr, 16)));
  return out;
}

// LayoutHost over the state a Redux log records: the current font's widths, the names, the
// message variables, the 0x400d widths (item names are not logged: lines with 0x4008 are
// skipped).
struct LogHost : text::LayoutHost {
  Lang lang = Lang::kFrench;
  std::vector<u8> font;  // s32 count, s32 height, widths (a font block without its rows)
  std::array<std::string, 12> names;
  std::array<s32, 16> vars{};
  bool side = false;
  std::vector<u8> fixed;
  int glyph_width(u32 c) override { return c + 7 < font.size() ? font[c + 7] : 0; }
  s32 message_variable(int var) override { return vars[static_cast<size_t>(var & 15)]; }
  int char_name_width(int ch, s32* count) override {  // text_char_name_width (unchanged in FR / DE)
    const std::string& s = names[static_cast<size_t>(ch) % names.size()];
    int width = 0, n = 0, kana = 1;
    for (size_t i = 0; i < s.size(); ++i) {
      if (s[i] == '@') {
        kana ^= 1;
        continue;
      }
      ++n;
      s32 g = static_cast<u8>(s[i]);
      i += static_cast<size_t>(text::ascii_to_glyph(lang, &g, i + 1 < s.size() ? static_cast<u8>(s[i + 1]) : 0, kana));
      width += 1 + glyph_width(static_cast<u32>(g));
    }
    if (count) *count = n;
    return width;
  }
  int item_name_width(int, s32* count) override {
    if (count) *count = 0;
    return 0;
  }
  int number_width(int var, s32* count) override {
    s32 v = vars[static_cast<size_t>(var & 15)];
    if (side) v = static_cast<s16>(v);
    return text::number_width(lang, font.data(), v, count);
  }
  int measure_block(const u8** p) override {
    const u8* q = *p;
    while (*q != 0) {
      u32 c = *q++;
      if (c > 0x7F) c = (c & 0x7F) | static_cast<u32>(*q++) << 7;
      if (c == 0x400B) break;
    }
    *p = q;
    return 0;
  }
  int fixed_space_width(int i) override { return static_cast<size_t>(i) < fixed.size() ? fixed[static_cast<size_t>(i)] : 0; }
};

// The codes of a string up to its end (parameter bytes skipped: a parameter may be 0).
std::vector<u32> codes_of(const std::vector<u8>& s) {
  std::vector<u32> out;
  for (size_t i = 0; i < s.size() && s[i] != 0;) {
    u32 c = s[i++];
    if (c > 0x7F && i < s.size()) c = (c & 0x7F) | static_cast<u32>(s[i++]) << 7;
    out.push_back(c);
    if (c == 0x4003 || c == 0x4004 || c == 0x4007 || c == 0x4008 || c == 0x400C || c == 0x400D || c == 0x400E ||
        c == 0x400F)
      ++i;
    else if (c == 0x4006)
      i += 2;
  }
  return out;
}

bool has_code(const std::vector<u8>& s, u32 want) {
  for (u32 c : codes_of(s))
    if (c == want) return true;
  return false;
}

}  // namespace

TEST(text_rules_redux_lines) {
  const char* paths = std::getenv("SS_TEXT_REDUX_LOG");
  if (!paths || !*paths) {
    std::printf("  (skipped: SS_TEXT_REDUX_LOG not set)\n");
    return;
  }
  std::stringstream list(paths);
  std::string path;
  while (std::getline(list, path, ':')) {
    std::ifstream in(path);
    if (!in) {
      std::fprintf(stderr, "  cannot read %s\n", path.c_str());
      ++test::failures;
      continue;
    }
    LogHost host;
    int lines = 0, same = 0, skipped = 0, wrapped = 0;
    std::string row;
    while (std::getline(in, row)) {
      std::istringstream f(row);
      std::string tag;
      f >> tag;
      if (tag == "G") {
        std::string l;
        f >> l;
        host.lang = l == "de" ? Lang::kGerman : l == "fr" ? Lang::kFrench : Lang::kNorthAmerica;
      } else if (tag == "F") {
        std::string ptr, widths;
        s32 count = 0;
        f >> ptr >> count >> widths;
        host.font.assign(8, 0);
        std::memcpy(host.font.data(), &count, 4);
        const std::vector<u8> w = from_hex(widths);
        host.font.insert(host.font.end(), w.begin(), w.end());
      } else if (tag == "N") {
        std::string h;
        f >> h;
        const std::vector<u8> b = from_hex(h);
        for (size_t c = 0; c < host.names.size() && (c + 1) * 0x14 <= b.size(); ++c)
          host.names[c] = std::string(reinterpret_cast<const char*>(&b[c * 0x14]), strnlen(reinterpret_cast<const char*>(&b[c * 0x14]), 0x14));
      } else if (tag == "V") {
        int side = 0;
        f >> side;
        host.side = side != 0;
        for (s32& v : host.vars) f >> v;
      } else if (tag == "T") {
        std::string h;
        f >> h;
        host.fixed = from_hex(h);
      } else if (tag == "L") {
        int vs = 0, map = 0, w = 0, ww = 0, cont = 0, lh = 0;
        std::string texthex, arrow, h;
        f >> vs >> map >> w >> ww >> cont >> lh >> texthex >> arrow >> h;
        std::vector<u8> s = from_hex(texthex);
        s.push_back(0);
        if (has_code(s, 0x4008) || has_code(s, 0x400A)) {
          ++skipped;
          continue;
        }
        std::array<s32, 7> want{};
        bool have_out = true;
        for (s32& v : want) {
          std::string tok;
          if (!(f >> tok) || tok == "-") {
            have_out = false;
            break;
          }
          v = static_cast<s32>(std::stol(tok));
        }
        text::LayoutWindow lw;
        lw.w = w;
        lw.word_wrap = ww != 0;
        lw.continuing = cont != 0;
        lw.line_height = lh;
        std::array<s32, 64> out{};
        const int got_h = text::layout_line(host.lang, lw, host, s.data(), out.data());
        ++lines;
        bool ok = got_h == std::stoi(h);
        if (have_out) {
          for (size_t k = 0; k < 7; ++k) ok = ok && out[k] == want[k];
          // Wrapped: fewer glyphs than the text has before its next new-line / wait code.
          int glyphs = 0;
          for (u32 c : codes_of(s)) {
            if (c == 0x4000 || c == 0x4001 || c == 0x4002 || c == 0x4006) break;
            if (c < 0x4000) ++glyphs;
          }
          wrapped += want[4] == 0 && want[0] < glyphs;
        }
        if (ok) {
          ++same;
        } else if (lines - same <= 8) {
          std::fprintf(stderr, "  %s vsync %d map %d w %d cont %d: original %s h %s, rules %d/%d/%d/%d h %d (%s)\n",
                       path.c_str(), vs, map, w, cont, have_out ? std::to_string(want[0]).c_str() : "-", h.c_str(),
                       out[0], out[1], out[3], out[4], got_h, texthex.substr(0, 48).c_str());
        }
      }
    }
    std::printf("  %s: %d lines replayed, %d identical (%d of them wrapped), %d skipped (item names / ruby)\n",
                path.c_str(), lines, same, wrapped, skipped);
    CHECK(lines > 0);
    CHECK_EQ(same, lines);
  }
}
