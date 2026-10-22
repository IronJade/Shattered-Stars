#include "game/field/actor_base.h"

#include <cstring>

#include "game/common/math.h"
#include "game/field/actor_field.h"
#include "game/field/actors.h"
#include "game/field/field.h"
#include "game/field/map.h"
#include "game/field/screen.h"
#include "game/field/script_vm.h"
#include "game/field/sprite_pack.h"
#include "runtime/libgte.h"

namespace ss::game::field {
namespace {

// The abstract base vtable (0x80072dd4: every entry is __pure_virtual).
const ActorVtable kActorBaseVtable = {};

s32 ticks() { return globals().frame_ticks; }
s32 scene_scale() { return map_data().actor_scale; }  // 0x80075766: scene +0x68

u16 pop(Actor* a) {
  const u16 v = a->cmd[a->cmd_read];
  a->cmd_read = static_cast<u16>((a->cmd_read + 1) & 0xFF);
  return v;
}

// Pushes a command and its arguments when the ring has room for all of them.
template <typename... Args>
void push(Actor* a, u16 cmd, Args... args) {
  constexpr s16 words = 1 + sizeof...(Args);
  if (!actor_cmd_has_room(a, words)) return;
  a->cmd[a->cmd_write] = cmd;
  ((a->cmd_write = static_cast<u16>((a->cmd_write + 1) & 0xFF), a->cmd[a->cmd_write] = static_cast<u16>(args)), ...);
  a->cmd_write = static_cast<u16>((a->cmd_write + 1) & 0xFF);
}

s32 abs_shift12(s32 a, s32 b) {
  const s32 d = a - b;
  return (d < 0 ? b - a : d) >> 12;
}

// (s16 degrees << 12) / 360, as the interpreter converts its arguments.
s32 deg(u16 v) { return (static_cast<s32>(static_cast<s16>(v)) << 12) / 360; }

void copy_pos(s32* dst, const Actor* a) {
  dst[0] = a->x;
  dst[1] = a->y;
  dst[2] = a->z;
  dst[3] = a->w;
}

}  // namespace

Actor* actor_construct_base(Actor* self) {
  self->vtbl = &kActorBaseVtable;
  self->z = self->y = self->x = 0;
  self->target[2] = self->target[1] = self->target[0] = 0;
  self->start[2] = self->start[1] = self->start[0] = 0;
  self->vel_z = self->vel_x = 0;
  self->drawn_pos[2] = self->drawn_pos[1] = self->drawn_pos[0] = 0;
  self->drawn_scroll[1] = self->drawn_scroll[0] = 0;
  self->origin_y = self->origin_x = 0;
  self->field_3bc = self->field_3ba = 0;
  return self;
}

void actor_destroy(Actor* self, int flags) {
  self->vtbl = &kActorBaseVtable;
  delete[] self->pushed_state;
  self->pushed_state = nullptr;
  if (self->emote) actor_destroy(self->emote, 3);
  for (Actor* child : self->children)
    if (child) actor_destroy(child, 3);
  // Afterimages (0x800752ec) whose source is this actor go with it.
  if (actor_hooks().destroyed) actor_hooks().destroyed(self);
  if (flags & 1) delete self;
}

void actor_set_defaults(Actor* self) {
  self->drawn_lift = 1;
  self->drawn_frame = 4;
  self->anim_forward = 1;
  self->rgb[0] = self->rgb[1] = self->rgb[2] = 0x80;
  self->sprite_anim = -1;
  self->anim_data = nullptr;
  self->lift = 0;
  self->zone = nullptr;
  self->group = nullptr;
  self->flags = 0x1000F;
  self->cmd_read = 0;
  self->cmd_write = 0;
  self->move_mode = 0;
  self->turning = 0;
  self->wait_state = 0;
  self->frames = nullptr;
  self->anim_timer = 0;
  self->anim_frame = 0;
  self->cache_slots = 0;
  self->frozen = 0;
  self->fading = 0;
  self->blend = 0;
  self->spin_angle = 0;
  self->field_3e8 = 0;
  self->scale_x = 0x1000;
  self->scale_y = 0x1000;
  self->walked_last = 0;
  self->still_time = 0;
  self->laps = 0;
  self->spin_speed = 0;
  for (int i = 3; i >= 0; --i) self->cache_slot[i] = -1;
  self->pushed = 0;
  self->emote = nullptr;
  for (int i = 1; i >= 0; --i) self->children[i] = nullptr;
  self->lap_emote = 0x25;
  self->lap_limit = 8;
  self->shadow_w = 0x400;
  self->shadow_h = 0x400;
  self->tinted = 1;
  self->pushed_state = nullptr;
  self->depth = 0;
  self->vehicle = 0xFF;
}

void actor_init(Actor* self, s16 model, const s32 pos[3], int angle_deg, int layer, s16 talk_entry,
                s16 pickpocket_entry, u32 flags) {
  self->x = pos[0] << 12;
  self->y = pos[1] << 12;
  self->radius = 0x18;
  const s32 scale = scene_scale();
  self->z = pos[2] << 12;
  const s32 walk = (scale * 0x3000) / 100;
  self->walk_speed = walk;
  self->speed = walk;
  self->collide_radius = static_cast<s32>(static_cast<u32>(self->radius) * static_cast<u32>(scale) * 0x5A) / 0x960;
  const MapData& map = map_data();
  if (map.scene.empty()) {
    self->layer = 0;
  } else if (layer == -1) {
    self->layer = map.scene[0x5A];
  } else {
    self->layer = static_cast<u16>(layer);
  }
  self->anim = -1;
  self->model = model;
  self->zone = nullptr;
  self->talk_entry = talk_entry;
  self->pickpocket_entry = pickpocket_entry;
  self->flags = flags;
  self->drawn_frame = 0;
  const s32 a = (angle_deg % 360) * 0x1000;
  self->resolved_for = 0xFFFF;
  self->auto_anim = 0;
  self->prim_count = 0;
  self->walked = 0;
  self->cmd_read = 0;
  self->cmd_write = 0;
  self->move_mode = 0;
  self->turning = 0;
  self->fading = 0;
  self->blocked = 0;
  self->angle = static_cast<s16>(a / 360);
}

void actor_init_screen(Actor* self, s16 model, s16 x, s16 y, int layer) {
  const MapData& map = map_data();
  self->screen_x = x;
  self->screen_y = y;
  if (map.scene.empty()) {
    self->layer = 0;
  } else if (layer == -1) {
    self->layer = map.scene[0x5A];
  } else {
    self->layer = static_cast<u16>(layer);
  }
  self->flags = 0x820300;
  self->model = model;
  self->talk_entry = -1;
  self->drawn_frame = 0;
  self->anim = -1;
  self->resolved_for = 0xFFFF;
  self->auto_anim = 0;
  self->prim_count = 0;
  self->walked = 0;
  self->blocked = 0;
}

void actor_update(Actor* self, int control, int camera) {
  if (ticks() == 0) {
    self->walked = self->walked_last;
    return;
  }
  if (control == 0) actor_anim_advance(self);
  self->prev_angle = self->angle;
  if ((self->flags & kActorAnimPaused) == 0) self->anim_timer = static_cast<s16>(self->anim_timer + ticks());
  self->spin_angle = static_cast<s16>(self->spin_angle + self->spin_speed * ticks());
  if (control != 0) self->vtbl->control(self, camera);
  if (self->emote != nullptr && self->emote_timer != -1) {
    self->emote_timer = static_cast<s16>(self->emote_timer - ticks());
    if (self->emote_timer <= 0) {
      if (self->emote != nullptr) actor_destroy(self->emote, 3);
      self->emote = nullptr;
    }
  }
  if (self->frozen) {
    if (self->turning) {
      actor_turn_step(self);
      if (self->turning) return;
    }
    self->frozen = 0;
    self->frozen_turn_target = self->turn_target;
    self->frozen_turn_step = self->turn_step;
    self->frozen_turn_frames = self->turn_frames;
    self->frozen_turning = self->turning;
    return;
  }

  // The pending wait, if any.
  bool run = true;
  switch (self->wait_state) {
    case 1:
      self->wait_count = static_cast<s16>(static_cast<u16>(self->wait_count) - ticks());
      if (self->wait_count <= 0) {
        self->wait_state = 0;
      } else {
        run = false;
      }
      break;
    case 2:
    case 3:
      if (self->move_mode != 0) {
        run = false;
      } else if ((self->flags & kActorPatrol) && self->shuttle) {
        // Patrol: turn round and walk back.
        for (int i = 0; i < 4; ++i) std::swap(self->target[i], self->start[i]);
        self->heading = static_cast<s16>(ratan2(self->target[2] - self->z, self->target[0] - self->x));
        actor_save_anim(self);
        const s32 delay = actor_walk_frame_delay(self);
        actor_turn_start(self, self->heading, static_cast<u16>((delay * 0x1000) / 360), 0);
        const u8 mode = self->shuttle;
        self->turning = 1;
        self->shuttle = 0;
        self->still_time = 0;
        self->move_mode = mode;
        return;
      } else {
        self->wait_state = 0;
      }
      break;
    case 4:
      if (self->turning) {
        run = false;
      } else {
        self->wait_state = 0;
      }
      break;
    case 5: {
      const s32 base = self->loops_base, loops = self->anim_loops;
      if (self->loops_absolute == 0) {
        if (loops < base) {
          if (loops - (base - 0xFFFF) < self->wait_count) run = false;
        } else if (loops - base < self->wait_count) {
          run = false;
        }
      } else if (loops < self->wait_count) {
        run = false;
      }
      if (run) self->wait_state = 0;
      break;
    }
    case 6:
      if (self->fading) {
        run = false;
      } else {
        self->wait_state = 0;
      }
      break;
    default:
      break;
  }

  // The command interpreter: runs queued commands until one waits.
  for (;;) {
    if (self->cmd_read == self->cmd_write || !run) {
      if (self->move_mode != 0) {
        self->moved = 1;
        self->vtbl->move_step(self);
      }
      if (self->turning) {
        self->moved = 1;
        actor_turn_step(self);
      }
      if (self->fading) actor_fade_step(self);
      if (camera == 0) return;
      self->vtbl->follow_camera(self);
      return;
    }
    const s16 cmd = static_cast<s16>(pop(self));
    switch (cmd) {
      case kActorCmdWait: {
        self->wait_state = 1;
        const u16 frames = pop(self);
        self->wait_count = static_cast<s16>(frames);
        if (static_cast<s16>(frames) > 0) run = false;
        break;
      }
      case kActorCmdWaitMove:
        self->wait_state = 2;
        run = false;
        break;
      case kActorCmdWaitTurn:
        self->wait_state = 4;
        run = false;
        break;
      case kActorCmdWaitAnim:
        self->wait_state = 5;
        run = false;
        self->wait_count = static_cast<s16>(pop(self));
        self->loops_absolute = static_cast<u8>(pop(self));
        self->loops_base = self->anim_loops;
        break;
      case kActorCmdWaitFade:
        self->wait_state = 6;
        run = false;
        break;
      case kActorCmdTurnTo: {
        const u16 a = pop(self), s = pop(self);
        const s16 mode = static_cast<s16>(pop(self));
        actor_turn_start(self, static_cast<s16>(deg(a)), static_cast<u16>(static_cast<s16>(deg(s))), mode);
        self->turning = 1;
        break;
      }
      case kActorCmdTurnBy: {
        const u16 a = pop(self), s = pop(self);
        const s16 mode = static_cast<s16>(pop(self));
        const s16 angle = static_cast<s16>(static_cast<u16>(self->angle) + deg(a));
        actor_turn_start(self, angle, static_cast<u16>(static_cast<s16>(deg(s))), mode);
        self->turning = 1;
        break;
      }
      case kActorCmdTurnToActor: {
        const u32 slot = static_cast<u32>(static_cast<s32>(static_cast<s16>(pop(self))));
        if (slot < kFieldActorCount) {
          const Actor* other = field_actors()[slot];
          if (other != nullptr) {
            const s16 angle = static_cast<s16>(ratan2(other->z - self->z, other->x - self->x));
            const u16 s = pop(self);
            const s16 mode = static_cast<s16>(pop(self));
            actor_turn_start(self, angle, static_cast<u16>(static_cast<s16>(deg(s))), mode);
            self->turning = 1;
          } else {
            self->cmd_read = static_cast<u16>((self->cmd_read + 2) & 0xFF);
          }
        }
        break;
      }
      case kActorCmdMoveTo:
      case kActorCmdMoveToFar: {
        s32 tz;
        if (cmd == kActorCmdMoveTo) {
          self->target[0] = static_cast<s16>(pop(self)) << 12;
          self->target[1] = static_cast<s16>(pop(self)) << 12;
          tz = static_cast<s16>(pop(self)) << 12;
        } else {
          for (int i = 0; i < 2; ++i) {
            const u32 hi = pop(self), lo = pop(self);
            self->target[i] = static_cast<s32>((hi << 16 | lo) << 12);
          }
          const u32 hi = pop(self), lo = pop(self);
          tz = static_cast<s32>((hi << 16 | lo) << 12);
        }
        self->target[2] = tz;
        self->speed = static_cast<s16>(pop(self));
        actor_calc_speed(self);
        self->heading = static_cast<s16>(ratan2(self->target[2] - self->z, self->target[0] - self->x));
        self->move_mode = 1;
        copy_pos(self->start, self);
        self->still_time = 0;
        self->laps = 0;
        self->shuttle = 0;
        break;
      }
      case kActorCmdMoveToXz:
        if ((self->flags & kActorScreenSpace) == 0) {
          self->target[0] = static_cast<s16>(pop(self)) << 12;
          self->target[2] = static_cast<s16>(pop(self)) << 12;
          self->heading = static_cast<s16>(ratan2(self->target[2] - self->z, self->target[0] - self->x));
          self->speed = static_cast<s16>(pop(self));
          actor_calc_speed(self);
        } else {
          self->target[0] = static_cast<s16>(pop(self));
          self->target[1] = static_cast<s16>(pop(self));
          const u16 frames = pop(self);
          self->vel_z = self->screen_y;
          self->vel_x = self->screen_x;
          self->move_left = static_cast<s16>(frames);
          self->move_frames = static_cast<s16>(frames);
        }
        copy_pos(self->start, self);
        self->still_time = 0;
        self->laps = 0;
        self->shuttle = 0;
        self->move_mode = 2;
        break;
      case kActorCmdMoveBy:
        self->target[0] = (static_cast<s16>(pop(self)) << 12) + self->x;
        self->target[1] = (static_cast<s16>(pop(self)) << 12) + self->y;
        self->target[2] = (static_cast<s16>(pop(self)) << 12) + self->z;
        self->speed = static_cast<s16>(pop(self));
        actor_calc_speed(self);
        self->heading = static_cast<s16>(ratan2(self->target[2] - self->z, self->target[0] - self->x));
        copy_pos(self->start, self);
        self->still_time = 0;
        self->laps = 0;
        self->shuttle = 0;
        self->move_mode = 1;
        break;
      case kActorCmdMoveByXz:
        if ((self->flags & kActorScreenSpace) == 0) {
          self->target[0] = (static_cast<s16>(pop(self)) << 12) + self->x;
          self->target[2] = (static_cast<s16>(pop(self)) << 12) + self->z;
          self->speed = static_cast<s16>(pop(self));
          actor_calc_speed(self);
          self->heading = static_cast<s16>(ratan2(self->target[2] - self->z, self->target[0] - self->x));
        } else {
          self->target[0] = static_cast<s16>(pop(self)) + self->screen_x;
          self->target[1] = static_cast<s16>(pop(self)) + self->screen_y;
          const u16 frames = pop(self);
          self->vel_x = self->screen_x;
          self->vel_z = self->screen_y;
          self->move_left = static_cast<s16>(frames);
          self->move_frames = static_cast<s16>(frames);
        }
        copy_pos(self->start, self);
        self->still_time = 0;
        self->laps = 0;
        self->shuttle = 0;
        self->move_mode = 2;
        break;
      case kActorCmdMoveDir: {
        const u16 a = pop(self);
        const s32 dist = static_cast<s16>(pop(self));
        const s32 angle = static_cast<s16>(deg(a));
        self->target[0] = self->x + cos4096(static_cast<u32>(angle)) * dist;
        self->target[2] = self->z + sin4096(static_cast<u32>(angle)) * dist;
        self->speed = static_cast<s16>(pop(self));
        actor_calc_speed(self);
        self->heading = static_cast<s16>(ratan2(self->target[2] - self->z, self->target[0] - self->x));
        copy_pos(self->start, self);
        self->still_time = 0;
        self->laps = 0;
        self->shuttle = 0;
        self->move_mode = 2;
        break;
      }
      case kActorCmdMoveDirRel: {
        const u16 a = pop(self);
        const s32 dist = static_cast<s16>(pop(self));
        const s32 angle = static_cast<s16>(static_cast<u16>(self->angle) + deg(a));
        if ((self->flags & kActorScreenSpace) == 0) {
          self->target[0] = self->x + cos4096(static_cast<u32>(angle)) * dist;
          self->target[2] = self->z + sin4096(static_cast<u32>(angle)) * dist;
          self->heading = static_cast<s16>(ratan2(self->target[2] - self->z, self->target[0] - self->x));
          self->speed = static_cast<s16>(pop(self));
        } else {
          self->target[0] = self->screen_x + cos4096(static_cast<u32>(angle)) * dist;
          self->target[1] = self->screen_y + sin4096(static_cast<u32>(angle)) * dist;
          const u16 frames = pop(self);
          self->vel_z = self->screen_y;
          self->vel_x = self->screen_x;
          self->move_left = static_cast<s16>(frames);
          self->move_frames = static_cast<s16>(frames);
        }
        actor_calc_speed(self);
        copy_pos(self->start, self);
        self->still_time = 0;
        self->laps = 0;
        self->shuttle = 0;
        self->move_mode = 2;
        break;
      }
      case kActorCmdSetAnim: {
        const u16 anim = pop(self);
        self->anim_timer = 0;
        self->anim_frame = 0;
        self->anim_loops = 0;
        self->anim_forward = 1;
        self->anim = static_cast<s16>(anim);
        break;
      }
      case kActorCmdDelete:
        actor_delete(actor_system(), self);
        return;
      case kActorCmdWaitMoveB:
        self->wait_state = 3;
        run = false;
        break;
      case kActorCmdTurnToPoint: {
        const s32 px = static_cast<s16>(pop(self)), pz = static_cast<s16>(pop(self));
        const s32 angle = ratan2(pz - (self->z >> 12), px - (self->x >> 12));
        const u16 s = pop(self);
        const s16 mode = static_cast<s16>(pop(self));
        actor_turn_start(self, angle, static_cast<u16>(static_cast<s16>(deg(s))), mode);
        self->turning = 1;
        break;
      }
      case kActorCmdEmote: {
        actor_create_attachment(self);
        s32 icon = static_cast<s16>(pop(self));
        self->emote_timer = static_cast<s16>(pop(self));
        if (static_cast<s16>(pop(self)) != 0) {
          switch (icon) {
            case 0x24: icon = 0x39; break;
            case 0x27: icon = 0x36; break;
            case 0x2A: icon = 0x37; break;
            case 0x2E: icon = 0x38; break;
            case 0x35: icon = 0x3A; break;
            default: actor_or_flags(self->emote, 0, kActorFlipX); break;
          }
        }
        actor_cmd_set_anim(self->emote, static_cast<s16>(icon));
        actor_mark_dirty(self);
        break;
      }
      case kActorCmdClearEmote:
        if (self->emote != nullptr) {
          actor_destroy(self->emote, 3);
          self->emote = nullptr;
        }
        break;
      case kActorCmdSignal: {
        const u16 entry = pop(self);
        if (g_script_vm != nullptr) vm_call_sync(g_script_vm, entry);
        break;
      }
      case kActorCmdJumpTo:
      case kActorCmdJumpBy: {
        self->target[0] = static_cast<s16>(pop(self)) << 12;
        self->target[1] = static_cast<s16>(pop(self));
        self->target[2] = static_cast<s16>(pop(self)) << 12;
        if (cmd == kActorCmdJumpBy) {
          self->target[0] += self->x;
          self->target[2] += self->z;
        }
        self->jump_height = static_cast<s16>(pop(self));
        const u16 frames = pop(self);
        self->move_left = static_cast<s16>(frames);
        self->move_frames = static_cast<s16>(frames);
        if (self->move_left != 0) {
          self->vel_x = (self->target[0] - self->x) / self->move_left;
          self->vel_z = (self->target[2] - self->z) / self->move_left;
        }
        if (cmd == kActorCmdJumpBy) {
          if (self->target[1] == 0) {
            self->move_mode = 3;
            self->jump_angle_a = self->jump_height;
            break;
          }
        } else if (self->target[1] == self->y >> 12) {
          break;
        }
        actor_jump_setup(self);
        self->move_mode = 4;
        break;
      }
      case kActorCmdFade:
        actor_mark_dirty(self);
        for (int i = 0; i < 3; ++i) {
          self->fade_from[i] = self->rgb[i];
          self->fade_to[i] = static_cast<u8>(pop(self));
        }
        self->fade_frames = static_cast<s16>(pop(self));
        self->blend = static_cast<u8>(pop(self));
        if (self->fade_frames == 0) {
          for (int i = 0; i < 3; ++i) self->rgb[i] = self->fade_to[i];
        } else {
          self->fade_timer = 0;
          self->fading = 1;
        }
        break;
      default:
        break;
    }
  }
}

void actor_jump_setup(Actor* self) {
  if (self->target[1] < 1) {
    self->jump_angle_b = 0x800;
    if (self->jump_height < 0) {
      self->jump_angle_a = 0x400;
      self->jump_height = static_cast<s16>(-self->target[1]);
    } else {
      const s32 f = (self->target[1] * -0x1000) / self->jump_height;
      const s32 c = SquareRoot0(0x1000000 - f * f);
      self->jump_angle_a = static_cast<s16>(ratan2(f, c) & 0xFFF);
    }
    self->target[3] = self->y - self->jump_height;
  } else {
    const s32 t = self->target[1];
    self->jump_angle_a = 0;
    if (t < self->jump_height) {
      const s32 f = (t << 12) / self->jump_height;
      const s32 c = SquareRoot0(0x1000000 - f * f);
      self->jump_angle_b = static_cast<s16>(0x800 - (ratan2(f, c) & 0xFFF));
    } else {
      self->jump_height = static_cast<s16>(t);
      self->jump_angle_b = 0x400;
    }
    self->target[3] = self->y;
  }
  self->target[1] = self->target[1] * 0x1000 + self->y;
}

void actor_anim_advance(Actor* self) {
  const u8* anim = self->anim_data;
  if (anim == nullptr) return;
  const u8* frames = anim + 4;
  const s16 prev_cel = self->cel;
  auto duration = [&] { return frames[self->anim_frame * 4 + 3]; };
  bool timed_out = true;
  while (duration() != 0 || read_u16(anim) > 1) {
    const u8 d = duration();
    if (self->anim_timer <= static_cast<s16>(d)) {
      timed_out = false;
      break;
    }
    if (d != 0) self->anim_timer = static_cast<s16>(self->anim_timer - d);
    if (anim[2] == 1) {  // ping-pong
      const u16 f = static_cast<u16>(self->anim_frame);
      if (self->anim_forward == 0) {
        self->anim_frame = static_cast<s16>(f - 1);
        if (self->anim_frame == anim[3] - 1) {
          self->anim_frame = static_cast<s16>(f);
          self->anim_forward ^= 1;
          ++self->anim_loops;
        }
      } else {
        self->anim_frame = static_cast<s16>(f + 1);
        if (static_cast<u32>(static_cast<s32>(self->anim_frame)) == read_u16(anim)) {
          self->anim_frame = static_cast<s16>(f);
          self->anim_forward ^= 1;
        }
      }
    } else {
      self->anim_frame = static_cast<s16>(self->anim_frame + 1);
      if (static_cast<u32>(static_cast<s32>(self->anim_frame)) == read_u16(anim)) {
        self->anim_frame = anim[3];
        ++self->anim_loops;
      }
    }
  }
  if (timed_out) ++self->anim_loops;
  self->cel = static_cast<s16>(read_u16(frames + self->anim_frame * 4));
  if (prev_cel != self->cel) actor_mark_dirty(self);
}

void actor_calc_speed(Actor* self) {
  if (self->flags & kActorScreenSpace) return;
  if (self->speed < 0) {
    auto down8 = [](s32 v) { return (v < 0 ? v + 0xFF : v) >> 8; };
    psyq::VECTOR d{down8(self->x - self->target[0]), down8(self->y - self->target[1]), down8(self->z - self->target[2]), 0};
    psyq::Square0(&d, &d);
    const s32 len = SquareRoot0(d.vx + d.vy + d.vz);
    self->speed = (len << 8) / -self->speed;
  } else {
    self->speed = (self->speed * 0x1000 * scene_scale()) / 100;
  }
}

void actor_turn_start(Actor* self, s32 angle, u16 step, int mode) {
  const u16 a = static_cast<u16>(self->angle & 0xFFF);
  self->angle = static_cast<s16>(a);
  if (a > 0x7FF) self->angle = static_cast<s16>(a - 0x1000);
  s32 target = angle & 0xFFF;
  if (target > 0x7FF) target -= 0x1000;
  self->turn_step = static_cast<s16>(step & 0xFFF);
  if ((step & 0xFFF) == 0) self->turn_step = 1;
  if (mode == 0) {
    s32 d = target - self->angle;
    if (d >= 0x800) {
      d -= 0x1000;
    } else if (d < -0x800) {
      d += 0x1000;
    }
    s32 q = d / self->turn_step;
    if (q < 0) q = -q;
    self->turn_frames = static_cast<s16>(q);
    if (d < 0) self->turn_step = static_cast<s16>(-self->turn_step);
  } else if (mode == 1) {
    s32 d = target - self->angle;
    if (d < 0) d += 0x1000;
    s32 q = d / self->turn_step;
    if (q < 0) q = -q;
    self->turn_frames = static_cast<s16>(q);
    if (self->turn_step < 0) self->turn_step = static_cast<s16>(-self->turn_step);
  } else {
    s32 d = target - self->angle;
    if (d > 0) d -= 0x1000;
    s32 q = d / self->turn_step;
    if (q < 0) q = -q;
    self->turn_frames = static_cast<s16>(q);
    if (self->turn_step >= 1) self->turn_step = static_cast<s16>(-self->turn_step);
  }
  self->turn_target = static_cast<s16>(target);
}

void actor_turn_step(Actor* self) {
  for (s32 i = 0; i < ticks();) {
    self->turn_frames = static_cast<s16>(self->turn_frames - 1);
    ++i;
    if (self->turn_frames <= 0) {
      self->turning = 0;
      self->angle = self->turn_target;
      break;
    }
    self->angle = static_cast<s16>(self->angle + self->turn_step);
  }
  actor_mark_dirty(self);
}

void actor_fade_step(Actor* self) {
  self->fade_timer = static_cast<s16>(static_cast<u16>(self->fade_timer) + ticks());
  if (self->fade_timer < self->fade_frames) {
    for (int i = 0; i < 3; ++i) {
      const u32 delta = static_cast<u32>(self->fade_to[i]) - static_cast<u32>(self->fade_from[i]);
      const s32 step = static_cast<s32>(delta * static_cast<u32>(static_cast<s32>(self->fade_timer))) / self->fade_frames;
      self->rgb[i] = static_cast<u8>(self->fade_from[i] + static_cast<u8>(step));
    }
  } else {
    self->fading = 0;
    for (int i = 0; i < 3; ++i) self->rgb[i] = self->fade_to[i];
  }
  if (Actor* e = self->emote) {
    for (int i = 0; i < 3; ++i) e->rgb[i] = self->rgb[i];
    e->blend = self->blend;
    actor_mark_dirty(e);
  }
  actor_mark_dirty(self);
}

void actor_resolve_anim(Actor* self) {
  const u16 auto_anim = self->auto_anim;
  bool changed = false;
  if (self->resolved_for != auto_anim) {
    // Animations 0-0x23 are 12 directions x 3 (stand, walk, run): 7 sprite
    // animations per kind, the other directions drawn mirrored (table 0x8007445c).
    auto direction_anim = [](u32 a) {
      return static_cast<s16>(read_u32(field_data(0x8007445C + (a % 12) * 4)) + (a / 12) * 7);
    };
    if (self->anim == -1) {
      self->sprite_anim = direction_anim(auto_anim);
    } else if (static_cast<u16>(self->anim) < 0x24) {
      self->sprite_anim = direction_anim(static_cast<u32>(self->anim));
    } else {
      self->sprite_anim = static_cast<s16>(auto_anim);
    }
    const u32 id = static_cast<u32>(static_cast<s32>(self->sprite_anim));
    if (actor_slot(self) == 0x15) {
      // Slot 21 takes the last matching record of all packs.
      const SpritePackSlot* slots = sprite_pack_slots();
      for (int s = 0; s < 16; ++s) {
        if (slots[s].data.empty()) continue;
        const u8* pack = slots[s].data.data();
        for (u32 g = 0; g < read_u32(pack); ++g) {
          const u8* grp = pack + read_u32(pack + 4 + 4 * g);
          for (u32 r = 0; r < read_u32(grp); ++r) {
            const u8* rec = grp + 4 + r * kSpriteRecordSize;
            if (static_cast<s32>(read_u32(rec)) == self->model && read_u32(rec + 4) <= id && id <= read_u32(rec + 8))
              self->group = grp;
          }
        }
      }
    } else {
      self->group = sprite_pack_find_group(id, static_cast<u32>(static_cast<s32>(self->model)));
    }
    changed = true;
  }
  self->resolved_for = self->auto_anim;
  if (self->group == nullptr) return;  // (the console reads low RAM here)
  if (changed) {
    self->anim_loops = 0;
    const u8* grp = self->group;
    const s32 records = static_cast<s32>(read_u32(grp));
    s32 r = 0;
    u32 index = 0;
    for (; r < records; ++r) {
      const u8* rec = grp + 4 + r * kSpriteRecordSize;
      if (static_cast<s32>(read_u32(rec)) != self->model) continue;
      const u8* ids = grp + read_u32(rec + 0xC);
      index = 0;
      while (static_cast<s32>(index) < ids[0] && static_cast<u16>(self->sprite_anim) != ids[1 + index]) ++index;
      if (index != ids[0]) break;
    }
    if (r == records) return;
    const u8* rec = grp + 4 + r * kSpriteRecordSize;
    self->cel_header = grp + read_u32(rec + 0x14);
    self->cels = self->cel_header + 0x14;
    const u8* anims = grp + read_u32(rec + 0x10);
    const u8* frames = grp + read_u32(rec + 0x18);
    self->frames = frames + (self->cel_header[2] == 1 ? 0xC : 0x10);
    self->pixels = frames + read_u32(frames + 4);
    self->palettes = frames + read_u32(frames);
    self->anim_data = anims + read_u32(anims + 4 + index * 4);
  }
  self->cel = static_cast<s16>(read_u16(self->anim_data + self->anim_frame * 4 + 4));
}

bool actor_cmd_has_room(const Actor* self, s16 n) {
  if (self == nullptr) return false;
  const u16 r = self->cmd_read, w = self->cmd_write;
  if (w < r) return ((w + n) & 0xFF) < r;
  const u16 nw = static_cast<u16>((w + n) & 0xFF);
  if (r <= nw) return w <= nw;
  return true;
}

void actor_link_prims(Actor* self, int children_only, u32 layer_sel, psyq::P_TAG* alt_ot) {
  const ActorSystem* sys = actor_system();
  if (self->layer == 0) return;
  if (children_only != 0) {
    if (self->prim_count == 0) return;
    for (Actor* child : self->children)
      if (child) actor_link_prims(child, 0, 0xFFFFFFFF, nullptr);
    return;
  }
  Screen* screen = field_screen();
  auto rgb_of = [](psyq::P_TAG* p) { return reinterpret_cast<u8*>(psyq::prim_words(p)); };
  if (sys->tint_on && self->tinted) {
    for (s32 i = self->prim_count - 1; i >= 0; --i) {
      u8* c = rgb_of(self->prims[i]);
      for (int k = 0; k < 3; ++k) c[k] = static_cast<u8>((static_cast<u32>(c[k]) * sys->tint[k]) >> 7);
    }
  }
  if (self->rgb[0] != 0x80 || self->rgb[1] != 0x80 || self->rgb[2] != 0x80) {
    for (s32 i = self->prim_count - 1; i >= 0; --i) {
      u8* c = rgb_of(self->prims[i]);
      for (int k = 0; k < 3; ++k) {
        u32 v = (static_cast<u32>(c[k]) * self->rgb[k]) >> 7;
        if (v > 0xFF) v = 0xFF;
        c[k] = static_cast<u8>(v);
      }
    }
  }
  for (s32 i = self->prim_count - 1; i >= 0; --i) {
    if (layer_sel != 0xFFFFFFFF && layer_sel == self->prim_ot[i]) {
      // OT layers (0x800761c4): sorted by depth into the layer's own table. The
      // console links through the entry the table points at; OT layers are not
      // ported yet, so this path is unverified.
      const s32 d = self->depth >> 1;
      if (d < 0x800 && alt_ot != nullptr) psyq::addPrim(&alt_ot[0x820 - d], self->prims[i]);
    } else {
      psyq::addPrim(screen->ot_entry(16 + self->prim_ot[i]), self->prims[i]);
    }
  }
  if (self->prim_count != 0 && self->emote != nullptr) actor_link_prims(self->emote, 0, 0xFFFFFFFF, nullptr);
}

// --- command pushers ---

void actor_cmd_wait(Actor* self, s16 frames) { push(self, kActorCmdWait, frames); }
void actor_cmd_wait_move(Actor* self) { push(self, kActorCmdWaitMove); }
void actor_cmd_wait_move_b(Actor* self) { push(self, kActorCmdWaitMoveB); }
void actor_cmd_wait_turn(Actor* self) { push(self, kActorCmdWaitTurn); }
void actor_cmd_turn_to(Actor* self, s16 deg_, s16 speed_deg, s16 mode) { push(self, kActorCmdTurnTo, deg_, speed_deg, mode); }
void actor_cmd_turn_by(Actor* self, s16 deg_, s16 speed_deg, s16 mode) { push(self, kActorCmdTurnBy, deg_, speed_deg, mode); }
void actor_cmd_turn_to_actor(Actor* self, s16 actor, s16 speed_deg, s16 mode) {
  push(self, kActorCmdTurnToActor, actor, speed_deg, mode);
}
void actor_cmd_turn_to_point(Actor* self, s16 x, s16 z, s16 speed_deg, s16 mode) {
  push(self, kActorCmdTurnToPoint, x, z, speed_deg, mode);
}
void actor_cmd_move_to_xz(Actor* self, s16 x, s16 z, s16 speed) { push(self, kActorCmdMoveToXz, x, z, speed); }
void actor_cmd_move_to(Actor* self, s16 x, s16 y, s16 z, s16 speed) { push(self, kActorCmdMoveTo, x, y, z, speed); }
void actor_cmd_move_to_far(Actor* self, s32 x, s32 y, s32 z, s16 speed) {
  const u32 ux = static_cast<u32>(x), uy = static_cast<u32>(y), uz = static_cast<u32>(z);
  push(self, kActorCmdMoveToFar, ux >> 16, ux, uy >> 16, uy, uz >> 16, uz, speed);
}
void actor_cmd_move_by_xz(Actor* self, s16 dx, s16 dz, s16 speed) { push(self, kActorCmdMoveByXz, dx, dz, speed); }
void actor_cmd_move_by(Actor* self, s16 dx, s16 dy, s16 dz, s16 speed) { push(self, kActorCmdMoveBy, dx, dy, dz, speed); }
void actor_cmd_move_dir(Actor* self, s16 deg_, s16 dist, s16 speed) { push(self, kActorCmdMoveDir, deg_, dist, speed); }
void actor_cmd_move_dir_rel(Actor* self, s16 deg_, s16 dist, s16 speed) {
  push(self, kActorCmdMoveDirRel, deg_, dist, speed);
}
void actor_cmd_set_anim(Actor* self, s16 anim) { push(self, kActorCmdSetAnim, anim); }
void actor_cmd_wait_anim(Actor* self, s16 loops, s16 absolute) { push(self, kActorCmdWaitAnim, loops, absolute); }
void actor_cmd_signal(Actor* self, s16 value) { push(self, kActorCmdSignal, value); }
void actor_cmd_emote(Actor* self, s16 icon, s16 frames, s16 flag) { push(self, kActorCmdEmote, icon, frames, flag); }
void actor_cmd_clear_emote(Actor* self) { push(self, kActorCmdClearEmote); }

void actor_delete_attachment(Actor* a) {
  if (a->emote != nullptr) {
    actor_destroy(a->emote, 3);
    a->emote = nullptr;
  }
}

void actor_queue_delete(Actor* a) { push(a, kActorCmdDelete); }
void actor_queue_move_to(Actor* a, s16 x, s16 y, s16 z, s16 height, s16 frames) {
  push(a, kActorCmdJumpTo, x, y, z, height, frames);
}
void actor_queue_move_by(Actor* a, s16 dx, s16 y, s16 dz, s16 height, s16 frames) {
  push(a, kActorCmdJumpBy, dx, y, dz, height, frames);
}
void actor_queue_fade_colour(Actor* a, s16 r, s16 g, s16 b, s16 frames, s16 blend) {
  push(a, kActorCmdFade, r, g, b, frames, blend);
}
void actor_queue_wait_fade(Actor* a) { push(a, kActorCmdWaitFade); }

// --- queries and state ---

bool actor_collides(Actor* a, const s32* pos, int absolute) {
  s32 p[3];
  for (int i = 0; i < 3; ++i) p[i] = absolute == 0 ? (&a->x)[i] + pos[i] : pos[i];
  for (int i = 0; i < 0x80; ++i) {
    const Actor* o = field_actors()[i];
    if (o == nullptr || o == a || (o->flags & kActorSolid) == 0) continue;
    const s32 r = o->collide_radius;
    const s32 dx = abs_shift12(p[0], o->x);
    if (dx > r) continue;
    const s32 dz = abs_shift12(p[2], o->z);
    if (dz > r) continue;
    const s32 dy = abs_shift12(p[1], o->y);
    if (dy > r) continue;
    if (dy < 0xB5 && SquareRoot0(dx * dx + dy * dy + dz * dz) <= (r + a->collide_radius) / 2) return true;
  }
  return false;
}

bool actor_touches_save_point(const Actor* a) {
  const Actor* sp = field_actors()[0x80];
  if (sp == nullptr) return false;
  const s32 r = sp->collide_radius;
  const s32 dx = abs_shift12(a->x, sp->x);
  if (r < dx) return false;
  const s32 dz = abs_shift12(a->z, sp->z);
  if (r < dz) return false;
  if (abs_shift12(a->y, sp->y) > 0xB4) return false;
  return SquareRoot0(dx * dx + dz * dz) <= (r + a->collide_radius) / 2;
}

bool actor_is_busy(const Actor* a) {
  if (a == nullptr) return false;
  if (a->cmd_read != a->cmd_write) return true;
  if (a->turning || a->move_mode) return true;
  return a->wait_state != 0;
}

int actor_state_size(const Actor* a) {
  s32 r = a->cmd_read;
  if (a->cmd_write < r) r -= 0x100;
  return (a->cmd_write - r) * 2 + 0xA6;
}

void actor_save_state(Actor* a, u8* out) {
  a->emote_anim = a->emote == nullptr ? static_cast<s16>(-1) : a->emote->anim;
  std::memcpy(out, a, 0xA6);  // +0x00..+0xa1 and the first two ring words (overwritten below)
  u16 count = 0;
  for (u32 r = a->cmd_read; r != a->cmd_write; r = (r + 1) & 0xFF) {
    std::memcpy(out + 0xA6 + count * 2, &a->cmd[r], 2);
    ++count;
  }
  std::memcpy(out + 0xA2, &count, 2);
}

void actor_load_state(Actor* a, const u8* in, int recreate_attachment) {
  std::memcpy(static_cast<void*>(a), in, kActorStateBytes);
  a->cmd_read = 0;
  u16 count;
  std::memcpy(&count, in + 0xA2, 2);
  a->cmd_write = count;
  for (u32 r = 0, k = 0; r != a->cmd_write; r = (r + 1) & 0xFF, ++k) std::memcpy(&a->cmd[k], in + 0xA6 + k * 2, 2);
  if (recreate_attachment && a->emote_anim != -1) actor_create_attachment(a);
  actor_mark_dirty(a);
}

void actor_create_attachment(Actor* a) {
  if (a->emote != nullptr) actor_destroy(a->emote, 3);
  // The world map's class (0x80082d8c, 0x44c bytes) is not ported: the field class
  // stands in for it.
  a->emote = fieldactor_construct(new Actor);
  const s32 pos[3] = {0, 0, 0};
  actor_init(a->emote, 0, pos, 0, 0xFF, 0, -1, 0x1000F);
  Actor* e = a->emote;
  e->model = 0x7FFF;
  e->resolved_for = 0xFFFF;
  e->anim = a->emote_anim;
  e->anim_timer = 0;
  e->anim_frame = 0;
  e->anim_forward = 1;
  actor_or_flags(e, 0, kActorScreenSpace);
  actor_and_flags(e, 0, ~kActorShaded);
}

void actor_push_state(Actor* a) {
  if (a == nullptr) return;
  a->pushed = 1;
  a->saved_angle = a->angle;
  delete[] a->pushed_state;
  a->pushed_state = new u8[static_cast<size_t>(actor_state_size(a))];
  actor_save_state(a, a->pushed_state);
  a->wait_state = 0;
  a->move_mode = 0;
  a->turning = 0;
  a->moved = 0;
  a->cmd_read = a->cmd_write;
}

void actor_push_state_and_turn(Actor* a, int angle, int mode) {
  if (a == nullptr) return;
  if (a->flags & kActorLockedFacing) {
    actor_push_state(a);
    return;
  }
  a->pushed = 1;
  a->saved_angle = a->frozen ? a->frozen_turn_target : a->angle;
  delete[] a->pushed_state;
  a->pushed_state = new u8[static_cast<size_t>(actor_state_size(a))];
  actor_save_state(a, a->pushed_state);
  a->wait_state = 0;
  a->move_mode = 0;
  a->turning = 0;
  a->cmd_read = a->cmd_write;
  actor_save_anim(a);
  a->pop_turn_mode = static_cast<u8>(mode);
  actor_turn_start(a, angle, 0xAA, mode);
  a->turning = 1;
  a->moved = 0;
  actor_mark_dirty(a);
}

void actor_pop_state(Actor* a, int turn_back) {
  if (a == nullptr || a->pushed == 0) return;
  const s16 screen_x = a->screen_x, screen_y = a->screen_y;
  const s32 pos[4] = {a->x, a->y, a->z, a->w};
  const u8 mode = a->pop_turn_mode;
  const s32 back_angle = a->saved_angle;
  const u8 moved = a->moved;
  const s16 anim = a->anim, timer = a->anim_timer, frame = a->anim_frame;
  const u8 forward = a->anim_forward;
  const u32 flags = a->flags;
  a->pushed = 0;
  a->saved_angle = a->angle;
  actor_update(a, 0, 0);
  Actor* emote = a->emote;
  s16 emote_timer = 0, emote_anim = 0;
  if (emote != nullptr) {
    emote_timer = a->emote_timer;
    emote_anim = a->emote_anim;
  }
  actor_load_state(a, a->pushed_state, 0);
  if (a->pushed_state != nullptr) {
    delete[] a->pushed_state;
    a->pushed_state = nullptr;
  }
  a->angle = a->saved_angle;
  a->screen_x = screen_x;
  a->screen_y = screen_y;
  a->x = pos[0];
  a->y = pos[1];
  a->z = pos[2];
  a->w = pos[3];
  a->anim = anim;
  a->anim_timer = timer;
  a->anim_frame = frame;
  a->anim_forward = forward;
  a->flags = (a->flags & ~kActorLockedFacing) | (flags & kActorLockedFacing);
  if (turn_back != 0 && moved == 0 && (flags & kActorLockedFacing) == 0) {
    actor_save_anim(a);
    int m = 0;
    if (mode != 0) m = mode == 1 ? 2 : 1;
    actor_turn_start(a, back_angle, 0xAA, m);
    a->turning = 1;
  }
  if (emote != nullptr) {
    a->emote = emote;
    a->emote_timer = emote_timer;
    a->emote_anim = emote_anim;
  }
  actor_mark_dirty(a);
}

void actor_stop(Actor* a) {
  if (a == nullptr) return;
  a->move_mode = 0;
  a->turning = 0;
  a->fading = 0;
  a->cmd_read = a->cmd_write;
}

void actor_set_anim(Actor* a, s16 anim) {
  if (a == nullptr) return;
  a->anim = anim;
  a->anim_timer = 0;
  a->anim_frame = 0;
  a->anim_forward = 1;
}

void actor_mark_dirty(Actor* a) {
  if (a != nullptr) a->drawn_frame = 0;
}

void actor_set_flags(Actor* a, s16 part, u32 flags) {
  if (a != nullptr && part == 0) a->flags = flags;
}

void actor_or_flags(Actor* a, s16 part, u32 bits) {
  if (a != nullptr && part == 0) a->flags |= bits;
}

void actor_and_flags(Actor* a, s16 part, u32 mask) {
  if (a != nullptr && part == 0) a->flags &= mask;
}

int actor_walk_frame_delay(const Actor* a) {
  const s32 v = ((a->speed * 100) / scene_scale()) >> 12;
  switch (v < 1 ? 1 : v) {
    case 1: return 4;
    case 2: return 6;
    case 3: return 8;
    case 4: return 12;
    case 5: return 16;
    case 6: return 20;
    case 7: return 30;
    default: return 90;
  }
}

void actor_save_anim(Actor* a) {
  a->frozen = 1;
  a->frozen_turn_target = a->turn_target;
  a->frozen_turn_step = a->turn_step;
  a->frozen_turn_frames = a->turn_frames;
  a->frozen_turning = a->turning;
}

void actor_set_special(Actor* a, u8 laps, s16 emote) {
  a->lap_limit = laps;
  a->lap_emote = emote;
  a->flags |= kActorPatrol | kActorPatrolEmote;
}

}  // namespace ss::game::field
