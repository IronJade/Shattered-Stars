// The spell clash banner, the skill notice and the scan window. See spell_windows.h.

#include "game/battle/spell_windows.h"

#include <cstring>
#include <string>

#include "game/battle/bsfx.h"
#include "game/battle/shatter.h"
#include "game/battle/spellcast.h"
#include "game/battle/spells.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/battle/window.h"
#include "game/common/screen.h"
#include "game/field/post_battle.h"
#include "game/title/new_game.h"
#include "runtime/libgpu.h"

namespace ss::game::battle {
namespace {

u8 image_u8(u32 address) {
  const u8* p = battle_data(address);
  return p != nullptr ? *p : 0;
}

// ---- SpellClashWindow (0x30 bytes, vtable 0x8006cfa4, g_spell_clash_window 0x8006fab4) ----

struct SpellClashWindow : BWindow {
  s32 blink = 0;                 // +0x20
  Sprite* side_icon[2] = {};     // +0x24
  Sprite* banner = nullptr;      // +0x2c
  SpellClashWindow() : BWindow(0x20) {}
  void draw() override;          // [1] spell_clash_window_draw 0x80062a60
  void release() override;       // [4] spell_clash_window_release 0x800629ec
};
SpellClashWindow* g_clash = nullptr;
SpellClashWindow* g_clash_closed = nullptr;  // (released windows wait here to be freed)

void SpellClashWindow::release() {
  for (Sprite*& s : side_icon) {
    sprite_free(s);
    s = nullptr;
  }
  sprite_free(banner);
  banner = nullptr;
  BWindow::release();
  g_clash = nullptr;
}

void SpellClashWindow::draw() {
  BattleGlobals& g = bg();
  SpellCast* first = spellcast_slot(0);
  if (first == nullptr) {
    release();
    delete g_clash_closed;
    g_clash_closed = this;  // (the console never frees it; deleting it from its own update is not safe here)
    return;
  }
  window_text_begin(this);
  for (int i = 0; i < 2; ++i) {
    const SpellCast* sc = spellcast_slot(i);
    if (sc == nullptr) continue;
    text_draw_id(this, i * 0x50, 0, sc->caster->is_enemy + 0x2C, 0);
    text_draw_id(this, i * 0x50 + 0x10, 0, spell_element(sc->spell) + 0x14, 0);
  }
  blink = (blink + g.frame_ticks) & 0x1F;
  // The label: the word table at 0x8007001c indexed by the combo (1-4: 0x29 / 0x2b / 0x28 /
  // 0x2b); its entry 0 is model_shatter's OT bias global.
  if (blink < 0x14) {
    s32 label = shatter_ot_bias();
    if (first->combo != 0) {
      const u8* p = battle_data(0x8007001Cu + first->combo * 4u);
      label = p != nullptr ? static_cast<s32>(p[0] | p[1] << 8 | p[2] << 16 | static_cast<u32>(p[3]) << 24) : 0;
    }
    text_draw_id(this, 0x2A, 0, label, 0);
  }
  window_text_end(this);
}

// ---- SkillNoticeWindow (0x88 bytes, vtable 0x8006d0a8, g_skill_notice 0x8006fab0) ----

struct SkillNoticeWindow : BWindow {
  s32 skill[5] = {};         // +0x20: entry 0 the header
  s32 sx[5] = {};            // +0x34: x on screen
  s32 speed[5] = {};         // +0x48
  s32 hold[5] = {};          // +0x5c
  s32 count = 0;             // +0x70
  s32 closing = 0;           // +0x74
  s32 fade = 0;              // +0x78: the backdrop 0..0x100
  s32 height = 0;            // +0x7c
  s32 chain = 0;             // +0x80
  Sprite* header = nullptr;  // +0x84
  SkillNoticeWindow() : BWindow(0x20) {}
  void draw() override;      // [1] skill_notice_draw 0x80064504
};
SkillNoticeWindow* g_notice = nullptr;
SkillNoticeWindow* g_notice_closed = nullptr;

void skill_notice_init(SkillNoticeWindow* w, int skill) {
  // 0x80064378.
  w->open(0xC0, 0x2E, 0x80, 0x50);
  w->header = sprite_spawn_ui(0x34, 0, nullptr);
  for (int i = 0; i < 2; ++i) {
    w->sx[i] = 0x140;
    w->speed[i] = 8;
    w->skill[i] = skill;
    w->hold[i] = 0;
  }
  w->count = 2;
  w->closing = 0;
  w->height = 0;
  w->fade = 0;
  w->chain = 1;
  ot_layer_to_front(bg().prim_pool_b);
}

void skill_notice_add(SkillNoticeWindow* w, int skill) {
  // 0x80064440.
  w->closing = 0;
  if (w->count == 5) {
    for (int i = 1; i < 4; ++i) {
      w->sx[i] = w->sx[i + 1];
      w->skill[i] = w->skill[i + 1];
      w->hold[i] = w->hold[i + 1];
      w->speed[i] = w->speed[i + 1];
    }
    w->count = 4;
  }
  w->sx[w->count] = 0x140;
  w->speed[w->count] = 8;
  w->skill[w->count] = skill;
  for (s32& h : w->hold) h = 0;
  ++w->count;
  ++w->chain;
}

void quad_g4(BWindow* w, s16 x0, s16 y0, s16 x1, s16 y1, s16 x2, s16 y2, s16 x3, s16 y3, u32 c01, u32 c23) {
  auto* p = screen_alloc_prims<psyq::POLY_G4>(w->ctx.arena, 1);
  if (p == nullptr) return;
  psyq::setPolyG4(p);
  p->code = 0x3A;
  p->r0 = static_cast<u8>(c01), p->g0 = static_cast<u8>(c01 >> 8), p->b0 = static_cast<u8>(c01 >> 16);
  p->r1 = static_cast<u8>(c01), p->g1 = static_cast<u8>(c01 >> 8), p->b1 = static_cast<u8>(c01 >> 16);
  p->r2 = static_cast<u8>(c23), p->g2 = static_cast<u8>(c23 >> 8), p->b2 = static_cast<u8>(c23 >> 16);
  p->r3 = static_cast<u8>(c23), p->g3 = static_cast<u8>(c23 >> 8), p->b3 = static_cast<u8>(c23 >> 16);
  p->x0 = x0, p->y0 = y0, p->x1 = x1, p->y1 = y1, p->x2 = x2, p->y2 = y2, p->x3 = x3, p->y3 = y3;
  psyq::addPrim(w->ctx.ot_head, p);
}

void SkillNoticeWindow::draw() {
  BattleGlobals& g = bg();
  if (header != nullptr) {
    sprite_set_screen_pos(header, static_cast<s16>(sx[0] + 0x11), 0x36);
    // The underline under the header (a line and its shadow).
    for (int i = 0; i < 2; ++i) {
      auto* l = screen_alloc_prims<psyq::LINE_F2>(ctx.arena, 1);
      if (l == nullptr) break;
      psyq::setLineF2(l);
      const u32 c = i == 0 ? 0x4080FF : 0x081020;
      l->r0 = static_cast<u8>(c), l->g0 = static_cast<u8>(c >> 8), l->b0 = static_cast<u8>(c >> 16);
      s16 x0 = static_cast<s16>(sx[0] + i);
      if (chain < 10) x0 = static_cast<s16>(x0 + 8);
      l->x0 = x0;
      l->y0 = static_cast<s16>(0x3E + i);
      l->x1 = static_cast<s16>(sx[0] + i + 0x69);
      l->y1 = static_cast<s16>(0x3E + i);
      psyq::addPrim(ctx.ot_head, l);
    }
  }
  s32 best;
  std::memcpy(&best, g.config + 0x2C, 4);
  if (best < chain) std::memcpy(g.config + 0x2C, &chain, 4);
  window_text_begin(this);
  char digits[16];
  window_draw_small_text(this, sx[0] - 0xC9, 0, field::format_decimal(digits, chain, 2, ' '), 0xF);
  for (int i = 1; i < count; ++i) text_draw_id(this, sx[i] - 0xD0, 0xB + (i - 1) * 0xE, skill[i] + 0x1F1F, 0);
  window_text_end(this);
  const u32 c = scale_colour(0x808080, fade);
  height += g.frame_ticks * 4;
  if (count * 0xE + 0x10 < height) height = count * 0xE + 0x10;
  const s16 left = static_cast<s16>(sx[0]);
  const s16 l = static_cast<s16>(left - 6), r = static_cast<s16>(left + 0x74), il = static_cast<s16>(left + 2),
            ir = static_cast<s16>(left + 100);
  const s16 bottom = static_cast<s16>(height + 0x1E), outer = static_cast<s16>(height + 0x2E);
  // The backdrop and its four shaded edges, subtracted.
  quad_g4(this, il, 0x36, ir, 0x36, il, bottom, ir, bottom, c, c);
  quad_g4(this, l, 0x2E, l, outer, il, 0x36, il, bottom, 0, c);
  quad_g4(this, l, 0x2E, r, 0x2E, il, 0x36, ir, 0x36, 0, c);
  quad_g4(this, r, 0x2E, r, outer, ir, 0x36, ir, bottom, 0, c);
  quad_g4(this, l, outer, r, outer, il, bottom, ir, bottom, 0, c);
  if (auto* m = screen_alloc_prims<psyq::DR_MODE>(ctx.arena, 1)) {
    psyq::SetDrawMode(m, 0, 1, 0x40, nullptr);
    psyq::addPrim(ctx.ot_head, m);
  }
  if ((g.flags & 4) != 0) return;
  int done = 0;
  for (int i = 0; i < count; ++i) {
    if (closing == 0) {
      if (i == 0) {
        fade += g.frame_ticks * 0x10;
        if (0x100 < fade) fade = 0x100;
      }
      sx[i] -= speed[i] * g.frame_ticks;
      const s32 stop = i == 0 ? 0xD0 : 0x140 - text_width_id(skill[i] + 0x1F1F);
      if (sx[i] < stop) {
        sx[i] = stop;
        speed[i] = 8;
        hold[i] += g.frame_ticks;
        if (0x3C < hold[i]) ++done;
      } else if (sx[i] < stop + 0x30) {
        speed[i] -= g.frame_ticks;
        if (speed[i] < 1) speed[i] = 1;
      }
    } else {
      sx[i] += g.frame_ticks * 0xC;
      if (i == 0) {
        fade -= g.frame_ticks * 0x18;
        if (fade < 0) fade = 0;
      }
    }
  }
  if (sx[0] < 0x140) {
    if (done == count) closing = 1;
    return;
  }
  sprite_free(header);
  header = nullptr;
  release();
  g_notice = nullptr;
  delete g_notice_closed;
  g_notice_closed = this;
}

// ---- ScanWindow (0x3c bytes, vtable 0x8006d0e8, g_scan_window 0x8006faac) ----

struct ScanWindow : BWindow {
  Unit* unit = nullptr;      // +0x20
  Sprite* icon = nullptr;    // +0x24
  s32 page = 0;              // +0x28: 0 name, 1 HP, 2 MP, 3 elements
  s32 element = 0;           // +0x2c
  s32 step = 0;              // +0x30: 0 start, 1 sliding in, 2 showing, 3 sliding out
  s32 ticks = 0;             // +0x34
  s32 speed = 0;             // +0x38
  ScanWindow() : BWindow(0x10) {}
  void draw() override;      // [1] scan_window_draw 0x80064dd4
};
ScanWindow* g_scan = nullptr;
ScanWindow* g_scan_closed = nullptr;
Unit* g_scan_queue[4] = {};  // 0x80070880
int g_scan_queue_count = 0;  // 0x8007087c

void scan_window_init(ScanWindow* w, Unit* u) {
  // 0x80064d40.
  w->open(static_cast<s16>(-0x60), 0x2C, 0x10, 0x1C);
  ot_layer_to_front(bg().prim_pool_b);
  w->icon = sprite_spawn_ui(0x3D, 0, nullptr);
  w->unit = u;
  w->page = 0;
  w->element = 0;
  w->ticks = 0;
  w->step = 0;
}

std::string image_string(u32 address) {
  const u8* p = battle_data(address);
  std::string s;
  while (p != nullptr && *p != 0 && s.size() < 16) s.push_back(static_cast<char>(*p++));
  return s;
}

void ScanWindow::draw() {
  BattleGlobals& g = bg();
  u8 text[64] = {};
  char digits[24];
  const Stats* st = unit->st();
  auto glyphs = [&](const std::string& ascii) { text_from_ascii(ascii.c_str(), text); };
  switch (page) {
    case 0: {
      char name[17] = {};
      std::memcpy(name, static_cast<const EnemyUnit*>(unit)->name, 16);
      glyphs(name);
      break;
    }
    case 1:
      glyphs(image_string(0x8006D0D8) + field::format_decimal(digits, st->hp, 7, ' ') + image_string(0x8006D0DC) +
             field::format_decimal(digits, st->hp_max, 7, ' '));
      break;
    case 2:
      glyphs(image_string(0x8006D0E0) + field::format_decimal(digits, st->mp, 7, ' ') + image_string(0x8006D0DC) +
             field::format_decimal(digits, st->mp_max, 7, ' '));
      break;
    case 3: {
      // Two glyph strings: the element's name and ":Weaker point" / ":Stronger point".
      const u8* a = text_find(element + 0x14);
      const u8* b = text_find(1 < st->element[element] ? 0x1F : 0x1E);
      size_t n = 0;
      auto append = [&](const u8* s) {
        if (s == nullptr) return;
        // (window_strcpy / strcat copy bytes up to the first zero.)
        while (*s != 0 && n + 1 < sizeof text) text[n++] = *s++;
        text[n] = 0;
      };
      append(a);
      append(b);
      break;
    }
    default: break;
  }
  const s32 width = text_width(text) + 0x14;
  if ((g.flags & 4) == 0) {
    bool slide = false;
    if (step == 0) {
      x = static_cast<s16>(-width);
      w = static_cast<s16>(width);
      step = 1;
      ticks = 0;
      speed = 0x10;
      slide = true;
    } else if (step == 1) {
      slide = true;
    } else if (step == 2) {
      ticks += g.frame_ticks;
      if (0x3C < ticks || 2 < g.state) step = 3;
    } else if (step == 3) {
      const s32 nx = static_cast<s16>(static_cast<u16>(x) + g.frame_ticks * -10);
      x = static_cast<s16>(nx);
      if (nx <= -width - 8) {
        step = 0;
        if (page == 3) ++element;
        else ++page;
        if (page == 3) {
          while (element < 10 && st->element[element] == 0) ++element;
        }
        if ((page == 3 && 9 < element) || 2 < g.state) {
          sprite_free(icon);
          icon = nullptr;
          release();
          g_scan = nullptr;
          delete g_scan_closed;
          g_scan_closed = this;
          if (g_scan_queue_count != 0) {
            Unit* next = g_scan_queue[0];
            for (int i = 0; i < 3; ++i) g_scan_queue[i] = g_scan_queue[i + 1];
            --g_scan_queue_count;
            g_scan_queue[3] = nullptr;  // (0x8007088c)
            scan_window_show(next);
          }
          return;
        }
      }
    }
    if (slide) {
      const s32 nx = static_cast<s16>(static_cast<u16>(x) + (speed * g.frame_ticks) / 2);
      x = static_cast<s16>(nx);
      if (-0x31 < nx) {
        speed -= g.frame_ticks;
        if (speed < 8) speed = 8;
      }
      if (7 < x) {
        x = 8;
        step = 2;
        if (page == 0) bsfx_play_ui(0xE);
      }
    }
  }
  window_text_begin(this);
  text_draw(this, 4, 0, text, 0);
  window_text_end(this);
  sprite_set_screen_pos(icon, static_cast<s16>(x + width - 0x2F), static_cast<s16>(y + 0x17));
  window_draw_frame(this);
}

}  // namespace

void spell_clash_window_open() {
  if (g_clash != nullptr) return;
  SpellClashWindow* w = new SpellClashWindow();
  g_clash = w;
  // spell_clash_window_init 0x800628ac.
  w->open(0xC0, 0xCA, 0x80, 0x1C);
  for (int i = 0; i < 2; ++i) {
    const SpellCast* sc = spellcast_slot(i);
    int anim;
    if (sc != nullptr && static_cast<u8>(sc->combo - 3) < 2) anim = i != 0 ? 0x43 : 0x42;
    else anim = i != 0 ? 0x42 : 0x43;
    w->side_icon[i] = sprite_spawn_ui(anim, 0, nullptr);
    if (w->side_icon[i] != nullptr) sprite_set_screen_pos(w->side_icon[i], static_cast<s16>(w->x + 0x25 * (i + 1)), static_cast<s16>(w->y + 8));
  }
  w->banner = sprite_spawn_ui(0x41, 0, nullptr);
  sprite_set_screen_pos(w->banner, static_cast<s16>(w->x + 0x54), w->y);
  w->blink = 0;
  ot_layer_to_front(bg().prim_pool_b);
}

void skill_notice_show(int skill) {
  if (g_notice == nullptr) {
    g_notice = new SkillNoticeWindow();
    skill_notice_init(g_notice, skill);
  } else {
    skill_notice_add(g_notice, skill);
  }
}

void scan_window_show(Unit* enemy) {
  if (2 < bg().state || enemy == nullptr) return;
  if (g_scan == nullptr) {
    g_scan = new ScanWindow();
    scan_window_init(g_scan, enemy);
  } else if (g_scan_queue_count < 4) {
    g_scan_queue[g_scan_queue_count++] = enemy;
  } else {
    bsfx_play_ui(7);
  }
}

void spell_windows_reset() {
  delete g_clash;
  g_clash = nullptr;
  delete g_clash_closed;
  g_clash_closed = nullptr;
  delete g_notice;
  g_notice = nullptr;
  delete g_notice_closed;
  g_notice_closed = nullptr;
  delete g_scan;
  g_scan = nullptr;
  delete g_scan_closed;
  g_scan_closed = nullptr;
  g_scan_queue_count = 0;
  for (Unit*& u : g_scan_queue) u = nullptr;
}

}  // namespace ss::game::battle
