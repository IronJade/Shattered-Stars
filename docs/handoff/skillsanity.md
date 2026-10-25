# Handoff: skills as items, the skill guilds as checks (a new option)

Queued 2026-10-25 by the main session. No agent yet.

## The user's request (verbatim)
> The game also has people 'buy' the skills from skill guilds. Could the skills be picked up as
> drops or checks as well? Could we make that an option, and the skill guilds just give a list of
> drops per the number of skills they would have gotten? Like sensability 1 gives I believe 5
> skills, so the user with this option on could instead get 2 blackberries, a resurection mist, a
> sword for Claude, and a Key for an area instead of the skills. Then the user opens a chest and
> actually gets Motormouth, or Below the Belt, or Knife, as an item drop instead?

## The design to work out (research first, then report before building)
- Reverse the guilds: which guilds sell which skill sets (shop / menu code, docs/notes/menu.md),
  each set's skills, prices, how "a skill is available to learn" is stored (bits in the game state
  or the config), and how learning with SP works afterwards; catalog it.
- Option (APWorld, so2.apworld 0.2.0 unreleased), e.g. `skillsanity` off / on: each skill becomes an
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
