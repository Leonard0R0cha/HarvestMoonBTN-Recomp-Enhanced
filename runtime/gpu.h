/* PS1 GPU: GP0/GP1 command processor with an exact software rasterizer over 1MB VRAM.
 * All commands complete instantly (the GPU is always "ready"). */
#pragma once
#include <stdint.h>
#include <stddef.h>

void     gpu_init(void);
void     gpu_write_gp0(uint32_t v);       /* write to 1F801810h */
void     gpu_write_gp1(uint32_t v);       /* write to 1F801814h */
uint32_t gpu_read_data(void);             /* read  from 1F801810h (GPUREAD) */
uint32_t gpu_read_status(void);           /* read  from 1F801814h (GPUSTAT) */

/* DMA channel 2 helpers (called by the DMA controller) */
void     gpu_dma_write(const uint32_t *src, int words);   /* block mode CPU->GPU (GP0) */
void     gpu_dma_read(uint32_t *dst, int words);          /* block mode GPU->CPU (GPUREAD) */

/* Timing hooks from the scheduler */
void     gpu_vblank(void);                /* called at start of vblank (toggles interlace field) */
void     gpu_set_line_parity(int odd);    /* GPUSTAT.31 for non-interlaced modes */

/* Display output */
typedef struct {
    int enabled;          /* display enabled (GP1(03h)) */
    int vram_x, vram_y;   /* display area start in VRAM (GP1(05h)) */
    int width, height;    /* visible resolution in pixels (from horizontal/vertical range + mode) */
    int is_24bit;
    int interlaced;
    int pal;
    int hres_mode;        /* raw GPUSTAT bits 16-18 */
} GpuDisplay;
void     gpu_get_display(GpuDisplay *d);
/* Convert the visible display area to 0xAARRGGBB pixels; out must hold 1024*512*n*n
 * entries (n = gpu_upscale()). Returns width/height actually written (row stride = *w). */
void     gpu_render_display(uint32_t *out, int *w, int *h);

/* Internal resolution: n = 2..4 also draws into an n-times larger copy of VRAM that
 * gpu_render_display shows; 1 = off (default). The native VRAM is unaffected either way. */
void     gpu_set_upscale(int n);
int      gpu_upscale(void);

/* PGXP (upscaled display only): sub-pixel positions of GTE-projected vertices. The recompiled
 * SWC2 of a screen XY register tags that RAM word with gpu_pgxp_store(); the GPU DMA sets
 * g_gp0_addr to the RAM address of each word it sends, and a polygon vertex whose word still
 * holds the tagged value is drawn at the precise position (and depth, for g_pgxp_persp). */
extern int      g_pgxp;           /* precise vertices (needs upscaling) */
extern int      g_pgxp_persp;     /* perspective-correct textures (needs g_pgxp) */
extern uint32_t g_gp0_addr;       /* RAM address of the GP0 word being written, ~0 = none */
void     gpu_pgxp_store(uint32_t addr, uint32_t value, int ok, float x, float y, float w);

/* PGXP for 2D sprites placed at projected points: gpu_sprite_anchor records the sub-pixel
 * offset (precise - whole) of a projected anchor; gpu_sprite_begin(x, y) selects the offset of
 * that anchor (g_sprite_on = found) for the following gpu_sprite_tag calls, which tag RAM words
 * of the sprite being built (or drop an old sprite tag when no anchor was found). A sprite
 * rectangle whose position word carries a matching tag (or a quad whose corners all do) is
 * drawn shifted by the offset in the upscaled copy (rigidly, so the pixel art stays crisp).
 * With g_sprite_depth it is also scaled by the anchor's depth scale s about the anchor, and with
 * g_sprite_ws != 1 (widescreen) squeezed horizontally about it, like the GTE squeezes the world. */
extern int g_sprite_tags;         /* anchors and sprite tags are recorded (pgxp or widescreen; upscaled) */
extern int g_sprite_on;
extern int g_sprite_depth;        /* scale tagged sprites with depth (needs g_pgxp) */
extern float g_sprite_ws;         /* horizontal squeeze of tagged sprites, 1 = none */
void     gpu_sprite_anchor(int x, int y, float dx, float dy, float s);
void     gpu_sprite_begin(int x, int y);
void     gpu_sprite_alias(int ax, int ay, int bx, int by);   /* point b uses anchor a (offset, scale, centre) */
void     gpu_sprite_tag(uint32_t addr, uint32_t value);
/* Widescreen HUD: the following gpu_sprite_tag calls (on = 1) tag the HUD, which is squeezed by
 * g_sprite_ws about the middle of the drawing area, or (on = 0) drop old tags. */
void     gpu_hud_begin(int on);
extern int g_hud_call;            /* the HUD icon routine is calling the sprite builder */

const uint16_t *gpu_vram(void);           /* 1024x512 16-bit VRAM (debug / screenshots) */

size_t   gpu_state_size(void);
void     gpu_save_state(void *dst);
void     gpu_load_state(const void *src);
