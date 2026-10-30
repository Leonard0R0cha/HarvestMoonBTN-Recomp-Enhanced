/* Upscaled polygons in bands: a copy of the GPU, with state of its own (everything in gpu.c is
 * static), that draws band 4 of the queued polygons on a worker thread (band4_run; see
 * gpu_band1.c .. gpu_band4.c and "Upscaled polygons in bands" in gpu.c). Its public names are
 * renamed so they don't clash with the real GPU; its data gets sections of its own, as in
 * gpu_replay.c. */
#pragma clang section bss=".bss$band4" data=".data$band4"
static char guard_head[64] __attribute__((aligned(64), used));
static char guard_data_head[64] __attribute__((aligned(64), used)) = { 1 };
#define GPU_BAND 1
#define GPU_BAND_RUN band4_run
#define g_pgxp band4_g_pgxp
#define g_pgxp_persp band4_g_pgxp_persp
#define g_pgxp_cracks band4_g_pgxp_cracks
#define g_gp0_addr band4_g_gp0_addr
#define gpu_pgxp_store band4_gpu_pgxp_store
#define g_sprite_tags band4_g_sprite_tags
#define g_sprite_on band4_g_sprite_on
#define g_sprite_depth band4_g_sprite_depth
#define g_hud_call band4_g_hud_call
#define g_held_call band4_g_held_call
#define g_item_call band4_g_item_call
#define g_menu_tile band4_g_menu_tile
#define g_sprite_ws band4_g_sprite_ws
#define gpu_sprite_anchor band4_gpu_sprite_anchor
#define gpu_sprite_begin band4_gpu_sprite_begin
#define gpu_held_begin band4_gpu_held_begin
#define gpu_display_menu band4_gpu_display_menu
#define gpu_menu_begin band4_gpu_menu_begin
#define gpu_hud_begin band4_gpu_hud_begin
#define gpu_sprite_alias band4_gpu_sprite_alias
#define gpu_sprite_tag band4_gpu_sprite_tag
#define g_scene_3d band4_g_scene_3d
#define g_interp band4_g_interp
#define gpu_set_camera band4_gpu_set_camera
#define gpu_interp_enable band4_gpu_interp_enable
#define gpu_render_interp band4_gpu_render_interp
#define gpu_interp_prepare band4_gpu_interp_prepare
#define gpu_interp_work band4_gpu_interp_work
#define gpu_interp_late band4_gpu_interp_late
#define gpu_write_gp0 band4_gpu_write_gp0
#define gpu_dma_write band4_gpu_dma_write
#define gpu_read_data band4_gpu_read_data
#define gpu_dma_read band4_gpu_dma_read
#define gpu_write_gp1 band4_gpu_write_gp1
#define gpu_read_status band4_gpu_read_status
#define gpu_vblank band4_gpu_vblank
#define gpu_set_line_parity band4_gpu_set_line_parity
#define gpu_get_display band4_gpu_get_display
#define gpu_render_display band4_gpu_render_display
#define gpu_vram band4_gpu_vram
#define gpu_changes band4_gpu_changes
#define gpu_init band4_gpu_init
#define gpu_set_upscale band4_gpu_set_upscale
#define gpu_upscale band4_gpu_upscale
#define gpu_save_state band4_gpu_save_state
#define gpu_load_state band4_gpu_load_state
#define gpu_state_size band4_gpu_state_size
#include "gpu.c"
static char guard_tail[64] __attribute__((aligned(64), used));
static char guard_data_tail[64] __attribute__((aligned(64), used)) = { 1 };
