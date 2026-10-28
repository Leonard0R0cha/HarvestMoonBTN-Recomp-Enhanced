/* Frame interpolation (prototype): a second copy of the GPU, with state of its own (everything in
 * gpu.c is static), that redraws a recorded frame from the in-between camera on a worker thread
 * (gpu_replay_run3: the third worker's copy; see gpu_replay.c). Its public names are renamed so they don't clash with the real GPU. */
/* The worker writes these variables all the time: they get sections of their own, aligned to and
 * padded with whole cache lines, so they never share a cache line with the emulator's data (one
 * worker's sprite offsets had ended up next to the GTE registers). */
#pragma clang section bss=".bss$replay3" data=".data$replay3"
static char guard_head[64] __attribute__((aligned(64), used));
static char guard_data_head[64] __attribute__((aligned(64), used)) = { 1 };
#define GPU_REPLAY 1
#define GPU_REPLAY_RUN gpu_replay_run3
#define g_pgxp replay3_g_pgxp
#define g_pgxp_persp replay3_g_pgxp_persp
#define g_gp0_addr replay3_g_gp0_addr
#define gpu_pgxp_store replay3_gpu_pgxp_store
#define g_sprite_tags replay3_g_sprite_tags
#define g_sprite_on replay3_g_sprite_on
#define g_sprite_depth replay3_g_sprite_depth
#define g_hud_call replay3_g_hud_call
#define g_held_call replay3_g_held_call
#define g_item_call replay3_g_item_call
#define g_menu_tile replay3_g_menu_tile
#define g_sprite_ws replay3_g_sprite_ws
#define gpu_sprite_anchor replay3_gpu_sprite_anchor
#define gpu_sprite_begin replay3_gpu_sprite_begin
#define gpu_held_begin replay3_gpu_held_begin
#define gpu_display_menu replay3_gpu_display_menu
#define gpu_menu_begin replay3_gpu_menu_begin
#define gpu_hud_begin replay3_gpu_hud_begin
#define gpu_sprite_alias replay3_gpu_sprite_alias
#define gpu_sprite_tag replay3_gpu_sprite_tag
#define g_scene_3d replay3_g_scene_3d
#define g_interp replay3_g_interp
#define gpu_set_camera replay3_gpu_set_camera
#define gpu_interp_enable replay3_gpu_interp_enable
#define gpu_render_interp replay3_gpu_render_interp
#define gpu_interp_prepare replay3_gpu_interp_prepare
#define gpu_interp_work replay3_gpu_interp_work
#define gpu_interp_late replay3_gpu_interp_late
#define gpu_write_gp0 replay3_gpu_write_gp0
#define gpu_dma_write replay3_gpu_dma_write
#define gpu_read_data replay3_gpu_read_data
#define gpu_dma_read replay3_gpu_dma_read
#define gpu_write_gp1 replay3_gpu_write_gp1
#define gpu_read_status replay3_gpu_read_status
#define gpu_vblank replay3_gpu_vblank
#define gpu_set_line_parity replay3_gpu_set_line_parity
#define gpu_get_display replay3_gpu_get_display
#define gpu_render_display replay3_gpu_render_display
#define gpu_vram replay3_gpu_vram
#define gpu_changes replay3_gpu_changes
#define gpu_init replay3_gpu_init
#define gpu_set_upscale replay3_gpu_set_upscale
#define gpu_upscale replay3_gpu_upscale
#define gpu_save_state replay3_gpu_save_state
#define gpu_load_state replay3_gpu_load_state
#define gpu_state_size replay3_gpu_state_size
#include "gpu.c"
static char guard_tail[64] __attribute__((aligned(64), used));
static char guard_data_tail[64] __attribute__((aligned(64), used)) = { 1 };
