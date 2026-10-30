/* Frame interpolation (prototype): a second copy of the GPU, with state of its own (everything in
 * gpu.c is static), that redraws a recorded frame from the in-between camera on a worker thread
 * (gpu_replay_run; gpu_replay2.c and gpu_replay3.c are the other workers'). Its public names are renamed so they don't clash with the real GPU. */
/* The worker writes these variables all the time: they get sections of their own, aligned to and
 * padded with whole cache lines, so they never share a cache line with the emulator's data (one
 * worker's sprite offsets had ended up next to the GTE registers). */
#pragma clang section bss=".bss$replay1" data=".data$replay1"
static char guard_head[64] __attribute__((aligned(64), used));
static char guard_data_head[64] __attribute__((aligned(64), used)) = { 1 };
#define GPU_REPLAY 1
#define GPU_REPLAY_RUN gpu_replay_run
#define g_pgxp replay_g_pgxp
#define g_pgxp_persp replay_g_pgxp_persp
#define g_pgxp_cracks replay_g_pgxp_cracks
#define g_gp0_addr replay_g_gp0_addr
#define gpu_pgxp_store replay_gpu_pgxp_store
#define g_sprite_tags replay_g_sprite_tags
#define g_sprite_on replay_g_sprite_on
#define g_sprite_depth replay_g_sprite_depth
#define g_hud_call replay_g_hud_call
#define g_held_call replay_g_held_call
#define g_item_call replay_g_item_call
#define g_menu_tile replay_g_menu_tile
#define g_sprite_ws replay_g_sprite_ws
#define gpu_sprite_anchor replay_gpu_sprite_anchor
#define gpu_sprite_begin replay_gpu_sprite_begin
#define gpu_held_begin replay_gpu_held_begin
#define gpu_display_menu replay_gpu_display_menu
#define gpu_menu_begin replay_gpu_menu_begin
#define gpu_hud_begin replay_gpu_hud_begin
#define gpu_sprite_alias replay_gpu_sprite_alias
#define gpu_sprite_tag replay_gpu_sprite_tag
#define g_scene_3d replay_g_scene_3d
#define g_interp replay_g_interp
#define gpu_set_camera replay_gpu_set_camera
#define gpu_interp_enable replay_gpu_interp_enable
#define gpu_render_interp replay_gpu_render_interp
#define gpu_interp_prepare replay_gpu_interp_prepare
#define gpu_interp_work replay_gpu_interp_work
#define gpu_interp_late replay_gpu_interp_late
#define gpu_write_gp0 replay_gpu_write_gp0
#define gpu_dma_write replay_gpu_dma_write
#define gpu_read_data replay_gpu_read_data
#define gpu_dma_read replay_gpu_dma_read
#define gpu_write_gp1 replay_gpu_write_gp1
#define gpu_read_status replay_gpu_read_status
#define gpu_vblank replay_gpu_vblank
#define gpu_set_line_parity replay_gpu_set_line_parity
#define gpu_get_display replay_gpu_get_display
#define gpu_render_display replay_gpu_render_display
#define gpu_vram replay_gpu_vram
#define gpu_changes replay_gpu_changes
#define gpu_init replay_gpu_init
#define gpu_set_upscale replay_gpu_set_upscale
#define gpu_upscale replay_gpu_upscale
#define gpu_save_state replay_gpu_save_state
#define gpu_load_state replay_gpu_load_state
#define gpu_state_size replay_gpu_state_size
#include "gpu.c"
static char guard_tail[64] __attribute__((aligned(64), used));
static char guard_data_tail[64] __attribute__((aligned(64), used)) = { 1 };
