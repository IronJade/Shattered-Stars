#include "game/common/text_rules.h"

// The per-language text rules; see text_rules.h and docs/port/notes/regions_text.md. North
// America's paths are the ported field functions (field/message_text.cpp) restated over
// LayoutHost; ss_game_tests checks they give the same lines on the NA disc.
namespace ss::game::text {

namespace {

// French and German share the font layout and the code; German counts one more letter
// (glyph 0x62, u-umlaut; it is an empty glyph in the French font).
bool latin(Lang lang) { return lang == Lang::kFrench || lang == Lang::kGerman; }
u32 latin_letters(Lang lang) { return lang == Lang::kGerman ? 0x56 : 0x55; }

constexpr MapGlyphs kNaGlyphs{0xAA, 0xB4, 0x105, 0x107, 0xF7, 7, 276};
constexpr MapGlyphs kLatinGlyphs{0x01, 0x0B, 0x72, 0x74, 0x69, 4, 132};

// The code at p (a byte below 0x80, else two bytes), without consuming it.
u32 peek_code(const u8* p) {
  const u32 c = p[0];
  return c < 0x80 ? c : ((c & 0x7F) | static_cast<u32>(p[1]) << 7);
}

u32 next_code(const u8*& p) {
  u32 c = *p++;
  if (c > 0x7F) c = (c & 0x7F) | static_cast<u32>(*p++) << 7;
  return c;
}

}  // namespace

Lang lang() { return region::current().text; }

const MapGlyphs& map_glyphs(Lang lang) { return latin(lang) ? kLatinGlyphs : kNaGlyphs; }

s32 port_symbol_glyph(Lang lang, s32 code) {
  if (!latin(lang)) return -1;
  // The FR / DE map font's symbols (the same glyphs in every 132- / 136-glyph map font of
  // both releases; maps 1113 / 1114 use a 492-glyph font of their own).
  switch (code) {
    case '-': return 0x0B;
    case '\'': case '`': return 0x27;
    case ')': return 0x65;
    case ']': return 0x66;
    case '>': return 0x67;
    case '}': return 0x68;
    case '(': return 0x69;
    case '[': return 0x6A;
    case '<': return 0x6B;
    case '{': return 0x6C;
    case '\\': return 0x71;  // as '/'
    case '"': return 0x7B;
    case '#': return 0x75;
    case '$': return 0x76;
    case '%': return 0x77;
    case '&': return 0x78;
    case '*': return 0x79;
    case '+': return 0x7A;
    case '_': return 0x7D;
    case '|': return 0x7E;
    case '~': return 0x7F;
    case '@': case '^': return 0x72;  // no such glyph: a space
    default: return -1;
  }
}

int ascii_to_glyph(Lang lang, s32* code, int next, int kana) {
  s32 c = *code;
  if (static_cast<u32>(c - 0xA6) >= 0x3A) {
    s32 g;
    if (latin(lang)) {
      // FR / DE 0x8005e704: a jump table over 0x20-0x7a; any other byte stays as it is.
      g = c;
      if (c >= '0' && c <= '9') {
        g = c - 0x2F;
      } else if (c >= 'A' && c <= 'Z') {
        g = c - 0x34;
      } else if (c >= 'a' && c <= 'z') {
        g = c - 0x39;
      } else {
        switch (c) {
          case ' ': g = 0x72; break;
          case '!': g = 0x64; break;
          case ',': g = 0x70; break;
          case '.': g = 0x0C; break;
          case '/': g = 0x71; break;
          case ':': g = 0x6E; break;
          case ';': g = 0x6F; break;
          case '=': g = 0x6D; break;
          case '?': g = 0x63; break;
          default: break;
        }
      }
    } else if (c >= '0' && c <= '9') {
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
  // Half-width kana (every release); 0xde / 0xdf after it are the voiced marks.
  int consumed = 0;
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

int format_number(Lang lang, s32 value, u8* out) {
  const MapGlyphs& g = map_glyphs(lang);
  u32 v = static_cast<u32>(value);
  const bool negative = value < 0;
  if (negative) {
    *out++ = static_cast<u8>(g.minus);
    v = 0u - v;
  }
  u8 digits[12];
  int n = 0;
  do {
    digits[n++] = static_cast<u8>(v % 10 + g.digit0);
    v /= 10;
  } while (v != 0);
  for (int i = n - 1; i >= 0; --i) *out++ = digits[i];
  *out = 0;
  return n + (negative ? 1 : 0);
}

int number_width(Lang lang, const u8* font, s32 value, s32* digits) {
  const MapGlyphs& g = map_glyphs(lang);
  u32 v = static_cast<u32>(value);
  int width = 0;
  const bool negative = value < 0;
  if (negative) {
    v = 0u - v;
    width = font[g.minus + 7] + 1;
  }
  int n = negative ? 1 : 0;
  do {
    ++n;
    width += 1 + font[v % 10 + g.digit0 + 7];
    v /= 10;
  } while (v != 0);
  if (digits) *digits = n;
  return width;
}

int wrap_class(Lang lang, u32 c, u32 next) {
  if (latin(lang)) {
    if ((c & 0xFFFF) != 0 && (c & 0xFFFF) < 0xB) return 2;
    if (c - 0xD < latin_letters(lang) || c == 0x73) return 3;
    if (c - 0x63 < 10 || c - 0xB < 2) return -1;
    if (c == 0x400C) return 5;
    if (c < 0x4000) return next - 0x63 < 2 ? -1 : 0;
    return -1;
  }
  if (c - 0x107 < 0x3EF9) return 4;
  if (c - 0x55 < 0x55) return 1;
  if (c - 0xAA < 0xB) return 2;
  if (c - 0xB6 < 0x34 || c == 0x106) return 3;
  if (c - 0xEA < 0x14 || c == 0xB5) return -1;
  if (c == 0x400C) return 5;
  if (c < 0x4000) return 0;
  return -1;
}

int layout_line(Lang lang, const LayoutWindow& w, LayoutHost& host, const u8* text, s32* out) {
  const bool fr_de = latin(lang);
  const u32 skip_glyph = map_glyphs(lang).layout_skip;
  const int limit = w.w - 0x10;
  int count = 0;        // glyphs on the line (s4)
  int x = 0;            // pixel width (s1)
  int ruby = 0;         // ruby glyphs and fixed spaces (s3)
  int cls = 0;          // word-wrap class of the previous glyph (s5 / s7)
  int brk_count = 0;    // the last break opportunity: glyphs, width, ruby (s2, s6, s7)
  int brk_x = 0;
  int brk_ruby = 0;
  int marker = 0;       // 0x4009 block state
  int spaces = 0;       // 0x400d codes
  int ended = 0;
  int ruby_right = 0;   // right edge of the last ruby block
  int ruby_overflow = 0;
  int height = w.line_height;
  int centred = 0;
  int skip_space = w.continuing ? 1 : 0;
  s32* cursor = out;    // advances one entry per 0x4009 block
  const u8* marker_text = nullptr;
  int marker_x = 0;
  int marker_w = 0;
  int prev_x = 0;
  const u8* p = text;
  if (out) out[5] = 0;
  while (*p != 0) {
    const u32 c = next_code(p);
    int adv = 0;  // a0
    int inc = 0;  // glyphs this code adds
    if (c < 0x4000) {
      adv = host.glyph_width(c) + 1;
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
          const int ch = c == 0x400C ? *p : host.message_variable(*p);
          s32 n = 0;
          adv = host.char_name_width(ch, &n);
          inc = n;
          ++p;
          break;
        }
        case 0x4008: {
          const int var = *p++;
          s32 n = 0;
          adv = host.item_name_width(var, &n);
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
          const int n = host.measure_block(&p);
          const int span = n * 6;
          int left = marker_x - (span - marker_w) / 2;
          if (left < ruby_right) {
            const int shift = ruby_right - left;
            x += shift;
            if (cursor) cursor[5] = shift;
            left = ruby_right;
          }
          if (limit < x) {
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
          adv = host.fixed_space_width(*p++);
          inc = 1;
          break;
        case 0x400E: {
          const int var = *p++;
          s32 n = 0;
          adv = host.number_width(var, &n);
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
    // A glyph, or an inserted string of 'inc' glyphs and width 'adv'.
    if (!skip_space || c != skip_glyph) {
      x += adv;
      skip_space = 0;
    }
    if (limit < x) {
      // Overflow: break here, or back at the last opportunity (word wrap).
      x -= adv;
      if (ruby_overflow) x -= 0x7FFF;
      if (!w.word_wrap) goto done;
      bool rewind;
      if (fr_de) {
        // FR / DE 0x8005c3b8.
        const u32 c16 = c & 0xFFFF;
        if (c16 != 0 && c16 < 0xB) {
          rewind = static_cast<u32>(cls - 2) < 2;  // a digit after digits / letters
        } else if (c - 0xD < latin_letters(lang) || c16 == 0x70) {
          rewind = static_cast<u32>(cls - 2) < 2;  // a letter or ',' after digits / letters
        } else if (c16 == 0x400C) {
          goto done;
        } else if (c - 0x63 < 6 || c16 == 0x73 || c - 0xB < 2) {
          rewind = true;  // ? ! ) ] and the like, the apostrophe, '-', '.': never start a line
        } else if (c16 == 0x72) {
          rewind = peek_code(p) - 0x63 < 2;  // a space before '?' / '!' stays with the word
        } else {
          goto done;
        }
      } else if (static_cast<u16>(c - 0x55) < 0x55) {
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
        if (x + 2 <= limit) {
          // Glyphs 2 and 3 still fit when squeezed.
          ++count;
          x = x + 1 + host.glyph_width(c);
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
    if (!fr_de && c == 1 && w.word_wrap && count != 0) goto done;  // NA: glyph 1 starts a new line
    {
      // The class of this glyph; a class change is a break opportunity before it.
      const int k = wrap_class(lang, c, fr_de && c < 0x4000 ? peek_code(p) : 0);
      bool mark = false;
      switch (k) {
        case 1: mark = static_cast<u32>(cls - 1) >= 2; break;
        case 2: mark = cls != 2; break;
        case 3: mark = static_cast<u32>(cls - 2) >= 2; break;
        case 4: mark = cls != 4 && cls != 2; break;
        case 0:
        case 5: mark = true; break;
        default: break;
      }
      if (mark) {
        brk_count = count;
        brk_x = x;
        brk_ruby = ruby;
      }
      if (k >= 0) cls = k;
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
    out[0] = count;
    out[1] = x;
    out[2] = 0;
    out[3] = ruby;
    out[4] = ended;
    out[6] = centred;
  }
  if (ruby != spaces) height += 5;
  return height;
}

int ascii_high_bias(Lang lang, Program program) {
  if (!latin(lang) || program == Program::kRename) return -1;
  if (program == Program::kTitle) return lang == Lang::kFrench ? 4 : -1;
  return lang == Lang::kFrench ? 6 : 0xC;
}

namespace {
constexpr NameKeyboard kNaKeyboard{5, 0x2A, 2, 2, true};
constexpr NameKeyboard kLatinKeyboard{3, 0x4C, 1, 0, false};
}  // namespace

const NameKeyboard& name_keyboard(Lang lang) { return latin(lang) ? kLatinKeyboard : kNaKeyboard; }

KeyAction keyboard_button(Lang lang, int column) {
  if (latin(lang)) {
    switch (column) {
      case 0: return KeyAction::kOk;
      case 1: return KeyAction::kDelete;
      case 2: return KeyAction::kReturn;
      default: return KeyAction::kNone;
    }
  }
  switch (column) {
    case 0: return KeyAction::kCapital;
    case 1: return KeyAction::kLower;
    case 2: return KeyAction::kOk;
    case 3: return KeyAction::kDelete;
    case 4: return KeyAction::kReturn;
    default: return KeyAction::kNone;
  }
}

int cvram_choose(Lang lang, std::span<const FreeBlock> blocks, int w, int h) {
  int best = 0;
  s16 best_w = 0x400, best_h = 0x400;
  const s16 rw = static_cast<s16>(w), rh = static_cast<s16>(h);
  for (size_t i = 0; i < blocks.size(); ++i) {
    const FreeBlock& b = blocks[i];
    if (!b.free || b.w < rw || b.h < rh) continue;
    if (latin(lang)) {
      if (b.w <= best_w && b.h <= best_h) {
        best = static_cast<int>(i) + 1;
        best_w = b.w;
        best_h = b.h;
      }
    } else if (b.h < best_h) {
      best = static_cast<int>(i) + 1;
      best_h = b.h;
    }
  }
  return best;
}

namespace {

constexpr Layout kNaLayout{
    .report_heading_w = 0x14, .report_items_dx = 0, .report_item_w = 0x14,
    .options_s1_mono_dx = 0x70, .options_s1_values_x = 0xA8, .options_s1_row8_dx = 0x38,
    .options_s1_rowc_dx = 0x50, .options_s2_icon_x = 0xB8, .options_s2_icon_dx = 0xE,
    .options_s2_vib_dx = 0x40,
    .specialty_mp_x = 0x88, .link_combo_on_x = 0xA0, .link_combo_off_x = 0xC0,
    .skill_detail_x = 0xE0, .skills_heading_message = false, .skills_heading_x = {0x38, 0xCC},
    .equip_options_w = 0xD0, .equip_options_x = {0x80, 0xA0},
    .check_dialog_w = 0xC8, .save_slot_x = 0xA8, .save_slot_dx = 0x40, .save_slot_label_x = {0xB8, 0xF8},
    .sound_lines_stereo = 5, .vibration_heading_x = 0x54,
    .digit_strip_v = 0x32, .voice_label_w = 0x2D, .exit_label = {0x1F, 0x50}, .bgm_label = {0x2E, 0xC, 0x32},
    .random_label = {0x3F, 5}, .stop_label = {0x24, 0xD, 4, 0x60},
    .sound_subtitle_x = 0x8C, .screen2_subtitle_x = 0x98, .screen3_subtitle_x = 0x80, .name_heading_w = 0x80,
    .name_subtitle_x = 0x80, .tpage7_heading_w = 0x86, .tpage7_subtitle_x = 0x88, .tpage8_heading_w = 0x96,
    .tpage8_subtitle_x = 0x90, .tpage8_lit_selected = false,
    .rename_hand_u = 0x80, .rename_cursor_u = 0xA0, .rename_blank_u = 0x90, .rename_extra_vram = false,
    .rename_heading_w = 0x80, .rename_subtitle = {0x88, 0x80, 0x80, 0x25},
    .end_logo3_x1 = 0x26C, .end_logo3_u1 = 0x88,
    .result_prefix_order = false, .scan_first_page = 0,
    .lid_notice_battle = {0x3C, 0xC0}, .lid_notice_field = {5, 8, 0x1E},
};

constexpr Layout kFrLayout{
    .report_heading_w = 0x24, .report_items_dx = 8, .report_item_w = 0x12,
    .options_s1_mono_dx = 0x78, .options_s1_values_x = 0xB0, .options_s1_row8_dx = 0x38,
    .options_s1_rowc_dx = 0x50, .options_s2_icon_x = 0xB8, .options_s2_icon_dx = 0xE,
    .options_s2_vib_dx = 0x50,
    .specialty_mp_x = 0x70, .link_combo_on_x = 0x80, .link_combo_off_x = 0xA8,
    .skill_detail_x = 0xE0, .skills_heading_message = true, .skills_heading_x = {0x10, 0xA4},
    .equip_options_w = 0x110, .equip_options_x = {0x88, 0xC0},
    .check_dialog_w = 0x100, .save_slot_x = 0xA8, .save_slot_dx = 0x40, .save_slot_label_x = {0xB8, 0xF8},
    .sound_lines_stereo = 5, .vibration_heading_x = 0x30,
    .digit_strip_v = 0x31, .voice_label_w = 0x2D, .exit_label = {0x39, 0x4F}, .bgm_label = {0x43, 0xE, 0x30},
    .random_label = {0x4A, 4}, .stop_label = {0x3C, 0xB, 3, 0x5F},
    .sound_subtitle_x = 0x68, .screen2_subtitle_x = 0x68, .screen3_subtitle_x = 0x58, .name_heading_w = 0x90,
    .name_subtitle_x = 0x98, .tpage7_heading_w = 0xB0, .tpage7_subtitle_x = 0xA8, .tpage8_heading_w = 0xB0,
    .tpage8_subtitle_x = 0xB0, .tpage8_lit_selected = true,
    .rename_hand_u = 0xC8, .rename_cursor_u = 0xE8, .rename_blank_u = 0xD8, .rename_extra_vram = true,
    .rename_heading_w = 0x90, .rename_subtitle = {0x98, 0x90, 0, 0x26},
    .end_logo3_x1 = 0x205, .end_logo3_u1 = 0x98,
    .result_prefix_order = true, .scan_first_page = 0,
    .lid_notice_battle = {0x1C, 0x100}, .lid_notice_field = {2, 0xA, 0x24},
};

constexpr Layout kDeLayout{
    .report_heading_w = 0x24, .report_items_dx = 8, .report_item_w = 0x12,
    .options_s1_mono_dx = 0x78, .options_s1_values_x = 0xB0, .options_s1_row8_dx = 0x48,
    .options_s1_rowc_dx = 0x30, .options_s2_icon_x = 0xC8, .options_s2_icon_dx = 0xC,
    .options_s2_vib_dx = 0x50,
    .specialty_mp_x = 0x70, .link_combo_on_x = 0x80, .link_combo_off_x = 0xA8,
    .skill_detail_x = 0x100, .skills_heading_message = true, .skills_heading_x = {0x10, 0xA4},
    .equip_options_w = 0x110, .equip_options_x = {0x88, 0xC0},
    .check_dialog_w = 0x100, .save_slot_x = 0x88, .save_slot_dx = 0x50, .save_slot_label_x = {0x98, 0xE8},
    .sound_lines_stereo = 6, .vibration_heading_x = 0x54,
    .digit_strip_v = 0x31, .voice_label_w = 0x4B, .exit_label = {0x39, 0x4F}, .bgm_label = {0x43, 0xE, 0x30},
    .random_label = {0x4A, 4}, .stop_label = {0x3C, 0xB, 3, 0x5F},
    .sound_subtitle_x = 0x8C, .screen2_subtitle_x = 0x98, .screen3_subtitle_x = 0x80, .name_heading_w = 0x80,
    .name_subtitle_x = 0x80, .tpage7_heading_w = 0xB0, .tpage7_subtitle_x = 0xA8, .tpage8_heading_w = 0x96,
    .tpage8_subtitle_x = 0x90, .tpage8_lit_selected = true,
    .rename_hand_u = 0xC8, .rename_cursor_u = 0xE8, .rename_blank_u = 0xD8, .rename_extra_vram = true,
    .rename_heading_w = 0x90, .rename_subtitle = {0x98, 0x90, 0, 0x26},
    .end_logo3_x1 = 0x235, .end_logo3_u1 = 0xC8,
    .result_prefix_order = false, .scan_first_page = 1,
    .lid_notice_battle = {0x3C, 0xC0}, .lid_notice_field = {2, 0xA, 0x24},
};

}  // namespace

const Layout& layout(Lang lang) {
  switch (lang) {
    case Lang::kFrench: return kFrLayout;
    case Lang::kGerman: return kDeLayout;
    default: return kNaLayout;
  }
}

}  // namespace ss::game::text
