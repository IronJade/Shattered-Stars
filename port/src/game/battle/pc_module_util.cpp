#include "game/battle/pc_module_util.h"

#include <cstring>
#include <unordered_map>

#include "game/battle/battle.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/render.h"
#include "game/battle/stage.h"
#include "game/common/screen.h"
#include "psx/gte.h"
#include "runtime/libgpu.h"
#include "runtime/libgte.h"

namespace ss::game::battle {
namespace {

std::unordered_map<const Shot*, Afterimage*> g_shot_images;

}  // namespace

Afterimage* shot_image(const Shot* s) {
  auto it = g_shot_images.find(s);
  return it == g_shot_images.end() ? nullptr : it->second;
}

void shot_image_set(const Shot* s, Afterimage* a) {
  Afterimage*& slot = g_shot_images[s];  // (element references survive a rehash)
  slot = a;
  if (a != nullptr) a->back = &slot;
}

void shot_image_reset(const Shot* s) {
  auto it = g_shot_images.find(s);
  if (it == g_shot_images.end()) return;
  if (it->second != nullptr) {
    it->second->back = nullptr;
    it->second->ending = 1;
  }
  g_shot_images.erase(it);
}

void shot_image_end(const Shot* s) {
  if (Afterimage* a = shot_image(s)) a->ending = 1;
}

void pc_camera_frame_target_for(PartyUnit* u, int ticks) {
  BattleGlobals& g = bg();
  if (u->camera_focus == 0 || g.controlled_slot != u->slot) return;
  if (unit_is_down(u->target)) return;
  camera_set_focus_unit(g.camera, u, u->target);
  camera_schedule_end(7, u, static_cast<s16>(ticks));
}

void gte_origin_at(const s32 pos[3]) {
  ViewState& v = view();
  psx::Gte& q = psyq::gte();
  const s16 d[3] = {static_cast<s16>((pos[0] - v.view_eye[0]) >> 12),
                    static_cast<s16>((pos[1] - v.view_eye[1]) >> 12),
                    static_cast<s16>((pos[2] - v.view_eye[2]) >> 12)};
  psyq::SetRotMatrix(&v.render_view);
  q.write_data(psx::Gte::VXY0, static_cast<u32>(static_cast<u16>(d[0])) | static_cast<u32>(static_cast<u16>(d[1])) << 16);
  q.write_data(psx::Gte::VZ0, static_cast<u32>(static_cast<s32>(d[2])));
  q.execute(psx::Gte::kRtv0);
  psyq::MATRIX m = v.render_view;
  m.t[0] = static_cast<s32>(q.read_data(psx::Gte::MAC1));
  m.t[1] = static_cast<s32>(q.read_data(psx::Gte::MAC2));
  m.t[2] = static_cast<s32>(q.read_data(psx::Gte::MAC3));
  psyq::SetRotMatrix(&v.render_view);
  psyq::SetTransMatrix(&m);
}

WeaponElementBits::WeaponElementBits(Unit* owner, u16 bits) : stats(owner->stats) {
  std::memcpy(&saved, stats + 0x5C, 2);
  const u16 v = static_cast<u16>(saved | bits);
  std::memcpy(stats + 0x5C, &v, 2);
}

WeaponElementBits::~WeaponElementBits() { std::memcpy(stats + 0x5C, &saved, 2); }

}  // namespace ss::game::battle
