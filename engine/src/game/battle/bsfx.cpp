#include "game/battle/bsfx.h"

#include <cstring>

#include "game/battle/render.h"
#include "game/boot/sound.h"

namespace ss::game::battle {
namespace {

struct Channels {
  u8 slot[8] = {};           // 0x8006fa54: the driver's effect slot (0 = free)
  BObj* emitter[8] = {};     // 0x8006fa5c
  s32 distance[8] = {};      // 0x8006fa7c
  BObj internal[8];          // 0x80081150: emitters of mode-2 effects
};
Channels& ch() {
  static Channels c;
  return c;
}

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

// The sound-test bitset's checksum: sum = ~sum + word for the 64 words at +0x1a0.
u32 sound_test_sum(const u8* config) {
  u32 sum = 0;
  for (int i = 0; i < 0x40; ++i) sum = ~sum + rd32(config + 0x1A0 + i * 4);
  return sum;
}

}  // namespace

void bsfx_reset() {
  Channels& c = ch();
  for (int i = 0; i < 8; ++i) {
    c.slot[i] = 0;
    c.emitter[i] = nullptr;
    c.distance[i] = 0;
    c.internal[i] = BObj();
  }
}

u32 bsfx_play(int sfx, const void* clip, BObj* emitter, int mode) {
  BattleGlobals& g = bg();
  Channels& c = ch();
  int free = 0;
  while (free < 8 && c.slot[free] != 0) ++free;
  if (free > 7) return 0xFFFFFFFFu;
  int s0 = 0;
  if (mode == 1) {
    const u8* p = static_cast<const u8*>(clip);
    const u8* bank = p + rd32(p);
    const int m = static_cast<s32>(rd32(bank) - rd16(bank + 4)) < 0x57E5 ? 1 : 2;
    s0 = sound_open_clip(g.sound, clip, m, nullptr);
  } else if (mode == 0) {
    s0 = sound_play_sfx(g.sound, sfx, 0, 0x7F40);
  } else if (mode == 2) {
    s0 = sound_play_sfx(g.sound, sfx, 0, 0x7F40);
    if (s0 < 1) return 0xFFFFFFFFu;
    emitter = &c.internal[s0 - 1];
  }
  if (s0 < 1) return 0xFFFFFFFFu;
  // The sound test: the effect's number (+0x20 of its driver slot) marks config +0x1a0.
  const SoundEffect& e = g.sound->effects[s0 - 1];
  const s32 number = e.script != nullptr ? e.header_low : 0;
  const u32 n = static_cast<u32>(number - 1);
  int use = 0;
  if (n < 0x500 && g.config != nullptr) {
    if (sound_test_sum(g.config) != rd32(g.config + 0x50)) {
      g.sound_test_tampered = 1;  // 0x8006f000
    } else {
      u8* w = g.config + 0x1A0 + (n >> 5) * 4;
      const u32 bits = rd32(w);
      if (((bits >> (n & 0x1F)) & 1) == 0) {
        const u32 nb = bits | 1u << (n & 0x1F);
        std::memcpy(w, &nb, 4);
        const u32 sum = sound_test_sum(g.config);
        std::memcpy(g.config + 0x50, &sum, 4);
      }
    }
  }
  use = free;
  for (int i = 0; i < 8; ++i)
    if (c.slot[i] == static_cast<u8>(s0)) {
      use = i;
      break;
    }
  if (emitter != nullptr) c.distance[use] = static_cast<s32>(0xFFFE7960);
  c.slot[use] = static_cast<u8>(s0);
  c.emitter[use] = emitter;
  if (emitter != nullptr) bsfx_pan_update(use);
  return (static_cast<u32>(s0) & 0xFF) << 16 | (static_cast<u32>(sfx) & 0xFF00FFFFu);
}

int bsfx_play_at_pos(int sfx, const s32 pos[3]) {
  const u32 r = bsfx_play(sfx, nullptr, nullptr, 2);
  if (static_cast<s32>(r) < 1) return -1;
  const u32 slot = ((r >> 16) & 0xFF) - 1;
  if (slot < 8) {
    BObj& e = ch().internal[slot];
    std::memcpy(e.pos, pos, 12);
  }
  return static_cast<s32>(r);
}

void bsfx_update() {
  Channels& c = ch();
  BattleGlobals& g = bg();
  for (int i = 0; i < 8; ++i) {
    if (c.slot[i] == 0) continue;
    if (sound_effect_playing(g.sound, c.slot[i]) == 0) {
      c.slot[i] = 0;
    } else if (c.emitter[i] != nullptr) {
      bsfx_pan_update(i);
    }
  }
}

void bsfx_pan_update(int i) {
  Channels& c = ch();
  const ViewState& v = view();
  const BObj* e = c.emitter[i];
  const s32 rx = (e->pos[0] - v.view_eye[0]) >> 12;
  const s32 ry = (e->pos[1] - v.view_eye[1]) >> 12;
  const s32 rz = (e->pos[2] - v.view_eye[2]) >> 12;
  s32 a = ratan2(rz, rx) - v.yaw;
  if (a < -0x7FF) a += 0x1000;
  if (0x7FF < a) a -= 0x1000;
  const int invert = a < 0 ? 1 : 0;
  // gte_ldlvl / sqr0: the components through the 16-bit IR registers.
  const s32 x = static_cast<s16>(rx), y = static_cast<s16>(ry), z = static_cast<s16>(rz);
  const s32 d = square_root0(x * x + y * y + z * z);
  if (a < 0) a = -a;
  s32 p = a * 0x7F;
  if (p < 0) p += 0x7FF;
  s32 vol = 0x7F;
  if (0x9FF < d) {
    s32 t = d - 0xA00;
    if (t < 0) t = d - 0x9C1;
    vol = 0x7F - (t >> 6);
  }
  if (vol < 0x10) vol = 0x10;
  sound_start_clip(bg().sound, c.slot[i], vol & 0xFF, (0x7F - (p >> 11)) & 0xFF, 0x2000, invert);
  c.distance[i] = d;
}

}  // namespace ss::game::battle
