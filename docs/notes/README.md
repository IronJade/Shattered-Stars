# Analysis notes

Prose companions to `docs/catalog`: how each program is put together, written while
reversing it. The catalog CSVs hold the names; these notes hold the structure, the
data layouts and the reasoning, so none of it has to be worked out twice. Paths under
`analysis/` they mention are local working files (git-ignored, regenerated from the
user's discs); the facts that matter are recorded here or in the catalog.

| File | Covers |
|---|---|
| `field_part1.md` | Field program 0x8002f810-0x80040c2c: shared library code, the Actor base class, command ring, animation, tint |
| `field_part2.md` | 0x80040c2c-0x80053e8c: ActorSystem, the field object, main loop, `field_update` states, subsystems, globals |
| `field_part3.md` | 0x80053e8c-0x80064f30: map loading, preloads, task list, message system, the map scene / cell grid formats |
| `field_part4.md` | 0x80064f30-0x8007c00c: event-script VM, opcode tables, battle load list, voice queue |
| `field_overlay_c1d.md` | Code overlay 0xc1d (#3101) at 0x8007e000: BG renderer, field actors, walking and collision, camera pan queue, effects |
| `battle.md` | Battle program #2129: entry and main loop, field↔battle handoff, classes, data formats, subsystems, port order |
| `battle_port.md` | The battle port itself: file layout, deliberate deviations, how it was checked against PCSX-Redux, what is missing |
| `battle_image_map.md` | Which parts of the #2129 image above 0x800715d0 are live code and which are stale copies |
| `encounter_maps.md` | Field maps with random encounters (rate, group, stage), from a sweep of every map |
