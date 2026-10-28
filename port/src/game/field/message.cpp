#include "game/field/message.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/common/text_rules.h"
#include "game/field/field.h"
#include "game/field/message_internal.h"
#include "game/field/notice.h"
#include "runtime/runtime.h"

// Message system core: the system task, window creation and destruction, the
// text-cell allocator, the window state machine and the typewriter
// (field 0x8005835c-0x8005a7bc, 0x8005e594-0x8005e808).
namespace ss::game::field {
namespace {

MsgSystem* g_msg = nullptr;                       // 0x80075288
MsgWindow* g_windows[kMsgWindowCount] = {};       // message_windows 0x80075668
const u8* g_map_font = nullptr;                   // 0x80075714
const u8* g_map_glyphs = nullptr;                 // 0x80075718

MsgLine& current_line(MsgWindow* w) { return w->lines[(w->first_line + w->used_lines) % w->line_count]; }

// Script flag byte 0x5e bit 1: automatic text (waits end after 300 frames).
bool auto_text() {
  const u8* flags = globals().script_flags;
  return flags && (flags[0x5E] >> 1 & 1) != 0;
}

u16 config_button(int i) {
  const u8* cfg = globals().config;
  return cfg ? static_cast<u16>(cfg[i * 2] | cfg[i * 2 + 1] << 8) : 0;
}

// ---- Port settings (TextOptions, message.h): instant text and auto-advance --------------

TextOptions g_text_options;

bool msg_trace() {
  static const bool on = [] {
    const char* v = std::getenv("SS_MSG_TRACE");
    return v != nullptr && *v != 0 && *v != '0';
  }();
  return on;
}

int window_index(const MsgWindow* w) {
  for (int i = 0; i < kMsgWindowCount; ++i)
    if (g_windows[i] == w) return i;
  return -1;
}

// Auto-advance may stand in for the player's press: no choice is up and no field menu the
// script asked for is pending (msg_vm_choice_or_menu_wait, message_draw.cpp).
bool auto_advance_allowed() { return !msg_vm_choice_or_menu_wait(); }

// Instant text: msg_window_print until the page stops (a wait, a pause, a full window, its
// end). The calls are the ones the typewriter makes over several frames, made at once.
void print_page_at_once(MsgWindow* w) {
  const u8 page = w->page_read;
  for (int guard = 0; guard < 64 && w->state == 4 && w->page_read == page && page != w->page_write; ++guard)
    msg_window_print(w, kMsgInstantChars);
}

}  // namespace

TextOptions& text_options() { return g_text_options; }

s32 g_text_speed[5] = {2, 60, 16, 8, 4};
void (*g_msg_project_anchor)(s16* xy) = nullptr;

MsgSystem* msg_system() { return g_msg; }
MsgWindow* msg_window(int n) { return n >= 0 && n < kMsgWindowCount ? g_windows[n] : nullptr; }

void msg_set_map_font(const u8* font) {
  g_map_font = font;
  g_map_glyphs = font ? font + msg_rd32(font) + 8 : nullptr;
}

const u8* msg_map_font() { return g_map_font; }

// Font access for the text renderer (message_text.cpp).
const u8*& msg_font_ref() { return g_map_font; }
const u8*& msg_glyphs_ref() { return g_map_glyphs; }

MsgSystem* msg_system_create(MsgSystem* self) {
  self->default_x = self->default_y = self->default_w = self->default_h = 0;
  self->tint_enabled = 0;
  for (auto& w : g_windows) w = nullptr;
  self->free_list = &self->head;
  self->head.cells = 0;
  self->free_list->used = 1;
  auto* all = new MsgCells;
  self->free_list->prev = all;
  self->free_list->next = all;
  all->cells = 0x24D;  // 31 x 19 cells
  all->used = 0;
  all->prev = self->free_list;
  all->next = self->free_list;
  g_msg = self;
  task_register(self, 4);
  self->default_x = 1;
  self->default_y = 2;
  self->default_w = 0x18;
  self->default_h = 4;
  return self;
}

MsgSystem::~MsgSystem() { msg_system_destroy(this); }

void msg_system_destroy(MsgSystem* self) {
  for (auto& w : g_windows) {
    if (w != nullptr) {
      msg_window_free(w);
      w = nullptr;
    }
  }
  for (MsgCells* c = self->free_list->next; c != self->free_list;) {
    MsgCells* next = c->next;
    delete c;
    c = next;
  }
  self->free_list->next = self->free_list->prev = self->free_list;
  task_unregister(self);
  if (g_msg == self) g_msg = nullptr;
}

void MsgSystem::update() { msg_system_update(this); }

void msg_system_update(MsgSystem* self) {
  FieldGlobals& g = globals();
  const u8 setting = g.game_state ? g.game_state[0x118] : 3;
  if (const u8* speeds = field_data(0x800751CC + setting * 4u)) std::memcpy(&g_text_speed[0], speeds, 4);
  bool swapped = false;
  const u8* saved_font = nullptr;
  const u8* saved_glyphs = nullptr;
  for (int i = 0; i < kMsgWindowCount; ++i) {
    if (i == 10 && g_side_font != nullptr) {
      // Windows 10-19 print with the 0xfc2 font.
      swapped = true;
      saved_font = g_map_font;
      saved_glyphs = g_map_glyphs;
      g_map_font = g_side_font;
      g_map_glyphs = g_side_glyphs;
    }
    MsgWindow*& w = g_windows[i];
    if (w == nullptr) continue;
    if (msg_window_get_state(w) == 10) {
      msg_cells_free(self, w->cells);
      msg_window_free(w);
      w = nullptr;
      continue;
    }
    if (g.disc_error != 0) {
      if (i == 5) {  // the disc-error message stays live, untinted
        const u8 tint = self->tint_enabled;
        self->tint_enabled = 0;
        msg_window_update(g_windows[5]);
        self->tint_enabled = tint;
      }
      continue;
    }
    msg_window_update(w);
  }
  if (swapped) {
    g_map_font = saved_font;
    g_map_glyphs = saved_glyphs;
  }
}

int msg_window_create(MsgSystem* msg, int x, int y, int w, int h, const u8* text, int window) {
  if (g_windows[window] != nullptr) return 0;
  int width;
  if (w < 0) {
    width = -w;
  } else if (w < 6) {
    width = 6 * 8;
  } else if (w < 0x29) {
    width = w << 3;
  } else {
    width = 0x28 << 3;
  }
  h = h < 0 ? -h : h * 13;
  const int px = x < 0 ? -x : x << 3;
  const int py = y < 0 ? -y : y << 3;
  int cols = width + 7;
  if (cols < 0) cols = width + 0xE;
  MsgCells* cells = msg_cells_alloc(msg, cols >> 3, (h + 0xC) / 0xD);
  MsgWindow* win = msg_window_construct(new MsgWindow);
  g_windows[window] = win;
  win->cells = cells;
  msg_window_init(win, static_cast<s16>(px), static_cast<s16>(py), static_cast<s16>(width), h, text);
  if (static_cast<u32>(window) < 5 && g_notice_object() != nullptr) notice_dismiss(g_notice_object());
  return 1;
}

void msg_window_open(MsgSystem* msg, int x, int y, int w, int h, const u8* text, int window, int hold) {
  if (g_windows[window] == nullptr) {
    msg_window_create(msg, x, y, w, h, text, window);
  } else {
    msg_window_push_text(g_windows[window], text);
  }
  if (hold != 0) g_windows[window]->hold = static_cast<u8>(hold);
}

void msg_window_open_default(MsgSystem* msg, const u8* text, int window, int hold) {
  if (g_windows[window] == nullptr) {
    msg_window_create(msg, msg->default_x, msg->default_y, msg->default_w, msg->default_h, text, window);
  } else {
    msg_window_push_text(g_windows[window], text);
  }
  if (hold != 0) g_windows[window]->hold = static_cast<u8>(hold);
}

void msg_window_close(MsgSystem*, int window) {
  if (g_windows[window] != nullptr) msg_window_begin_close(g_windows[window]);
}

void msg_window_destroy(MsgSystem* msg, int window) {
  auto destroy = [msg](MsgWindow*& w) {
    if (w == nullptr) return;
    msg_cells_free(msg, w->cells);
    msg_window_free(w);
    w = nullptr;
  };
  if (window == -1) {
    for (auto& w : g_windows) destroy(w);
  } else {
    destroy(g_windows[window]);
  }
}

void msg_window_destroy_ptr(MsgSystem* msg, MsgWindow* w) {
  for (auto& slot : g_windows) {
    if (slot == w) {
      msg_cells_free(msg, slot->cells);
      msg_window_free(slot);
      slot = nullptr;
      return;
    }
  }
}

int msg_window_status(MsgSystem*, int window) {
  if (window == -1) {
    for (MsgWindow* w : g_windows) {
      if (w == nullptr) continue;
      const int state = msg_window_get_state(w);
      if (state != 1) return state;
    }
  } else if (g_windows[window] != nullptr) {
    return msg_window_get_state(g_windows[window]);
  }
  return -1;
}

MsgCells* msg_cells_alloc(MsgSystem* msg, int cols, int lines) {
  int base = 0;
  MsgCells* block = msg->free_list->next;
  for (;; block = block->next) {
    if (block == msg->free_list) return nullptr;
    if (block->used == 0 && cols * lines <= block->cells) break;
    base += block->cells;
  }
  auto* rest = new MsgCells;
  for (int line = 0; line < lines; ++line) {
    int run = 0;
    if (cols > 0) {
      int left = cols;
      do {
        block->pos[line][run][0] = base % 31;
        block->pos[line][run][1] = base / 31;
        if ((base + left) / 31 == base / 31) {
          block->run_len[line][run] = static_cast<u16>(left);
        } else {
          block->run_len[line][run] = static_cast<u16>(31 - block->pos[line][run][0]);
        }
        const int n = block->run_len[line][run];
        ++run;
        left -= n;
        base += n;
      } while (left > 0);
    }
    for (; run < 3; ++run) block->run_len[line][run] = 0;
  }
  rest->used = 0;
  rest->cells = static_cast<u16>(block->cells - cols * lines);
  block->next->prev = rest;
  rest->next = block->next;
  block->next = rest;
  rest->prev = block;
  block->cells = static_cast<u16>(cols * lines);
  block->used = 1;
  return block;
}

void msg_cells_free(MsgSystem*, MsgCells* cells) {
  if (cells == nullptr) return;
  MsgCells* block = cells;
  if (cells->prev->used == 0) {
    cells->next->prev = cells->prev;
    cells->prev->next = cells->next;
    cells->prev->cells = static_cast<u16>(cells->prev->cells + cells->cells);
    block = cells->prev;
    delete cells;
  }
  MsgCells* next = block->next;
  if (next->used == 0) {
    next->next->prev = block;
    block->cells = static_cast<u16>(block->cells + next->cells);
    block->next = next->next;
    delete next;
  }
  block->used = 0;
}

void msg_window_resize(MsgSystem* msg, int window, int w, int h) {
  msg_cells_free(msg, g_windows[window]->cells);
  int width;
  if (w < 0) {
    width = -w;
  } else if (w < 6) {
    width = 6 * 8;
  } else if (w < 0x29) {
    width = w << 3;
  } else {
    width = 0x28 << 3;
  }
  h = h < 0 ? -h : h * 13;
  int cols = width + 7;
  if (cols < 0) cols = width + 0xE;
  g_windows[window]->cells = msg_cells_alloc(msg, cols >> 3, (h + 0xC) / 0xD);
}

u8* msg_system_tint(MsgSystem* msg) { return msg->tint; }
void msg_system_set_tint_enabled(MsgSystem* msg, int on) { msg->tint_enabled = static_cast<u8>(on); }

MsgWindow* msg_window_construct(MsgWindow* w) {
  for (auto& m : w->moves) m.x = m.y = 0;
  w->line_height = 0xD;
  w->anchor[1] = 0;
  w->anchor[0] = 0;
  w->continuing = 0;
  w->custom_clip = 0;
  w->hold = 0;
  w->justify = 1;
  w->body_semi = 1;
  w->speed = 0;
  w->state = 1;
  w->page_read = w->page_write = 0;
  w->page_offset = 0;
  w->timer = 0;
  w->unknown_137 = 0;
  w->unknown_ce = 0;
  w->first_line = w->used_lines = w->text_height = w->page_height = 0;
  w->draw_frame = 1;
  w->draw_body = 1;
  w->scroll_offset = 0;
  w->scroll_pending = 0;
  w->line_count = 0;
  w->move_read = w->move_write = 0;
  w->ot_offset = 0;
  w->cancel_closes = 0;
  w->fade_in = 0;
  w->blink = 1;
  w->insert_start = 1;
  w->show_prompt = 1;
  w->force_advance = 0;
  w->page_new_line = 1;
  w->button_advance = 1;
  w->anchored = 0;
  w->word_wrap = 1;
  w->pending_glyphs = 0;
  w->draw_callback = nullptr;
  w->close_callback = nullptr;
  if (const u8* cfg = globals().config) std::memcpy(w->colour, cfg + 0x30, 16);
  msg_window_init_shadow_lut(w);
  msg_window_set_text_colour(w, 3);
  w->lines = nullptr;
  return w;
}

void msg_window_free(MsgWindow* w) {
  for (int i = 0; i < w->line_count; ++i) msg_window_clear_line(w, i);
  delete[] w->lines;
  delete w;
}

void msg_window_init(MsgWindow* w, s16 x, s16 y, s16 w_px, int h_px, const u8* text) {
  msg_window_push_text(w, text);
  const int lh = w->line_height;
  w->x = x;
  w->y = y;
  w->w = w_px;
  w->h = static_cast<s16>(h_px);
  w->line_count = static_cast<s16>((h_px + lh - 1) / lh);
  if (w->line_count > kMsgMaxLines) w->line_count = kMsgMaxLines;
  w->timer = 10;
  w->state = 2;
  w->lines = new MsgLine[static_cast<size_t>(w->line_count)]();
}

void msg_window_begin_close(MsgWindow* w) {
  if (static_cast<u8>(w->state - 9) > 1) {
    w->timer = 0;
    w->state = 9;
  }
}

void msg_window_update(MsgWindow* w) {
  FieldGlobals& g = globals();
  if (w->anchored != 0) {
    const s16 old_x = w->anchor[0], old_y = w->anchor[1];
    if (g_msg_project_anchor) g_msg_project_anchor(w->anchor);
    w->x = static_cast<s16>(w->x + (old_x - w->anchor[0]));
    w->y = static_cast<s16>(w->y + (old_y - w->anchor[1]));
    w->draw_x = static_cast<s16>(w->draw_x + (old_x - w->anchor[0]));
    w->draw_y = static_cast<s16>(w->draw_y + (old_y - w->anchor[1]));
  }
  int ticks = g.frame_ticks;
  w->blink = static_cast<u8>(static_cast<u8>(w->blink + ticks) % 56);
  // Last frame's upload buffers are done.
  for (int i = 0; i < w->line_count; ++i) {
    for (auto*& b : w->lines[i].bitmap) {
      delete[] b;
      b = nullptr;
    }
  }
  const u16 pressed = g.pressed[0];
  if (w->state != 9 && w->cancel_closes != 0 && (pressed & config_button(1)) != 0) {
    w->timer = 0;
    if (w->hold == 0) w->state = 9;
  }
  int saved_speed = -1;
  if (static_cast<u8>(w->state - 3) < 2 && ((pressed & config_button(0)) != 0 || (pressed & config_button(1)) != 0)) {
    saved_speed = w->speed;  // a button press speeds the text up for this frame
    w->speed = 3;
  }
  // Port (TextOptions): how long the window has waited for confirm, and auto-advance's
  // stand-in press once it has waited the set time.
  bool auto_press = false;
  if (static_cast<u8>(w->state - 6) < 2 && w->button_advance != 0) {
    w->button_wait = static_cast<u16>(std::min(w->button_wait + std::max(ticks, 0), 0xFFFF));
    const int delay = g_text_options.auto_advance_frames;
    auto_press = delay > 0 && w->button_wait >= delay && auto_advance_allowed();
  } else {
    w->button_wait = 0;
  }
  const bool instant = g_text_options.instant;
  int instant_steps = 0;  // instant text: printing / scrolling rounds this frame (a safety cap)
  bool again = true;
  w->page_started = 0;
  while (again) {
    if (w->move_read != w->move_write) msg_window_step_move(w);
    again = false;
    switch (w->state) {
      case 1:  // idle: a new page starts printing
        if (w->page_read == w->page_write) break;
        w->insert_start = 1;
        w->page_offset = 0;
        w->continuing = 0;
        if (w->page_new_line == 0) {
          w->page_started = 0;
          w->scroll_pending = 0;
        } else if (w->pending_glyphs == 0) {
          w->scroll_pending = 1;
        } else {
          if (msg_window_new_line(w) != 1) break;
          w->page_started = 1;
          w->scroll_pending = 0;
        }
        w->state = 4;
        again = true;
        break;
      case 2:  // opening
        w->timer = static_cast<s16>(w->timer - ticks);
        msg_window_step_open_anim(w);
        if (w->timer > 0) break;
        w->state = 4;
        ticks = -w->timer;
        again = ticks != 0;
        break;
      case 3:  // delay
        w->timer = static_cast<s16>(w->timer - ticks);
        if (w->timer > 0) break;
        w->state = 4;
        ticks = -w->timer;
        again = ticks != 0;
        break;
      case 4: {  // printing
        if (g.frame_ticks == 0) break;
        if (w->page_read != w->page_write) {
          if ((w->page_offset == 0 && w->insert_start != 0 && w->page_started == 0) || w->scroll_pending != 0) {
            text_render_line(w, w->pages[w->page_read] + w->page_offset,
                             (w->first_line + w->used_lines) % w->line_count, 0, nullptr);
            w->scroll_pending = 0;
          }
          w->page_started = 0;
          if (instant) {
            // Port (TextOptions::instant): the page up to its next stop, at once; a line
            // that scrolls in (state 5) is scrolled and printed in this frame too.
            print_page_at_once(w);
            if (w->state == 5) again = ++instant_steps < 64;
          } else if (w->speed < 5 && g_text_speed[w->speed] > 0) {
            msg_window_print(w, g_text_speed[w->speed]);
            if (ticks == 1 && w->state == 4) {
              w->state = 3;
              w->timer = static_cast<s16>(ticks);
            }
          } else {
            msg_window_print(w, 1);
            if (w->state != 4) break;
            if (w->speed >= 6) {
              w->state = 3;
              w->timer = static_cast<s16>(w->speed - 5);
            } else if (w->speed < 5) {
              w->state = 3;
              w->timer = static_cast<s16>(-g_text_speed[w->speed]);
            }
          }
        }
        if (w->state == 4 && --ticks > 0) again = true;
        break;
      }
      case 5:  // scrolling
        if (instant) {
          // Port (TextOptions::instant): the whole scroll at once (2 pixels a frame
          // otherwise), then the page goes on printing in this frame.
          msg_window_scroll(w, (std::max<int>(w->scroll_remaining, 0) + 1) / 2);
          w->state = 4;
          ticks = 1;
          again = ++instant_steps < 64;
          break;
        }
        msg_window_scroll(w, ticks);
        if (w->scroll_remaining > 0) break;
        w->state = 4;
        ticks = -w->scroll_remaining;
        again = ticks != 0;
        break;
      case 6:
      case 7:  // waiting for the confirm button (7: the window empties first)
        if (w->button_advance == 0) break;
        if ((pressed & config_button(0)) == 0 && w->force_advance == 0 && !auto_press) break;
        if (msg_trace()) {
          std::fprintf(stderr, "[msg] vblank %llu window %d wait %d -> %s\n",
                       static_cast<unsigned long long>(runtime::Runtime::get().vblanks()), window_index(w),
                       w->button_wait,
                       (pressed & config_button(0)) != 0 ? "button" : w->force_advance != 0 ? "forced" : "auto");
        }
        w->button_wait = 0;
        if (w->state == 7) {
          w->used_lines = 0;
          w->text_height = 0;
        }
        w->force_advance = 0;
        if (w->scroll_pending != 0) {
          w->state = 5;
          w->scroll_remaining = w->scroll_pending;
        } else {
          w->state = 4;
        }
        w->page_height = 0;
        if (instant) again = ++instant_steps < 64;  // instant text: the next page in this frame
        break;
      case 8: {  // timed wait
        w->timer = static_cast<s16>(w->timer - ticks);
        const bool button = w->button_advance != 0 && (pressed & config_button(0)) != 0;
        if (!button && w->force_advance == 0 && w->timer > 0) break;
        if (msg_trace()) {
          std::fprintf(stderr, "[msg] vblank %llu window %d timed wait, %d frames left -> %s\n",
                       static_cast<unsigned long long>(runtime::Runtime::get().vblanks()), window_index(w),
                       std::max<int>(w->timer, 0), button ? "button" : w->force_advance != 0 ? "forced" : "timer");
        }
        w->force_advance = 0;
        if (w->scroll_pending != 0) {
          w->state = 5;
          w->scroll_remaining = w->scroll_pending;
        } else {
          w->state = 4;
        }
        w->page_height = 0;
        if (instant) again = ++instant_steps < 64;  // instant text: the next page in this frame
        break;
      }
      case 9:  // closing
        w->timer = static_cast<s16>(w->timer + ticks);
        msg_window_step_open_anim(w);
        if (w->timer < 10) break;
        if (w->close_callback) w->close_callback();
        w->state = 10;
        ticks = w->timer - 10;
        again = ticks != 0;
        break;
      case 10:
        msg_window_destroy_ptr(g_msg, w);
        return;
      default:
        break;
    }
    if (w->state == 6) break;
  }
  msg_window_draw(w);
  if (saved_speed != -1 && w->speed == 3) w->speed = static_cast<u8>(saved_speed);
}

void msg_window_scroll(MsgWindow* w, int frames) {
  s16 step = static_cast<s16>(frames * 2);
  if (w->scroll_remaining - frames * 2 < 0) step = w->scroll_remaining;
  w->scroll_offset = static_cast<s16>(w->scroll_offset - step);
  w->scroll_remaining = static_cast<s16>(w->scroll_remaining - step);
  for (;;) {
    const s16 lh = w->lines[w->first_line].height;
    if (-w->scroll_offset < lh) break;
    w->scroll_offset = static_cast<s16>(w->scroll_offset + lh);
    msg_window_clear_line(w, w->first_line);
    w->first_line = static_cast<s16>((w->first_line + 1) % w->line_count);
    if (w->used_lines != 0) --w->used_lines;
  }
  if (w->scroll_remaining < 1) w->scroll_remaining = static_cast<s16>(step - frames * 2);
}

void msg_window_step_move(MsgWindow* w) {
  MsgMove& m = w->moves[w->move_read];
  if (m.t == 0) {
    w->move_x = w->x;
    w->move_y = w->y;
  }
  m.t = static_cast<u16>(m.t + globals().frame_ticks);
  if (m.t < m.frames) {
    const int frames = m.frames;
    w->x = static_cast<s16>(w->move_x + ((((m.x - w->move_x) * 0x1000) / frames) * m.t >> 12));
    w->y = static_cast<s16>(w->move_y + ((((m.y - w->move_y) * 0x1000) / frames) * m.t >> 12));
  } else {
    w->x = static_cast<s16>(m.x);
    w->move_read = static_cast<u8>((w->move_read + 1) & 7);
    w->y = static_cast<s16>(m.y);
  }
  w->draw_x = w->x;
  w->draw_y = w->y;
}

void msg_window_step_open_anim(MsgWindow* w) {
  const int t = w->timer;
  if (t < 1) {
    w->draw_x = w->x;
    w->draw_y = w->y;
    w->draw_w = w->w;
    w->draw_h = static_cast<s16>(w->h + 0x10);
    return;
  }
  int dw, dh;
  if (t < 5) {
    dw = w->w;
    dh = (w->h * (5 - t)) / 5;
  } else {
    dh = 8;
    dw = (w->w * (10 - t)) / 5;
  }
  const int hh = dh > 0xF ? dh : 8;
  w->draw_w = static_cast<s16>(dw);
  w->draw_h = static_cast<s16>(hh);
  w->draw_x = static_cast<s16>((w->x * 2 + w->w) / 2 - dw / 2);
  w->draw_y = static_cast<s16>((w->y * 2 + w->h + 0x10) / 2 - hh / 2);
}

void msg_window_print(MsgWindow* w, int chars) {
  const text::MapGlyphs& push = text::map_glyphs(text::lang());  // opening brackets / quotes
  const u8* p = w->pages[w->page_read] + w->page_offset;
  int left = chars;    // characters this call may still reveal
  int synced = chars;  // 'left' when the current line's count was last updated
  int line_left = current_line(w).remaining;
  u32 prev = 0;        // previous code (the original's s6 holds the caller's line counter here)
  u32 code = 0;
  if (left <= 0) goto store;
  for (;;) {
    if (*p == 0 || left <= 0) {
      // End of the page, or of this call's budget.
      if (w->state != 4) goto store;
      current_line(w).shown = static_cast<u16>(current_line(w).shown + (synced - left));
      if (*p != 0) goto store;
      w->page_read = static_cast<u8>((w->page_read + 1) & 0xF);
      if (w->page_read == w->page_write) {
        if (w->hold != 0) {
          w->state = 1;
        } else {
          w->timer = 0;
          w->state = 9;
        }
      } else {
        w->page_offset = 0;
        if (w->pending_glyphs != 0) {
          if (msg_window_new_line(w) != 1) goto store;
          w->page_started = 1;
        }
        p = w->pages[w->page_read] + w->page_offset;
        line_left = current_line(w).remaining;
      }
      goto store;
    }
    ++w->page_offset;
    code = *p++;
    if (code > 0x7F) {
      ++w->page_offset;
      code = (code & 0x7F) | static_cast<u32>(*p++) << 7;
    }
    if (code < 0x4000) {
      --left;
      if (line_left < 2 && (line_left < 1 || code - push.push_first < push.push_count)) {  // NA 0xf7, 7
        // The line is used up: lay out and render the next one before this glyph.
        current_line(w).shown = static_cast<u16>(current_line(w).shown + (synced - left) - 1);
        w->page_offset = static_cast<u16>(w->page_offset - (code < 0x80 ? 1 : 2));
        if (prev == 0x4009) w->page_offset = static_cast<u16>(w->page_offset - 2);
        w->continuing = 1;
        if (msg_window_new_line(w) != 0) {
          w->page_offset = static_cast<u16>(w->page_offset + (code < 0x80 ? 1 : 2));
          if (prev == 0x4009) w->page_offset = static_cast<u16>(w->page_offset + 2);
          synced = left + 1;
          line_left = current_line(w).remaining;
          w->continuing = 0;
        }
      }
      --line_left;
    } else {
      switch (code) {
        case 0x4000:  // new line
          current_line(w).shown = static_cast<u16>(current_line(w).shown + (synced - left));
          msg_window_new_line(w);
          synced = left;
          line_left = current_line(w).remaining;
          w->continuing = 0;
          break;
        case 0x4001: {  // wait, then empty the window
          w->state = 7;
          w->scroll_pending = 0;
          for (int i = 0; i < w->line_count; ++i) {
            w->scroll_pending = static_cast<u8>(w->scroll_pending +
                                                static_cast<u8>(w->lines[(w->first_line + i) % w->line_count].height));
          }
          w->scroll_pending = static_cast<u8>(w->scroll_pending + static_cast<u8>(w->scroll_offset));
          current_line(w).shown = static_cast<u16>(current_line(w).shown + (synced - left));
          w->continuing = 0;
          break;
        }
        case 0x4002:  // wait for the button
          if (auto_text()) {
            w->state = 8;
            w->timer = 300;
          } else {
            w->state = 6;
          }
          current_line(w).shown = static_cast<u16>(current_line(w).shown + (synced - left));
          w->continuing = 0;
          break;
        case 0x4003:  // speed n, or a pause of n - 5 frames
          ++w->page_offset;
          w->speed = *p++;
          if (w->speed < 5) {
            left += g_text_speed[w->speed];
            synced += g_text_speed[w->speed];
          } else {
            w->state = 3;
            w->timer = static_cast<s16>(w->speed - 5);
            current_line(w).shown = static_cast<u16>(current_line(w).shown + (synced - left));
          }
          break;
        case 0x4004:  // colour (applied by text_render_line)
        case 0x400F:  // x position (text_render_line)
          ++p;
          ++w->page_offset;
          break;
        case 0x4006:  // timed wait
          w->state = 8;
          w->timer = static_cast<s16>(p[0] + p[1] * 0x100);
          p += 2;
          w->page_offset = static_cast<u16>(w->page_offset + 2);
          current_line(w).shown = static_cast<u16>(current_line(w).shown + (synced - left));
          w->continuing = 0;
          break;
        case 0x4007:
        case 0x4008:
        case 0x400C:
        case 0x400E: {  // inserted string: one glyph per character of budget
          if (line_left == 0) {
            current_line(w).shown = static_cast<u16>(current_line(w).shown + (synced - left));
            w->page_offset = static_cast<u16>(w->page_offset - 2);
            msg_window_new_line(w);
            return;
          }
          if (w->insert_start != 0) {
            if (code == 0x4008) {
              text_item_name_width(w, *p, &w->insert_remaining);
            } else if (code == 0x4007 || code == 0x400C) {
              int ch = *p;
              if (code == 0x4007) ch = msg_rd32(globals().script_globals + 0x38 + *p * 4);
              text_char_name_width(ch, &w->insert_remaining);
            } else {
              text_number_width(w, *p, &w->insert_remaining);
            }
            w->insert_start = 0;
          }
          if (--w->insert_remaining == 0) {
            w->insert_start = 1;
            p += 3;
            w->page_offset = static_cast<u16>(w->page_offset + 3);
          }
          --line_left;
          --left;
          p -= 2;
          w->page_offset = static_cast<u16>(w->page_offset - 2);
          break;
        }
        case 0x400A: {  // ruby block: skipped as a whole
          const u8* start = p;
          text_measure_block(w, &p);
          w->page_offset = static_cast<u16>(w->page_offset + (p - start));
          break;
        }
        case 0x400D:  // fixed space / icon: one character
          --left;
          --line_left;
          ++w->page_offset;
          ++p;
          break;
        default:  // 0x4005, 0x4009, 0x400b and unknown codes
          break;
      }
    }
    prev = code;
    if (w->state != 4) goto store;
  }
store:
  current_line(w).remaining = static_cast<u16>(line_left);
}

int msg_window_new_line(MsgWindow* w) {
  TextLine info;
  const u8* text = w->pages[w->page_read] + w->page_offset;
  const int h = text_layout_line(w, text, &info);
  const int limit = w->h;
  if (limit < w->text_height + h) {
    const int over = w->text_height + h - limit;
    if (limit < w->page_height + h) {
      // The page is full: wait for the button, then scroll.
      if (auto_text()) {
        w->state = 8;
        w->timer = 300;
      } else {
        w->state = 6;
      }
      w->scroll_pending = static_cast<u8>(over);
    } else {
      w->state = 5;
      w->scroll_remaining = static_cast<s16>(over);
      w->scroll_pending = 1;
    }
    w->text_height = static_cast<s16>(w->text_height - over);
    ++w->used_lines;
    return 0;
  }
  text_render_line(w, text, (w->first_line + w->used_lines + 1) % w->line_count, h, &info);
  ++w->used_lines;
  return 1;
}

void msg_window_push_text(MsgWindow* w, const u8* text) {
  if (((w->page_write + 1) & 0xF) != w->page_read) {
    w->pages[w->page_write] = text;
    w->page_write = static_cast<u8>((w->page_write + 1) & 0xF);
  }
}

void msg_window_push_move(MsgWindow* w, s32 x, s32 y, u16 frames) {
  if (((w->move_write + 1) & 7) != w->move_read) {
    MsgMove& m = w->moves[w->move_write];
    m.x = x;
    m.frames = frames;
    m.t = 0;
    m.y = y;
    w->move_write = static_cast<u8>((w->move_write + 1) & 7);
  }
}

void msg_window_clear_line(MsgWindow* w, int line) {
  MsgLine& l = w->lines[line];
  for (auto*& b : l.bitmap) {
    delete[] b;
    b = nullptr;
  }
  l.shown = 0;
  l.all_faded = 0;
  std::memset(l.fade, 0, sizeof l.fade);
}

void msg_window_line_pos(MsgWindow* w, int line, s32* xy, u32* end_x, s32* height) {
  int y = 0;
  for (int i = 0; i < w->line_count; ++i) {
    const MsgLine& l = w->lines[(w->first_line + i) % w->line_count];
    if (i == line) {
      xy[0] = w->x + 8;
      xy[1] = w->y + y + w->scroll_offset + 8 + l.y_offset;
      *end_x = l.glyph_x[l.shown];
      *height = l.height;
      return;
    }
    y += l.height;
  }
}

void msg_window_set_anchor(MsgWindow* w, int on) {
  if (w == nullptr) return;
  w->anchored = static_cast<u8>(on);
  if (on != 0 && g_msg_project_anchor) g_msg_project_anchor(w->anchor);
}

int msg_window_get_state(MsgWindow* w) {
  if (w == nullptr) return -1;
  if (w->state == 1 && w->page_read != w->page_write) return 4;
  return w->state;
}

}  // namespace ss::game::field
