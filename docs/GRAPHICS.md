# Graphics enhancements

What was added on top of the original-accurate renderer, how it works, and what was learned
about the game along the way. All of it is optional (see the `[video]` settings in the
[README](../README.md#configuration) or the launcher), and apart from widescreen none of it
changes what the game itself sees: with every other option on, RAM and VRAM stay
bit-identical to a plain run (checked at several frames of a recorded play session).

| Setting | What it does | Needs |
|---|---|---|
| `upscale = 2..4` | draws everything a second time at 2–4× resolution, for display | |
| `pgxp = 1` | 3D vertices at their exact sub-pixel position (no wobble), 2D objects placed exactly | upscale 2+ |
| `perspective = 1` | perspective-correct textures (no warping on large polygons) | pgxp |
| `sprite_depth = 1` | 2D objects get smaller further away, like the 3D ground | pgxp |
| `widescreen = 16:9` | wider 3D view (also `16:10`, `21:9`) | upscale 2+ for 2D objects |
| `scaling = sharp-bilinear` | sharp pixels that stay even at any window size | |

The launcher (`launcher.pyw` in the build folder, needs Python) edits all of these.

## Higher internal resolution: two renderers in one

The PlayStation GPU draws into 1 MB of VRAM, and the game reads that VRAM back (textures
live there, and some effects copy screen areas). So the original-resolution VRAM must stay
exact. The GPU therefore draws every command twice:

- into the native 1024×512 VRAM, exactly as before, and
- into an n-times larger copy that is only ever displayed.

Polygons are rasterized again at the higher resolution (that's what makes 3D edges smooth);
sprites, lines, fills, copies and uploads are mirrored as n×n blocks. Textures are always
read from the native VRAM, so they look as they did.

**Finding: black lines along polygon edges.** At high resolution a pixel can lie very close
to a polygon's edge, and sampling the texture at the pixel centre (+0.5, as the native
rasterizer does) overshoots into the next texel, which is often unused black. The upscaled
pass samples at the pixel's coverage point plus a tiny guard (2⁻¹¹ texel) instead.

**Sharp-bilinear scaling.** 320×240 (or the upscaled picture) almost never fits the screen
by a whole number, and plain nearest-pixel scaling then makes some pixels wider than others,
which shimmers when the picture scrolls. The picture is first enlarged by the largest whole
factor (nearest), then scaled the rest of the way with bilinear filtering: crisp, even
pixels at any size.

## PGXP: precise vertex positions

The GTE (the geometry coprocessor) rounds every projected vertex to a whole pixel, which is
what makes PS1 polygons wobble. The runtime keeps the unrounded result next to the GTE's
screen-coordinate FIFO. When the recompiled code stores a screen coordinate (`swc2`), the
precise value is recorded in a shadow copy of RAM for that word, together with the stored
value. When a polygon arrives at the GPU (the DMA tells the GPU the RAM address of every
word), each vertex whose RAM word still holds the recorded value is drawn at the precise
position, and with its depth for perspective-correct texturing. If the game changed the
word in between, the shadow value no longer matches and the vertex is drawn as usual.

## 2D objects on a 3D field

Stumps, rocks, weeds, flowers, villagers and the player are not 3D models. The game
projects each object's 3D position with the GTE and draws a flat sprite (a rectangle or a
few quads) at the resulting whole-pixel position.

**Finding: why field objects "hop" or warp.** With PGXP the 3D ground scrolls smoothly, but
the sprites standing on it still move in whole-pixel steps, so they drift against the ground
by up to a pixel and then jump back. The same happens in emulators (it was reproduced in
DuckStation with every option); it is how the game works, and at native resolution the
ground wobbles too, which hides it.

**Fix: sub-pixel anchors.** Hooks emitted by the recompiler (see
[TECHNICAL.md](TECHNICAL.md#the-recompiler-toolsrecomppy)):

| Address | Routine | Hook |
|---|---|---|
| 8001DFD4 | object projection | records each anchor: whole-pixel position → sub-pixel offset (and depth scale) |
| 80016280 | rectangle sprite builder | looks its anchor up on entry and tags every word it writes |
| 800165D8 | multi-part quad builder (villagers) | same |
| 80016C08 | single-quad builder (the player) | same |
| 800D93C8 | farm field object loop | big stumps and boulders are drawn at the anchor plus a y offset; that point is registered as an alias of the anchor (800D9590) |
| 80024A4C | HUD icons | deliberately not tagged |

A sprite rectangle whose position word carries a tag, or a quad whose corners all carry the
same tag, is drawn in the upscaled copy shifted by the anchor's offset, rigidly, by whole
upscaled pixels, so the pixel art stays crisp. Untagged sprites and tags that no longer
match are drawn as before.

**Finding: objects seem to grow when you walk away.** The sprites have one fixed size (a
big stump is a single 56×47 rectangle, the boulder 60×42), and the PS1 cannot scale
rectangles, but the ground under them is in perspective. Measured on the farm, a real 3D
object would be about 71% of its mid-screen size at the top of the view and 122% at the
bottom. Walking away from a stump moves it up the screen, where the ground shrinks but the
stump doesn't, so it looks bigger. With the wobbly original ground this was hard to see.

**Fix (optional): depth scaling.** With `sprite_depth` each tagged sprite is also scaled
about its anchor by

    s = depth where the projection centre's ray meets the anchor's horizontal plane
        ------------------------------------------------------------------------------
                                  depth of the anchor

computed from the GTE's rotation matrix (the plane normal in view space is row 1 of the
matrix's inverse, so a scaled matrix doesn't matter). Sprites at the centre of the view keep
their drawn size (so the player, whom the camera follows, doesn't change), and everything else
follows the ground. Checked against the game: for the ~150 ground objects of a farm frame,
depth × s came out at the same value (9866) for every one. Scaled sprites are sampled
nearest-texel, so they stay sharp; their pixels become slightly uneven. Objects above the
ground (something held overhead) are scaled for their own height, so they can come out a
few percent smaller than the player.

## Widescreen

Unlike the other options, widescreen changes what the game sees, because the game has to draw
more of the world. The GTE scales the screen X of every perspective transform by
(4/3) / (W/H) about the projection centre (3/4 for 16:9), so a wider view fits the 320 pixels
the game draws, and the frontend shows the picture stretched to W:H. The game culls and sorts
with the squeezed coordinates, so it draws what a wider screen would show.

- **2D objects keep their shape.** People, stumps and rocks are flat sprites of a fixed size
  at a projected point (see above): the squeeze moves their anchor but not their width, so
  stretched to 16:9 they would look a third too wide. In the upscaled copy every tagged
  sprite is squeezed by the same factor about its anchor (nearest texel, like depth scaling;
  at upscale 4 and 16:9 a texel is exactly 3 pixels wide). This needs upscale 2 or more but
  not PGXP: without PGXP the anchors are recorded with no sub-pixel offset.
- **Screens without 3D stay 4:3.** The title screen, menus and videos are 2D pictures. The
  picture is shown wide while the game projected at least 32 vertices in one of the last 10
  frames, and never for 24-bit video. "Any projection" is not enough: with the full-screen
  Start menu open the game still projects one vertex every other frame (against ~2,600 for
  the house), and the menu came out stretched.
- **Area changes stay wide.** Leaving an area, the game stops drawing 3D and fades out the
  last picture, then shows black or "Now Loading" (97% black). Switching to 4:3 as soon as the
  3D stopped squeezed the fading room for a moment. Once wide, the picture now stays wide
  until a picture without 3D that is mostly not black follows a black one (at least 90% of
  the pixels with red, green and blue at 0 or 1): the Start menu is less than 13% black from
  its first frame after the fade. In the new-game intro and the village tour the only
  switches left are for the Start menu, on its first frame and on the black frame after it.
- Text, menus and the HUD drawn over the 3D view are stretched.
- The PGXP positions and the depth scale use the squeezed coordinates (the ground scale
  undoes the squeeze to get the view ray).

**Finding: saw-tooth gaps at the sides.** With only the squeeze, black wedges appeared at the
left and right edges of the farm (small at 16:9, a saw-tooth down both sides at 21:9). The
model renderer (8001C0D4) skips each part of a model, such as a piece of the ground, unless
its centre (a single point) passes the test at 8001C1C0: the direction from the camera, against
four planes at 80061EA8. Those planes are rebuilt every frame (8001D348) by turning the camera
matrix by four angles at 800491E0. On the farm they are 2156 and −108 (180° + 9.5° and −9.5°,
a little wider than the 4:3 view's 9.1°) for the sides, and 93 and 1962 for top and bottom.
A hook on the two loads of the side angles (8001D45C, 8001D4A4) widens them so that their
tangent grows like the view (−108 → −143 for 16:9). The polygons' own screen test (8001D21C)
is loose (x −20…398) and works on squeezed coordinates, so it needs nothing.

What remains black at the sides is the edge of the map (past the town wall, beyond the
ground at the farm exit), which a 4:3 camera never shows; the black areas stay the same with
the side planes opened all the way. The game draws more, but it doesn't slow down: it flips
the same number of frames (30 per second) at 4:3, 16:9 and 21:9 on the farm and through the
village tour.

## Testing method

- `--record FILE` saves the pad input of a play session; `--input @FILE` replays it
  deterministically (`--headless --unthrottled --shots N --shotstart F --dumpram F` for
  screenshots and RAM/VRAM dumps). The recording starts with `fastcd=` because faster
  loading changes timing: an early wrong diagnosis came from replaying a recording made
  with `fastcd = 1` without it, so the replay went somewhere else entirely.
- Every option was checked for game-state identity (RAM + VRAM dumps compared with a plain
  run) and with unit tests in `runtime/tests/` (upscaled texel sampling, PGXP vertices,
  perspective, sprite shifting and depth scaling, the ground-scale formula, the widescreen
  projection, sprite squeeze and frustum angles). Widescreen changes the game state on
  purpose; with it off the game state is unchanged.
