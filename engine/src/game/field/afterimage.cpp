#include "game/field/afterimage.h"

#include "game/field/actor_field.h"
#include "game/field/actors.h"
#include "game/field/field.h"

namespace ss::game::field {
namespace {

constexpr int kGhosts = 12;
constexpr int kFirstSlot = 0x82;   // effect actors 0x82-0xc1
constexpr u32 kShowMask = 0xEFFEFFFF;  // clears kActorHidden and kActorShadow

Afterimage* g_afterimages[12] = {};  // 0x800752ec

Actor* ghost(const Afterimage* a, int i) { return a->slot[i] == -1 ? nullptr : field_actors()[a->slot[i]]; }

}  // namespace

Afterimage* afterimage_create(Afterimage* self, Actor* source, int count, u32 rgb, int blend, int mode) {
  self->source = source;
  self->count = static_cast<u8>(count > kGhosts ? kGhosts : count);
  self->head = 0;
  self->entry = nullptr;
  self->rgb = rgb;
  self->blend = static_cast<u8>(blend);
  self->stopping = 0;
  self->mode = static_cast<u8>(mode);
  // Each ghost takes the next free effect slot: a field actor with no sprite at the
  // origin (fieldactor_construct_full(0, 0, 0, 0, -1, -1, 0x1000f)).
  int s = kFirstSlot;
  Actor** actors = field_actors();
  for (int i = 0; i < kGhosts; ++i) {
    self->state[i] = 0;
    for (; s < kFieldActorCount; ++s) {
      if (actors[s] == nullptr) {
        static const s32 zero[3] = {0, 0, 0};
        actors[s] = fieldactor_construct_full(new Actor, 0, zero, 0, 0, -1, -1, 0x1000F);
        self->slot[i] = s++;
        break;
      }
    }
    if (s == kFieldActorCount) self->slot[i] = -1;
  }
  task_register(self, 2);
  return self;
}

void afterimage_destroy(Afterimage* self, int flags) {
  Actor** actors = field_actors();
  for (int i = 0; i < kGhosts; ++i) {
    const int s = self->slot[i];
    if (s != -1 && actors[s] != nullptr) {
      actor_destroy(actors[s], 3);
      actors[s] = nullptr;
    }
  }
  if (self->entry != nullptr) *self->entry = nullptr;
  task_unregister(self);
  if (flags & 1) delete self;
}

void Afterimage::update() { afterimage_update(this); }

void afterimage_update(Afterimage* self) {
  const s32 ticks = globals().frame_ticks;
  for (int i = 0; i < kGhosts; ++i)
    if (Actor* g = ghost(self, i)) actor_or_flags(g, 0, kActorHidden);
  // The 'count' ghosts 2, 4, 6 .. frames old take their faded colour.
  for (int k = 0; k < self->count; ++k) {
    const int i = (self->head + k * 2 + 2) % kGhosts;
    if (self->state[i] == 0) continue;
    if (Actor* g = ghost(self, i)) {
      g->blend = self->blend;
      actor_mark_dirty(g);
      const u32 b = static_cast<u32>(self->bright[i]);
      g->rgb[0] = static_cast<u8>(((self->rgb & 0xFF) * b) >> 7);
      g->rgb[1] = static_cast<u8>((((self->rgb >> 8) & 0xFF) * b) >> 7);
      g->rgb[2] = static_cast<u8>((((self->rgb >> 16) & 0xFF) * b) >> 7);
      actor_and_flags(g, 0, kShowMask);
    }
  }
  // Every live ghost fades (and grows / spins) and stays shown.
  int live = kGhosts;
  for (int i = 0; i < kGhosts; ++i) {
    if (self->state[i] == 0) {
      --live;
      continue;
    }
    if (self->slot[i] == -1) continue;
    self->bright[i] -= 10;
    if (self->bright[i] < 1) self->state[i] = 0;
    Actor* g = field_actors()[self->slot[i]];
    if (g == nullptr) continue;
    const u8 st = self->state[i];
    if (st == 2 || st == 4) {
      g->scale_x += ticks * 0x80;
      g->scale_y += ticks * 0x80;
    }
    if (st == 3 || st == 4) g->spin_angle = static_cast<s16>((g->spin_angle + ticks * 0x28) & 0xFFF);
    actor_and_flags(g, 0, kShowMask);
  }
  if (self->stopping != 0) {
    if (live == 0) afterimage_destroy(self, 3);
    return;
  }
  // A new ghost: a copy of the source as it is now.
  const int h = self->head;
  self->state[h] = static_cast<u8>(self->mode + 1);
  self->mode = 0;
  if (Actor* g = ghost(self, h); g != nullptr && self->source != nullptr) {
    // The original copies all 0x3fc bytes but its own vtable (+0x3f8), sharing the
    // source's owned pointers; the port leaves the ghost's own (none) so that deleting
    // a ghost cannot free the source's emote, children or pushed state.
    const ActorVtable* vtbl = g->vtbl;
    Actor* emote = g->emote;
    Actor* children[2] = {g->children[0], g->children[1]};
    u8* pushed = g->pushed_state;
    *g = *self->source;
    g->vtbl = vtbl;
    g->emote = emote;
    g->children[0] = children[0];
    g->children[1] = children[1];
    g->pushed_state = pushed;
    actor_or_flags(g, 0, kActorAnimPaused);
    actor_and_flags(g, 0, kShowMask);
    g->blend = self->blend;
    actor_mark_dirty(g);
    g->rgb[0] = static_cast<u8>(self->rgb);
    g->rgb[1] = static_cast<u8>(self->rgb >> 8);
    g->rgb[2] = static_cast<u8>(self->rgb >> 16);
    self->bright[h] = 0x80;
  }
  if (--self->head < 0) self->head = kGhosts - 1;
}

void afterimage_attach(Actor* source, int count, u32 rgb, int blend, int mode) {
  if (source == nullptr) return;
  for (auto& e : g_afterimages) {
    if (e != nullptr) continue;
    e = afterimage_create(new Afterimage, source, count, rgb, blend, mode);
    e->entry = &e;
    return;
  }
}

void afterimage_detach(const Actor* source) {
  for (auto& e : g_afterimages) {
    if (e != nullptr && e->source == source) {
      afterimage_destroy(e, 3);
      e = nullptr;
    }
  }
}

void field_destroy_map_objects() {
  for (auto& e : g_afterimages) {
    if (e != nullptr) {
      afterimage_destroy(e, 3);
      e = nullptr;
    }
  }
}

}  // namespace ss::game::field
