#include "game/common/menu_save.h"

#include <cstring>

#include "disc/slz.h"
#include "game/boot/pad.h"
#include "game/boot/sound.h"
#include "game/common/memcard.h"
#include "game/common/menu_scene.h"
#include "game/title/title_program.h"
#include "runtime/runtime.h"

namespace ss::game::menu {
namespace {

MenuScene* scene() { return globals().scene; }
MenuSession* session() { return globals().session; }
bool loading() { return session() && session()->load_mode != 0; }
MemcardSlot& slot_of(MemoryCard* card, int slot) { return card->slots[slot]; }

s16 rd16s(const u8* p) {
  s16 v;
  std::memcpy(&v, p, 2);
  return v;
}
s32 rd32s(const u8* p) {
  s32 v;
  std::memcpy(&v, p, 4);
  return v;
}
void wr16(u8* p, u16 v) { std::memcpy(p, &v, 2); }
void wr32(u8* p, u32 v) { std::memcpy(p, &v, 4); }

// title 0x80067d44: the card slot the page works on (0 / 1).
s16 g_slot = 0;
// title 0x80067d3c / 0x80067d40: highest save counter on the shown card and its position.
s32 g_newest_counter = 0;
s32 g_newest_index = 0;
// title 0x80067b98: the name glyph table (page data block 4) sjis_name reads.
const u8* g_name_table = nullptr;

// A message box on a plain text box in the focused widget (title 0x8006ac6c shape).
void show_message(s16 group, int index) {
  auto* box = new MessageBox;
  box->set_group(group);
  box->show(index);
}

// title 0x80034768: places a list's top row and cursor so 'target' is visible. The
// original's loop walks its two pointers instead of their values; that happens only
// when the target is past the rows shown, and is reproduced on the same stack words.
void list_place(s32* top, s32* cursor, s32 target, s32 last_row) {
  // Stack: the four zero words of the caller, then top, cursor and four free words.
  s32 stack[10] = {0, 0, 0, 0, *top, *cursor, 0, 0, 0, 0};
  int t = 4, c = 5;
  const s32 rel0 = stack[c] - stack[t];
  stack[c] = rel0;
  const s32 rel = target - stack[t];
  if (target != stack[t] + rel0) {
    stack[c] = rel;
    if (rel < 0) {
      stack[t] += rel;
      stack[c] = 0;
    }
    while (c >= 0 && t < 10 && last_row < stack[c]) {
      ++t;
      --c;
    }
  }
  if (c >= 0 && t < 10) stack[c] += stack[t];
  *top = stack[4];
  *cursor = stack[5];
}

}  // namespace

// ==== SaveMenu ===========================================================================

SaveMenu::SaveMenu() {
  MenuGlobals& g = globals();
  strings_set(g.data.data());
  // Block 6: the slot-number badges (a TIM: pixels at +0x220, CLUTs at +0x14).
  const u8* packed = strings_block(6);
  std::vector<u8> tim;
  if (auto out = disc::slz::unpack_block(std::span<const u8>(packed, read_u32(packed + 4) + 0x10), 0, 0, nullptr))
    tim = std::move(*out);
  // title 0x80013310 shrinks the page data to strings_word(5) bytes (block 6 is no
  // longer needed); the port keeps the buffer as it is.
  if (tim.size() >= 0x220 + 0x40 * 0x40 * 2) {
    upload_image_ref(psyq::RECT{0x140, 0, 0x40, 0x40}, tim.data() + 0x220);
    upload_image_ref(psyq::RECT{0x130, 0x1E0, 0x10, 0x10}, tim.data() + 0x14);
    upload_free_later(std::move(tim));
  }
  TextRenderer::set_default_font(strings_block(1), strings_block(0));
  g_name_table = strings_block(4);
  create_help();
  card = scene()->memcard;
  status[0] = status[1] = 0;
  shown_load = 0;
  usable[0] = usable[1] = 0;
  load_handle = 0;
  slot_box = new TextBox;
  slot_box->set_rect(0, 0xA2, 0x138, 0x26, 0);
  no_card = nullptr;
  g_slot = 0;
  cursor->add_grid(&g_slot, 1, 2, 1, 0xA8, 0xC, 0x40, 0, slot_box);
  rebuild_slots();
}

SaveMenu::~SaveMenu() = default;

void SaveMenu::draw_help() { help->add_message(static_cast<s16>(session()->load_mode + 0x11)); }

void SaveMenu::frame() {
  if (load_handle != 0 && bgload_owner_done(&loads)) {
    entry_cache_load_0bd2(bgload_request(load_handle)->dest);
    session()->cached_0bd2 = 1;
    load_handle = 0;
  }
  const s32 s0 = card->slots[0].status;
  const s32 s1 = card->slots[1].status;
  bool changed = false;
  if (s0 != status[0] || s1 != status[1] || load_handle != shown_load) {
    changed = true;
    status[0] = s0;
    status[1] = s1;
    shown_load = load_handle;
  }
  const s32 u0 = slot_usable(0) ? 1 : 0;
  const s32 u1 = slot_usable(1) ? 1 : 0;
  if (u0 != usable[0] || u1 != usable[1]) {
    changed = true;
    usable[0] = u0;
    usable[1] = u1;
  }
  if (changed) rebuild_slots();
  Panel::frame();
}

bool SaveMenu::slot_usable(int slot) const {
  const s32 st = status[slot];
  if (st == 0x2281) return true;  // unformatted: the format flow can run
  if (load_handle != 0 || st != 0x2282) return false;
  const u32 state = static_cast<u16>(card->slots[slot].state);
  if (state == 0x14) return true;                   // idle
  if (state - 7 <= 4) return true;                  // reading a save
  return state - 0x16 < 0xC;                        // writing a save
}

void SaveMenu::input() {
  Widget::input();
  if (card->slots[0].status == 0x1141 && card->slots[1].status == 0x1141) {
    if (no_card == nullptr) {
      no_card = new TextBox;
      no_card->set_rect_centered(0x100, 0x28, 0);
      no_card->pen_reset();
      no_card->set_group(5);
      no_card->add_message_lines(0, 1);
    }
  } else if (no_card != nullptr) {
    no_card->on_close();
    no_card = nullptr;
  }
}

void SaveMenu::on_confirm(int n) {
  const int slot = g_slot;
  if (!usable[slot]) return;
  if (status[slot] == 0x2281) {
    if (!loading()) {
      auto* flow = new FormatFlow;
      open_child(2, flow);
      flow->slot = slot;
    } else {
      auto* d = new NoticeDialog;
      open_child(0, d);
      show_message(9, 4);  // "the card is not formatted"
    }
  } else {
    const MemcardSlot& s = card->slots[slot];
    if (s.free_blocks + s.save_count != 0) {
      open_child(1, new SaveList);
    } else {
      auto* d = new NoticeDialog;
      open_child(0, d);
      show_message(7, session()->load_mode);  // nothing to load / no room to save
    }
  }
  Widget::on_confirm(n);
}

void SaveMenu::on_child_done(int id, int state, int) {
  if (id == 1) {
    if (state != 3) return;
    // A game was loaded: its settings take effect.
    const u8* config = globals().config;
    clock_set(menu_pad(), rd32s(config + 0x10) * 0x3C);
    pad_enable_vibration(menu_pad(), 0, config[0x46] != 0);
    sound_set_output_mode(menu_sound(), config[0x44]);
    session()->result = 1;
    if (!pad_is_dualshock(menu_pad(), 0)) {
      // TODO(port): vibration setup 0x80013c0c(pad, 0, 0, 0, config[0x47], config[0x48],
      // 0x20) / 0x80013c60(pad, 0, 0x50 x4) / 0x80013ca4(pad, 0, 1) (not ported, as at boot).
    }
    finish(0);
  } else if (id == 2 && state == 3) {
    open_child(1, new SaveList);
  }
}

void SaveMenu::rebuild_slots() {
  TextBox* b = slot_box;
  b->clear();
  b->set_group(3);
  b->pen_reset();
  b->pen_move(0, -1);
  b->add_message_lines(0, 1);
  for (int i = 0; i < 2; ++i) {
    b->pen_at(i == 0 ? 0xB8 : 0xF8, 8);
    b->text->set_colour(usable[i] ? 1 : 4);
    b->add_message(static_cast<s16>(i + 1));
  }
}

// ==== SaveList ============================================================================

SaveList::SaveList() {
  card = scene()->memcard;
  const MemcardSlot& s = card->slots[g_slot];
  free_blocks = s.free_blocks;
  save_count = s.save_count;
  total = free_blocks + save_count;
  const u32 black[4] = {0, 0, 0, 0};
  g_newest_counter = 0;
  g_newest_index = 0;
  for (int i = 0; i < total && i < 15; ++i) {
    boxes[i] = new TextBox;
    boxes[i]->set_colours(black);
    boxes[i]->set_rect(0, i * 0x36, 0x138, 0x36, 0);
    empty[i] = 0;
  }
  for (int i = 0; i < total && i < 15; ++i) show_entry(i);
  // The last position used on this slot (config + 0x198), else the newest save.
  const s32 last = rd32s(globals().config + 0x198 + g_slot * 4);
  if (last != 0) {
    g_newest_index = last - 1;
    if (total < last - 1) g_newest_index = 0;
  }
  s32 top_limit = total - 3;
  if (top_limit < 0) top_limit = 0;
  s32 top = g_newest_index - 1;
  if (top < 0) top = 0;
  if (top_limit < top) top = top_limit;
  s32 row = 0;
  list_place(&top, &row, g_newest_index, 2);
  cursor_index = static_cast<s16>(row);
  cursor->add_list(&cursor_index, &scroll, static_cast<s16>(total), 3, 0, 0x12, 0x36);
  pending_load = 0;
  card_gone = 0;
  scroll = static_cast<s16>(top * 4);
}

SaveList::~SaveList() = default;

void SaveList::frame() {
  for (int i = 0; i < total && i < 15; ++i) {
    int dy_ = scroll * -0x36;
    if (dy_ < 0) dy_ += 3;
    boxes[i]->dy = static_cast<s16>(dy_ >> 2);
  }
  Dialog::frame();
  // Clip everything this window draws to the list area above the slot box.
  auto* a = prim_alloc<psyq::DR_AREA>(ctx);
  if (!a) return;
  const psyq::RECT r{0, static_cast<s16>(screen_draw_y()), 0x140, 0xA2};
  psyq::SetDrawArea(a, &r);
  prim_link(ctx, 0, a);
}

void SaveList::input() {
  Widget::input();
  if (card_gone == 0 && card->slots[g_slot].status != 0x2282) {
    auto* d = new NoticeDialog;
    open_child(2, d);
    notice_card_removed();
    card_gone = 1;
  }
}

void SaveList::on_confirm(int n) {
  if (!loading() || empty[cursor_index] == 0) {
    auto* flow = new LoadFlow;
    open_child(1, flow);
    flow->start(g_slot, cursor_index);
    Widget::on_confirm(-n);
  }
}

void SaveList::on_child_done(int id, int state, int) {
  if (id == 1) {
    if (!loading()) {
      if (state == 3) show_entry(cursor_index);
    } else if (state == 3) {
      finish(0);
    }
  } else if (id == 2) {
    cancel();
  }
}

void SaveList::show_entry(int index) {
  TextBox* box = boxes[index];
  box->clear();
  StrBuf sb;
  box->set_colours(nullptr);
  // The position number badge (page data block 6, uploaded at (0x140, 0)).
  const int n = index < 0 ? -index : index;
  PolyFT4Raw badge{};
  badge.x0 = -4, badge.y0 = -4;
  badge.x1 = 0x14, badge.y1 = -4;
  badge.x2 = -4, badge.y2 = 0x14;
  badge.x3 = 0x14, badge.y3 = 0x14;
  const u8 bu = static_cast<u8>((n % 10) * 0x18), bv = static_cast<u8>((n / 10) * 0x18);
  badge.u0 = bu, badge.v0 = bv;
  badge.u1 = static_cast<u8>(bu + 0x18), badge.v1 = bv;
  badge.u2 = bu, badge.v2 = static_cast<u8>(bv + 0x18);
  badge.u3 = static_cast<u8>(bu + 0x18), badge.v3 = static_cast<u8>(bv + 0x18);
  badge.clut = 0x7813;
  badge.tpage = 5;
  box->add_icon(badge);
  box->set_group(0xE);
  box->pen(0x28, 8, 0xE);
  const MemcardSlot& s = card->slots[g_slot];
  if (s.save_map[index] < 0) {
    box->add_message_lines(0, 1);  // "Star Ocean 2 data not found."
    empty[index] = 1;
    return;
  }
  if (!save_summary_valid(card, g_slot, index)) {
    box->add_message_lines(1, 1);  // damaged data
    empty[index] = 1;
    return;
  }
  const u8* summary = s.summaries[index];
  if (g_newest_counter < rd32s(summary + 0x54)) {
    g_newest_counter = rd32s(summary + 0x54);
    g_newest_index = index;
  }
  u32 colours[4];
  std::memcpy(colours, summary + 0x24, sizeof colours);
  box->set_colours(colours);
  for (int k = 0; k < 8; ++k) {
    box->pen_at(static_cast<s16>(0x60 + k * 0x18), 4);
    const int id = static_cast<s8>(summary[0x34 + k * 4]);
    if (id > 0) box->add_character(static_cast<s16>(id), summary[0x35 + k * 4], 9, 0x1000);
  }
  // "TIME" and hours:minutes, "BATTLES" and the count (ASCII strings, title 0x8006742c..).
  const char* time_label = reinterpret_cast<const char*>(table(0x8006742C, 0x80082408));
  const char* battles_label = reinterpret_cast<const char*>(table(0x80067450, 0x8008242C));
  box->pen(0x28, 8, 10);
  box->add_string(time_label ? time_label : "   \x08\x02TIME\x08\x01\n");
  sb.set("");
  s32 hours = rd32s(summary + 0x1C) / 0x3C;
  if (hours > 999) hours = 999;
  sb.put("%4d");
  sb.put_int(hours);
  sb.put(":");
  sb.put("%02d");
  sb.put_int(rd32s(summary + 0x1C) % 0x3C);
  box->add_string(sb.str());
  box->pen(0x28, 0x1C, 10);
  box->add_string(battles_label ? battles_label : "\x08\x02" "BATTLES\x08\x01\n");
  sb.set("");
  sb.put("%7d");
  sb.put_int(rd32s(summary + 0x20));
  box->add_string(sb.str());
}

// ==== FormatFlow ===========================================================================

FormatFlow::FormatFlow() {
  state = 0;
  last = -1;
  flags |= 2;
}

void FormatFlow::frame() {
  if (state != last) {
    last = state;
    switch (state) {
      case 0: {
        auto* c = new CheckDialog;
        open_child(1, c);
        c->setup_format();
        ++state;
        break;
      }
      case 2: {
        auto* f = new FormatDialog;
        open_child(1, f);
        f->start(slot);
        ++state;
        break;
      }
      case 4: {
        auto* d = new NoticeDialog;
        open_child(1, d);
        notice_card_error();
        ++state;
        break;
      }
      case 6:
        sound_play_sfx(menu_sound(), 0x12, 0, 0x7F40);
        finish(0);
        break;
      case 8: {
        sound_play_sfx(menu_sound(), 7, 0, 0x7F40);
        auto* d = new NoticeDialog;
        open_child(1, d);
        notice_card_formatted();
        ++state;
        break;
      }
      case 10:
        cancel();
        break;
      default:
        break;
    }
  }
  Dialog::frame();
}

void FormatFlow::on_child_done(int, int child_state, int) {
  if (state == 3) {
    state = child_state == 3 ? 4 : 8;
  } else if (state == 1) {
    state = child_state == 3 ? 2 : 10;
  } else if (state == 5 || state == 9) {
    ++state;
  }
}

// ==== LoadFlow ==============================================================================

LoadFlow::LoadFlow() {
  flags |= 2;
  card = scene()->memcard;
  state = 0;
  last = -1;
}

LoadFlow::~LoadFlow() { pad_set_option(menu_pad(), 1); }

void LoadFlow::frame() {
  if (state != last) {
    last = state;
    u8* config = globals().config;
    switch (state) {
      case 0: {
        auto* c = new CheckDialog;
        open_child(1, c);
        c->setup_check();
        if (loading() || card->slots[slot].save_map[index] < 0) {
          state += 3;  // nothing to overwrite
          break;
        }
        ++state;
        break;
      }
      case 2: {
        auto* c = new CheckDialog;
        open_child(1, c);
        c->setup_overwrite();
        ++state;
        break;
      }
      case 4: {
        // Wait for the list's background load (field only).
        auto* list = static_cast<SaveList*>(parent);
        if (list->pending_load == 0) {
          state += 2;
        } else if (bgload_owner_done(&list->loads)) {
          list->pending_load = 0;
          state += 2;
        }
        last = 0;
        break;
      }
      case 6: {
        saved_last[0] = rd32s(config + 0x198);
        saved_last[1] = rd32s(config + 0x19C);
        pad_set_option(menu_pad(), 0);
        auto* p = new ProgressDialog;
        open_child(1, p);
        p->start(slot, index);
        ++state;
        break;
      }
      case 8:
        // A load replaced the configuration: keep the last-used positions.
        wr32(config + 0x198, static_cast<u32>(saved_last[0]));
        wr32(config + 0x19C, static_cast<u32>(saved_last[1]));
        wr32(config + 0x198 + slot * 4, static_cast<u32>(index + 1));
        if (loading()) scene()->input_off = 1;
        sound_play_sfx(menu_sound(), 0x12, 0, 0x7F40);
        finish(0);
        break;
      case 10: {
        sound_play_sfx(menu_sound(), 7, 0, 0x7F40);
        auto* d = new NoticeDialog;
        open_child(1, d);
        notice_transfer_failed();
        ++state;
        break;
      }
      case 12:
        cancel();
        break;
      default:
        break;
    }
  }
  Dialog::frame();
}

void LoadFlow::on_child_done(int, int child_state, int) {
  if (state == 1 || state == 3) {
    state = child_state == 3 ? state + 1 : 0xC;
  } else if (state == 7) {
    state = child_state == 3 ? 8 : 10;
  } else if (state == 0xB) {
    state = 0xC;
  }
}

// ==== CheckDialog ============================================================================

CheckDialog::CheckDialog() {
  card = scene()->memcard;
  box = new TitledBox;
  card_gone = 0;
}

void CheckDialog::input() {
  if (card_gone == 0 && static_cast<u16>(card->slots[g_slot].status - 0x2281) > 1) {
    cancel();
    card_gone = 1;
  }
  Widget::input();
}

void CheckDialog::on_confirm(int n) {
  if (unknown34 == 0) {  // YES
    finish(0);
    Widget::on_confirm(-n);
  } else {
    on_cancel(n);
  }
}

void CheckDialog::setup() {
  TextBox* b = box;
  b->set_rect_centered(200, 0x40, 1);
  b->pen_at(10, 5);
  b->set_group(loading() ? 0xC : 0xA);
  b->add_message(0);  // the title ("Data Load Check")
  b->set_group(0x10);
  b->pen_at(0x38, 0x2C);
  b->add_message(0);  // YES
  b->pen_at(0x68, 0x2C);
  b->add_message(1);  // NO
  b->set_group(loading() ? 0xD : 0xB);
  b->pen_at(0x10, 0x1C);
  cursor->add_grid(&unknown34, 1, 2, 1, 0x28, 0x30, 0x30, 0, b);
}

void CheckDialog::setup_check() {
  setup();
  box->add_message_lines(0, 1);
}

void CheckDialog::setup_overwrite() {
  unknown34 = 1;
  setup();
  box->add_message_lines(4, 1);
}

void CheckDialog::setup_format() {
  TextBox* b = box;
  b->set_rect_centered(0xF8, 0x4E, 1);
  b->pen_at(10, 5);
  b->set_group(8);
  b->add_message(0);
  b->set_group(0x10);
  b->pen_at(0x48, 0x3A);
  b->add_message(0);
  b->pen_at(0x78, 0x3A);
  b->add_message(1);
  b->set_group(loading() ? 0xD : 0xB);
  b->pen(0x10, 0x1C, 0xE);
  unknown34 = 1;
  cursor->add_grid(&unknown34, 1, 2, 1, 0x38, 0x3E, 0x30, 0, b);
  b->set_group(9);
  b->add_message_lines(0, 1);
}

// ==== Notices ===================================================================================

void NoticeDialog::on_confirm(int n) {
  Widget::on_confirm(-n);
  finish(0);
}

void notice_card_removed() {
  auto* box = new TextBox;
  box->set_rect_centered(0, 0, 0);
  box->pen_reset();
  box->text_renderer();
  box->set_group(0xF);
  message_box_show(box, 0);
}

void notice_card_error() { show_message(9, 2); }
void notice_card_formatted() { show_message(9, 3); }
void notice_transfer_failed() { show_message(loading() ? 0xD : 0xB, 3); }

// ==== Progress ==================================================================================

ProgressBox::ProgressBox() {
  target = 0x38400;
  total = 0x38400;
  current = 0;
}

void ProgressBox::draw_background() {
  if (current < target) current += 0x4C;
  const int w = total != 0 ? current * 0xB0 / total : 0;
  const s16 px = static_cast<s16>(x), py = static_cast<s16>(y);
  const s16 left = static_cast<s16>(px + 0x10);
  const s16 top = static_cast<s16>(py + 0x28), bottom = static_cast<s16>(py + 0x34);
  auto quad = [&](s16 right, s16 y0, s16 y2, u32 rgb) {
    auto* g = prim_alloc<psyq::POLY_G4>(ctx);
    if (!g) return;
    psyq::setlen(g, 8);
    const u32 c0 = menu_colour_code(0x38000000u, colour, rgb);
    const u32 c = menu_colour(colour, rgb);
    std::memcpy(&g->r0, &c0, 4);
    std::memcpy(&g->r1, &c, 4);
    std::memcpy(&g->r2, &c, 4);
    std::memcpy(&g->r3, &c, 4);
    g->x0 = left, g->y0 = y0;
    g->x1 = right, g->y1 = y0;
    g->x2 = left, g->y2 = y2;
    g->x3 = right, g->y3 = y2;
    prim_link(ctx, slot, g);
  };
  quad(static_cast<s16>(left + w), bottom, top, 0xFF5010);   // the part done (blue)
  quad(static_cast<s16>(px + 0xC0), top, bottom, 0xF0E0E0);  // the track
}

ProgressDialog::ProgressDialog() {
  flags |= 2;
  card = scene()->memcard;
  bar = new ProgressBox;
  bar->set_rect_centered(0xD0, 0x3C, 1);
  done = 0;
  card->write_buffer = new u8[0x2000]();
  buffer.assign(0x2000, 0);
}

ProgressDialog::~ProgressDialog() {
  delete[] card->write_buffer;
  card->write_buffer = nullptr;
}

void ProgressDialog::start(int slot_, int index_) {
  slot = slot_;
  index = index_;
  TextBox* b = bar;
  if (!loading()) {
    b->set_group(0xB);
    length = save_build(card, buffer.data(), slot, index);
    steps = reinterpret_cast<const s16*>(table(0x80067B74, 0x80082B50));
  } else {
    b->set_group(0xD);
    length = load_start(card, buffer.data(), slot, index);
    steps = reinterpret_cast<const s16*>(table(0x80067B8C, 0x80082B68));
  }
  total = 0;
  length += 0x7F;
  for (const s16* p = steps; p && *p != 0; ++p) total += *p == -1 ? length : *p;
  b->pen_reset();
  b->add_message_lines(1, 1);  // "Loading in progress. Do not remove the MEMORY CARD."
}

void ProgressDialog::frame() {
  u32 state = static_cast<u16>(card->slots[slot].state);
  bool active;
  if (!loading()) {
    active = state - 0x16 < 0xC;
    if (active) state -= 0x16;
  } else {
    active = state - 7 < 5;
    if (active) state -= 7;
  }
  if (active && steps) {
    s32 sum = 0;
    u32 i = 0;
    for (const s16* p = steps; *p != 0; ++p) {
      const s32 step = *p == -1 ? length : *p;
      if (state <= i) break;
      ++i;
      sum += step;
    }
    bar->set_progress(sum, total);
  }
  if (done == 0) {
    int r = loading() ? load_finish(card, slot) : save_finish(card, slot);
    if (length == -1) r = -1;
    if (r != 0) {
      if (r == 1) {
        finish(0);
      } else {
        cancel();
      }
      ++done;
    }
  }
  Dialog::frame();
}

// ==== FormatDialog ================================================================================

FormatDialog::FormatDialog() {
  flags |= 2;
  pad_set_option(menu_pad(), 0);
  card = scene()->memcard;
  box = new TextBox;
  box->set_rect_centered(0xD0, 0x2A, 1);
  step = 0;
}

FormatDialog::~FormatDialog() { pad_set_option(menu_pad(), 1); }

void FormatDialog::start(int slot_) {
  slot = static_cast<s16>(slot_);
  box->set_group(9);
  box->pen_reset();
  box->add_message_lines(1, 1);
  step = 1;
  delay = 8;
}

void FormatDialog::frame() {
  MemcardSlot& s = card->slots[slot];
  switch (step) {
    case 1:
      if (delay != 0 && --delay == 0) {
        card_request_format(card, slot);
        ++step;
      }
      break;
    case 2:
      if (!(s.flags & 0x2000)) {
        ++step;
        if (s.flags & 0x4000) {  // the format failed
          cancel();
          step = 0;
        }
      }
      break;
    case 3:
      if (s.status == 0x1141) {
        cancel();
        step = 0;
      }
      if (s.status == 0x2282) ++step;
      break;
    case 4:
      finish(0);
      step = 0;
      break;
    default:
      break;
  }
  Dialog::frame();
}

// ==== Card operations ===============================================================================

bool save_summary_valid(MemoryCard* card, int slot, int index) {
  return save_summary_check(card->slots[slot].summaries[index]);
}

bool save_summary_check(const u8* summary) {
  // "STAR OCEAN 03/01" (title 0x8004deb8).
  static const u8 kMagic[16] = {'S', 'T', 'A', 'R', ' ', 'O', 'C', 'E', 'A', 'N', ' ', '0', '3', '/', '0', '1'};
  if (std::memcmp(summary, kMagic, 16) != 0) return false;
  if (static_cast<u16>(rd16s(summary + 0x18)) != 0x5555) return false;
  u8 copy[0x80];
  std::memcpy(copy, summary, sizeof copy);
  const u32 saved = static_cast<u32>(rd32s(copy + 0x10));
  wr16(copy + 0x18, 0);
  wr32(copy + 0x10, 0);
  return saved == byte_sum(copy, 0x80);
}

int save_build(MemoryCard* card, u8* file, int slot, int index) {
  MemcardSlot& s = slot_of(card, slot);
  if (s.state != 0x14) return -1;
  MenuGlobals& g = globals();
  u8* config = g.config;
  // The leader's name (card codes) and level.
  u8 name[8] = {};
  s32 level = 0;
  for (int i = 0; i < 8; ++i) {
    if (rd16s(g.party + i * 0x60) == config[0x45] + 1) {
      text_to_card_codes(reinterpret_cast<const char*>(g.records + i * 0xD0 + 0x24), name, sizeof name);
      level = rd16s(g.party + i * 0x60 + 0x26);
    }
  }
  std::memset(file, 0, 0x200);
  file[0] = 'S', file[1] = 'C', file[2] = 0x13, file[3] = 0x01;
  // Title: "ＳＯ２　" + position, play time h:mm, the leader's name, "ＬＶ" + level.
  StrBuf sb;
  char tmp[64];
  auto put_sjis = [&](const char* text) {
    if (text[0] == '%') {
      sb.parse_format(text);
    } else {
      sb.put(text);
    }
  };
  const char* prefix = reinterpret_cast<const char*>(table(0x800674E0, 0x800824BC));
  const char* space = reinterpret_cast<const char*>(table(0x800674EC, 0x800824C8));
  const char* colon = reinterpret_cast<const char*>(table(0x800674F0, 0x800824CC));
  const char* lv = reinterpret_cast<const char*>(table(0x800674F4, 0x800824D0));
  sb.set(prefix ? prefix : "");
  s16 number = s.save_map[index];
  if (number < 0) number = static_cast<s16>(-number);
  *sjis_2digits(number, tmp) = 0;
  put_sjis(tmp);
  put_sjis(space ? space : "");
  s32 hours = rd32s(config + 0x10) / 0x3C;
  if (hours > 999) hours = 999;
  *sjis_number(hours, tmp) = 0;
  put_sjis(tmp);
  put_sjis(colon ? colon : "");
  *sjis_2digits(rd32s(config + 0x10) % 0x3C, tmp) = 0;
  put_sjis(tmp);
  put_sjis(space ? space : "");
  *sjis_name(name, tmp) = 0;
  put_sjis(tmp);
  put_sjis(lv ? lv : "");
  *sjis_number(level, tmp) = 0;
  put_sjis(tmp);
  std::memcpy(file + 4, sb.str(), std::strlen(sb.str()));
  // Icon: CLUT (0x60) and three frames (0x80); zero in the title's copy of the module.
  if (const u8* icon = table(0x80067B9C, 0x80082B78)) std::memcpy(file + 0x60, icon, 0x1A0);
  // Summary.
  u8* summary = file + kSaveSummary;
  static const u8 kMagic[16] = {'S', 'T', 'A', 'R', ' ', 'O', 'C', 'E', 'A', 'N', ' ', '0', '3', '/', '0', '1'};
  std::memcpy(summary, kMagic, 16);
  std::memset(summary + 0x10, 0, 10);
  wr32(summary + 0x1C, static_cast<u32>(rd32s(config + 0x10)));
  wr32(summary + 0x20, static_cast<u32>(rd32s(config + 0x14)));
  std::memcpy(summary + 0x24, config + 0x30, 0x10);
  for (int i = 0; i < 8; ++i) party_summary(i, summary + 0x34 + i * 4);
  wr32(summary + 0x54, static_cast<u32>(g_newest_counter + 1));
  wr32(config + 0x24, static_cast<u32>(rd32s(config + 0x24) + 1));
  std::memcpy(summary + 0x58, name, 8);
  std::memcpy(file + kSaveUnlocks, config + 0x1A0, 0x100);
  const int packed = save_serialize(0, file + kSavePacked);
  const int length = kSavePacked + packed;
  wr16(summary + 0x1A, static_cast<u16>(length));
  wr32(summary + 0x14, byte_sum(file, length));
  wr32(summary + 0x10, byte_sum(summary, 0x80));
  // Start the driver writing it (memcard_file_update state 0x16).
  s.state = 0x16;
  s.offset = 0;
  s.buffer = file;
  s.length = static_cast<s16>(length);
  s.index = static_cast<s16>(index);
  s.flags |= 0x1000;
  return length;
}

int save_finish(MemoryCard* card, int slot) {
  MemcardSlot& s = slot_of(card, slot);
  if (s.status != 0x2282) return -1;
  if (s.flags & 0x1000) return 0;
  // The driver read the file back into the write buffer: compare.
  return std::memcmp(s.dest, card->write_buffer, static_cast<size_t>(card->write_length)) == 0 ? 1 : -2;
}

int load_start(MemoryCard* card, u8* file, int slot, int index) {
  MemcardSlot& s = slot_of(card, slot);
  if (s.state != 0x14) return -1;
  const s16 length = rd16s(s.summaries[index] + 0x1A);
  s.state = 7;
  s.offset = 0;
  s.buffer = file;
  s.index = static_cast<s16>(index);
  s.flags |= 0x1000;
  s.length = length;
  return length;
}

int load_finish(MemoryCard* card, int slot) {
  MemcardSlot& s = slot_of(card, slot);
  if (s.status != 0x2282) return -1;
  if (s.flags & 0x1000) return 0;
  u8* file = s.buffer;
  const u32 saved = static_cast<u32>(rd32s(file + 0x214));
  wr16(file + 0x218, 0);
  wr32(file + 0x210, 0);
  wr32(file + 0x214, 0);
  if (saved != byte_sum(file, rd16s(file + 0x21A))) return -2;
  std::memcpy(globals().config + 0x1A0, file + kSaveUnlocks, 0x100);
  save_serialize(1, file + kSavePacked);
  return 1;
}

int save_serialize(int load, u8* packed) {
  MenuGlobals& g = globals();
  std::vector<u8> image(0x2000, 0);
  if (load) zero_unpack(image.data(), packed);
  auto copy = [load](u8* in_image, u8* live, size_t n) {
    if (load) {
      std::memcpy(live, in_image, n);
    } else {
      std::memcpy(in_image, live, n);
    }
  };
  copy(image.data(), g.config, 0x1A0);
  copy(image.data() + 0x1A0, g.party, 0x300);
  copy(image.data() + 0x4A0, g.records, 0x680);
  // The inventory's check bytes are not saved; they are made again with a new key.
  u8* inv = g.inventory;
  std::memset(inv + 0x824, 0, 0x400);
  copy(image.data() + 0xB20, inv, 0xC28);
  for (u32 i = 0; i < 0x400; ++i)
    inv[0x824 + i] = inventory_check_byte(reinterpret_cast<const u16*>(inv), i, 0xFFFF);
  auto* snapshot = static_cast<u8*>(handoff_get(menu_handoff(), 0xE));
  if (snapshot) copy(image.data() + 0x1748, snapshot, 0x440);
  if (load) return kSaveImageSize;
  return static_cast<s16>(zero_pack(packed, image.data(), kSaveImageSize));
}

void card_request_format(MemoryCard* card, int slot) {
  MemcardSlot& s = slot_of(card, slot);
  s.flags |= s.status == 0x2281 ? 0x2000 : 0x4000;
}

void party_summary(int slot, u8* out) {
  const u8* p = static_cast<const u8*>(handoff_get(menu_handoff(), 3)) + slot * 0x60;
  out[0] = 0;
  if (rd16s(p) > 0) out[0] = static_cast<u8>(rd16s(p));
  out[1] = p[2];
  std::memcpy(out + 2, p + 0x26, 2);
}

u32 byte_sum(const u8* data, int n) {
  u32 sum = 0;
  for (int i = 0; i < n; ++i) sum += data[i];
  return sum;
}

int zero_pack(u8* out, const u8* in, int n) {
  // Output: u16 byte count, then the stream: nonzero bytes as they are, one zero as 0,
  // runs of 2-256 zeros as 0 0 (run - 2).
  u8* p = out + 2;
  int run = 0;
  auto flush = [&]() {
    if (run == 1) {
      *p++ = 0;
    } else if (run >= 2) {
      *p++ = 0;
      *p++ = 0;
      *p++ = static_cast<u8>(run - 2);
    }
    run = 0;
  };
  for (int i = 0; i < n; ++i) {
    const u8 c = in[i];
    if (c == 0 && run < 0x100) {
      ++run;
      continue;
    }
    flush();
    if (c == 0) {
      run = 1;
    } else {
      *p++ = c;
    }
  }
  flush();
  const int count = static_cast<int>(p - out) - 2;
  wr16(out, static_cast<u16>(count));
  return static_cast<s16>(count) + 2;
}

void zero_unpack(u8* out, const u8* in) {
  const s16 count = rd16s(in);
  const u8* p = in + 2;
  int zeros = 0;
  for (int i = 0; i < count;) {
    const u8 c = *p++;
    *out++ = c;
    if (c == 0) {
      if (++zeros == 2) {
        const u8 more = *p++;
        ++i;
        for (int k = 0; k < more; ++k) *out++ = 0;
        zeros = 0;
      }
    } else {
      zeros = 0;
    }
    ++i;
  }
}

char* sjis_digit(int digit, char* out) {
  const u8* t = table(0x80067B5C, 0x80082B38);
  static const u8 kDigits[20] = {0x82, 0x4F, 0x82, 0x50, 0x82, 0x51, 0x82, 0x52, 0x82, 0x53,
                                 0x82, 0x54, 0x82, 0x55, 0x82, 0x56, 0x82, 0x57, 0x82, 0x58};
  if (!t) t = kDigits;
  out[0] = static_cast<char>(t[digit * 2]);
  out[1] = static_cast<char>(t[digit * 2 + 1]);
  return out + 2;
}

char* sjis_2digits(int value, char* out) { return sjis_digit(value % 10, sjis_digit(value / 10, out)); }

char* sjis_number(int value, char* out) {
  const int hundreds = value / 100;
  if (hundreds != 0) out = sjis_digit(hundreds, out);
  const int tens = value % 100 / 10;
  if (tens != 0 || hundreds != 0) out = sjis_digit(tens, out);
  return sjis_digit(value % 100 % 10, out);
}

char* sjis_name(const u8* codes, char* out) {
  for (; *codes != 0; ++codes) {
    const u8* glyph = g_name_table + (*codes - 1) * 3;
    *out++ = static_cast<char>(glyph[0]);
    *out++ = static_cast<char>(glyph[1]);
  }
  return out;
}

int load_save_direct(MemoryCard* card, int slot, int index) {
  // As the title's load screen: a session holds the game-state pointers and handoff 0xe.
  MenuSession session;
  // Let the driver finish scanning the card.
  for (int i = 0; i < 2000 && card->slots[slot].state != 0x14; ++i) memcard_driver_update(card);
  MemcardSlot& s = card->slots[slot];
  if (s.status != 0x2282 || index < 0 || index > 14 || s.save_map[index] < 0) return 0;
  if (!save_summary_valid(card, slot, index)) return 0;
  std::vector<u8> file(0x2000, 0);
  if (load_start(card, file.data(), slot, index) < 0) return 0;
  int r = 0;
  for (int i = 0; i < 2000 && r == 0; ++i) {
    memcard_driver_update(card);
    r = load_finish(card, slot);
  }
  if (r != 1) return 0;
  // LoadFlow state 8: the last-used positions (load_game_screen cleared both).
  u8* config = globals().config;
  wr32(config + 0x198, 0);
  wr32(config + 0x19C, 0);
  wr32(config + 0x198 + slot * 4, static_cast<u32>(index + 1));
  // The save menu's Panel recomputes the party's stats when it closes.
  party_select(0);
  { StatCalc recompute(false); }
  // SaveMenu::on_child_done's settings.
  clock_set(menu_pad(), rd32s(config + 0x10) * 0x3C);
  pad_enable_vibration(menu_pad(), 0, config[0x46] != 0);
  sound_set_output_mode(menu_sound(), config[0x44]);
  session.result = 1;
  return 1;
}

}  // namespace ss::game::menu
