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

Phase rule (coordinator, 2026-10-25): until the coordinator sends "commit done", edit only the
port's shop code (menu_shop.cpp, the marker's drawing) and new test files; the smoke cases,
options.py, seed_format.md, apworld.md, engine/README.md and the player guide after it. Other
agents: hubproof (docs/handoff/hubproof.md), hubask2 (hub.*, seed.*, hub.py; hubask.md).

## Done (log)
- 2026-10-25 agent "shopboth" started. engine/build-shopboth configured (RelWithDebInfo, AP net +
  setup on, as build-hubcore). Runner: scratchpad/shopboth/run.sh NAME ARGS (prlimit 4 GB,
  scratch XDG + card, no mods, dummy audio, offscreen video; log runs/NAME.log).
- Read: menu_shop.cpp (settle calls ap::on_shop_buy, which today keeps the first copy as the
  check: returns count - 1), ap.cpp on_shop_buy / shop_slot_open, smoke ap_p4_shop, test_ap
  ap_shop_slots. The list's TextLine already carries icons (code 0x400c button icons, drawn as
  POLY_FT4 by TextRenderer::draw_line): the plan for the marker is a port icon there.

- Found: the colour-2 marker of 0.02.00 NEVER SHOWED. draw_row called set_colour(2) after
  draw_item's begin_line, which had already latched the colour table (set_tables), so a slot's
  name looked like any other (baseline capture scratchpad/shopboth/cap/base_list_zoom.png).
  Colour 2 is the money box's pink. Also: draw_row asked shop_slot_open for the SELL list too.
- "commit done" (366343f) received: phase rule lifted (keep hunks small: skillsanity also edits
  options.py and maybe the shop code).
- Port change (built, ss_game_tests 174/0):
  - menu_shop.cpp settle: the original's booking again (inventory_add of every copy; orders
    whole, e.cart), then one AP loop after the purchases AND the money write:
    ap::on_shop_buy per bought / ordered entry. (After the money: a Fol check was overwritten by
    the balance before, a bug of the old code too.)
  - ap.cpp/.h on_shop_buy now returns bool (this purchase was the check), logs "shop S entry I
    (item N): C bought, and the slot's check"; a full stack: the check's item waits (pending,
    logged "the inventory is full; X waits").
  - ap::order_overflow + field_world.cpp courier (state 0x1e): with AP on, ordered copies the
    inventory refuses (20 of a kind; a check's own Blueberry, the filler, can be the 21st of an
    order of 20) go in as far as they fit and the rest waits as "Got X" items instead of
    vanishing as in the original. AP off: the original (nothing added, nothing done).
  - The mark: the Archipelago logo (its six circles, drawn as 11 x 11 pixel art with the text's
    drop shadow, 12 x 12 texels, 15-bit) uploaded by ShopList (buy list with an open slot only)
    into the menus' text VRAM (cvram_alloc, freed in ~ShopList), drawn after the name as a port
    image in the line's icon list (menu_gfx.h TextIcon tpage/u/v/w/h; draw_line draws it), kept
    left of the price's first digit. No VRAM: the name in colour 2 (set_tables, now working).
  - test_ap.cpp ap_shop_slots: buy -> both; second buy -> item only; Alice's slot sent; full
    stack -> the check's item waits; courier overflow -> fits + waits; AP off -> nothing.
- Capture: scratchpad/shopboth/cap/new_list_01890.png (Sandals and Lavender marked).

