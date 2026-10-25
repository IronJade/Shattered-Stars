#pragma once

#include "core/types.h"

// The leader stand-in (Archipelago open world; port feature, not in the original;
// docs/ap/seed_format.md "Party v2", docs/handoff/leader.md).
//
// In a seed with `world_mode: open_world`, on field maps, the walking actor shows the Formation
// leader (config +0x41: the member the camp menu's Combat > Formation > Leader picks, the one
// the player controls in battle) when that member is active and is not the story's hero, and the
// hero's name token in the map scripts' messages ({name:00} in Claude's game, {name:01} in
// Rena's) reads as the leader's name, in the text and in the speaker labels. The other hero's
// token stays theirs.
//
// Presentation only. The walking actor keeps the hero's slot (G[0xc4]), model, animation state
// (frame, timer, loops: what the scripts' animation waits count) and collision; scripts see the
// hero; nothing is written to the game state. Only the drawn sprite and the text differ, and
// with them what follows the drawn sprite: the emote above the head and the default message
// window row (top or bottom of the screen, from the sprite's top).
//
// One rule decides sprite and names together, so a box never shows a sprite and a name that
// disagree. The stand-in shows unless:
// - the seed is not open world, the map is a world map (the members' world sprites have 4 of
//   the 21 animations), the leader is the hero or not an active member (a Private Action keeps
//   only the hero), or a script has given the walking actor another model (the bunny, a
//   disguise);
// - the stand-in's sprite can't be had: the map's party packs are chosen by the party at the
//   map load, so for a member who joined (or was made leader) since, their own pack is read on
//   the side from the disc; without a disc, or while a script's palette tint is on (the own
//   copy has none), the hero;
// - "the hero for this scene" is latched. It latches when
//     * the stand-in's own actor exists (a scene shows them as themselves: the two would show),
//     * a script message names the stand-in (their literal name token, or a name variable
//       holding them), from that message on, so it never reads "Ashton: ... Ashton,"
//     * the walking actor is asked for an animation the stand-in lacks (scene animations, ids
//       above the 21 walking ones),
//   and clears when free play resumes (the walking actor has the controls back: no event or
//   talk running, the map's thread not waiting on a transfer, fade or choice, no fade or window
//   up) with none of these holding.
// Running: non-heroes have no run sprites; the stand-in shows their walk at the run's pace.
// Afterimage ghosts copy the actor as it is and show the hero.
namespace ss::game::field {

struct Actor;

struct StandIn {
  int model = -1;  // the leader's character id - 1: sprite key and name index
  int hero = -1;   // the hero's: 0 Claude (script flag 1), 1 Rena
};

// The stand-in now, or false (the hero shows). Evaluates the scene latch.
bool leader_standin(StandIn* out = nullptr);

// A map script's message text: with a stand-in, a copy with the hero's literal name tokens
// (code 0x400c) naming the stand-in instead; 'text' itself otherwise, or when the message names
// the stand-in (which latches the hero for the scene). Copies live until the next map.
const u8* leader_message_text(const u8* text);

// fieldactor_draw, right after actor_resolve_anim: while alive, the walking actor's (or its
// mirror children's) sprite fields show the stand-in; the destructor puts the hero's back.
class LeaderSpriteSwap {
 public:
  explicit LeaderSpriteSwap(Actor* self);
  ~LeaderSpriteSwap();
  LeaderSpriteSwap(const LeaderSpriteSwap&) = delete;
  LeaderSpriteSwap& operator=(const LeaderSpriteSwap&) = delete;
  bool active() const { return actor_ != nullptr; }

 private:
  Actor* actor_ = nullptr;
  const u8* saved_[5] = {};
  s16 saved_cel_ = 0;
};

// The sprite record of 'model' holding animation 'id' in sprite group 'group', as
// actor_resolve_anim reads it.
struct SpriteView {
  const u8* cel_header = nullptr;
  const u8* cels = nullptr;
  const u8* frames = nullptr;
  const u8* pixels = nullptr;
  const u8* palettes = nullptr;
  const u8* anim_data = nullptr;
};
bool sprite_view(const u8* group, int model, int id, SpriteView* out);

// The walking sprite animation the stand-in shows for the hero's sprite animation 'id'
// (0-6 walk, 7-13 stand, 14-20 run, by direction): the same, run -> walk; -1 for anything else
// (scene animations).
int leader_standin_anim(int id);

// The latch as the rules above set it (tests, the debug console).
bool leader_scene_latched();

// Tests and debugging. SS_LEADER_STANDIN=0 turns the stand-in off, =1 on without an open-world
// seed; SS_LEADER_SLOT=N takes party slot N as the Formation leader instead of config +0x41
// (presentation only: config is not written). Read once; the setters override them (-2: the
// environment's value).
void leader_set_debug(int standin, int slot);
void leader_reset();

}  // namespace ss::game::field
