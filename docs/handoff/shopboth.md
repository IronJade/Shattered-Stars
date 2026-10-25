# Handoff: shop slots give the check AND the item bought (shuffle_shops)

Queued 2026-10-25 by the main session. No agent yet.

## The user's request (verbatim)
> The other is in the shuffle shops. We need to either shuffle the actual items in the shops, and
> just keep the specific shops with healing items(Blueberry, Blackberry, Ressurection Bottle)
> having those always available, but the other items shuffled around against the other shops
> listing and all, OR we need to allow the user to get the check on the first buy of the shop
> listing as well as the item they believed that they were buying. Last night I went to buy items
> for my team and I got a bunch of items(from the checks) that weren't what I bought, but the
> money was gone and I couldn't buy items and had to go farm for money again. This can confuse and
> irritate players. Either solution here works for me. Severed-Chains does this very thing, and
> marks the items in the shops with an archipelago symbol.

## The coordinator's choice: the second way (as Severed Chains)
Today (seed_format.md "Shop slots"): the first copy bought of a slot's item *is* the check: the
slot's item is given instead and the player doesn't get what they paid for. New: the first
purchase of a slot gives **both** the item bought (every copy paid for) **and** the slot's check
(its item given or sent, banner as now). Order shops (0x5a-0x5e): the order is delivered as
ordered, and the order is the check. Mark a slot whose check is still open with an Archipelago
symbol in the shop list (Severed Chains' way): a small icon drawn by the port beside the name, or a
glyph the game's font already has; keep colour 2 if the icon can't be done cleanly; say which.
Once checked, the entry is an ordinary one (no mark).

- Port: menu_shop.cpp `settle` (and the order shops), the list's marker; unit tests and smoke
  cases (buy -> both the item and the check; second buy -> just the item; full inventory; an
  order).
- APWorld: the ShuffleShops option text ("you get what you buy, and the slot's check too"),
  apworld.md, the player guide, seed_format.md "Shop slots". No logic change expected (shop slots
  never hold progression); say if the batch results change.
- Released players: shuffle_shops shipped in so2.apworld 0.1.x / port 0.02.00 with the old
  behaviour: the port change alone fixes existing seeds (no seed change needed); write that for
  the release notes.

## Rules
As in claudesword.md (own build dir engine/build-shopboth, scratchpad/shopboth/). APWorld tests
and the batches as docs/handoff/apworld2.md says.
