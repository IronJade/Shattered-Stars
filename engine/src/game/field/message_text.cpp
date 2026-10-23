#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <vector>

#include "disc/slz.h"
#include "game/common/text_rules.h"
#include "game/field/field.h"
#include "game/field/load_queue.h"
#include "game/field/message.h"
#include "game/field/message_internal.h"
#include "runtime/libgpu.h"

// Message text: line layout with word wrap, glyph rendering into 4-bit bitmaps and
// the upload into the window's text cells, inserted names / numbers / item names,
// and the 0xfc2 side text (field 0x8005c21c-0x8005e1a4, 0x8005e808-0x8005ea4c,
// 0x80053b44).
namespace ss::game::field {

namespace tr = ss::game::text;

const u8* g_side_font = nullptr;     // 0x800762f0
const u8* g_side_glyphs = nullptr;   // 0x800762f4
const u8* g_side_strings = nullptr;  // 0x800762f8

namespace {

std::vector<u8> g_side_font_data, g_side_strings_data;

// The scratchpad copies of the two expansion tables (0x1f8003a0 / 0x1f800380).
u16 g_scratch_text_lut[16], g_scratch_shadow_lut[16];

s32 message_variable(int var) { return msg_rd32(globals().script_globals + 0x38 + var * 4); }

const u8* char_name(int ch) { return globals().game_state + 0x28 + ch * 0x14; }

// Next code of a text byte stream.
u32 next_code(const u8*& p) {
  u32 c = *p++;
  if (c > 0x7F) c = (c & 0x7F) | static_cast<u32>(*p++) << 7;
  return c;
}

// The 0xfc2 string with id var + 2999, or null.
const u8* side_string(int id) {
  const u8* table = g_side_strings;
  if (table == nullptr) return nullptr;
  for (const u8* p = table;; p += 4) {
    const u32 key = msg_rd16(p);
    if (key == 0xFFFF) return nullptr;
    if (static_cast<int>(key - 2999) == id) return table + msg_rd16(p + 2);
  }
}

// text_rules' view of the field's message system, for the releases whose text rules differ
// from NA's (France, Germany: text_rules.h).
struct FieldLayoutHost final : tr::LayoutHost {
  MsgWindow* w;
  explicit FieldLayoutHost(MsgWindow* window) : w(window) {}
  int glyph_width(u32 c) override { return msg_glyph_width(c); }
  s32 message_variable(int var) override { return field::message_variable(var); }
  int char_name_width(int ch, s32* n) override { return text_char_name_width(ch, n); }
  int item_name_width(int var, s32* n) override { return text_item_name_width(w, var, n); }
  int number_width(int var, s32* n) override { return text_number_width(w, var, n); }
  int measure_block(const u8** p) override { return text_measure_block(w, p); }
  int fixed_space_width(int i) override { return field_data(0x80076318)[i]; }
};

bool latin_text() { return tr::map_glyphs(tr::lang()).digit0 != tr::map_glyphs(tr::Lang::kNorthAmerica).digit0; }

}  // namespace

void msg_window_set_text_colour(MsgWindow* w, int colour) {
  const u16 c = static_cast<u16>(colour);
  const u16 v = static_cast<u16>(c | c << 4 | c << 8);
  for (u32 i = 0; i < 16; ++i) {
    u16 mask = (i & 8) ? 0xF : 0;
    if (i & 4) mask |= 0xF0;
    if (i & 2) mask |= 0xF00;
    u16 t = static_cast<u16>(v & mask);
    if (i & 1) t = static_cast<u16>((v | c << 12) & (mask | 0xF000));
    w->text_lut[i] = t;
  }
}

void msg_window_init_shadow_lut(MsgWindow* w) {
  for (u32 i = 0; i < 16; ++i) {
    u16 mask = (i & 8) ? 0xF : 0;
    if (i & 4) mask |= 0xF0;
    if (i & 2) mask |= 0xF00;
    if (i & 1) mask |= 0xF000;
    w->shadow_lut[i] = static_cast<u16>(mask & 0x1111);
  }
}

namespace {

int layout_line(MsgWindow* w, const u8* text, TextLine* out);

// SS_TEXT_LOG=<file> (debug, default off): one line per laid-out line, in the format of the
// PCSX-Redux logger of docs/notes/regions_text.md section 9 ("L vblank map w word_wrap
// continuing line_height text_hex -> height glyphs width 0 ruby ended - centred"), to compare
// the port's line breaks with the original's.
std::FILE* text_log() {
  static std::FILE* f = [] {
    const char* path = std::getenv("SS_TEXT_LOG");
    return path && *path ? std::fopen(path, "w") : nullptr;
  }();
  return f;
}

}  // namespace

int text_layout_line(MsgWindow* w, const u8* text, TextLine* out) {
  std::FILE* log = text_log();
  if (log == nullptr) return layout_line(w, text, out);
  TextLine local;
  if (out == nullptr) out = &local;
  const int h = layout_line(w, text, out);
  std::fprintf(log, "L 0 0 %d %d %d %d ", w->w, w->word_wrap != 0, w->continuing != 0, w->line_height);
  // The text up to its end code (parameter bytes skipped: a parameter may be 0).
  for (const u8* p = text; *p != 0 && p - text < 400;) {
    const u8* start = p;
    const u32 c = next_code(p);
    int params = 0;
    if (c == 0x4003 || c == 0x4004 || c == 0x4007 || c == 0x4008 || (c >= 0x400C && c <= 0x400F)) params = 1;
    if (c == 0x4006) params = 2;
    p += params;
    for (const u8* q = start; q < p; ++q) std::fprintf(log, "%02x", *q);
  }
  std::fprintf(log, " -> %d %d %d %d %d %d %d %d\n", h, out->v[0], out->v[1], out->v[2], out->v[3], out->v[4], out->v[5],
               out->v[6]);
  std::fflush(log);
  return h;
}

namespace {

int layout_line(MsgWindow* w, const u8* text, TextLine* out) {
  if (latin_text()) {
    // France / Germany (0x8005c24c): the Latin font's word-wrap classes (text_rules.h).
    tr::LayoutWindow lw;
    lw.w = w->w;
    lw.word_wrap = w->word_wrap != 0;
    lw.continuing = w->continuing != 0;
    lw.line_height = w->line_height;
    FieldLayoutHost host(w);
    return tr::layout_line(tr::lang(), lw, host, text, out ? out->v : nullptr);
  }
  int count = 0;        // glyphs on the line (s4)
  int x = 0;            // pixel width (s1)
  int ruby = 0;         // ruby glyphs and fixed spaces (s3)
  int cls = 0;          // character class of the previous glyph, for word wrap (s5)
  int brk_count = 0;    // last break opportunity: glyphs, width, ruby (s2, s6, s7)
  int brk_x = 0;
  int brk_ruby = 0;
  int marker = 0;       // 0x4009 choice block state (a3)
  int spaces = 0;       // 0x400d codes (sp+0x20)
  int ended = 0;        // sp+0x30
  int ruby_right = 0;   // right edge of the last ruby block (sp+0x48)
  int ruby_overflow = 0;  // sp+0x50
  int height = w->line_height;
  int centred = 0;
  int skip_space = w->continuing != 0;
  s32* cursor = out ? out->v : nullptr;  // advances 4 bytes per 0x4009 block
  const u8* marker_text = nullptr;  // text after the block's first glyph (sp+0x60)
  int marker_x = 0;     // sp+0x40
  int marker_w = 0;     // sp+0x38
  int prev_x = 0;       // x before the current glyph (sp+0x70)
  const u8* p = text;
  if (out) out->v[5] = 0;
  while (*p != 0) {
    const u32 c = next_code(p);
    int adv;    // a0
    int inc;    // glyphs this code adds (sp+0x10)
    if (c < 0x4000) {
      adv = msg_glyph_width(c) + 1;
      prev_x = x;
      inc = 1;
    } else {
      skip_space = 0;
      switch (c) {
        case 0x4000:
        case 0x4001:
        case 0x4002:
          ended = 1;
          goto next;
        case 0x4003:
        case 0x4004:
          ++p;
          goto next;
        case 0x4005:
          centred = 1;
          goto next;
        case 0x4006:
          ended = 1;
          p += 2;
          goto next;
        case 0x4007:
        case 0x400C: {
          const int ch = c == 0x400C ? *p : message_variable(*p);
          s32 n = 0;
          adv = text_char_name_width(ch, &n);
          inc = n;
          ++p;
          break;
        }
        case 0x4008: {
          const int var = *p++;
          s32 n = inc = 0;
          adv = text_item_name_width(w, var, &n);
          inc = n;
          break;
        }
        case 0x4009:
          marker = 1;
          marker_w = 0;
          if (cursor) ++cursor;
          goto next;
        case 0x400A: {
          if (cursor) cursor[5] = 0;
          const int n = text_measure_block(w, &p);
          const int span = n * 6;
          int left = marker_x - (span - marker_w) / 2;
          if (left < ruby_right) {
            const int shift = ruby_right - left;
            x += shift;
            if (cursor) cursor[5] = shift;
            left = ruby_right;
          }
          if (w->w - 0x10 < x) {
            x += 0x7FFF;
            ruby_overflow = 1;
            p = marker_text;
          }
          ruby_right = left + span;
          ruby += n;
          marker = 0;
          goto next;
        }
        case 0x400D:
          ++ruby;
          ++spaces;
          adv = field_data(0x80076318)[*p++];
          inc = 1;
          break;
        case 0x400E: {
          const int var = *p++;
          s32 n = 0;
          adv = text_number_width(w, var, &n);
          inc = n;
          break;
        }
        case 0x400F:
          x = *p++;
          adv = 0;
          inc = 0;
          break;
        default:
          goto next;
      }
    }
    // A glyph or an inserted string of 'inc' glyphs and width 'adv'.
    if (!skip_space || c != 0x107) {
      x += adv;
      skip_space = 0;
    }
    if (w->w - 0x10 < x) {
      // Overflow: break here, or back at the last opportunity (word wrap).
      x -= adv;
      if (ruby_overflow) x -= 0x7FFF;
      if (w->word_wrap == 0) goto done;
      bool rewind;
      if (static_cast<u16>(c - 0x55) < 0x55) {
        rewind = static_cast<u32>(cls - 1) < 2;
      } else if (c - 0xAA < 0xB) {
        rewind = static_cast<u32>(cls - 2) < 2;
      } else if (c - 0xB6 < 0x34 || c == 0x103) {
        rewind = static_cast<u32>(cls - 2) < 2;
      } else if (c == 0x400C) {
        goto done;
      } else if (c >= 0x107) {
        rewind = cls == 4 || cls == 2;
      } else if (c - 0xEA < 0xD || c - 4 < 0xB || c - 0xA7 < 3 || c == 0x106 || c == 0xB5) {
        rewind = true;  // punctuation that may not start a line
      } else if (c - 2 < 2) {
        if (x + 2 <= w->w - 0x10) {
          // Glyphs 2 and 3 still fit when squeezed.
          ++count;
          x = x + 1 + msg_glyph_width(c);
          goto done;
        }
        rewind = true;
      } else {
        goto done;
      }
      if (rewind && brk_count != 0) {
        count = brk_count;
        x = brk_x;
        ruby = brk_ruby;
      }
      goto done;
    }
    if (c == 1 && w->word_wrap != 0 && count != 0) goto done;  // glyph 1 starts a new line
    if (c - 0x107 < 0x3EF9) {
      if (cls != 4 && cls != 2) {
        brk_count = count;
        brk_x = x;
        brk_ruby = ruby;
      }
      cls = 4;
    } else if (c - 0x55 < 0x55) {
      if (static_cast<u32>(cls - 1) >= 2) {
        brk_count = count;
        brk_x = x;
        brk_ruby = ruby;
      }
      cls = 1;
    } else if (c - 0xAA < 0xB) {
      if (cls != 2) {
        brk_count = count;
        brk_x = x;
        brk_ruby = ruby;
        cls = 2;
      }
    } else if (c - 0xB6 < 0x34 || c == 0x106) {
      if (static_cast<u32>(cls - 2) >= 2) {
        brk_count = count;
        brk_x = x;
        brk_ruby = ruby;
      }
      cls = 3;
    } else if (c - 0xEA < 0x14 || c == 0xB5) {
      // punctuation keeps the class
    } else if (c == 0x400C) {
      brk_count = count;
      brk_x = x;
      brk_ruby = ruby;
      cls = 5;
    } else if (c < 0x4000) {
      brk_count = count;
      brk_x = x;
      brk_ruby = ruby;
      cls = 0;
    }
    if (marker != 0) {
      if (marker == 1) {
        marker_text = p;
        marker_x = prev_x;
      }
      ++marker;
      marker_w += x - prev_x;
    }
    count += inc;
  next:
    if (ended) break;
  }
done:
  if (out) {
    out->v[0] = count;
    out->v[1] = x;
    out->v[2] = 0;
    out->v[3] = ruby;
    out->v[4] = ended;
    out->v[6] = centred;
  }
  if (ruby != spaces) height += 5;
  return height;
}

}  // namespace

int text_number_width(MsgWindow*, int var, s32* digits) {
  const u8* font = msg_font_ref();
  const tr::MapGlyphs& g = tr::map_glyphs(tr::lang());
  u32 v = static_cast<u32>(message_variable(var));
  if (g_side_strings != nullptr) v = static_cast<u32>(static_cast<s32>(static_cast<s16>(v)));
  int width = 0;
  const bool negative = static_cast<s32>(v) < 0;
  if (negative) {
    v = 0u - v;
    width = font[g.minus + 7] + 1;  // '-' (NA glyph 0xb4)
  }
  int n = negative ? 1 : 0;
  do {
    ++n;
    width += 1 + font[v % 10 + g.digit0 + 7];  // digit glyphs (NA 0xaa-0xb3)
    v /= 10;
  } while (v != 0);
  if (digits) *digits = n;
  return width;
}

int text_item_name_width(MsgWindow*, int var, s32* count) {
  int id = message_variable(var);
  if (g_side_strings != nullptr) id >>= 16;
  const u8* s = side_string(id);
  if (s == nullptr) return 0;
  int width = 0, n = 0;
  for (u32 c; (c = next_code(s)) != 0;) {
    if (c < 0x4000) {
      ++n;
      width += 1 + msg_glyph_width(c);
    }
  }
  if (count) *count = n;
  return width;
}

int text_copy_char_name(MsgWindow*, int ch, u8* out) {
  const u8* name = char_name(ch);
  int n = 0;
  while (name[n] != 0) {
    out[n] = name[n];
    ++n;
  }
  out[n] = 0;
  return n;
}

int text_format_number(MsgWindow*, int var, u8* out) {
  const tr::MapGlyphs& g = tr::map_glyphs(tr::lang());
  u32 v = static_cast<u32>(message_variable(var));
  if (g_side_strings != nullptr) v = static_cast<u32>(static_cast<s32>(static_cast<s16>(v)));
  const bool negative = static_cast<s32>(v) < 0;
  if (negative) {
    *out++ = static_cast<u8>(g.minus);  // NA 0xb4
    v = 0u - v;
  }
  u8 digits[12];
  int n = 0;
  do {
    digits[n++] = static_cast<u8>(v % 10 + g.digit0);  // NA 0xaa
    v /= 10;
  } while (v != 0);
  for (int i = n - 1; i >= 0; --i) *out++ = digits[i];
  *out = 0;
  return n + (negative ? 1 : 0);
}

void text_find_item_name(MsgWindow*, int var, const u8** out) {
  int id = message_variable(var);
  if (g_side_strings != nullptr) id >>= 16;
  if (const u8* s = side_string(id)) *out = s;
}

void text_blit_glyph(MsgWindow* w, int glyph, int pitch, u16* dst, u32 x) {
  const int stride = pitch >> 2;
  const u16* lut = w->shadow_lut_ptr;
  const u8* rows = msg_glyphs_ref() + ((glyph - 1) & 0xFFFF) * 0x18;
  // The shadow, one pixel right and one down.
  const u32 sx = (x + 1) & 3;
  u16* row = dst + stride;
  if (sx == 0) {
    for (int r = 0; r < 12; ++r, row += stride) {
      const u32 bits = msg_rd16(rows + r * 2);
      if (bits == 0) continue;
      row[1] = lut[bits >> 12];
      row[2] = lut[bits >> 8 & 0xF];
      row[3] = lut[bits >> 4 & 0xF];
      row[4] = lut[bits & 0xF];
    }
  } else {
    const u32 rsh = (4 - sx) * 4, lsh = 16 - rsh;
    for (int r = 0; r < 12; ++r, row += stride) {
      const u32 bits = msg_rd16(rows + r * 2);
      if (bits == 0) continue;
      const u32 v0 = lut[bits >> 12], v1 = lut[bits >> 8 & 0xF], v2 = lut[bits >> 4 & 0xF], v3 = lut[bits & 0xF];
      row[0] = static_cast<u16>(row[0] | v0 << lsh);
      row[1] = static_cast<u16>(v1 << lsh | v0 >> rsh);
      row[2] = static_cast<u16>(v2 << lsh | v1 >> rsh);
      row[3] = static_cast<u16>(v3 << lsh | v2 >> rsh);
      row[4] = static_cast<u16>(v3 >> rsh);
    }
  }
  // The face.
  lut = w->text_lut_ptr;
  const u32 fx = x & 3;
  row = dst;
  if (fx == 0) {
    for (int r = 0; r < 12; ++r, row += stride) {
      const u32 bits = msg_rd16(rows + r * 2);
      if (bits == 0) continue;
      row[0] = static_cast<u16>(row[0] | lut[bits >> 12]);
      row[1] = static_cast<u16>(row[1] | lut[bits >> 8 & 0xF]);
      row[2] = static_cast<u16>(row[2] | lut[bits >> 4 & 0xF]);
      row[3] = static_cast<u16>(row[3] | lut[bits & 0xF]);
    }
  } else {
    const u32 rsh = (4 - fx) * 4, lsh = 16 - rsh;
    for (int r = 0; r < 12; ++r, row += stride) {
      const u32 bits = msg_rd16(rows + r * 2);
      if (bits == 0) continue;
      const u32 v0 = lut[bits >> 12], v1 = lut[bits >> 8 & 0xF], v2 = lut[bits >> 4 & 0xF], v3 = lut[bits & 0xF];
      row[0] = static_cast<u16>(row[0] | v0 << lsh);
      row[1] = static_cast<u16>(row[1] | v1 << lsh | v0 >> rsh);
      row[2] = static_cast<u16>(row[2] | v2 << lsh | v1 >> rsh);
      row[3] = static_cast<u16>(row[3] | v3 << lsh | v2 >> rsh);
      row[4] = static_cast<u16>(row[4] | v3 >> rsh);
    }
  }
}

void text_render_line(MsgWindow* w, const u8* text, int line, int height, TextLine* info) {
  MsgLine& L = w->lines[line];
  TextLine local;
  if (info == nullptr) {
    L.height = static_cast<s16>(text_layout_line(w, text, &local));
    info = &local;
  } else {
    L.height = static_cast<s16>(height);
  }
  const int lh = w->line_height;
  L.y_offset = static_cast<s16>(L.height - lh);
  L.remaining = static_cast<u16>(info->v[0]);
  w->pending_glyphs = static_cast<s16>(info->v[0]);
  int width = info->v[1];
  const int extras = info->v[3];
  const int centred = info->v[6];
  msg_window_clear_line(w, line);
  L.shown = 0;
  if (L.remaining == 0) return;
  L.extra_count = static_cast<s16>(extras);
  int justify = 0;  // pixels to spread over a wrapped line
  int lead = 0;     // left margin of a centred line
  if (centred != 0) {
    lead = (w->w - (width + 0x10)) / 2;
    width += lead * 2;
  } else if (w->justify != 0) {
    justify = w->w - (width + 0x10);
    if (L.remaining < justify) justify = L.remaining;
  }
  int v = width + 0x37;
  if (v < 0) v = width + 0x3E;
  const int pitch = (v >> 3) * 8;
  // The line bitmap (the scratchpad when it fits in 0x380 bytes, else the heap).
  // Room is added past the end: the cell upload copies whole runs per row.
  std::vector<u8> bitmap(static_cast<size_t>(pitch * lh / 2 + 40 * 4 + 16), 0);
  u16* buf = reinterpret_cast<u16*>(bitmap.data());
  w->shadow_lut_ptr = g_scratch_shadow_lut;
  std::memcpy(g_scratch_shadow_lut, w->shadow_lut, sizeof g_scratch_shadow_lut);
  w->text_lut_ptr = g_scratch_text_lut;
  std::memcpy(g_scratch_text_lut, w->text_lut, sizeof g_scratch_text_lut);

  int remaining = L.remaining;
  int step;  // 16.16 glyphs per extra justification pixel
  if (justify < 1 || info->v[4] != 0) {
    step = 0x1400000;
  } else {
    step = (remaining << 16) / (justify + 1);
  }
  L.glyph_x[0] = 0;
  int frac = 0;
  u32 x = 0;
  if (centred != 0) x = static_cast<u32>(lead);
  int marker = 0, marker_k = 0, marker_x = 0, marker_w = 0;
  int gi = 0;  // glyph index
  int ei = 0;  // extra index
  int ended = 0;
  bool skip = w->continuing != 0;
  const u32 space = tr::map_glyphs(tr::lang()).space;
  const u8* p = text;
  // One glyph drawn at x; advances x and records its end.
  auto put_glyph = [&](u32 glyph) {
    text_blit_glyph(w, static_cast<int>(glyph), pitch, buf + (x >> 2), x);
    const u32 start = x;
    x = x + 1 + static_cast<u32>(msg_glyph_width(glyph));
    if (marker != 0) {
      if (marker == 1) marker_x = static_cast<int>(start);
      ++marker;
      marker_w += static_cast<int>(x - start);
    }
    ++gi;
    if (gi < 64) L.glyph_x[gi] = static_cast<u16>(x);
    frac += 0x10000;
    if (frac >= step) {
      ++x;
      frac -= step;
    }
  };
  do {
    const u8* saved_font = msg_font_ref();
    const u8* saved_glyphs = msg_glyphs_ref();
    const u32 c = next_code(p);
    if (c < 0x4000) {
      if (remaining <= 0) break;
      if (marker == 1) x += static_cast<u32>(info->v[marker_k + 5]);
      const u32 start = x;
      if (!skip || c != space) {  // a wrapped line drops its leading space (NA 0x105)
        text_blit_glyph(w, static_cast<int>(c), pitch, buf + (x >> 2), x);
        skip = false;
        x = x + 1 + static_cast<u32>(msg_glyph_width(c));
      }
      if (marker != 0) {
        if (marker == 1) marker_x = static_cast<int>(start);
        ++marker;
        marker_w += static_cast<int>(x - start);
      }
      ++gi;
      if (gi < 64) L.glyph_x[gi] = static_cast<u16>(x);
      frac += 0x10000;
      if (frac >= step) {
        ++x;
        frac -= step;
      }
      --remaining;
      continue;
    }
    skip = false;
    switch (c) {
      case 0x4000:
      case 0x4001:
      case 0x4002:
        ended = 1;
        break;
      case 0x4003:
        ++p;
        break;
      case 0x4004:
        msg_window_set_text_colour(w, *p * 2 + 3);
        ++p;
        std::memcpy(w->text_lut_ptr, w->text_lut, sizeof w->text_lut);
        break;
      case 0x4006:
        ended = 1;
        p += 2;
        break;
      case 0x4007:
      case 0x400C: {
        if (remaining <= 0) break;
        int kana = 1;
        const int ch = c == 0x400C ? *p : message_variable(*p);
        const u8* name = char_name(ch);
        for (int i = 0; name[i] != 0; ++i) {
          if (name[i] == '@') {
            kana ^= 1;
            continue;
          }
          --remaining;
          s32 glyph = name[i];
          i += text_ascii_to_glyph(&glyph, name[i + 1], kana);
          put_glyph(static_cast<u32>(glyph) & 0xFFFF);
        }
        ++p;
        break;
      }
      case 0x4008: {
        if (remaining <= 0) break;
        msg_font_ref() = g_side_font;
        msg_glyphs_ref() = g_side_glyphs;
        static const u8 kEmpty[1] = {0};
        const u8* s = kEmpty;
        text_find_item_name(w, *p, &s);
        while (*s != 0) {
          const u32 g = next_code(s);
          if (g < 0x4000) {
            --remaining;
            put_glyph(g);
          }
        }
        ++p;
        msg_font_ref() = saved_font;
        msg_glyphs_ref() = saved_glyphs;
        break;
      }
      case 0x4009:
        marker = 1;
        marker_w = 0;
        ++marker_k;
        break;
      case 0x400A: {
        // Ruby: small glyphs centred over the glyphs of the preceding 0x4009 block.
        const u8* block = p;
        const int n = text_measure_block(w, &p);
        const u8* after = p;
        p = block;
        --marker;  // glyphs in the block
        int kana = 1;
        int name_left = 0;
        const u8* resume = nullptr;
        int acc_x = 0;   // iVar30: glyph-slot accumulator
        int acc_i = 0;   // iVar14
        int acc_w = marker_w;  // iVar33
        const int first = gi - marker;
        u8 name[0x20];
        for (int k = 0; k < n; ++k) {
          if (name_left != 0 && --name_left < 1) p = resume;
          u32 b = *p;
          if (b == 0x40) {  // '@' in an inserted name toggles kana
            acc_i -= n;
            --k;
            acc_w -= marker_w;
            acc_x -= marker;
            kana ^= 1;
            ++p;
          } else {
            const u8* q = p + 1;
            bool is_name = false;
            if (static_cast<u16>(b - 0xA6) >= 0x3A) {
              b = (b & 0x7F) | static_cast<u32>(p[1]) << 7;
              q = p + 2;
              is_name = name_left == 0 && b == 0x400C;
            }
            if (!is_name) {
              p = q;
              u16 code = static_cast<u16>(b);
              if (*p == 0xDE) {
                if (static_cast<u16>(code - 0xB6) < 0xF) {
                  code = static_cast<u16>(code + 0x28);
                } else if (static_cast<u16>(code - 0xCA) < 5) {
                  code = static_cast<u16>(code + 0x23);
                } else if (code == 0xB3) {
                  code = 0xF4;
                } else {
                  code = static_cast<u16>(code + 0xA6);
                  goto voiced_done;
                }
                ++p;
              } else if (*p == 0xDF) {
                if (static_cast<u16>(code - 0xCA) < 5) {
                  code = static_cast<u16>(code + 0x28);
                  ++p;
                } else {
                  code = static_cast<u16>(code + 0xA6);
                }
              }
            voiced_done:
              const s16 base = kana == 0 ? -0x52 : -0xA6;
              MsgExtra& e = L.extra[ei < 32 ? ei : 31];
              if (n == marker) {
                e.x = static_cast<s16>(L.glyph_x[first + k] + (L.glyph_x[first + k + 1] - L.glyph_x[first + k]) / 2 - 3);
              } else if (n == marker * 2) {
                const int g = first + k / 2;
                const s16 mid = static_cast<s16>(L.glyph_x[g] + (L.glyph_x[g + 1] - L.glyph_x[g]) / 2);
                e.x = static_cast<s16>((k & 1) == 0 ? mid - 6 : mid);
              } else if (n * 6 <= marker_w) {
                e.x = static_cast<s16>(marker_x + acc_w / (n + 1) - 3);
              } else {
                e.x = static_cast<s16>(marker_x - (n * 6 - marker_w) / 2 + k * 6);
              }
              e.glyph = static_cast<s16>(first + (n < marker ? acc_i / marker : acc_x / n));
              e.code = static_cast<s16>(code + base);
              ++ei;
            } else {
              // An inserted name inside the block: read it in place of the text.
              acc_x -= marker;
              acc_i -= n;
              --k;
              acc_w -= marker_w;
              const u8 ch = p[2];
              p += 3;
              name_left = text_copy_char_name(w, ch, name) + 1;
              resume = p;
              p = name;
            }
          }
          acc_x += marker;
          acc_i += n;
          acc_w += marker_w;
        }
        if (name_left != 0) p = resume;
        (void)after;
        marker = 0;
        p += 2;
        break;
      }
      case 0x400D: {
        MsgExtra& e = L.extra[ei < 32 ? ei : 31];
        ++ei;
        const u8 i = *p;
        --remaining;
        e.glyph = static_cast<s16>(gi);
        ++gi;
        e.x = static_cast<s16>(x);
        e.code = static_cast<s16>(~i);
        if (gi < 64) L.glyph_x[gi] = static_cast<u16>(x);
        ++p;
        x += field_data(0x80076318)[i];
        break;
      }
      case 0x400E: {
        if (remaining <= 0) break;
        u8 digits[16];
        const int n = text_format_number(w, *p++, digits);
        for (int i = 0; i < n; ++i) {
          --remaining;
          put_glyph(digits[i]);
        }
        break;
      }
      case 0x400F:
        x = *p++;
        break;
      default:
        break;
    }
  } while (ended == 0);

  // Upload the line into its text cells, run by run.
  MsgCells* cells = w->cells;
  for (int k = 0; k < 3 && cells != nullptr; ++k) {
    const int len = cells->run_len[line][k];
    if (len == 0) continue;
    delete[] L.bitmap[k];
    L.bitmap[k] = new u16[static_cast<size_t>(len * lh * 2)];
    const psyq::RECT r{static_cast<s16>(cells->pos[line][k][0] * 2 + 0x1C0),
                       static_cast<s16>(cells->pos[line][k][1] * lh + 0x100), static_cast<s16>(len * 2),
                       static_cast<s16>(lh)};
    int skip_cells = 0;
    for (int j = 0; j < k; ++j) skip_cells += cells->run_len[line][j];
    const u8* src = bitmap.data() + skip_cells * 4;
    u8* dst = reinterpret_cast<u8*>(L.bitmap[k]);
    for (int t = 0; t < lh; ++t) {
      const size_t at = static_cast<size_t>(src - bitmap.data());
      const size_t n = static_cast<size_t>(len * 4);
      if (at + n <= bitmap.size()) {
        std::memcpy(dst, src, n);
      } else {
        std::memset(dst, 0, n);
      }
      dst += n;
      src += (pitch >> 3) * 4;
    }
    psyq::LoadImage(&r, L.bitmap[k]);
  }
  w->text_height = static_cast<s16>(w->text_height + L.height);
  w->page_height = static_cast<s16>(w->page_height + L.height);
}

int text_measure_block(MsgWindow* w, const u8** text) {
  int n = 0;
  for (;;) {
    const u8* p = *text;
    if (*p == 0) return n;
    *text = p + 1;
    const u32 c = *p;
    if (c - 0xA6 < 0x3A) {
      if (p[1] == 0xDE) {
        if (c - 0xB6 > 0xE && c - 0xCA > 4 && c != 0xB3) {
          ++n;
          continue;
        }
        *text = *text + 1;
      } else if (p[1] == 0xDF) {
        if (c - 0xCA > 4) {
          ++n;
          continue;
        }
        *text = p + 2;
      } else {
        ++n;
        continue;
      }
      ++n;
      continue;
    }
    *text = p + 2;
    const u32 code = (c & 0x7F) | static_cast<u32>(p[1]) << 7;
    if (code == 0x400B) return n;
    if (code == 0x400C) {
      *text = p + 3;
      u8 name[0x20];
      const int len = text_copy_char_name(w, p[2], name);
      for (int i = 0; i < len; ++i) {
        const u32 b = name[i];
        if (b == 0x40) continue;
        if (name[i + 1] == 0xDE) {
          if (b - 0xB6 < 0xF || b - 0xCA < 5 || b == 0xB3) ++i;
        } else if (name[i + 1] == 0xDF) {
          if (b - 0xCA < 5) ++i;
        }
        ++n;
      }
      --n;
    }
    ++n;
  }
}

int text_char_name_width(int ch, s32* count) {
  const u8* name = char_name(ch);
  int width = 0, n = 0, kana = 1;
  for (int i = 0; name[i] != 0; ++i) {
    if (name[i] == 0x40) {
      kana ^= 1;
      continue;
    }
    ++n;
    s32 g = name[i];
    i += text_ascii_to_glyph(&g, name[i + 1], kana);
    width += 1 + msg_glyph_width(static_cast<u32>(g));
  }
  if (count) *count = n;
  return width;
}

int text_ascii_to_glyph(s32* code, int next, int kana) {
  if (latin_text()) return tr::ascii_to_glyph(tr::lang(), code, next, kana);  // FR / DE 0x8005e704
  s32 c = *code;
  int consumed = 0;
  if (static_cast<u32>(c - 0xA6) >= 0x3A) {
    s32 g;
    if (c >= '0' && c <= '9') {
      g = c + 0x7A;
    } else if (c >= 'A' && c <= 'Z') {
      g = c + 0x75;
    } else if (c >= 'a' && c <= 'z') {
      g = c + 0x6F;
    } else {
      switch (c) {
        case '!': g = 0xEF; break;
        case ',': g = 0x103; break;
        case '.': g = 0xB5; break;
        case '/': g = 0x104; break;
        case ':': g = 0x100; break;
        case ';': g = 0x101; break;
        case '=': g = 0xFF; break;
        case '?': g = 0xED; break;
        case 0xA4: g = 0x102; break;
        case 0xA5: g = 0xEB; break;
        default: g = 0xFE; break;
      }
    }
    *code = g;
    return 0;
  }
  // Half-width kana; 0xde / 0xdf after it are the voiced marks.
  if (next == 0xDE) {
    if (static_cast<u32>(c - 0xB6) < 0xF) {
      c += 0x28;
      consumed = 1;
      if (kana) ++c;
    } else if (static_cast<u32>(c - 0xCA) < 5) {
      c += 0x23;
      consumed = 1;
      if (kana) ++c;
    } else if (c != 0xB3) {
      c += 0xA6;
    } else {
      c = 0xDE;
      consumed = 1;
    }
  } else if (next == 0xDF) {
    if (static_cast<u32>(c - 0xCA) < 5) {
      c += 0x28;
      consumed = 1;
      if (kana) ++c;
    } else {
      c += 0xA6;
    }
  }
  c -= 0xA6;
  if (kana || c == 0xB0) c += 0x51;
  *code = c + 4;
  return consumed;
}

void field_install_side_text(const u8* entry, size_t size) {
  // u32 strings offset, u32 font offset: two SLZ blocks.
  const std::span<const u8> data(entry, size);
  auto unpack = [data](s32 off, std::vector<u8>& out) {
    auto unpacked = disc::slz::unpack_block(data, static_cast<size_t>(off), 0, nullptr);
    out = unpacked ? std::move(*unpacked) : std::vector<u8>{};
  };
  unpack(msg_rd32(entry + 4), g_side_font_data);
  unpack(msg_rd32(entry), g_side_strings_data);
  g_side_font = g_side_font_data.empty() ? nullptr : g_side_font_data.data();
  g_side_strings = g_side_strings_data.empty() ? nullptr : g_side_strings_data.data();
  g_side_glyphs = g_side_font ? g_side_font + msg_rd32(g_side_font) + 8 : nullptr;
  // The original ends by turning voice streaming back on (0x8004358c(queue, 1)), which the
  // side load (field_side_load) turned off; the callers free the source buffer.
  load_queue_set_streaming(load_queue(), true);
}

void field_free_side_text() {
  g_side_font = g_side_glyphs = g_side_strings = nullptr;
  g_side_font_data.clear();
  g_side_strings_data.clear();
}

const u8* field_side_strings() { return g_side_strings; }

const u8* field_sys_icons() {
  const u8* data = globals().system_data;
  if (data == nullptr) return nullptr;
  return data + msg_rd32(data + 0x0C) + 0x0C;
}

}  // namespace ss::game::field
