#include "game/battle/hud.h"

#include <cstring>
#include <memory>

#include "game/battle/input.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/boot/sound.h"
#include "game/common/screen.h"
#include "game/field/post_battle.h"
#include "runtime/libgpu.h"

namespace ss::game::battle {
namespace {

std::unique_ptr<BattleHud> g_hud;            // 0x8006fa9c
std::unique_ptr<DebugWindow> g_debug_window; // 0x8006faa0
MessageWindow* g_message = nullptr;          // 0x8006faa8 (deletes itself when released)

void add_prim(BWindow* w, void* p) { psyq::addPrim(w->ctx.ot_head, p); }

template <typename T>
T* alloc(BWindow* w) {
  return screen_alloc_prims<T>(w->ctx.arena, 1);
}

void queue_draw_mode(BWindow* w, int dfe, int dtd, int tpage) {
  if (auto* m = alloc<psyq::DR_MODE>(w)) {
    psyq::SetDrawMode(m, dfe, dtd, tpage, nullptr);
    add_prim(w, m);
  }
}

const char* str(u32 address) { return reinterpret_cast<const char*>(battle_data(address)); }

void small_text(BWindow* w, int x, int y, const char* s, int pal) { window_draw_small_text(w, x, y, s, pal); }

// 0x8003c838 colour_lerp: per channel from + (to - from) * t >> 12 (t 0..0x1000).
u32 colour_lerp(u32 from, u32 to, s32 t) {
  const u32 r0 = (from & 0xFF) * 0x10, g0 = from >> 4 & 0xFF0, b0 = from >> 0xC & 0xFF0;
  const u32 r1 = (to & 0xFF) * 0x10, g1 = to >> 4 & 0xFF0, b1 = to >> 0xC & 0xFF0;
  const u32 r = static_cast<u32>(static_cast<s32>(r0) + (static_cast<s32>((r1 - r0) * static_cast<u32>(t)) >> 12)) >> 4;
  const u32 g = (g0 + (static_cast<s32>((g1 - g0) * static_cast<u32>(t)) >> 12)) * 0x10 & 0xFF00;
  const u32 b = (b0 + (static_cast<s32>((b1 - b0) * static_cast<u32>(t)) >> 12)) * 0x1000 & 0xFF0000;
  return r | g | b;
}

// Palette of a value against its maximum: 10 (red) at 0, 15 below a quarter, else 0.
int value_palette(s32 value, s32 max) {
  if (value == 0) return 10;
  s32 q = max;
  if (q < 0) q += 3;
  return value < (q >> 2) ? 0xF : 0;
}

int mp_palette(s32 mp, s32 mp_max) {
  if (mp_max == 0) return 0;
  return value_palette(mp, mp_max);
}

void free_sprite(Sprite*& s) {
  sprite_free(s);
  s = nullptr;
}

// The killer-move button icon for a config key bit.
int key_icon_anim(u16 key, int previous) {
  switch (key) {
    case 1: return 0xE;
    case 2: return 0x10;
    case 4: return 0xD;
    case 8: return 0xF;
    case 0x10: return 10;
    case 0x20: return 9;
    case 0x40: return 0xC;
    case 0x80: return 0xB;
    default: return previous;  // the console keeps whatever s3 held
  }
}

// The controlled member's numbers, the killer-move buttons and names (target select).
void hud_draw_target_stats(BattleHud* h, int c, u8* slot, u8* rec) {
  BattleGlobals& g = bg();
  const Stats* s = stats_of(slot);
  small_text(h, 0x78, 0, reinterpret_cast<const char*>(rec + 0x24), 0);
  small_text(h, 0x62, 8, str(0x8006D118), 7);
  small_text(h, 0x62, 0x10, str(0x8006D11C), 7);
  char buf[16];
  small_text(h, 0x7C, 8, field::format_decimal(buf, s->hp, 4, ' '), value_palette(s->hp, s->hp_max));
  small_text(h, 0x7C, 0x10, field::format_decimal(buf, s->mp, 4, ' '), mp_palette(s->mp, s->mp_max));
  small_text(h, 0x98, 8, str(0x8006D12C), 0);
  small_text(h, 0x98, 0x10, str(0x8006D12C), 0);
  small_text(h, 0xA4, 8, field::format_decimal(buf, s->hp_max, 4, ' '), 0);
  small_text(h, 0xA4, 0x10, field::format_decimal(buf, s->mp_max, 4, ' '), 0);
  hud_draw_gauge(h, 0x80, 0x13, 0x48, h->hp_shown[c], s->hp_max);
  hud_draw_gauge(h, 0x80, 0x1B, 0x48, h->mp_shown[c], s->mp_max);
  queue_draw_mode(h, 0, 1, 0x20);
  int anim = 0;  // the console's s3: an unknown key keeps the previous button
  for (int i = 0; i < 2; ++i) {
    if (h->key_icon[i] != nullptr) continue;
    u16 key;
    std::memcpy(&key, g.config + 8 + i * 2, 2);
    anim = key_icon_anim(key, anim);
    h->key_icon[i] = sprite_spawn_ui(anim, 0, nullptr);
    sprite_set_screen_pos(h->key_icon[i], static_cast<s16>(h->x + 0xD6), static_cast<s16>(h->y + i * 0x10 + 6));
  }
  if (h->clip == 0) window_text_begin(h);
  PartyUnit* u = g.party_units[c];
  for (int i = 0; i < 2; ++i) {
    const u32 move = rec[0xCC + i];
    if (move == 0) continue;
    const s16 id = static_cast<s16>(slot[0] | slot[1] << 8);
    // Greyed while sealed (effect word 2 bit 31) or short of the move's MP (vtable [35]).
    const int cost = u != nullptr ? u->killer_move_cost(i) : 0;
    const bool grey = (u != nullptr && u->status_test(2, 0x80000000) != 0) || s->mp < cost;
    text_draw_id(h, h->x + 0xDE, h->y + i * 0x10 - 2, static_cast<int>(move + (id - 1) * 0x32 + 4999), grey ? 1 : 0);
  }
  if (h->clip == 0) window_text_end(h);
}

void hud_draw_target_select(BattleHud* h) {
  BattleGlobals& g = bg();
  free_sprite(h->run_icon);
  free_sprite(h->cursor);
  free_sprite(h->member_cursor);
  h->run_icon = sprite_spawn_ui(-1, 0, nullptr);
  free_sprite(h->item_icon);
  free_sprite(h->item_ready);
  h->cursor = sprite_spawn_ui(4, 0, nullptr);
  for (Sprite*& t : h->tactic_icon) free_sprite(t);
  const int c = g.controlled_slot & 3;
  u8* slot = g.party_slot[c];
  u8* rec = g.party_record[c];
  if (g.target_enemy != 0xFF && !unit_is_down(g.enemy_units[g.target_enemy & 7]))
    small_text(h, 0, 4, g.enemy_units[g.target_enemy & 7]->name, 0);
  if (slot != nullptr && rec != nullptr) hud_draw_target_stats(h, c, slot, rec);
  window_save_rect(h);
  h->w = 0x60;
  window_draw_frame(h);
  h->x = 0x60;
  h->w = 0x70;
  window_draw_frame(h);
  h->x = 0xD0;
  h->w = 0x70;
  window_draw_frame(h);
  window_restore_rect(h);
}

}  // namespace

BattleHud* battle_hud() { return g_hud.get(); }
MessageWindow* battle_message() { return g_message; }

void battle_windows_create() {
  g_hud = std::make_unique<BattleHud>();
  g_debug_window = std::make_unique<DebugWindow>();
  g_message = nullptr;
}

void battle_windows_open() {
  g_hud->open(0, 0, 0x140, 0x28);
  g_debug_window->open(0, 0x28, 0x10, 0x10);
}

void battle_windows_free() {
  if (g_message != nullptr) {
    delete g_message;
    g_message = nullptr;
  }
  g_debug_window.reset();
  g_hud.reset();
}

void hud_init(BattleHud* h) {
  BattleGlobals& g = bg();
  h->run_icon = sprite_spawn_ui(-1, 0, nullptr);
  h->cursor = sprite_spawn_ui(4, 0, nullptr);
  h->member_cursor = nullptr;
  h->phase = static_cast<s16>(0xFF01);
  h->amp = 0;
  h->key_icon[0] = h->key_icon[1] = nullptr;
  for (int i = 0; i < 4; ++i) {
    h->tactic_icon[i] = nullptr;
    g.hud_member_flash[i] = 0;
    const u8* slot = g.party_slot[i];
    if (slot != nullptr && static_cast<s16>(slot[0] | slot[1] << 8) > 0) {
      h->hp_shown[i] = stats_of(const_cast<u8*>(slot))->hp;
      h->mp_shown[i] = stats_of(const_cast<u8*>(slot))->mp;
    }
  }
  h->item_icon = nullptr;
  h->item_ready = nullptr;
  h->visible = 1;
  h->clip = 0;
}

void hud_set_visible(BattleHud* h, int on) {
  if (on != 0) {
    hud_init(h);
    return;
  }
  h->visible = 0;
  free_sprite(h->run_icon);
  free_sprite(h->cursor);
  free_sprite(h->member_cursor);
  free_sprite(h->key_icon[0]);
  free_sprite(h->key_icon[1]);
  free_sprite(h->item_icon);
  free_sprite(h->item_ready);
}

void hud_draw_gauge(BWindow* w, int x, int y, int width, s32 value, s32 max) {
  const s32 len = width - 1;
  const s16 fill = static_cast<s16>(max == 0 ? 0 : (len * value) / max);
  s32 pct = max == 0 ? 0 : (value * 0xC0) / max;
  const u32 right = static_cast<u32>(pct) << 8 | static_cast<u32>(0xC0 - pct) | static_cast<u32>((pct < 0 ? pct + 3 : pct) >> 2) << 16;
  if (auto* p = alloc<psyq::POLY_G4>(w)) {
    psyq::setPolyG4(p);
    const u32 left = 0xC0C0;
    std::memcpy(&p->r0, &left, 3);
    std::memcpy(&p->r1, &right, 3);
    std::memcpy(&p->r2, &left, 3);
    std::memcpy(&p->r3, &right, 3);
    p->x0 = p->x2 = static_cast<s16>(w->x + x);
    p->x1 = p->x3 = static_cast<s16>(w->x + x + fill);
    p->y0 = p->y1 = static_cast<s16>(w->y + y);
    p->y2 = p->y3 = static_cast<s16>(w->y + y + 4);
    add_prim(w, p);
  }
  if (auto* t = alloc<psyq::TILE>(w)) {
    psyq::setTile(t);
    t->r0 = 0x40, t->g0 = 0x20, t->b0 = 0x10;
    t->x0 = static_cast<s16>(w->x + x + fill);
    t->y0 = static_cast<s16>(w->y + y);
    t->w = static_cast<s16>(len - fill);
    t->h = 4;
    add_prim(w, t);
  }
  if (auto* l = alloc<psyq::LINE_F3>(w)) {
    psyq::setLineF3(l);
    l->r0 = 0x38, l->g0 = 0x20, l->b0 = 0x20;
    // The console leaves the window x out of these points.
    l->x0 = static_cast<s16>(x + 1);
    l->y0 = static_cast<s16>(w->y + y + 4);
    l->x1 = static_cast<s16>(x + len + 1);
    l->y1 = static_cast<s16>(w->y + y + 4);
    l->x2 = static_cast<s16>(x + len + 1);
    l->y2 = static_cast<s16>(w->y + y);
    add_prim(w, l);
  }
}

void BattleHud::draw() {
  BattleGlobals& g = bg();
  if (g.sound_test_tampered != 0) {
    // The sound-test record failed its check: a notice and the game stops.
    window_save_rect(this);
    x = 0x30, y = 0x70, w = 0xD8, h = 0x1C;
    window_text_begin(this);
    text_draw_id(this, 4, 0, 0x10, 0);
    window_text_end(this);
    window_draw_frame(this);
    window_restore_rect(this);
    if ((g.flags & 0x20) == 0) sound_pause(g.sound, 0, 0);
    g.flags = static_cast<u8>(g.flags | 0x24);
  } else if (g.flags & 0x20) {
    // (The disc checks: the host's disc never opens or fails.)
    g.flags = static_cast<u8>(g.flags & 0xDB);
    sound_resume(g.sound, 0);
  }
  if (visible == 0) return;
  if (clip != 0) window_text_begin(this);
  const s32 ticks = g.frame_ticks;
  for (int i = 0; i < 4; ++i) {
    const u8* slot = g.party_slot[i];
    if (slot == nullptr || static_cast<s16>(slot[0] | slot[1] << 8) < 1) continue;
    const Stats* s = stats_of(const_cast<u8*>(slot));
    s32 hs = s->hp_max;
    if (hs < 0) hs += 0x3F;
    hs >>= 6;
    if (hs == 0) hs = 1;
    s32 ms = s->mp_max;
    if (ms < 0) ms += 0x3F;
    ms >>= 6;
    if (ms == 0) ms = 1;
    hs *= ticks;
    ms *= ticks;
    if (hp_shown[i] < s->hp) {
      hp_shown[i] += hs;
      if (s->hp < hp_shown[i]) hp_shown[i] = s->hp;
    } else if (s->hp < hp_shown[i]) {
      hp_shown[i] -= hs;
      if (hp_shown[i] < s->hp) hp_shown[i] = s->hp;
    }
    if (mp_shown[i] < s->mp) {
      mp_shown[i] += ms;
      if (s->mp < mp_shown[i]) mp_shown[i] = s->mp;
    } else if (s->mp < mp_shown[i]) {
      mp_shown[i] -= ms;
      if (mp_shown[i] < s->mp) mp_shown[i] = s->mp;
    }
  }
  if (battle_input().mode == 2) {
    hud_draw_target_select(this);
  } else {
    small_text(this, 8, 8, str(0x8006D118), 7);
    small_text(this, 8, 0x10, str(0x8006D11C), 7);
    free_sprite(key_icon[0]);
    free_sprite(key_icon[1]);
    char buf[16];
    for (int i = 0; i < 4; ++i) {
      const int off = i * 0x3B;
      u8* slot = g.party_slot[i];
      if (slot == nullptr) continue;
      const Stats* s = stats_of(slot);
      const u8* rec = g.party_record[i];
      small_text(this, 0x22 + off, 0, reinterpret_cast<const char*>(rec + 0x24), 0);
      small_text(this, 0x36 + off, 8, field::format_decimal(buf, s->hp, 4, ' '), value_palette(s->hp, s->hp_max));
      small_text(this, 0x36 + off, 0x10, field::format_decimal(buf, s->mp, 4, ' '), mp_palette(s->mp, s->mp_max));
      int sx = off + 0x22, sy = 0x10;
      const u8 st = s->status;
      if (st & 4) {
        small_text(this, sx, 0x10, str(0x8006D120), 0);
        sy = 8;
      }
      if (st & 2) {
        small_text(this, sx, sy, str(0x8006D124), 0);
        sy -= 8;
        if (sy < 8) {
          sx = off + 0x2A;
          sy = 0x10;
        }
      }
      if (st & 8) small_text(this, sx, sy, str(0x8006D128), 1);
      hud_draw_gauge(this, 0x2A + off, 0x13, 0x30, hp_shown[i], s->hp_max);
      hud_draw_gauge(this, 0x2A + off, 0x1B, 0x30, mp_shown[i], s->mp_max);
      if (rec[0x38] == 5 && clip == 0) {
        if (tactic_icon[i] == nullptr) {
          tactic_icon[i] = sprite_spawn_ui(0x2C, 0, nullptr);
          sprite_set_screen_pos(tactic_icon[i], static_cast<s16>(x + off + 0x21), static_cast<s16>(y + 0x20));
        }
      } else if (tactic_icon[i] != nullptr) {
        free_sprite(tactic_icon[i]);
      }
    }
    // Running / paused.
    const int want = (g.flags == 0 && g.slowmo_frames == 0) ? 2 : 1;
    if ((run_icon == nullptr || run_icon->anim_id != want) && clip == 0) {
      sprite_free(run_icon);
      run_icon = sprite_spawn_ui(want, 0, nullptr);
      sprite_set_screen_pos(run_icon, static_cast<s16>(x + 0x120), static_cast<s16>(y + 7));
    }
    if (item_icon == nullptr) {
      item_icon = sprite_spawn_ui(0x3B, 0, nullptr);
      sprite_set_screen_pos(item_icon, static_cast<s16>(x + 0x10E), static_cast<s16>(y + 0x1C));
    }
    if (g.item_cooldown == 0) {
      if (item_ready == nullptr) {
        item_ready = sprite_spawn_ui(0x3C, 0, nullptr);
        sprite_set_screen_pos(item_ready, static_cast<s16>(x + 0x110), static_cast<s16>(y + 0xF));
      }
    } else {
      free_sprite(item_ready);
    }
    // The item cooldown gauge.
    const s32 cd = g.item_cooldown;
    if (cd != 0) {
      if (auto* p = alloc<psyq::POLY_G4>(this)) {
        psyq::setPolyG4(p);
        const u32 top = colour_lerp(0xC06000, 0x20F0, (cd * 0x1000) / 0xF0);
        const u32 bottom = 0xC06000;
        std::memcpy(&p->r0, &top, 3);
        std::memcpy(&p->r1, &top, 3);
        std::memcpy(&p->r2, &bottom, 3);
        std::memcpy(&p->r3, &bottom, 3);
        p->x0 = p->x2 = static_cast<s16>(x + 0x10F);
        p->x1 = p->x3 = static_cast<s16>(x + 0x11B);
        p->y0 = p->y1 = static_cast<s16>(y - (static_cast<s16>(cd) / 0xC - 0x1C));
        p->y2 = p->y3 = static_cast<s16>(y + 0x1C);
        add_prim(this, p);
      }
    }
    if (auto* t = alloc<psyq::TILE>(this)) {
      psyq::setTile(t);
      psyq::setSemiTrans(t, 1);
      t->r0 = 0, t->g0 = 0xC0, t->b0 = 0x20;
      t->x0 = static_cast<s16>(x + 0x10F);
      t->y0 = static_cast<s16>(y + 8);
      t->w = 0xC;
      t->h = 0x14;
      add_prim(this, t);
    }
    if (auto* l = alloc<psyq::LINE_F3>(this)) {
      psyq::setLineF3(l);
      psyq::setSemiTrans(l, 1);
      l->r0 = 0, l->g0 = 0x20, l->b0 = 0;
      l->x0 = static_cast<s16>(x + 0x110), l->y0 = static_cast<s16>(y + 0x1D);
      l->x1 = static_cast<s16>(x + 0x11C), l->y1 = static_cast<s16>(y + 0x1D);
      l->x2 = static_cast<s16>(x + 0x11C), l->y2 = static_cast<s16>(y + 8);
      add_prim(this, l);
    }
    queue_draw_mode(this, 0, 1, 0);
    if (1 < g.state) {
      const int c = g.controlled_slot;
      sprite_set_screen_pos(cursor, static_cast<s16>(c * 0x3B + x + 0x3C), static_cast<s16>(y + 0x20));
      BattleInput& in = battle_input();
      if (in.mode == 1) {
        if (member_cursor == nullptr) {
          member_cursor = sprite_spawn_ui(in.member_purpose != 0 ? 0x23 : 5, 0, nullptr);
          if (member_cursor != nullptr) member_cursor->scale_y = 0;
        }
        if (member_cursor != nullptr) {
          member_cursor->scale_y += ticks * 0x100;
          if (0x1000 < member_cursor->scale_y) member_cursor->scale_y = 0x1000;
        }
      } else if (member_cursor != nullptr) {
        member_cursor->scale_y += ticks * -0x100;
        if (member_cursor->scale_y < 1) free_sprite(member_cursor);
      }
      if (member_cursor != nullptr)
        sprite_set_screen_pos(member_cursor, static_cast<s16>(c * 0x3B + x + 0x3C), static_cast<s16>(y + 0x2C));
      // The controlled member's panel glows.
      if (auto* p = alloc<psyq::POLY_G4>(this)) {
        psyq::setPolyG4(p);
        u32 colours[4];
        for (int k = 0; k < 4; ++k) {
          s32 t = phase + k * 0x80;
          if (0xFF < t) t -= 0x200;
          if (t < 0) t = -t;
          const s32 a = amp;
          colours[k] = static_cast<u32>((a * t * 0x40) >> 16) | static_cast<u32>((a * t * 0x90) >> 16) << 8 |
                       static_cast<u32>((a * t * 0xA0) >> 16) << 16;
        }
        std::memcpy(&p->r0, &colours[0], 3);
        std::memcpy(&p->r1, &colours[1], 3);
        std::memcpy(&p->r2, &colours[2], 3);
        std::memcpy(&p->r3, &colours[3], 3);
        p->code = 0x3A;
        const s16 base = static_cast<s16>(c * 0x3B + 0x20);
        p->x0 = p->x2 = static_cast<s16>(x + base + 7);
        p->x1 = p->x3 = static_cast<s16>(x + base + 0x38);
        p->y0 = p->y1 = static_cast<s16>(y + 4);
        p->y2 = p->y3 = static_cast<s16>(y + 0x24);
        add_prim(this, p);
      }
      // Members just hit flash.
      for (int i = 0; i < 4; ++i) {
        const s32 f = g.hud_member_flash[i];
        if (f == 0) continue;
        if (auto* t = alloc<psyq::TILE>(this)) {
          psyq::setTile(t);
          psyq::setSemiTrans(t, 1);
          std::memcpy(&t->r0, &f, 3);
          t->x0 = static_cast<s16>(x + 0x1F + i * 0x3B + 8);
          t->y0 = static_cast<s16>(y + 4);
          t->w = 0x34;
          t->h = 0x20;
          add_prim(this, t);
        }
      }
      queue_draw_mode(this, 0, 1, 0x20);
      if ((g.flags & 4) == 0) {
        phase = static_cast<s16>(phase + ticks * 2);
        amp = static_cast<s16>(amp + ticks * 2);
      }
      if (0xFF < phase) phase = static_cast<s16>(phase - 0x200);
      if (0x100 < amp) amp = 0x100;
    }
    window_draw_frame(this);
  }
  for (s32& f : g.hud_member_flash) {
    if (f != 0) {
      f -= ticks * 0x10;
      if (f < 0) f = 0;
    }
  }
  if (clip != 0) {
    if (auto* p = alloc<psyq::DR_AREA>(this)) {
      const psyq::RECT area{x, static_cast<s16>(screen_draw_y() + clip), w, static_cast<s16>(h - clip)};
      psyq::SetDrawArea(p, &area);
      add_prim(this, p);
    }
  }
}

// ---- The message banner -------------------------------------------------------------

void battle_message_show(int id) {
  if (id == 0) return;
  if (g_message == nullptr) {
    g_message = new MessageWindow();
    battle_message_start(g_message, id);
  } else {
    if (g_message->step < 2) g_message->step = 2;
    g_message->queued = id;
  }
}

void battle_message_start(MessageWindow* m, int id) {
  const int width = text_width_id(id) + 0x18;
  m->text_id = id;
  m->open(static_cast<s16>(0xA0 - width / 2), 0xE8, static_cast<s16>(width), 0x1C);
  m->step = 0;
  m->ticks = 0;
  m->queued = 0;
  if (width < 0x19) {
    m->release();
    g_message = nullptr;
    delete m;
  }
}

void MessageWindow::draw() {
  BattleGlobals& g = bg();
  if ((g.flags & 4) == 0) {
    if (step == 1) {
      ticks += g.frame_ticks;
      if (0x3C < ticks || 2 < g.state) step = 2;
    } else if (step == 0) {
      y = static_cast<s16>(y - g.frame_ticks * 3);
      if (y < 0xC1) {
        y = 0xC0;
        step = 1;
      }
    } else if (step == 2) {
      y = static_cast<s16>(y + g.frame_ticks * 3);
      if (0xE7 < y) {
        const int next = queued;
        release();
        g_message = nullptr;
        // The window deletes itself (window_release -> the dtor with flags 3); the task
        // list no longer holds it, so it is safe to go here.
        delete this;
        if (next != 0) battle_message_show(next);
        return;
      }
    }
  }
  // debug_print("MESSAGE(" id ")") is empty.
  window_text_begin(this);
  text_draw_id(this, 4, 0, text_id, 0);
  window_text_end(this);
  window_draw_frame(this);
}

}  // namespace ss::game::battle
