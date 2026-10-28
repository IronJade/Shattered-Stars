# GPU accuracy: the port, PCSX-Redux and the hardware

The port's software GPU (`port/src/psx/gpu.cpp`) is compared with PCSX-Redux captures
throughout the tests, but PCSX-Redux's software renderer is a reference, not the
hardware. Where the two disagreed, the hardware VRAM dumps that ship with JaCzekanski's
ps1-tests (`gpu/triangle`, `gpu/uv-interpolation`, `gpu/quad`, `gpu/transparency`,
`gpu/lines`; 15-bit VRAM captured from a console) decided. The tests were replayed on
`psx::Gpu` with a small harness (`scratchpad/fx_worker/ps1tests/harness.cpp`, not in
the repository) and compared pixel for pixel. Redux's sources read for this are
`src/gpu/soft/{polys.cc,pixel-writer.h,raster-state.h,soft.cc}` at the commit the
installed Redux was built from (f63cfc34, October 2026).

| Question | Hardware dumps | Port before | PCSX-Redux | Port now |
|---|---|---|---|---|
| Gouraud / UV interpolation | planes per triangle, 12-bit gradients, half-unit round | exact rational, truncated: 3244 / 18733 / 4185 pixels off on gpu/triangle, 15860 on the UV rows | edge walker with 16.16 span gradients: 310 / 1848 / 162, 276 off | identical (0) |
| Coverage (fill rule, quad seams) | | identical | | identical |
| Flat blending, the four modes | | identical (gpu/quad, gpu/transparency) | | identical |
| Order of dithering and blending | dither and saturate to 5 bits, then blend 5-bit values (gpu/lines: the transparent dithered gouraud polyline is `(B + Q) >> 1` of the opaque one's dithered value Q at every pixel; 8 channels are impossible if blending came first) | as the hardware | blends the 8-bit colour with the background expanded to 8 bits, then dithers | as the hardware (Redux's order under `--reference-dither`: `Gpu::blend_before_dither`) |
| Modulated colour in a blend | saturated (the blend is 5-bit) | saturated | not saturated: up to 62 enters B - F, B + F, (B + F) / 2 | saturated (unsaturated under `--reference-dither`: `Gpu::blend_unsaturated_modulation`) |
| Line walk and line dithering | gpu/lines (below) | DDA, nearest pixel; gouraud lines dithered | Bresenham octants; no line dithered | identical (0) |
| LoadImage from a misaligned address | (DMA: MADR bits 0-1 ignored) | took the address as given | aligns down | aligns down (`psyq::dma_source`) |

## The interpolation model

For each triangle (a quad is triangles 0-1-2 and 2-1-3), with the vertices sorted by y
and `det = (x1 - x0)(y2 - y1) - (x2 - x1)(y1 - y0)`:

- `dA/dx = ((A1 - A0)(y2 - y1) - (A2 - A1)(y1 - y0)) * 4096 / det` (truncated), and
  `dA/dy = ((x1 - x0)(A2 - A1) - (x2 - x1)(A1 - A0)) * 4096 / det`, both kept with 24
  fractional bits (`<< 12`) in 32-bit arithmetic;
- the plane starts at the leftmost vertex (chosen in the command's vertex order, the
  later vertex winning ties): `A(x, y) = ((A_left << 12) + 2048 << 12) + dA/dx (x -
  x_left) + dA/dy (y - y_left)`, and the pixel takes bits 24-31 (wrapping).

This is Mednafen's model as DuckStation carries it. PCSX-Redux's comments cite their own
hardware suites (gpu-raster-phase 7, 17-22) for a walker with a +0x8000 seed and
truncated per-row UV steps; it reproduces the hardware where the gradients divide
cleanly, but not in general.

## What it explains in this game

- The ±1 "gouraud" differences in `test_field_message` (window bodies): about 3% came
  from the interpolation, the rest from Redux's blend order. With Redux's order the
  frames would be identical to its captures; the hardware does not blend that way, so
  the test keeps its one-step tolerance there.
- The New Game portraits fading (modulated by up to 0x90, semi-transparent): Redux's
  unsaturated modulated colour gives its 1-2 LSB differences.
- The end screen's logo (#3105): its image is 10 bytes into its buffer, so the DMA
  starts at the halfword before and the logo lands one pixel right; with
  `psyq::dma_source` the end screen (caption, logo, "fin.") is identical to Redux.
