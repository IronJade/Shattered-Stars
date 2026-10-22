#include "game/common/menu_viewer.h"

#include <cstring>

#include "disc/slz.h"
#include "game/common/menu_gfx.h"
#include "game/common/menu_scene.h"
#include "game/common/menu_widget.h"
#include "game/common/program_image.h"
#include "psx/gte.h"
#include "runtime/libgpu.h"
#include "runtime/libgte.h"
#include "runtime/runtime.h"

namespace ss::game::menu {
namespace {

constexpr int kModelEntry = 0x1189;  // item files, addressed by sector
constexpr int kViewerCode = 3001;    // entry 3001 at 0x80088000: the viewer's code and constants

psyq::MATRIX viewer_matrix(u32 address) {
  psyq::MATRIX m{};
  if (const u8* p = program_data_at(kViewerCode, 0x80088000, address)) {
    for (int i = 0; i < 9; ++i) m.m[i / 3][i % 3] = static_cast<s16>(read_u16(p + i * 2));
    for (int i = 0; i < 3; ++i) m.t[i] = static_cast<s32>(read_u32(p + 0x14 + i * 4));
  }
  return m;
}

std::vector<u8> unpack_at(const std::vector<u8>& data, size_t offset) {
  if (offset + 0x10 > data.size()) return {};
  auto out = disc::slz::unpack_block(data, offset, 0, nullptr);
  return out ? std::move(*out) : std::vector<u8>{};
}

}  // namespace

ItemViewer::ItemViewer() {
  owner_ = static_cast<Widget*>(globals().scene->windows->focus());
  owner_->children->add(this, 2);
  ctx_.arena = owner_->ctx->arena;
  // Angles and translation from 0x8008cf20 (all zero).
  counter_ = 0;
  item_ = 0;
  phase_ = 0;
}

ItemViewer::~ItemViewer() {
  for (Slot& s : slots_) release(&s);
  if (has_ot_) screen_destroy_ot(ctx_.ot);
}

void ItemViewer::on_close() { owner_->children->kill(this); }

void ItemViewer::place(s16 x, s16 y, s16 z) {
  // 0x80088294: the GTE screen offset is the model's screen position; z sets its distance
  // and the depth range of its ordering table (z * 0x40 +- 0x140).
  psyq::SetGeomOffset(x, y);
  pos_[2] = z << 8;
  ot_near_ = static_cast<s16>(z * 0x40 - 0x140);
  ot_far_ = static_cast<s16>(z * 0x40 + 0x140);
  psyq::SetGeomScreen(0x100);
  psyq::SetBackColor(0x40, 0x40, 0x40);
  const psyq::MATRIX colour = viewer_matrix(0x8008D298);
  psyq::SetColorMatrix(&colour);
  if (has_ot_) screen_destroy_ot(ctx_.ot);
  render_ctx_create_ot(&ctx_, ot_far_ - ot_near_);
  has_ot_ = true;
}

ItemViewer::Slot* ItemViewer::find(u16 flags, int item) {
  for (Slot& s : slots_) {
    bool match = flags == 0 || (s.flags & flags) != 0;
    if (item != 0 && s.item != item) match = false;
    if (match) return &s;
  }
  return nullptr;
}

ItemViewer::Slot* ItemViewer::oldest() {
  Slot* best = nullptr;
  s32 age = 0x7FFFFFFF;
  for (Slot& s : slots_) {
    if ((s.flags & 2) && s.age < age) age = s.age, best = &s;
  }
  return best;
}

void ItemViewer::release(Slot* s) {
  if (!s || !(s->flags & 2)) return;
  s->flags = 1;
  if (s->load != 0) {
    bgload_cancel(s->load);
    s->load = 0;
  }
  s->data.clear();
  s->data.shrink_to_fit();
}

void ItemViewer::set_item(int item) {
  if (item != 0) {
    if (Slot* s = find(2, item)) {
      s->age = counter_++;
    } else {
      Slot* slot = find(1, 0);
      if (!slot) {
        slot = oldest();
        release(slot);
      }
      if (slot && table_) {
        slot->flags = 2;
        slot->item = static_cast<u16>(item);
        slot->age = counter_++;
        const u8* e = table_ + (item - 1) * 4;
        const s16 sector = static_cast<s16>(read_u16(e));
        const u16 sectors = read_u16(e + 2);
        slot->data.assign(static_cast<size_t>(sectors) << 11, 0);
        slot->load = static_cast<s16>(bgload_queue(&client_, kModelEntry, 0, slot->data.data(), sector, sectors));
      }
    }
  }
  item_ = static_cast<s16>(item);
  shown_ = false;  // the model object (0x80089728) goes
}

void ItemViewer::unpack(const Slot& s) {
  text_ = unpack_at(s.data, 0);
  if (s.data.size() >= 8) {
    const size_t second = ((read_u32(s.data.data() + 4) + 3) & ~3u) + 0x10;
    model_ = unpack_at(s.data, second);
  }
}

void ItemViewer::show() {
  shown_ = true;
  upload_texture();
  // 0x80088968(this, 0): the turn starts from scratch.
  angle_[0] = angle_[1] = angle_[2] = 0;
  speed_[0] = speed_[1] = speed_[2] = 0;
  phase_ = 0;
}

void ItemViewer::upload_texture() { upload_model_texture(model_, 0x200, 0x100, 0, 0x1E0, &clut_, &tpage_); }

void ItemViewer::upload_model_texture(const std::vector<u8>& model, int x, int y, int clut_x, int clut_y, u16* clut,
                                      u16* tpage) {
  // 0x800898cc: the model's texture block (header word 6): an 8-bit image (0x4000 bytes)
  // to (x, y) and its 256-colour CLUT to (clut_x, clut_y).
  *clut = static_cast<u16>((clut_y << 6) | ((clut_x >> 4) & 0x3F));
  *tpage = static_cast<u16>(((y & 0x100) >> 4) | ((x & 0x3FF) >> 6) | 0x80 | ((y & 0x200) << 2));
  if (model.size() < 0x2C) return;
  const u32 off = read_u32(model.data() + 0x18);
  if (off == 0 || off + 0x214 > model.size()) return;
  const u8* tex = model.data() + off;
  const u8* img = tex + 8 + read_u32(tex + 8);
  if (img + 0x400C > model.data() + model.size()) return;
  std::vector<u16> pixels(0x2000);
  std::memcpy(pixels.data(), img + 0xC, 0x4000);
  upload_image(psyq::RECT{static_cast<s16>(x), static_cast<s16>(y), static_cast<s16>(read_u16(img + 8)), static_cast<s16>(read_u16(img + 10))}, std::move(pixels));
  std::vector<u16> cluts(0x100);
  std::memcpy(cluts.data(), tex + 0x14, 0x200);
  upload_image(psyq::RECT{static_cast<s16>(clut_x), static_cast<s16>(clut_y), static_cast<s16>(read_u16(tex + 0x10)), static_cast<s16>(read_u16(tex + 0x12))}, std::move(cluts));
}

void ItemViewer::draw() { draw_model(model_, clut_, tpage_); }

void ItemViewer::draw_model(const std::vector<u8>& model, u16 clut, u16 tpage) {
  // 0x80088ba8: each triangle (0x20-byte records: flags, vertex / normal / colour / UV
  // indices) is transformed (RTPT), dropped when facing away (NCLIP), lit by its flags
  // (4 one normal, 8 smooth; 1 one colour, 2 three) and linked by depth into the viewer's
  // ordering table, which is spliced into its owner's entry 9.
  if (model.size() < 0x2C || !has_ot_) return;
  const u8* hdr = model.data();
  auto block = [&](int i) { return hdr + read_u32(hdr + i * 4); };
  const u8* prims = block(0);
  const u8* verts = block(1);
  const u8* normals = block(3);
  const u8* colours = block(4);
  const u8* uvs = block(5);
  const int count = read_u16(hdr + 0x1C);
  psx::Gte& g = psyq::gte();
  psyq::SetBackColor(0x20, 0x20, 0x20);
  const psyq::MATRIX colour = viewer_matrix(0x8008D298);
  psyq::SetColorMatrix(&colour);
  ctx_.ot_head = screen_clear_ot(ctx_.ot);
  const psyq::SVECTOR angle{static_cast<s16>(angle_[0] & 0xFFF), static_cast<s16>(angle_[1] & 0xFFF),
                            static_cast<s16>(angle_[2] & 0xFFF), 0};
  psyq::MATRIX rot, lit;
  psyq::RotMatrix(&angle, &rot);
  const psyq::MATRIX light = viewer_matrix(0x8008D2B8);
  psyq::MulMatrix0(&light, &rot, &lit);
  psyq::SetLightMatrix(&lit);
  psyq::MATRIX m;
  psyq::RotMatrix(&angle, &m);
  const psyq::VECTOR t{pos_[0], pos_[1], pos_[2], pos_[3]};
  psyq::TransMatrix(&m, &t);
  const psyq::VECTOR scale{0x1000, 0x1000, 0x1000, 0};
  psyq::ScaleMatrix(&m, &scale);
  psyq::SetRotMatrix(&m);
  psyq::SetTransMatrix(&m);
  const u32 grey = 0x404040;  // scratchpad +0xc: the colour of unlit-coloured faces
  auto ldv = [&](int i, const u8* v) {
    g.write_data(psx::Gte::VXY0 + i * 2, read_u32(v));
    g.write_data(psx::Gte::VZ0 + i * 2, read_u32(v + 4));
  };
  auto nccs = [&](const u8* n, u32 rgb) {
    ldv(0, n);
    g.write_data(psx::Gte::RGBC, rgb);
    g.execute(psx::Gte::kNccs);
    return g.read_data(psx::Gte::RGB2);
  };
  for (int i = 0; i < count; ++i) {
    const u8* r = prims + i * 0x20;
    const u16 f = read_u16(r);
    auto vtx = [&](int k) { return verts + read_u16(r + 2 + k * 2) * 8; };
    auto nrm = [&](int k) { return normals + read_u16(r + 0xE + k * 2) * 8; };
    auto col = [&](int k) { return read_u32(colours + read_u16(r + 0x14 + k * 2) * 4); };
    auto uv = [&](int k) { return read_u16(uvs + read_u16(r + 0x1A + k * 2) * 2); };
    ldv(0, vtx(0)), ldv(1, vtx(1)), ldv(2, vtx(2));
    g.execute(psx::Gte::kRtpt);
    g.execute(psx::Gte::kNclip);
    if (static_cast<s32>(g.read_data(psx::Gte::MAC0)) <= 0) continue;
    const u32 xy[3] = {g.read_data(psx::Gte::SXY0), g.read_data(psx::Gte::SXY1), g.read_data(psx::Gte::SXY2)};
    g.execute(psx::Gte::kAvsz3);
    const s32 otz = static_cast<s32>(g.read_data(psx::Gte::OTZ));
    u32 c[3] = {0, 0, 0};
    if (f & 4) {
      if (f & 1) {
        c[0] = c[1] = c[2] = nccs(nrm(0), col(0));
      } else if (f & 2) {
        for (int k = 0; k < 3; ++k) c[k] = nccs(nrm(k), col(k));
      } else {
        c[0] = c[1] = c[2] = nccs(nrm(0), grey);
      }
    } else if (f & 8) {
      if (f & 2) {
        for (int k = 0; k < 3; ++k) c[k] = nccs(nrm(k), col(k));
      } else {
        ldv(0, nrm(0)), ldv(1, nrm(1)), ldv(2, nrm(2));
        g.write_data(psx::Gte::RGBC, (f & 1) ? col(0) : grey);
        g.execute(psx::Gte::kNcct);
        c[0] = g.read_data(psx::Gte::RGB0), c[1] = g.read_data(psx::Gte::RGB1), c[2] = g.read_data(psx::Gte::RGB2);
      }
    } else if (f & 1) {
      c[0] = c[1] = c[2] = col(0);
    } else if (f & 2) {
      c[0] = col(0), c[1] = col(1), c[2] = col(2);
    }
    const int index = ot_far_ - otz;
    if (index < 0 || index >= ot_far_ - ot_near_) continue;
    auto set_rgb = [](u8* d, u32 v) { d[0] = static_cast<u8>(v), d[1] = static_cast<u8>(v >> 8), d[2] = static_cast<u8>(v >> 16); };
    auto set_xy = [](s16* d, u32 v) { d[0] = static_cast<s16>(v & 0xFFFF), d[1] = static_cast<s16>(v >> 16); };
    if (f & 0x10) {
      auto* p = prim_alloc<psyq::POLY_GT3>(&ctx_);
      if (!p) break;
      psyq::setPolyGT3(p);
      set_rgb(&p->r0, c[0]), set_rgb(&p->r1, c[1]), set_rgb(&p->r2, c[2]);
      p->code = (f & 0x80) ? 0x36 : 0x34;
      set_xy(&p->x0, xy[0]), set_xy(&p->x1, xy[1]), set_xy(&p->x2, xy[2]);
      const u16 a = uv(0), b = uv(1), d = uv(2);
      p->u0 = static_cast<u8>(a), p->v0 = static_cast<u8>(a >> 8);
      p->u1 = static_cast<u8>(b), p->v1 = static_cast<u8>(b >> 8);
      p->u2 = static_cast<u8>(d), p->v2 = static_cast<u8>(d >> 8);
      p->clut = clut;
      p->tpage = static_cast<u16>((f & 0x80) ? (tpage | (f & 0x60)) : tpage);
      prim_link(&ctx_, index, p);
    } else {
      auto* p = prim_alloc<psyq::POLY_G3>(&ctx_);
      if (!p) break;
      psyq::setPolyG3(p);
      set_rgb(&p->r0, c[0]), set_rgb(&p->r1, c[1]), set_rgb(&p->r2, c[2]);
      p->code = (f & 0x80) ? 0x32 : 0x30;
      set_xy(&p->x0, xy[0]), set_xy(&p->x1, xy[1]), set_xy(&p->x2, xy[2]);
      prim_link(&ctx_, index, p);
    }
  }
  // The texture page of the untextured faces (semi-transparency mode 1), drawn first.
  if (auto* mode = prim_alloc<psyq::DR_MODE>(&ctx_)) {
    const psyq::RECT tw{};
    psyq::SetDrawMode(mode, 0, 1, 0x36, &tw);
    prim_link(&ctx_, 0, mode);
  }
  psyq::P_TAG* own = ctx_.ot_head;
  psyq::P_TAG* host = owner_->ctx->ot_head + 9;
  own[ot_far_ - ot_near_ - 1].next = host->next;
  host->next = own;
}

bool ItemViewer::ready() {
  if (item_ == 0) return false;
  Slot* s = find(2, item_);
  if (!s || s->load != 0) return false;
  unpack(*s);
  show();
  return true;
}

void ItemViewer::update() {
  for (Slot& s : slots_) {
    if ((s.flags & 2) && s.load != 0 && bgload_is_done(s.load)) {
      bgload_cancel(s.load);
      s.load = 0;
    }
  }
  if (!shown_) {
    for (Slot& s : slots_) {
      if ((s.flags & 2) && s.item == static_cast<u16>(item_) && s.load == 0) {
        unpack(s);
        show();
      }
    }
    if (!shown_) return;
  }
  spin();
  draw();
}

void ItemViewer::spin() {
  // 0x800889ac: the turn speeds up, settles and wobbles through phases 0-5.
  switch (phase_) {
    case 0: ++phase_; break;
    case 1:
      if (++speed_[1] >= 0x41) ++phase_;
      break;
    case 2:
      if (++speed_[0] >= 0x40) ++phase_;
      break;
    case 3:
      if (++speed_[2] >= 0x80) ++phase_;
      break;
    case 4:
      ++speed_[1], --speed_[0], --speed_[2];
      if (speed_[1] >= 0x101) ++phase_;
      break;
    case 5:
      --speed_[1], ++speed_[0], ++speed_[2];
      if (speed_[1] <= 0x7F) --phase_;
      break;
    default: break;
  }
  auto eighth = [](s16 v) { return static_cast<s16>((v < 0 ? v + 7 : v) >> 3); };
  angle_[1] = static_cast<s16>(angle_[1] + eighth(speed_[1]));
  angle_[0] = static_cast<s16>(angle_[0] + eighth(speed_[0]));
  angle_[2] = static_cast<s16>(angle_[2] + eighth(speed_[2]));
}

bool ItemViewer::info(const u8** a, const u8** strings, const u8** font) const {
  if (item_ == 0 || !shown_ || text_.size() < 12) return false;
  const u8* t = text_.data();
  *a = t + read_u32(t);
  *strings = t + read_u32(t + 4);
  *font = t + read_u32(t + 8);
  return true;
}

// ==== CreationViewer =======================================================================

namespace {

// libgte SquareRoot0 (field 0x8006ffc4): the root table at field 0x80077bac.
s32 square_root0(s32 a) {
  psx::Gte& g = psyq::gte();
  g.write_data(psx::Gte::LZCS, static_cast<u32>(a));
  const u32 lz = g.read_data(psx::Gte::LZCR);
  if (lz == 32) return 0;
  const u32 even = lz & ~1u;
  const u32 shift = (31 - even) >> 1;
  s32 index;
  if (static_cast<s32>(even) - 24 < 0) {
    index = a >> (24 - even);
  } else {
    index = static_cast<s32>(static_cast<u32>(a) << (even - 24));
  }
  const u8* p = program_data(3100, 0x80077BAC + 2 * static_cast<u32>(index - 0x40));
  const s32 root = p ? static_cast<s16>(read_u16(p)) : 0;
  return static_cast<s32>((static_cast<u32>(root) << shift) >> 12);
}

// The model file's blocks: 1 vertices, 3 normals (8 bytes each); counts at +0x1c...
const u8* model_block(const std::vector<u8>& m, int block) { return m.data() + read_u32(m.data() + block * 4); }
int model_count(const std::vector<u8>& m, int field) { return m.size() >= 0x2C ? read_u16(m.data() + field) : 0; }
bool model_empty(const std::vector<u8>& m) { return model_count(m, 0x1C) == 0; }   // no triangles

struct V3 {
  s32 x, y, z;
};
V3 vec_at(const std::vector<u8>& m, int block, int count_field, int i) {
  if (i < 0 || i >= model_count(m, count_field)) return {0, 0, 0};
  const u8* v = model_block(m, block) + i * 8;
  if (v + 6 > m.data() + m.size()) return {0, 0, 0};
  return {static_cast<s16>(read_u16(v)), static_cast<s16>(read_u16(v + 2)), static_cast<s16>(read_u16(v + 4))};
}

// A vertex pushed out along its normal and in by its distance from the centre (0x8008aecc).
V3 morph_point(const std::vector<u8>& m, int i) {
  const V3 v = vec_at(m, 1, 0x1E, i);
  const V3 n = vec_at(m, 3, 0x22, i);
  psx::Gte& g = psyq::gte();
  g.write_data(psx::Gte::IR1, static_cast<u32>(v.x));
  g.write_data(psx::Gte::IR2, static_cast<u32>(v.y));
  g.write_data(psx::Gte::IR3, static_cast<u32>(v.z));
  g.execute(psx::Gte::kSqr0);
  const s32 len = square_root0(static_cast<s32>(g.read_data(psx::Gte::MAC1) + g.read_data(psx::Gte::MAC2) +
                                                g.read_data(psx::Gte::MAC3)));
  const s32 in = 0x400 - len / 2;
  auto eighth = [](s32 c) { return (c < 0 ? c + 7 : c) >> 3; };
  return {v.x + (eighth(n.x) - in), v.y + (eighth(n.y) - in), v.z + (eighth(n.z) - in)};
}

// Field 0x800700f4: out = a * ta / 4096 + b * tb / 4096 through GPF / GPL (saturated).
void lerp_vec(const u8* a, const u8* b, int ta, int tb, u8* out) {
  psx::Gte& g = psyq::gte();
  g.write_data(psx::Gte::IR0, static_cast<u32>(ta));
  g.write_data(psx::Gte::IR1, static_cast<u32>(static_cast<s16>(read_u16(a))));
  g.write_data(psx::Gte::IR2, static_cast<u32>(static_cast<s16>(read_u16(a + 2))));
  g.write_data(psx::Gte::IR3, static_cast<u32>(static_cast<s16>(read_u16(a + 4))));
  g.execute(psx::Gte::kGpf12);
  g.write_data(psx::Gte::IR0, static_cast<u32>(tb));
  g.write_data(psx::Gte::IR1, static_cast<u32>(static_cast<s16>(read_u16(b))));
  g.write_data(psx::Gte::IR2, static_cast<u32>(static_cast<s16>(read_u16(b + 2))));
  g.write_data(psx::Gte::IR3, static_cast<u32>(static_cast<s16>(read_u16(b + 4))));
  g.execute(psx::Gte::kGpl12);
  const u32 x = g.read_data(psx::Gte::IR1), y = g.read_data(psx::Gte::IR2), z = g.read_data(psx::Gte::IR3);
  const u32 w0 = (x & 0xFFFF) | (y << 16);
  std::memcpy(out, &w0, 4);
  std::memcpy(out + 4, &z, 4);
}

}  // namespace

CreationViewer::CreationViewer() {
  state_ = 0;
  for (s16& i : items_) i = 0;
}

CreationViewer::~CreationViewer() = default;

void CreationViewer::set_mode(int mode) {
  mode_ = static_cast<s16>(mode);
  state_ = 1;
}

void CreationViewer::set_items(int a, int b, int c, int d) {
  items_[0] = static_cast<s16>(a);
  items_[1] = static_cast<s16>(b);
  items_[2] = static_cast<s16>(c);
  items_[3] = static_cast<s16>(d);
  last_ = static_cast<s16>(d);
  for (s16& i : items_) {
    if (i == 0) i = -1;
  }
}

void CreationViewer::release_unused() {
  for (Slot& s : slots_) {
    if (!(s.flags & 2)) continue;
    bool keep = s.item == static_cast<u16>(last_);
    for (s16 i : items_) {
      if (s.item == static_cast<u16>(i)) keep = true;
    }
    if (!keep) release(&s);
  }
}

void CreationViewer::update() {
  ItemViewer::update();
  switch (state_) {
    case 1: {
      // Each item's file in the cache (the set_item calls queued them) becomes a model,
      // its texture at (0x200 + i * 0x40, 0x100) and CLUT row 0x1e0 + i.
      bool all = true;
      for (int i = 0; i < 4; ++i) {
        if (items_[i] == 0) continue;
        all = false;
        if (items_[i] == -1) {
          models_[i] = Model{};
          items_[i] = 0;
          continue;
        }
        Slot* s = find(2, items_[i]);
        if (s && s->load == 0) {
          unpack(*s);
          text_.clear();
          models_[i].data = std::move(model_);
          model_.clear();
          upload_model_texture(models_[i].data, 0x200 + i * 0x40, 0x100, 0, 0x1E0 + i, &models_[i].clut, &models_[i].tpage);
          items_[i] = 0;
          release_unused();
        }
      }
      if (all) {
        state_ = 2;
        // 0x80088968(this, 1): the turn starts already moving.
        angle_[0] = angle_[1] = angle_[2] = 0;
        speed_[0] = speed_[1] = speed_[2] = 0x40;
        phase_ = 1;
        frame_ = 0;
      }
      break;
    }
    case 3: {
      spin();
      const Pair& p = blend(frame_ * 0xD);
      draw_model(p.blended.data, p.blended.clut, p.blended.tpage);
      if (++frame_ > 300) state_ = 4;
      break;
    }
    default:
      break;
  }
}

void CreationViewer::prepare(Pair& p) {
  if (p.a_empty || p.b_empty) return;
  const std::vector<u8>& a = models_[p.a].data;
  const std::vector<u8>& b = models_[p.b].data;
  const int na = model_count(a, 0x1E), nb = model_count(b, 0x1E);
  p.table.assign(static_cast<size_t>(na) * 3, 0);
  std::vector<V3> qs(static_cast<size_t>(nb));
  for (int j = 0; j < nb; ++j) qs[static_cast<size_t>(j)] = morph_point(b, j);
  for (int i = 0; i < na; ++i) {
    const V3 pt = morph_point(a, i);
    u16 best = 0xFFFF;
    int at = 0xFFFF;
    for (int j = 0; j < nb; ++j) {
      const V3& q = qs[static_cast<size_t>(j)];
      const u32 dx = static_cast<u32>(q.x - pt.x), dy = static_cast<u32>(q.y - pt.y), dz = static_cast<u32>(q.z - pt.z);
      const u16 d = static_cast<u16>(square_root0(static_cast<s32>(dx * dx + dy * dy + dz * dz)));
      if (d <= best) {
        at = j;
        best = d;
      }
    }
    p.table[static_cast<size_t>(i) * 3] = static_cast<u16>(i);
    p.table[static_cast<size_t>(i) * 3 + 1] = static_cast<u16>(at);
    p.table[static_cast<size_t>(i) * 3 + 2] = best;
  }
}

void CreationViewer::build_morphs() {
  // Material 1 into the result and back, the other result into the result and back.
  static constexpr int kPairs[4][2] = {{0, 3}, {3, 0}, {2, 3}, {3, 2}};
  for (int i = 0; i < 4; ++i) {
    Pair& p = pairs_[i];
    p.a = kPairs[i][0];
    p.b = kPairs[i][1];
    p.blended = models_[p.a];
    p.a_empty = model_empty(models_[p.a].data);
    p.b_empty = model_empty(models_[p.b].data);
    p.table.clear();
    prepare(p);
  }
  // 0x8008ab60 / 0x8008ac14 with weight 0: the colours stay; the CLUTs go up again without
  // their semi-transparency bits.
  for (const Pair& p : pairs_) {
    const std::vector<u8>& m = p.blended.data;
    if (m.size() < 0x2C || p.blended.clut == 0) continue;
    const u32 off = read_u32(m.data() + 0x18);
    if (off == 0 || off + 0x214 > m.size()) continue;
    std::vector<u16> clut(0x100);
    for (int k = 0; k < 0x100; ++k) clut[static_cast<size_t>(k)] = read_u16(m.data() + off + 0x14 + k * 2) & 0x7FFF;
    upload_image(psyq::RECT{static_cast<s16>((p.blended.clut & 0x3F) << 4), static_cast<s16>(p.blended.clut >> 6), 0x100, 1},
                 std::move(clut));
  }
  state_ = 3;
}

const CreationViewer::Pair& CreationViewer::blend(int t) {
  int phase;
  if (t < 0x400) {
    phase = 0;
  } else if (t < 0x800) {
    phase = 1, t -= 0x400;
  } else if (t < 0xC00) {
    phase = 2, t -= 0x800;
  } else {
    phase = 3, t -= 0xC00;
  }
  const int w = (phase == 1 || phase == 3) ? ((0xC00 - t * 3) & 0xFFFF) : ((t * 3) & 0xFFFF);
  model_blend(pairs_[phase], w);
  return pairs_[phase];
}

void CreationViewer::model_blend(Pair& p, int t) {
  if (t > 0xFFF) t = 0xFFF;
  if (p.a_empty || p.b_empty) return;
  const std::vector<u8>& a = models_[p.a].data;
  const std::vector<u8>& b = models_[p.b].data;
  std::vector<u8>& out = p.blended.data;
  const int n = model_count(out, 0x1E);
  for (int i = 0; i < n && static_cast<size_t>(i) * 3 + 1 < p.table.size(); ++i) {
    const int ia = p.table[static_cast<size_t>(i) * 3];
    const int ib = p.table[static_cast<size_t>(i) * 3 + 1];
    if (ib >= model_count(b, 0x1E) || ib >= model_count(b, 0x22) || ia >= model_count(a, 0x22) ||
        i >= model_count(out, 0x22))
      continue;
    lerp_vec(model_block(b, 1) + ib * 8, model_block(a, 1) + ia * 8, t, 0xFFF - t, out.data() + read_u32(out.data() + 4) + i * 8);
    lerp_vec(model_block(b, 3) + ib * 8, model_block(a, 3) + ia * 8, t, 0xFFF - t, out.data() + read_u32(out.data() + 12) + i * 8);
  }
}

void CreationViewer::finish() {
  for (Model& m : models_) m = Model{};
  for (Pair& p : pairs_) p = Pair{};
  state_ = 0;
}

}  // namespace ss::game::menu
