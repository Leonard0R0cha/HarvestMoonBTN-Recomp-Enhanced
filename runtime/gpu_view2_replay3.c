/* For the second view's GPU (gpu_view2.c), as gpu_replay3.c for the game's. Frame interpolation (prototype): a second copy of the GPU, with state of its own (everything in
 * gpu.c is static), that redraws a recorded frame from the in-between camera on a worker thread
 * (view2_replay_run3: the third worker's copy; see gpu_replay.c). Its public names are renamed so they don't clash with the real GPU. */
/* The worker writes these variables all the time: they get sections of their own, aligned to and
 * padded with whole cache lines, so they never share a cache line with the emulator's data (one
 * worker's sprite offsets had ended up next to the GTE registers). */
#pragma clang section bss=".bss$v2replay3" data=".data$v2replay3"
static char guard_head[64] __attribute__((aligned(64), used));
static char guard_data_head[64] __attribute__((aligned(64), used)) = { 1 };
#define GPU_REPLAY 1
#define GPU_REPLAY_RUN view2_replay_run3
#define g_pgxp v2replay3_g_pgxp
#define g_pgxp_persp v2replay3_g_pgxp_persp
#define g_pgxp_cracks v2replay3_g_pgxp_cracks
#define g_gp0_addr v2replay3_g_gp0_addr
#define gpu_pgxp_store v2replay3_gpu_pgxp_store
#define g_sprite_tags v2replay3_g_sprite_tags
#define g_sprite_on v2replay3_g_sprite_on
#define g_sprite_depth v2replay3_g_sprite_depth
#define g_hud_call v2replay3_g_hud_call
#define g_held_call v2replay3_g_held_call
#define g_item_call v2replay3_g_item_call
#define g_menu_tile v2replay3_g_menu_tile
#define g_sprite_ws v2replay3_g_sprite_ws
#define gpu_sprite_anchor v2replay3_gpu_sprite_anchor
#define gpu_sprite_begin v2replay3_gpu_sprite_begin
#define gpu_held_begin v2replay3_gpu_held_begin
#define gpu_display_menu v2replay3_gpu_display_menu
#define gpu_menu_begin v2replay3_gpu_menu_begin
#define gpu_hud_begin v2replay3_gpu_hud_begin
#define gpu_sprite_alias v2replay3_gpu_sprite_alias
#define gpu_sprite_tag v2replay3_gpu_sprite_tag
#define g_scene_3d v2replay3_g_scene_3d
#define g_interp v2replay3_g_interp
#define gpu_set_camera v2replay3_gpu_set_camera
#define gpu_interp_enable v2replay3_gpu_interp_enable
#define gpu_render_interp v2replay3_gpu_render_interp
#define gpu_interp_prepare v2replay3_gpu_interp_prepare
#define gpu_interp_work v2replay3_gpu_interp_work
#define gpu_interp_late v2replay3_gpu_interp_late
#define gpu_write_gp0 v2replay3_gpu_write_gp0
#define gpu_dma_write v2replay3_gpu_dma_write
#define gpu_read_data v2replay3_gpu_read_data
#define gpu_dma_read v2replay3_gpu_dma_read
#define gpu_write_gp1 v2replay3_gpu_write_gp1
#define gpu_read_status v2replay3_gpu_read_status
#define gpu_vblank v2replay3_gpu_vblank
#define gpu_set_line_parity v2replay3_gpu_set_line_parity
#define gpu_get_display v2replay3_gpu_get_display
#define gpu_render_display v2replay3_gpu_render_display
#define gpu_vram v2replay3_gpu_vram
#define gpu_changes v2replay3_gpu_changes
#define gpu_init v2replay3_gpu_init
#define gpu_set_upscale v2replay3_gpu_set_upscale
#define gpu_upscale v2replay3_gpu_upscale
#define gpu_save_state v2replay3_gpu_save_state
#define gpu_load_state v2replay3_gpu_load_state
#define gpu_state_size v2replay3_gpu_state_size
#include "gpu.c"
static char guard_tail[64] __attribute__((aligned(64), used));
static char guard_data_tail[64] __attribute__((aligned(64), used)) = { 1 };
