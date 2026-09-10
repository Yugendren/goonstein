# Three candidate art styles

Three complete looks for the same game, each a handful of `look` lines. Any level picks one with a
single line:

    look include camcorder      # or flat, or ink

which pulls in `assets/looks/NAME.txt` and applies its lines where the include sits — so a level's
own lines before the include are overridden by it, and anything after the include wins over it.
The files are plain level look lines, so pasting their contents into a level does exactly the same
thing. `assets/levels/island.txt` deliberately includes none of them: the plain look is the default
and stays the default until this is decided.

The full grammar for every line used here is in `assets/levels/README.md`, section **Look**.

| | A camcorder | B flat | C ink |
|---|---|---|---|
| Family | Lethal Company, found footage | Peak, Roblox | the drawn look, DESIGN.md pillar 4 |
| Internal resolution | **640x400**, nearest upscale | 1280x800 | 1280x800 |
| Textures | capped at 256 px | full, but 85% flattened | full, 55% flattened |
| Bloom | off (passes skipped) | off (passes skipped) | off (passes skipped) |
| Outlines | none | 3 px depth ink | 2 px depth + luminance ink, boiling at 8 Hz |
| Colour | 32 levels, ordered dither | saturated, hard toon bands | desaturated to 45%, warm light / cool shade |
| Extras | chroma bleed, vignette, a trace of grain | soft sun shadow | crossed hatching in shadow, paper texture |
| Fog | heavy dusk (full by 60 m) | thin and pale | fades to paper white |

## What each one is

**A — Camcorder 2006.** The frame is rendered at 640x400 and blown up with nearest neighbour, so
one screen pixel in four is shaded. Everything else exists to make that read as a choice: a lens
that bleeds red and blue apart at the corners, a 32-level colour crunch with an ordered dither
under it so the dusk sky does not band, a vignette, and enough fog that the far plane never has to
prove itself. Grain is a separate knob (`look grain`) set to 0.012 — barely present — and setting
it to 0 changes nothing else.

**B — Clean flat colour.** Full resolution, hard toon bands with a bright shadow floor, thick black
ink on the depth edges, saturated grade. The lever that does the most work is `look flat 0.85`,
which blends every material 85% of the way toward its own texture's mean colour: the photoscans
still decide what colour a surface is but stop contributing detail, so a Poly Haven scan and a
hand-made box finally look like they came from the same game.

**C — Sketch ink.** Full resolution. Lines come from two sources: the depth buffer (silhouettes)
and a step in screen luminance (interior creases the depth buffer cannot see — a folded arm, a
window in a wall, the seam where two toon bands meet). The point those edges are sampled from
wanders on an **8 Hz step**, so lines redraw themselves at a drawing's pace instead of shimmering
at the monitor's. Shadow is crossed hatching, not darkness: a first ruling comes in below 40%
luminance and a second, denser one below 20%, which is how a drawing gets darker — more lines, not
darker lines. A tiling paper texture (`tools/make_paper.py` → `assets/textures/paper.png`) is
multiplied over the finished frame; if that file is missing the layer is a silent no-op. The
palette is two-note by construction: desaturated to 45%, highlights gained warm, shadows lifted
cool, so what survives is ochre light and blue shade.

## What `look flat` actually does to a character

Worth knowing before picking B or C. `look flat` blends each material toward the mean colour of
**the texture bound for that draw**, and every one of our humanoids is a single texture atlas. So
at `flat 1.0` a goon is one solid orange silhouette: cap, vest, boots and face all average to the
same colour, because they are all the same texture. B therefore runs at 0.60 and C at 0.70 rather
than 1.0 -- far enough that the world stops being photographs, not so far that a face stops being a
face. A per-part or per-submesh mean would fix it properly; a blurred mip of each atlas would fix
it cheaply. Neither exists yet, and neither is needed to decide which of these three to build.

## Measured cost

M4, `HOLLOW_NOVSYNC=1 HOLLOW_FIXED_DT=1`, the island at dusk, 900 frames per run, three runs per
row, median frame time from the `perftime:` line (the first 60 frames are discarded). `plain` is
`assets/levels/island.txt` as committed.

| look | pier (ms) | summit (ms) |
|---|---|---|
| plain     | 8.57  8.54  8.56 | 8.11  8.10  8.26 |
| camcorder | 8.52  8.52  8.51 | 8.41  8.22  8.45 |
| flat      | 8.60  8.53  8.54 | 8.09  8.09  8.08 |
| ink       | 8.54  8.54  8.59 | 8.23  8.59  8.63 |

