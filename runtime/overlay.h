/* The in-game overlay (F7, overlay.cpp, Dear ImGui): settings changed while the game is paused
 * (or runs behind it, with the player's controls on the overlay), drawn over the picture by either way of showing it (platform_sdl.c's SDL renderer, Vulkan's
 * vkr_present). For now: the CRT filters and their settings. */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

void overlay_init(int font_px);     /* the font's size in pixels */
int  overlay_open(void);
int  overlay_visible(void);         /* open, or a message of its own on the picture (a save state's result) */
int  overlay_pauses(void);          /* open, with "Pause the game" on (else the game runs behind it) */
void overlay_show(int on);
/* an SDL_Event for the overlay (while open); mx, my: the window's pixels per mouse unit */
void overlay_event(const void *sdl_event, float mx, float my);
void overlay_frame(int w, int h, float seconds);    /* the overlay's next picture (w x h: the window's pixels) */

/* drawing what overlay_frame made: triangles (vertices: x, y, u, v as floats, then the colour as 4
 * bytes R, G, B, A; 20 bytes each) of the font's texture (RGBA), clipped (x0, y0, x1, y1) */
void overlay_font(const unsigned char **rgba, int *w, int *h);
int  overlay_sizes(int *nvtx, int *nidx);       /* 0: nothing to draw */
typedef void (*OverlayList)(void *ctx, const void *vtx, int nvtx, const unsigned short *idx, int nidx);
typedef void (*OverlayCmd)(void *ctx, const float clip[4], int idx_offset, int vtx_offset, int count);
void overlay_draw(void *ctx, OverlayList list, OverlayCmd cmd);    /* each list, then each of its pieces */

/* from the game */
int    platform_crt_mode(void);
void   platform_crt(int mode);              /* the filter (0 off, 1 scanlines, 2 CRT), with its settings */
float *platform_crt_params(int shader);     /* 0 scanlines, 1 CRT: their settings (crt_params.h) */
int   *platform_crt_smooth(void);           /* 1: smooth 3D edges under them (the picture averaged) */
int    config_save(const char *section, const char *key, const char *value);   /* in hmbtn.ini */
const char *config_language(void);          /* the launcher's: en, pt, es */
int    platform_can_pause(void);            /* 0 in online co-op */

/* save states (savestate.c): asked here, done at the game's next main-loop pass */
#define SS_SLOTS 8
enum { SS_NONE, SS_WAITING, SS_SAVED, SS_LOADED, SS_EMPTY, SS_BUSY, SS_TIMEOUT, SS_ONLINE, SS_APART, SS_OTHER, SS_ERROR };
void savestate_request(int load, int slot);
int  savestate_pending(void);
int  savestate_status(int *slot, unsigned *serial);    /* the last result (SS_*), its slot; serial: +1 a result */
int  savestate_slot(void);                  /* the one F8 / F10 use */
void savestate_set_slot(int slot);
int  savestate_slot_info(int slot, char *out, int n);  /* when it was saved; 0: empty */
void overlay_state_text(char *out, int n);  /* overlay.cpp: the last result, in the launcher's language */

#ifdef __cplusplus
}
#endif
