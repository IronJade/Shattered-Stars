#include "game/field/leader.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "disc/archive.h"
#include "disc/slz.h"
#include "game/ap/ap.h"
#include "game/boot/loader.h"
#include "game/field/actor_base.h"
#include "game/field/actors.h"
#include "game/field/fade.h"
#include "game/field/field.h"
#include "game/field/message.h"
#include "game/field/script_vm.h"
#include "game/field/sprite_pack.h"
#include "runtime/runtime.h"

// The leader stand-in (leader.h): the rule, the walking actor's drawn sprite and the hero's
// name token in script messages. Port feature (Archipelago open world and hub); nothing here
// exists in the original, and without an open-world or hub seed none of it runs.
namespace ss::game::field {
namespace {

struct Debug {
  int standin = -2;  // -2: from SS_LEADER_STANDIN; -1 unset; 0 off; 1 on without a seed
  int slot = -2;     // -2: from SS_LEADER_SLOT; -1 config +0x41
};
Debug g_debug;

int env_int(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr && *v != 0 ? std::atoi(v) : -1;
}
int debug_standin() {
  static const int env = env_int("SS_LEADER_STANDIN");
  return g_debug.standin == -2 ? env : g_debug.standin;
}
int debug_slot() {
  static const int env = env_int("SS_LEADER_SLOT");
  return g_debug.slot == -2 ? env : g_debug.slot;
}

// The scene latch: "the hero for this scene" (leader.h), and what set it (traces).
bool g_latched = false;
const char* g_latch_why = "";
void latch(const char* why) {
  if (!g_latched) g_latch_why = why;
  g_latched = true;
}

// SS_LEADER_TRACE=1 (debugging, default off): a line on stdout whenever what the walking actor
// shows changes ("[standin] vblank V map M shows N (why)"), and for each message rewritten or
// latched.
bool tracing() {
  static const bool on = std::getenv("SS_LEADER_TRACE") != nullptr;
  return on;
}
int map_now() {
  const u8* gs = globals().game_state;
  return gs != nullptr ? static_cast<s16>(read_u16(gs + 0x1A)) : -1;
}
void trace_shows(int model, const char* why) {
  static int last_model = -2, last_map = -2;
  static const char* last_why = nullptr;
  if (!tracing()) return;
  const int map = map_now();
  if (model == last_model && map == last_map && why == last_why) return;
  last_model = model;
  last_map = map;
  last_why = why;
  std::printf("[standin] vblank %llu map %d shows %d (%s)\n",
              static_cast<unsigned long long>(runtime::Runtime::get().vblanks()), map, model, why);
}
void trace_text(const char* what, int a, int b) {
  if (!tracing()) return;
  std::printf("[standin] vblank %llu map %d message %s %d %d\n",
              static_cast<unsigned long long>(runtime::Runtime::get().vblanks()), map_now(), what, a, b);
}

// The own clock of a stand-in animation whose shape differs from the hero's (the run shown as
// the walk), for the walking actor and its two mirror children.
struct Clock {
  const Actor* actor = nullptr;
  int anim = -1;  // the stand-in animation it runs
  s16 frame = 0;
  s16 timer = 0;
  u8 forward = 1;
};
Clock g_clocks[3];

// The rewritten messages of this map (leader_message_text).
struct Rewrite {
  const u8* source = nullptr;
  int hero = -1, model = -1;
  std::vector<u8> original;  // the source's bytes when copied (a reused address is told apart)
  std::vector<u8> text;
};
std::vector<std::unique_ptr<Rewrite>> g_rewrites;
int g_rewrites_map = -1;

int rd16s(const u8* p) { return static_cast<s16>(read_u16(p)); }

// A member's own field sprite pack, read from the disc on the side when the party packs of the
// map don't hold them (they joined, or were picked as leader, after the map loaded: the map
// load chooses the packs by the party): the pack of that member alone (pack A 0xc27 + 2 * k
// for characters 3-7, pack B 0xc67 + k for 8-12, k their bit). A host read straight from the
// archive: no load queue, no drive time, nothing the game sees; only the stand-in's drawing
// uses it.
struct OwnPack {
  int character = -1;
  std::vector<u8> data;
};
OwnPack g_own_pack;

const u8* own_pack_group(int model, int id) {
  const int character = model + 1;
  if (character < 3 || character > 12) return nullptr;  // Claude and Rena are always loaded
  disc::Archive* archive = globals().loader != nullptr ? globals().loader->archive() : nullptr;
  if (g_own_pack.character != character && archive != nullptr) {
    g_own_pack = OwnPack{};
    g_own_pack.character = character;
    const int entry = character <= 7 ? 0xC27 + 2 * (1 << (character - 3)) : 0xC67 + (1 << (character - 8));
    if (archive->has_entry(static_cast<u32>(entry))) {
      std::vector<u8> raw(archive->entry_size(static_cast<u32>(entry)));
      if (archive->read_from_entry(static_cast<u32>(entry), raw.size(), raw.data())) {
        if (auto out = disc::slz::unpack_block(raw, 0, 0, nullptr)) g_own_pack.data = std::move(*out);
      }
    }
  }
  if (g_own_pack.character != character) return nullptr;
  const std::vector<u8>& pack = g_own_pack.data;
  if (pack.size() < 8) return nullptr;
  // sprite_pack_find_group over this one pack.
  const u8* base = pack.data();
  const u32 groups = read_u32(base);
  for (u32 g = 0; g < groups && 8 + 4 * g < pack.size(); ++g) {
    const u32 at = read_u32(base + 4 + 4 * g);
    if (at + 4 > pack.size()) return nullptr;
    const u8* group = base + at;
    const u32 records = read_u32(group);
    for (u32 r = 0; r < records; ++r) {
      const u8* rec = group + 4 + r * kSpriteRecordSize;
      if (rec + kSpriteRecordSize > base + pack.size()) return nullptr;
      if (static_cast<s32>(read_u32(rec)) == model && read_u32(rec + 4) <= static_cast<u32>(id) &&
          static_cast<u32>(id) <= read_u32(rec + 8))
        return group;
    }
  }
  return nullptr;
}

// Free play: the walking actor has the controls, unlocked, with no event or talk running and the
// map's thread not waiting on a transfer, fade, music change or choice (the camp menu's own
// test, field_menu_check, plus the choice), and no fade or window up. A map's thread may loop
// with other waits while the player walks.
bool free_play() {
  FieldGlobals& g = globals();
  const s16 control = static_cast<s16>(read_u16(g.field_flags + 0x10));
  if (control == -1 || control != g.game_state[0x24] || g.field_flags[0x26] != 0) return false;
  if (const ScriptCtx* ctx = g_script_vm) {
    const s32 w = ctx->wait_state;
    if (ctx->active_trigger != -1 || ctx->talk_active != 0 || w == 1 || w == 3 || w == 8 || w == 9) return false;
  }
  if (ScreenFade* f = field_fade(); f != nullptr && fade_is_active(f, -1)) return false;
  if (MsgSystem* m = msg_system()) {
    const int status = msg_window_status(m, -1);
    if (status != 1 && status != -1) return false;
  }
  return true;
}

// The stand-in's own actor: any field actor but the walking one drawn with their sprite key.
bool own_actor_exists(int model, const Actor* walking) {
  for (int i = 0; i < 0x82; ++i) {
    const Actor* a = field_actor(i);
    if (a != nullptr && a != walking && a->model == model) return true;
  }
  return false;
}

// Everything but the latch: who would stand in now ('why': the reason not, for traces).
bool candidate(StandIn* out, const char** why) {
  const int forced = debug_standin();
  *why = "off";
  if (forced == 0) return false;
  // Open-world and hub seeds (the hub's chambers play their original scenes through story views:
  // the same rule, latch and names as an open-world scene); story mode and non-AP games never.
  if (forced != 1 && !(ap::enabled() && ap::seed() != nullptr && (ap::seed()->open_world || ap::seed()->hub)))
    return false;
  FieldGlobals& g = globals();
  if (g.game_state == nullptr || g.config == nullptr || g.party == nullptr || g.field_flags == nullptr ||
      g.script_flags == nullptr)
    return false;
  *why = "world map";
  if (g.field_flags[0x68] != 0) return false;
  const int hero = ((g.script_flags[0] >> 1) & 1) != 0 ? 0 : 1;
  int slot = debug_slot();
  if (slot < 0) slot = g.config[0x41];
  *why = "no leader";
  if (slot < 0 || slot >= 8) return false;
  const int id = rd16s(g.party + slot * 0x60);
  *why = "the hero leads";
  if (id - 1 == hero) return false;
  *why = "leader not active";
  if (id < 1 || id > 12) return false;  // empty, or inactive (reserve, a Private Action)
  // The walking actor, when there is one, must still be the hero's: a script that gave it
  // another sprite (the bunny 199, a disguise) or made the other hero walk keeps its choice.
  *why = "walking actor's model changed";
  if (const Actor* walking = field_actor(g.game_state[0x24]); walking != nullptr && walking->model != hero)
    return false;
  *why = "";
  if (out != nullptr) {
    out->model = id - 1;
    out->hero = hero;
  }
  return true;
}

// A name code's member: 0x400c's literal index, or 0x4007's message variable.
int message_variable(int var) {
  const u8* globals_block = globals().script_globals;
  return globals_block == nullptr ? -1 : static_cast<s32>(read_u32(globals_block + 0x38 + var * 4));
}

}  // namespace

namespace {

// leader_standin with the reason when it says no (traces).
bool standin_why(StandIn* out, const char** why) {
  StandIn s;
  const bool free = free_play();
  if (free) g_latched = false;  // a scene's latch ends with it
  if (!candidate(&s, why)) return false;
  if (own_actor_exists(s.model, field_actor(globals().game_state[0x24]))) latch("own actor");
  if (g_latched) {
    *why = g_latch_why;
    return false;
  }
  if (out != nullptr) *out = s;
  return true;
}

}  // namespace

bool leader_standin(StandIn* out) {
  const char* why = "";
  return standin_why(out, &why);
}

bool leader_scene_latched() { return g_latched; }

int leader_standin_anim(int id) {
  if (id >= 0 && id <= 13) return id;        // walk, stand
  if (id >= 14 && id <= 20) return id - 14;  // run -> walk
  return -1;
}

bool sprite_view(const u8* group, int model, int id, SpriteView* out) {
  if (group == nullptr) return false;
  const s32 records = static_cast<s32>(read_u32(group));
  for (s32 r = 0; r < records; ++r) {
    const u8* rec = group + 4 + r * kSpriteRecordSize;
    if (static_cast<s32>(read_u32(rec)) != model) continue;
    const u8* ids = group + read_u32(rec + 0xC);
    u32 index = 0;
    while (static_cast<s32>(index) < ids[0] && static_cast<u32>(id) != ids[1 + index]) ++index;
    if (index == ids[0]) continue;
    SpriteView v;
    v.cel_header = group + read_u32(rec + 0x14);
    v.cels = v.cel_header + 0x14;
    const u8* anims = group + read_u32(rec + 0x10);
    const u8* frames = group + read_u32(rec + 0x18);
    v.frames = frames + (v.cel_header[2] == 1 ? 0xC : 0x10);
    v.pixels = frames + read_u32(frames + 4);
    v.palettes = frames + read_u32(frames);
    v.anim_data = anims + read_u32(anims + 4 + index * 4);
    if (out != nullptr) *out = v;
    return true;
  }
  return false;
}

LeaderSpriteSwap::LeaderSpriteSwap(Actor* self) {
  if (self == nullptr || self->anim_data == nullptr || self->cel_header == nullptr) return;
  FieldGlobals& g = globals();
  if (g.game_state == nullptr) return;
  Actor* walking = field_actor(g.game_state[0x24]);
  if (walking == nullptr) return;
  int which = -1;
  if (self == walking) {
    which = 0;
  } else if (self == walking->children[0]) {
    which = 1;
  } else if (self == walking->children[1]) {
    which = 2;
  }
  if (which < 0) return;
  StandIn s;
  const char* why = "";
  if (!standin_why(&s, &why)) {
    if (which == 0) trace_shows(self->model, why);
    return;
  }
  if (self->model != s.hero) return;  // a mirror child of another sprite
  const int id = leader_standin_anim(self->sprite_anim);
  if (id < 0) {
    // A scene animation the stand-in has none of: the hero for the rest of the scene.
    latch("scene animation");
    if (which == 0) trace_shows(self->model, g_latch_why);
    return;
  }
  // The map's party packs first (scripts' palette tints apply to them), else the member's own
  // pack from the disc; not while a tint is on (field_flags +0x27), which the own copy lacks.
  SpriteView v;
  bool own = false;
  if (!sprite_view(sprite_pack_find_group(static_cast<u32>(id), static_cast<u32>(s.model)), s.model, id, &v)) {
    own = g.field_flags[0x27] == 0 && sprite_view(own_pack_group(s.model, id), s.model, id, &v);
    if (!own) {
      if (which == 0) trace_shows(self->model, "stand-in sprite not loaded");
      return;
    }
  }
  if (which == 0) trace_shows(s.model, own ? "stand-in, own pack" : "stand-in");
  // The frame: the hero's own when the two animations have the same shape (the walks and
  // stands of all twelve do: 3 frames ping-pong / 1 frame, 8 vblanks each), else the
  // stand-in's own clock at the pace of the hero's current frame (the run, 6 vblanks).
  const u8* hero_anim = self->anim_data;
  const u8* anim = v.anim_data;
  const int count = read_u16(anim);
  if (count <= 0) return;
  int frame;
  Clock& c = g_clocks[which];
  if (read_u16(hero_anim) == count && hero_anim[2] == anim[2] && id == self->sprite_anim) {
    frame = self->anim_frame;
    c.actor = nullptr;
  } else {
    if (c.actor != self || c.anim != id) {
      c = Clock{};
      c.actor = self;
      c.anim = id;
    }
    const int hero_count = read_u16(hero_anim);
    const int at = self->anim_frame >= 0 && self->anim_frame < hero_count ? self->anim_frame : 0;
    int pace = hero_anim[4 + at * 4 + 3];
    if (pace <= 0) pace = anim[4 + c.frame * 4 + 3];
    c.timer = static_cast<s16>(c.timer + g.frame_ticks);
    for (int guard = 0; pace > 0 && c.timer >= pace && guard < 64; ++guard) {
      c.timer = static_cast<s16>(c.timer - pace);
      const int loop = anim[3] < count ? anim[3] : 0;
      if (anim[2] == 1) {  // ping-pong
        if (c.forward) {
          if (c.frame + 1 >= count) {
            c.forward = 0;
            c.frame = static_cast<s16>(c.frame > loop ? c.frame - 1 : c.frame);
          } else {
            ++c.frame;
          }
        } else if (c.frame - 1 < loop) {
          c.forward = 1;
          c.frame = static_cast<s16>(c.frame + 1 < count ? c.frame + 1 : c.frame);
        } else {
          --c.frame;
        }
      } else if (++c.frame >= count) {
        c.frame = static_cast<s16>(loop);
      }
    }
    frame = c.frame;
  }
  if (frame < 0 || frame >= count) frame = 0;
  actor_ = self;
  saved_[0] = self->cel_header;
  saved_[1] = self->cels;
  saved_[2] = self->frames;
  saved_[3] = self->pixels;
  saved_[4] = self->palettes;
  saved_cel_ = self->cel;
  self->cel_header = v.cel_header;
  self->cels = v.cels;
  self->frames = v.frames;
  self->pixels = v.pixels;
  self->palettes = v.palettes;
  self->cel = static_cast<s16>(read_u16(anim + 4 + frame * 4));
}

LeaderSpriteSwap::~LeaderSpriteSwap() {
  if (actor_ == nullptr) return;
  actor_->cel_header = saved_[0];
  actor_->cels = saved_[1];
  actor_->frames = saved_[2];
  actor_->pixels = saved_[3];
  actor_->palettes = saved_[4];
  actor_->cel = saved_cel_;
}

const u8* leader_message_text(const u8* text) {
  if (text == nullptr) return text;
  StandIn s;
  if (!leader_standin(&s)) return text;
  // One pass over the codes (text_layout_line's reading: a code above 0x7f takes a second
  // byte; 0x4003 / 0x4004 / 0x4007 / 0x4008 / 0x400c-0x400f carry one parameter byte, 0x4006
  // two): the hero's literal name codes, and whether the stand-in is named.
  std::vector<size_t> hero_at;
  size_t end = 0;
  for (const u8* p = text;;) {
    const u8* start = p;
    u32 c = *p++;
    if (c > 0x7F) c = (c & 0x7F) | static_cast<u32>(*p++) << 7;
    if (c == 0) {
      end = static_cast<size_t>(p - text);
      break;
    }
    if (c == 0x400C) {
      if (*p == s.model) {
        latch("message names the stand-in");  // they speak or are spoken to as themselves
        trace_text("names the stand-in", *p, 0);
        return text;
      }
      if (*p == s.hero) hero_at.push_back(static_cast<size_t>(p - text));
    } else if (c == 0x4007 && message_variable(*p) == s.model) {
      latch("message names the stand-in");
      trace_text("names the stand-in (variable)", *p, message_variable(*p));
      return text;
    }
    if (c == 0x4003 || c == 0x4004 || c == 0x4007 || c == 0x4008 || (c >= 0x400C && c <= 0x400F)) p += 1;
    if (c == 0x4006) p += 2;
    if (p - start > 8 || static_cast<size_t>(p - text) > 0x4000) return text;  // not text
  }
  if (hero_at.empty()) return text;
  // One copy per message, map and pair; a map change frees them (the load destroys every
  // window first).
  const int map = globals().game_state != nullptr ? rd16s(globals().game_state + 0x1A) : -1;
  if (map != g_rewrites_map) {
    g_rewrites.clear();
    g_rewrites_map = map;
  }
  for (const auto& r : g_rewrites) {
    if (r->source == text && r->hero == s.hero && r->model == s.model && r->original.size() == end &&
        std::memcmp(r->original.data(), text, end) == 0)
      return r->text.data();
  }
  auto r = std::make_unique<Rewrite>();
  r->source = text;
  r->hero = s.hero;
  r->model = s.model;
  r->original.assign(text, text + end);
  r->text = r->original;
  for (size_t at : hero_at) r->text[at] = static_cast<u8>(s.model);
  trace_text("rewritten", static_cast<int>(hero_at.size()), s.model);
  const u8* out = r->text.data();
  g_rewrites.push_back(std::move(r));
  return out;
}

void leader_set_debug(int standin, int slot) {
  g_debug.standin = standin;
  g_debug.slot = slot;
}

void leader_reset() {
  g_debug = Debug{};
  g_latched = false;
  g_latch_why = "";
  for (Clock& c : g_clocks) c = Clock{};
  g_rewrites.clear();
  g_rewrites_map = -1;
  g_own_pack = OwnPack{};
}

}  // namespace ss::game::field