**Read this table as "no result", not as "they cost the same".** Every row is 8.1 to 8.6 ms, which
is this display's 120 Hz refresh, and the runs repeat to within 0.05 ms. Two control probes at the
summit, 700 frames each, settle it:

| probe | median |
|---|---|
| plain                                        | 8.18 ms |
| `HOLLOW_NOSHADOW=1` (the entire sun shadow pass deleted) | 8.35 ms |
| `look res 0.25` + `look texcap 128` (a sixteenth of the pixels) | 8.49 ms |

Throwing away a whole scene pass does not make it faster. Rendering 320x200 instead of 1280x800
does not make it faster. `HOLLOW_NOVSYNC=1` is not getting through to the swapchain on this
machine, so the loop is pinned to the display and the GPU is idling behind it. **The island at
dusk does not stress an M4 at all, and no frame-time number taken on this machine can rank these
three looks.** The ranking has to come from counting what each one asks the hardware to do.

### What this costs on a weak GPU

Count the work instead. Every look draws the same geometry and the same 2048x2048 sun shadow map;
that part is vertex and draw-call bound (211 draws looking out to sea from the pier, 4830 looking
inland, 324 at the summit) and is identical across all four rows. The differences are entirely in
fragments: how many the world pass shades, and how many texture fetches the post pass does per one.

At 1280x800 the world pass shades **1,024,000** fragments. At `look res 0.5` it shades **256,000**.
That is the only structural difference between these three, and it applies to the depth pass, the
sky, the particles and every post pass as well as the world pass.

Post-pass fetches per pixel, and the total per frame that implies:

| look | fetches/px | pixels | fetches/frame | bloom passes |
|---|---|---|---|---|
| plain     | 2 (hdr, bloom)                          | 1.02M | 2.0M  | 5 |
| camcorder | 3 (hdr x3 for the chroma split)         | 0.26M | 0.8M  | 0 |
| flat      | 5 (hdr, 4 depth taps for the ink)       | 1.02M | 5.1M  | 0 |
| ink       | 10 (hdr, 4 depth, 4 hdr luma, 1 paper)  | 1.02M | 10.2M | 0 |

The bloom column is five full-screen passes at quarter internal resolution (one bright extract,
two blur ping-pongs); all three candidates set `bloom 0` and the engine now skips them outright.

**A is the only one that makes the game cheaper, and its advantage is structural.** A quarter of
the fragments, a quarter of the post-pass cost on top of already having the fewest fetches, no
bloom, and `look texcap 256` cutting texture memory and sampler bandwidth -- which is what matters
most on the shared-memory integrated parts where the GPU and the CPU fight over one bus. On a
machine that is fill-bound, A should land somewhere between 2x and 3x the frame rate of the plain
look, and its floor is the geometry, not the pixels.

**B costs a small constant.** Four extra depth fetches per pixel at full resolution is ~3M extra
reads a frame; on anything modern that is free, on a Steam Deck it is small, on an old integrated
part it is the difference between 60 and roughly 50. Dropping bloom gives most of it back.

**C is the expensive one.** Ten fetches per pixel is five times the plain look's post pass, and
four of those are of the HDR target rather than depth, so they are 16-bit-per-channel reads. Call
it 8M extra reads a frame. Still not catastrophic -- it is one pass, not a G-buffer -- but it is
the only candidate whose post pass is a real line item, and it is the one whose fog is thinnest, so
it also draws the most distant geometry.

**The honest caveat on `look flat`:** it does not save anything. The textures are still loaded,
still bound and still sampled; the mix toward their mean colour happens after the sample. The VRAM
win you would expect from "no textures" is not realised. Getting it would mean not binding them at
all, which is a different change.

**What follows for the decision.** If the target is genuinely bad hardware, A is not a stylistic
preference, it is a 4x fill-rate budget with a look attached. B and C can be given the same budget
only by adding `look res 0.5` to them too -- at which point B's 3 px ink is 3 px on a 640x400 frame
(so proportionally twice as bold) and C's 2 px boiling lines are close to unreadable. That
combination is worth a second contact sheet before anyone commits to B or C.

## Turning one off

Nothing here is sticky. `look include` only writes into the level's `Look`, and every value it
touches has a documented default in `assets/levels/README.md`. A level with no include is the plain
look, which is what island.txt still is.