- The mirror images on map 139 (#3106): the foot-shadow differences are gone; 3 pixels
  one step off remain where Redux's walker differs from the hardware model.
- Polygons: a quad is dropped whole when any of the five edges of its two triangles
  spans 1024 columns or 512 rows (Redux's notes, checked on three console models).
- Battle, semi-transparent dithered gouraud (phase 5): the result screen's last fade (the
  subtractive POLY_G4 band of battle_fade_task, abr 2, DR_MODE tpage 0x40), Precis's move
  9 sphere (additive lit POLY_G4), her move 3 vortex fading out and Opera's move 8 light
  sphere differed from Redux by up to 13% of the 4x4 blocks; the best-matching port frame
  seemed to drift (b-7 to b-5) because the fade's steps land differently. It is the order
  in the table above. `--reference-dither` now also sets `Gpu::blend_before_dither` (blend
  the 8-bit colour with the background expanded to 8 bits, then the same dither table)
  and `Gpu::blend_unsaturated_modulation`; with them all of these match Redux (no frame
  above 1%: prec_b1 / b10, the Dias result fades). Redux's own Dither16 rounding after the
  blend matched worse than the hardware table. The game keeps the hardware's behaviour.
- Captures: `Gpu::capture_shift` (also set by `--reference-dither`) writes 5-bit colour as
  `v << 3`, as Redux's raw captures are; with bit replication (`v << 3 | v >> 2`, the
  default) an 8-bit comparison sees the port about 2.5% "brighter" (prec_b1 b600 compared
  as 5-bit values: 72930 equal green values, 725 one step up, 475 down). framecmp's `// 8`
  comparison was never affected.
- Dias's tinted sword-wave copies (olive in Redux, brownish in the port; code 0x2e, tpage
  0x0f, mode 0): not the GPU. They are afterimage copies, and the port draws each copy once
  (original_bugs.md B2 fixed) where the original draws half of them twice and never the
  others, so fewer overlap. A build with the original copy order and the reference
  switches matches Redux there (dias_l4 b390-410 and b740-780: no frame above 1%).

## Lines

`gpu/lines` draws horizontal, vertical and diagonal lines of every slope, mono and
gouraud lines with dithering off and on, opaque and semi-transparent polylines and a
16-segment circle. The port's earlier DDA (rounded to the nearest pixel, colour
interpolated per step, only gouraud lines dithered) was 50 pixels off in the steep lines
(the whole line one column off from its middle on), 1 LSB off in 129 / 111 gouraud and
528 dithered mono pixels, and 48 in the polylines. `Gpu::draw_segment` now walks lines as
Mednafen and DuckStation do, and the whole 1024x512 dump is identical:

- k = the longer axis' length, k + 1 pixels, from the left end (the ends swap when the
  line runs leftwards);
- x and y in 32.32 from the pixel centre, x biased back by 1024 / 2^32 (and y too when it
  steps up), the per-step increment `(delta << 32 ± (k - 1)) / k` rounded away from zero;
- colours in 12-bit fixed point from the centre of the start value, `(c1 - c0) << 12 / k`;
- every line is dithered when dithering is on, mono or gouraud (psx-spx says so too);
  each polyline segment is drawn whole, so a semi-transparent polyline blends its shared
  vertices twice.

PCSX-Redux uses Bresenham octants (with a hardware-checked bias for shallow lines) and
dithers no line at all; `Gpu::dither_lines = false` (set by `--reference-dither` and
`test_field_message`) keeps the reference comparisons on its side.

One input of the test is not defined: `gouraudMultiLine` sets its third colour twice and
never the fourth, which is then stack garbage on the console. Fitting the last segment
gives 0x6400d2 (r 210, g 0, b 100) exactly; with it the dump is identical, with 0 instead
only those 32 + 32 pixels of the two last segments differ.

Replayed: `scratchpad/fx_worker/ps1tests/harness.cpp` (`./harness lines out.bin` with
`C3=0x6400d2`, then `cmp.py`; the dump is `gpu/lines/vram.png` of the ps1-tests
repository). Probes from it are in `port/tests/test_gpu.cpp`
(`gpu_lines_match_hardware_dump`). The transparency dump is white outside its 320x240
screen (the capture's state, not drawing); inside it is identical.