- Proofs, headless (scratchpad/shopboth/run.sh; logs runs/, captures cap/):
  - both (runs/both.log, cap/both_sheet.png): Hearn's shop, 1 Sandals + 3 Lavender: "Hookshot
    for Alice" sent, "Revival Card" ours; Fol 50000 -> 48520 (all four paid); the Equipment
    Wizard equips the bought Sandals; banner "Got Revival Card"; second visit: no marks,
    "Inventory 3" on Lavender, 1 more bought (Fol 48030), no check; probes (`item N 20` says how
    many fit): 4 Lavender, 1 Revival Card held.
  - full (runs/full.log, cap/full_sheet.png): 20 Revival Cards held first: the 3 Lavender go in,
    "the inventory is full; Revival Card waits"; a Revival Card sold on the second visit -> "given
    Revival Card", banner "Got Revival Card", 20 held.
  - order (runs/order.log, cap/order_sheet.png; mods-order/order.patch turns Hearn's B 0x6d 1
    at word 0x21d into the order shop 0x5a, seed order.apso2): marks on Aquaberry (Alice's) and
    Blueberry (ours: a Blueberry, the filler); 20 Blueberries ordered (Fol -1200), placing the
    order is the check (our Blueberry given at once); the courier (global 0x334 1, flag 13 1)
    "Blueberry x 20 acquired": 19 fit, 1 waits (`courier: 20 x item 757 ordered, 19 fit; 1
    wait`), 20 held. Without the guard all 20 would have vanished (1 + 20 > 20).
  - no seed (runs/noseed_new.log vs noseed_old.log, build-charselect's pre-change binary): the
    same purchase, frames 1590-2500 pixel-identical, the same Fol and inventory.

## Note for skillsanity (coordinator's question)
- "A slot that sells an AP item directly at a set price" fits on top of this: settle now books
  the game's copies first and then calls ap::on_shop_buy once per entry with a cart, and the list
  draws a port image per row (TextIcon tpage). An AP-only entry would need: an ItemList entry
  kind with no game item (settle skips its inventory_add, cart capped at 1, price from the seed
  instead of item field 0), draw_row drawing its name (the list's font is the item pack's
  92-glyph font, not the map font ap::glyph_text writes: the name needs that font's codes, or a
  port-drawn line), and shop_slot_open-style lookup for it. Not built.

- Docs and text: options.py ShuffleShops ("the first time you buy a slot's item, you get what
  you buy, and the slot's check too"; the logo), the player guide (shop paragraph + options
  row), docs/ap/apworld.md "Shop slots" (a "Buying" paragraph), seed_format.md "Shop slots",
  original_bugs.md W5 (the courier's overflow: kept without a seed). tools/ap_e2e.sh and
  ap_e2e_so2.sh: their shop regexes follow the new log line (not run: they need the AP server).
- Smoke (tools/smoke_field_events.sh): ap_p4_shop rewritten (both, the Wizard, second purchase,
  probes), ap_shop_full and ap_shop_order added.
- APWorld: world tests 318 passed, AP general 379 passed (as charselect's).
- Suites: ss_tests 99/0, ss_game_tests 174/0, smoke 246/0 (230 + the 16 new shop checks).
- Full batch (players, seeds 1-96 -> scratchpad/shopboth/batch/full): 96/96 exit 0, 2 .apso2
  each; check.py 192 files, 0 errors; compare.py vs scratchpad/apworld2/batch/full_cs: all 192
  byte-identical (no logic change, as expected).
- Hub batch (players_hub, seeds 1-96 -> scratchpad/shopboth/batch/hub): 96/96 exit 0; check.py
  192 files, 0 errors. vs hub_cs: 191 of 192 differ, NOT from this work: hubproof rewrote the hub
  tables (data.py HUB_REACHED, hub.py, at 16:23:03, after seed 1 at 16:23:00), and even seed 1
  differs from hub_cs in `bosses` (the hub commits since 15:44). shopboth's APWorld change is the
  option docstring only; the full batch's byte identity is the evidence.
- Scroll proof (cap/scroll_sheet.png): down to Spectacles and back; the reused ring lines carry
  no stale mark.
- Proof image: cap/proof_marks.png (marked list; after the checks; the order shop).

## Release notes line
(release notes: docs/release_notes/0.02.50.md)

## Done (continued)

- tools/ap_e2e_so2.sh (AP_E2E_WORK=scratchpad/shopboth/e2e_so2, AP_E2E_PORT=38393): 37 PASS,
  0 FAIL (a real generated seed over a local server: "the shop slot's check pays the Spectacles",
  "and the 3 Lavenders bought are ours", the server's checked locations hold the slot).
  tools/ap_e2e.sh (ChecksFinder; fixed shared work dir) not run: its one shop regex follows the
  same log line.

## In progress
- Nothing: done and verified (2026-10-25). Not committed (rule: no commits).

## Next steps
- Coordinator: commit (docs first). (release notes: docs/release_notes/0.02.50.md) Region discs (FR / DE names
  are longer): the mark is clamped left of the price's first digit, so a very long name may run
  under it; not captured on those discs.

## How to verify
    cmake --build engine/build-shopboth -j8
    engine/build-shopboth/ss_tests; engine/build-shopboth/ss_game_tests    # ap_shop_slots
    XDG_CONFIG_HOME=<scratch> SDL_AUDIO_DRIVER=dummy SS_DISC1=<cue> \
      tools/smoke_field_events.sh engine/build-shopboth/shattered-stars   # ap_p4_shop, ap_shop_*
    scratchpad/shopboth/run.sh NAME ...   # the proofs above (inputs in this log's runs/*.log
                                          # commands: see the smoke's SHOP1 / SHOP2 / order case)
    cd scratchpad/ap/Archipelago && SKIP_REQUIREMENTS_UPDATE=1 ../ap-venv/bin/python -m pytest -q -n 8 worlds/so2/test
    cd scratchpad/apworld2/batch && ./run_batch.sh players ../../shopboth/batch/full $(seq 1 96)

## For shopboth (from skillsanity, 2026-10-25 16:20)
- The Skill Guilds are not shops: a field-script library (choice window, then a purchase that
  sets script flag 700 + set), no shop page or shop code; skillsanity's guild checks will hook
  the flag, not menu_shop.cpp.
- The user's second extension (docs/handoff/skillsanity.md, "sold in shops"): with
  shuffle_shops and skillsanity, a shop slot holding one of this world's skill / killer move /
  spell items would sell it as itself ("Perseverance" 400 Fol, "Claude: Shooting Stars" 700 Fol),
  no game item behind it. Nothing built yet (waiting for the user's calls); when it is, it needs
  a non-item entry in the shop list (name, price, the AP mark) and a settle path that gives no
  game item. I'll ask before touching menu_shop.cpp; tell me in my log where your settle /
  marker code lands.

## For you (from cavefix, 2026-10-25 ~18:00): shared AP files
- tools/ap_world_tables.py: new `LEARN_CHESTS` (after `CAVE_OF_TRIALS`) and a skip in build()'s
  chest loop (plus a check that all three are found): the Cave of Trials learn chests (maps 740 /
  798 / 799, flags 2675 / 2657 / 2721, "Cave of Trials Level 1: Portrait F / G / J chest") are no
  longer locations. apworld/so2/data.py regenerated from the tree as it was (your working
  changes included): exactly those three LOCATIONS rows gone, nothing else; it cmp's identically
  with a fresh `tools/ap_world_tables.py -o`. Regenerate after your own table edits as usual.
