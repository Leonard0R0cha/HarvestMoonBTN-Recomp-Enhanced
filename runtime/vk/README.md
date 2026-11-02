# Vulkan renderer (experimental, `renderer = vulkan` / `--renderer vulkan`)

parallel-psx, a Vulkan PS1 GPU renderer, draws the upscaled picture. gpu.c keeps drawing the
console's native VRAM (exact, for what the game and the port read back) and hands parallel-psx
every command it runs (`vk_renderer.h`). Windows only (compiled in by tools/compile.py); Vulkan is
loaded at runtime, and without it the game falls back to the CPU renderer.

Supported: upscale 1–4, pgxp and perspective-correct textures, widescreen with the HUD kept 4:3,
sprite anchors and depth, texture packs, in-between pictures (interp), the crack fix (background and
seams), the co-op second window.

## Files
- `vk_renderer.{h,cpp}`: the glue (written for this port, MIT like the rest of the repo).
- `seams.comp`: gpu.c's seam pass as a compute shader (this port).
- `compat/`: the few libretro names the renderer uses (this port).
- `build_shaders.py`: rebuilds the shaders this port adds (`seams.comp.inc`,
  `ppsx/glsl/prebuilt/textured.tag*.inc`) with glslang (`deps/glslang-*/bin/glslang.exe`, from
  github.com/KhronosGroup/glslang releases). Only needed after changing a shader.
- `ppsx/`: parallel-psx from libretro/beetle-psx-libretro at the commit before 90f5ee54e (its
  `parallel-psx/` directory only; nothing from beetle's GPL parts). Changes for this port are
  marked `HMBTN` (renderer.hpp/.cpp: column scissors, texture overrides, tag shaders;
  glsl/primitive.frag: the `TAG` variant).

## Licences
| Part | Licence |
|---|---|
| `ppsx/renderer`, `ppsx/glsl` | ISC (`ppsx/COPYING`), file headers MIT (Hans-Kristian Arntzen, parallel-psx contributors) |
| `ppsx/volk` | MIT (in `volk.h`) |
| `ppsx/SPIRV-Cross` | Apache-2.0 (`LICENSE-Apache-2.0.txt`) |
| `ppsx/khronos` (Vulkan headers) | Apache-2.0 (`LICENSE-Apache-2.0.txt`) |
| `ppsx/stb` | public domain or MIT (in each file) |

## Test switches
`HMBTN_VK_STATS=1` logs command counts every 600 VBlanks; `HMBTN_VK_CPU_OFF=1` skips gpu.c's own
drawing (reads come back from the GPU); `HMBTN_RENDER_ALL=1` makes headless runs build every picture.
