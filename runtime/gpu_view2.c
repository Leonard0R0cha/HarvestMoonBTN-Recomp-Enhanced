/* The second view (--p2: a window that follows farmer 2): a copy of the GPU, with state of its own
 * (everything in gpu.c is static), that is sent what the real GPU is sent, except for the farm's
 * frames, which it gets from the farm's frame run once more with the view on farmer 2 (p2view.c,
 * g_gpu_route in gpu.c). Its public names are renamed so they don't clash with the real GPU; its
 * data gets sections of its own, as in gpu_replay.c. */
#pragma clang section bss=".bss$view2" data=".data$view2"
static char guard_head[64] __attribute__((aligned(64), used));
static char guard_data_head[64] __attribute__((aligned(64), used)) = { 1 };
#define GPU_VIEW2 1
#define g_pgxp view2_g_pgxp
#define g_pgxp_persp view2_g_pgxp_persp
#define g_pgxp_cracks view2_g_pgxp_cracks
#define g_gp0_addr view2_g_gp0_addr
#define gpu_pgxp_store view2_gpu_pgxp_store
#define g_sprite_tags view2_g_sprite_tags
#define g_sprite_on view2_g_sprite_on
#define g_sprite_depth view2_g_sprite_depth
#define g_hud_call view2_g_hud_call
#define g_held_call view2_g_held_call
#define g_item_call view2_g_item_call
#define g_menu_tile view2_g_menu_tile
#define g_sprite_ws view2_g_sprite_ws
#define gpu_sprite_anchor view2_gpu_sprite_anchor
#define gpu_sprite_begin view2_gpu_sprite_begin
#define gpu_held_begin view2_gpu_held_begin
#define gpu_display_menu view2_gpu_display_menu
#define gpu_menu_begin view2_gpu_menu_begin
#define gpu_hud_begin view2_gpu_hud_begin
#define gpu_sprite_alias view2_gpu_sprite_alias
#define gpu_sprite_tag view2_gpu_sprite_tag
#define g_scene_3d view2_g_scene_3d
#define g_interp view2_g_interp
#define gpu_set_camera view2_gpu_set_camera
#define gpu_interp_enable view2_gpu_interp_enable
#define gpu_render_interp view2_gpu_render_interp
#define gpu_interp_prepare view2_gpu_interp_prepare
#define gpu_interp_work view2_gpu_interp_work
#define gpu_interp_late view2_gpu_interp_late
#define gpu_write_gp0 view2_gpu_write_gp0
#define gpu_dma_write view2_gpu_dma_write
#define gpu_read_data view2_gpu_read_data
#define gpu_dma_read view2_gpu_dma_read
#define gpu_write_gp1 view2_gpu_write_gp1
#define gpu_read_status view2_gpu_read_status
#define gpu_vblank view2_gpu_vblank
#define gpu_set_line_parity view2_gpu_set_line_parity
#define gpu_get_display view2_gpu_get_display
#define gpu_render_display view2_gpu_render_display
#define gpu_vram view2_gpu_vram
#define gpu_changes view2_gpu_changes
#define gpu_init view2_gpu_init
#define gpu_set_upscale view2_gpu_set_upscale
#define gpu_upscale view2_gpu_upscale
#define gpu_save_state view2_gpu_save_state
#define gpu_load_state view2_gpu_load_state
#define gpu_state_size view2_gpu_state_size
#define gpu_set_threads view2_gpu_set_threads
#define gpu_threads view2_gpu_threads
#include "gpu.c"
static char guard_tail[64] __attribute__((aligned(64), used));
static char guard_data_tail[64] __attribute__((aligned(64), used)) = { 1 };
