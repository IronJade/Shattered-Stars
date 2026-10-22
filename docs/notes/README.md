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
| `field_overlay_c1e.md` | Code overlay 0xc1e (#3102) at 0x8007e000: the world map (torus of terrain blocks drawn as a planet, block format, section 3, camera, walking, lighting regions) |
| `menu.md` | The menu framework the title and field share (scene, session, pages and overlays, widgets, input), the field's menus (opening, state 3, launcher and runners, camp page 1), the save / load page and the save file format |
| `field_effects.md` | Field visual effects and the SceneRenderer: OT layers and splicing, the camera block matrices, how the effects were checked, quirks kept |
| `credits.md` | The credits program #3107 (A 0x2d, map 581): image layout, cue list, frames decoded to RAM, the vblank handler that shows them, the name cards and their GTE blend |
| `gpu_accuracy.md` | The software GPU against PCSX-Redux and hardware VRAM dumps: interpolation model, dither / blend order, DMA alignment |
| `battle.md` | Battle program #2129: entry and main loop, field↔battle handoff, classes, data formats, subsystems, port order |
| `battle_port.md` | The battle port itself: file layout, deliberate deviations, how it was checked against PCSX-Redux, what is missing |
| `battle_image_map.md` | Which parts of the #2129 image above 0x800715d0 are live code and which are stale copies |
| `battle_enemies.md` | The monster classes: the 16 resident families, the 40 encounter-pack code blocks and the image's default modules, how the port picks a class, Redux checks, quirks kept |
| `encounter_maps.md` | Field maps with random encounters (rate, group, stage), from a sweep of every map |
| `original_bugs.md` | The original's bugs: the port comments them out and corrects them (each listed with its status), and does not model the original's slowdown |
