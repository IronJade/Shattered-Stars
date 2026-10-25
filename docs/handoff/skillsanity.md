# Handoff: skills as items, the skill guilds as checks (a new option)

Queued 2026-10-25 by the main session. No agent yet.

## The user's request (verbatim)
> The game also has people 'buy' the skills from skill guilds. Could the skills be picked up as
> drops or checks as well? Could we make that an option, and the skill guilds just give a list of
> drops per the number of skills they would have gotten? Like sensability 1 gives I believe 5
> skills, so the user with this option on could instead get 2 blackberries, a resurection mist, a
> sword for Claude, and a Key for an area instead of the skills. Then the user opens a chest and
> actually gets Motormouth, or Below the Belt, or Knife, as an item drop instead?

## The user's extension (2026-10-25, verbatim)
> With the skills being able to be gotten as individual items/drops, should we add it as a
> "Skillsanity" setting, and include not just skills, but the killer moves and spells for the
> characters as well, instead of those being driven by level, they are driven by finding them.
>
> Skillsanity:
> none - Off, skills, killer moves/spells are gained per standard.
> skills only - Skills are found, killer moves/spells are gained per standard.
> special only - Killer moves/spells are found, skills are gained per standard.
> insanity - All skills, killer moves/spells are found from checks/drops.

**The user's answers on the design (2026-10-25):**
- **Learn points become checks.** Where a character used to learn a killer move or spell (a
  level-up, or an event / item that teaches it), that moment sends a check instead (e.g. "Claude
  Lv 3"), one location per move: items and locations stay balanced and levelling stays rewarding.
- **First move given.** Under special only / insanity each character keeps the first killer move /
  spell they'd learn (Claude's Air Slash, Rena's Heal, ...): known when they join; every later move
  is found.

**So the option is `skillsanity`: none / skills_only / special_only / insanity** (Archipelago
identifiers; the display names as the user wrote them). Skills = the guild sets (below); special =
killer moves and spells (character-specific items, e.g. "Claude: Air Slash"; received before the
member joins: kept and applied at the join, party v2). Interactions: the killer_move level checks
need the move; craftsanity needs the specialty skills; the hub (characters as items, its levels).

## The user's second extension (2026-10-25, verbatim): sold in shops
> Skills, Killer Moves, and Spells should also be able to have a chance of being found in the
> shops as unique items being sold. It introduces oddities, but the cost of them should be relative
> to the level it would have been gained at x100. A level 3 special should be 300, etc. A single
> skill, like perseverance should be whatever the cost of it's original package cost divided by the
> number of skills in that guild pack. I believe sensibility 2 had perseverance and cost 1600 at
> Herlie. It had 4 skills making each in that pack a possible 400 fol item if it appeared in a shop.

To work out (with "shopboth", which reworks the shop slots: docs/handoff/shopboth.md):
- With shuffle_shops on, a shop slot holding one of this world's skill / killer move / spell items
  sells it as itself ("Perseverance", "Claude: Air Slash") at the user's price: a move = the level
  it would have been learned at x 100 (an event-taught move: say what level it stands for); a skill
  = its guild set's price / the number of skills in the set (verify the user's example, Sensibility
  2 at Herlie, 1600 / 4 = 400). Buying it gives the item (and is the slot's check).
- "A chance": how often such items land in shop slots (a weight or a share; propose).
- Shop slots never hold progression today (rules.py). Skills / moves can be progression under
  skillsanity (craftsanity's specialties, killer-move checks). Options: keep shops progression-free
  (only non-progression skill / move items there), or allow these items in shops (a price is
  always payable by farming; the logic can't model Fol). Recommend one; it is the user's call.

## The design to work out (research first, then report before building)
- Reverse the guilds: which guilds sell which skill sets (shop / menu code, docs/notes/menu.md),
  each set's skills, prices, how "a skill is available to learn" is stored (bits in the game state
  or the config), and how learning with SP works afterwards; catalog it.
- Research also: every member's killer moves and spells with how each is learned (level, event,
  item), and where the game stores 'learned' per member; then the option (APWorld, so2.apworld
  0.2.0 unreleased) as above. For skills: each skill becomes an
  item ("Skill: Motormouth": received -> available to the party, as buying the set would make it);
  each set's purchase becomes N locations, N = the skills in the set (bought as now, for its price;
  each of the N checks gives or sends its item).
- Logic: skills gate things. Item creation and craftsanity locations need specialties (Customize,
  Cooking, Machinery...); super specialties need combinations; some talents / killer moves may too.
  With the option on, those locations must require the skill items (they become progression),
  in every world mode (story, open world, hub; the guilds the hub reaches: the hub proof).
- Port: the guild purchase gives the N checks instead of the skills; a received skill item sets
  the "available" state; the menus show it as usual. Sidecar / save guard unaffected (check).
- Tests, the batches (check.py extended), docs (seed_format.md, apworld.md, the player guide).
- If something must be the user's call (for example skills that matter to the story, or a set
  the hub can't reach): stop and report with a recommendation.

## Rules
As in claudesword.md (own build dir engine/build-skillsanity, scratchpad/skillsanity/). APWorld
tests and the batches as docs/handoff/apworld2.md says.

## Done (log; agent "skillsanity")
- 2026-10-25 ~16:00 started (step 1: research, report before building). Scratch:
  scratchpad/skillsanity/ (dis/d1: every disc-1 map script disassembled with strings;
  tables.py, spells.py, guild_menus.py: the readers).
- ~16:20 research done; written down in docs/notes/menu.md "Skill guilds" (sets, prices, flags,
  guild maps, skill ids, starting skills, the moves / spells outside the learn tables) and the
  catalog (globals.csv k_book_groups + script_flags 700-711; functions.csv field_special_command
  0x12 Float; script_opcodes.csv ext 0x73 call sites). The report went to the coordinator.
- Findings outside this task, in the report: the three Cave of Trials "Portrait F / G / J chest"
  rows (maps 740 / 798 / 799, flags 2675 / 2657 / 2721) are the learn chests of Extinction /
  tri-Ace / Meteor Swarm (the routine's push of the move index was read as an item); a
  cave_of_trials seed patches that index with a placeholder and the port's ext 0x73 then writes
  past the 0x680-byte records block. The hub state presets flags 700-711 (all 12 sets bought).

## In progress
- Waiting for the coordinator's go-ahead and the user's calls (the report's list).

## Next steps (step 2, after the go-ahead)
- Port: guild flags 700-711 -> the set's checks; skill_book_allowed from received skills;
  learn tables / joins / ext 0x73 / items 464-469 / special 0x12 send checks instead of teaching;
  received skill and move items (sidecar, applied at joins); first moves given.
- APWorld: option, items, locations, rules, seed fields; tests, batches (check.py), docs.

## How to verify
(filled in as the work lands)
