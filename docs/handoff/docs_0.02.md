# Docs for 0.02.00: continuation log

Agent `docs-0.02` (2026-10-23, from 16:30). Task: README, wiki (scratchpad/wiki, committed
locally, never pushed) and docs/release_notes/0.02.00.md for 0.02.00 (European, French, German
releases; so2.apworld 0.1.1).

## Rules given

- Every claim from the sources: docs/notes/regions.md (decisions, section 10),
  docs/notes/regions_text.md, docs/handoff/regions.md ("0.02.00 readiness"),
  docs/handoff/regions_text.md, docs/handoff/apareas.md, docs/ap/apworld.md (changelog),
  docs/release_notes/0.01.00.md (format), README.md, the wiki pages. Never quote the Prima guide,
  never the user's email, no game assets.
- Write only README.md, docs/release_notes/0.02.00.md, wiki pages, this log. No code edits.
- Don't commit or push the main repo; commit the wiki locally (check `git config user.email` =
  314439719+DreamGlyphStudios@users.noreply.github.com first; never change git config; end the
  message with the Co-Authored-By line), don't push it.
- Release notes: SHA-256 placeholder; "TO CONFIRM" on every claim that waits on verification
  in the readiness section.
- Few shell commands; stop and report on a refusal. One "docs-0.02: " line in
  scratchpad/checkpoint_notes.txt when done. Save and end past ~300k context.

## State (16:45): done

- [x] README: What it is, What you need + "Supported releases" table (serials, 60 Hz NA
      logic, saves per release, mixed pairs refused, Japan recognised), one line each in Mods
      and Archipelago. (The 16:38 checkpoint commit 1bb10c0 already includes it.)
- [x] docs/release_notes/0.02.00.md: draft in the 0.01.00 format, DRAFT banner, SHA-256
      placeholder, 4 TO CONFIRM markers: FR/DE event sweeps + Disc 2 + battle comparison;
      French battle result / German Scan vs Redux; ap_e2e_so2 on France; the El-voyage lead
      (docs/handoff/apareas.md, unfixed design decision). First draft in 1bb10c0; later edits
      uncommitted.
- [x] Wiki (scratchpad/wiki, local commit 3dc5c04, NOT pushed): Home, _Footer,
      Installing-and-Setup, Playing, FAQ, Archipelago (0.1.1 section, regenerate notice),
      Modding (new "Mods and the releases"), Making-a-Mod, Submitting-a-Mod,
      Installing-and-Managing-Mods, How-the-Port-Was-Made (region profiles), plus 0.02.00 in
      the version examples of Building-from-Source and Contributing.

## Source inconsistencies found

- 0.1.1 has two fixes (ship landings, teleporter islands; docs/ap/apworld.md "Changes"); both
  documented.
- The El-voyage lead (apareas.md) is not fixed in 0.1.1 and not in apworld.md's known limits.
- Mod submissions: .github/PULL_REQUEST_TEMPLATE/mod_submission.md and mods/README.md still
  require the NA discs, and tools/mod_check.py compares with the NA tables only; the wiki keeps
  NA for submissions and explains. tools/so2_data.py's error text still says "use the North
  American or European discs".
- The AP option value is `open_world` (not "open").
- How-the-Port-Was-Made "What's left" still has stale bullets (mod manager "being built",
  .apworld release "to come"); not touched.

## Notes

- Serials (docs/notes/regions.md section 1, region.cpp): NA SCUS-94421 / 94422, Europe
  SCES-02159 / SCES-12159, France SCES-02160 / SCES-12160, Germany SCES-02161 / SCES-12161,
  Japan SLPM-86105 / SLPM-86106 (recognised, refused).
- Left for the release step: README version badge and the download file names (0.01.00).
