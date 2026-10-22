# GPU accuracy: the port, PCSX-Redux and the hardware

The port's software GPU (`engine/src/psx/gpu.cpp`) is compared with PCSX-Redux captures
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
| Order of dithering and blending | dither and saturate to 5 bits, then blend 5-bit values (gpu/lines: the transparent dithered gouraud polyline is `(B + Q) >> 1` of the opaque one's dithered value Q at every pixel; 8 channels are impossible if blending came first) | as the hardware | blends the 8-bit colour with the background expanded to 8 bits, then dithers | as the hardware |
| Modulated colour in a blend | saturated (the blend is 5-bit) | saturated | not saturated: up to 62 enters B - F, B + F, (B + F) / 2 | saturated |
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
- Not covered: lines (the port's DDA against the hardware's Bresenham variants; gpu/lines
  was used only for the blend order).
