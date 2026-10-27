/* Frame interpolation (prototype): a second copy of the GPU, with state of its own (everything in
 * gpu.c is static), that redraws a recorded frame from the in-between camera on a worker thread
 * (gpu_replay_run2: the second worker's copy; see gpu_replay.c). Its public names are renamed so they don't clash with the real GPU. */
/* The worker writes these variables all the time: they get sections of their own, aligned to and
 * padded with whole cache lines, so they never share a cache line with the emulator's data (one
 * worker's sprite offsets had ended up next to the GTE registers). */
#pragma clang section bss=".bss$replay2" data=".data$replay2"
static char guard_head[64] __attribute__((aligned(64), used));
static char guard_data_head[64] __attribute__((aligned(64), used)) = { 1 };
#define GPU_REPLAY 1
#define GPU_REPLAY_RUN gpu_replay_run2
#define g_pgxp replay2_g_pgxp
#define g_pgxp_persp replay2_g_pgxp_persp
#define g_gp0_addr replay2_g_gp0_addr
#define gpu_pgxp_store replay2_gpu_pgxp_store
#define g_sprite_tags replay2_g_sprite_tags
#define g_sprite_on replay2_g_sprite_on
#define g_sprite_depth replay2_g_sprite_depth
#define g_hud_call replay2_g_hud_call
#define g_held_call replay2_g_held_call
#define g_item_call replay2_g_item_call
#define g_menu_tile replay2_g_menu_tile
#define g_sprite_ws replay2_g_sprite_ws
#define gpu_sprite_anchor replay2_gpu_sprite_anchor
#define gpu_sprite_begin replay2_gpu_sprite_begin
#define gpu_held_begin replay2_gpu_held_begin
#define gpu_display_menu replay2_gpu_display_menu
#define gpu_menu_begin replay2_gpu_menu_begin
#define gpu_hud_begin replay2_gpu_hud_begin
#define gpu_sprite_alias replay2_gpu_sprite_alias
#define gpu_sprite_tag replay2_gpu_sprite_tag
#define g_scene_3d replay2_g_scene_3d
#define g_interp replay2_g_interp
#define gpu_set_camera replay2_gpu_set_camera
#define gpu_interp_enable replay2_gpu_interp_enable
#define gpu_render_interp replay2_gpu_render_interp
#define gpu_interp_prepare replay2_gpu_interp_prepare
#define gpu_interp_work replay2_gpu_interp_work
#define gpu_interp_late replay2_gpu_interp_late
#define gpu_write_gp0 replay2_gpu_write_gp0
#define gpu_dma_write replay2_gpu_dma_write
#define gpu_read_data replay2_gpu_read_data
#define gpu_dma_read replay2_gpu_dma_read
#define gpu_write_gp1 replay2_gpu_write_gp1
#define gpu_read_status replay2_gpu_read_status
#define gpu_vblank replay2_gpu_vblank
#define gpu_set_line_parity replay2_gpu_set_line_parity
#define gpu_get_display replay2_gpu_get_display
#define gpu_render_display replay2_gpu_render_display
#define gpu_vram replay2_gpu_vram
#define gpu_init replay2_gpu_init
#define gpu_set_upscale replay2_gpu_set_upscale
#define gpu_upscale replay2_gpu_upscale
#define gpu_save_state replay2_gpu_save_state
#define gpu_load_state replay2_gpu_load_state
#define gpu_state_size replay2_gpu_state_size
#include "gpu.c"
static char guard_tail[64] __attribute__((aligned(64), used));
static char guard_data_tail[64] __attribute__((aligned(64), used)) = { 1 };
