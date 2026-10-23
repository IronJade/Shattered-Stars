# Handoff: the public README and wiki screenshots (agent "readme")

## Rules this agent was given (all mandatory)

- Write `README.md` at the repo root. Don't commit or push the main repo (the coordinator
  commits; pushing waits for the user). Don't edit code. Besides README.md, write only this
  log and ONE line in `scratchpad/checkpoint_notes.txt` prefixed "readme: ".
- Every claim backed by the repo. Never quote or paraphrase the Prima strategy guide; never
  invent a feature. Mod-manager parts not yet built are described as "coming" or left out.
- Never put the user's email address anywhere. No game code or assets; only the existing
  `docs/images` screenshots.
- Wiki: work in `scratchpad/wiki`; before each commit `git config user.email` must print
  `314439719+DreamGlyphStudios@users.noreply.github.com` (never change git config). Commit
  messages end with "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>".
  Push to the wiki's origin (authorised); never force-push. Images are linked by raw URL
  (`https://raw.githubusercontent.com/IronJade/Shattered-Stars/main/docs/images/<file>.png`),
  never added to the wiki repo.
- Check every link: wiki page names exist in `scratchpad/wiki`, relative paths exist.
- Cap command output; past ~400k tokens update this log and end the turn.

## State

(newest first)

- DONE. `README.md` written at the repo root (not committed): title, badges, hero
  (title-screen), what it is, highlights, a 3x3 gallery (field-arlia, world-map, battle-spell,
  boss-battle, camp-status, shop-equipment-wizard, late-game-fienal, ap-item-received,
  setup-discs), getting started (release not yet published), how it was made (Mermaid
  pipeline, the four steps, numbers), bugs fixed / quirks kept (rule, 83-case counts, 9
  examples), mods and the mod manager (guard, packages, setup's Mods page; curated-list checks
  and verification "coming"), Archipelago (setup-archipelago image), building, layout,
  contributing, legal, licence and acknowledgements. Every relative link and wiki link checked.
- Wiki (pushed, e6ad556): images on Home (title), Installing-and-Setup (setup-discs,
  setup-archipelago, title), Playing (2x2: field, world map, battle, camp), Archipelago
  (setup-archipelago, ap-item-received). Also B18-B20 rows and the 83 counts on the Bugs page,
  and the catalog's 5,452 confirmed / 0 likely on How-the-Port-Was-Made.
- Sources read: wiki pages (all), docs/HANDOFF.md, engine/README.md,
  docs/notes/original_bugs.md, docs/catalog/COVERAGE.md + functions.csv, tools/refs/README.md
  + refs.csv, docs/handoff/tools.md, docs/ap/apworld.md, apworld/so2/docs, docs/notes/mods.md,
  docs/handoff/modmanager.md, tools/third_party_licenses.sh, docs/images/README.md,
  docs/ap/release_checklist.md, docs/notes/m8_audit.md, docs/notes/gpu_accuracy.md.

## Facts used (and where from)

- 83 original-bug entries in the working tree (B18-B20 added after the wiki was written):
  43 fixed (E10 counts as fixed with E8), 20 avoided, 19 kept or held (8 intended, 3 gameplay,
  6 held, 2 not fixed yet), 1 not a bug. Source: docs/notes/original_bugs.md status column.
- Catalog: 5,452 functions, 149 modules, all confirmed; 4,463 ported, 901 not needed, 88 PsyQ
  (COVERAGE.md, working tree). The brief's "5446" is out of date (leads agent confirmed the
  last 6).
- Reference suite: 339 refs, 223 automated, last full run 219 pass / 3 regress / 1 review;
  quick set 28 (docs/handoff/tools.md, HANDOFF.md).
- Event sweep 827 maps / 5,326 entries (m8_audit.md); 564 encounters; 85 endings.
- Releases not published (release_checklist.md section 4).
- Mod manager (modmanager.md, 10:25): packages + mod.json, load order, conflicts, .zip
  install/remove, setup's rebuilt Mods page are implemented but not yet through the full
  suites; mod_check.py / mod_verify.py / index.json / mods/README.md / .github are not built
  (mod_check.py appeared during this pass). README calls the curated-list checks "coming".

## Open items for the coordinator

- When the modmgr agent finishes, the README's "Mods" section (the "coming" lines) and the
  wiki's Modding and Installing-and-Setup (Mods page) sections need updating.
- When releases are published, the README's "Download" note and the release badge.
