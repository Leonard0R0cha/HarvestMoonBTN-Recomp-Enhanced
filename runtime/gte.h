/* GTE (COP2 Geometry Transformation Engine) - exact fixed-point reimplementation. */
#pragma once
#include <stdint.h>
#include <stddef.h>

void     gte_reset(void);
uint32_t gte_read_data(int reg);              /* MFC2 / SWC2  (reg 0..31) */
void     gte_write_data(int reg, uint32_t v); /* MTC2 / LWC2  (reg 0..31) */
uint32_t gte_read_ctrl(int reg);              /* CFC2         (reg 0..31) */
void     gte_write_ctrl(int reg, uint32_t v); /* CTC2         (reg 0..31) */
void     gte_command(uint32_t op);            /* COP2 imm25 command word (instruction & 0x1FFFFFF) */

/* PGXP: unrounded screen x/y and view depth of SXY register reg (12..15) when the last
 * perspective transform produced it; 0 when unknown. Display only, not part of the state. */
int      gte_sxy_precise(int reg, float *x, float *y, float *w);
/* Depth scale for a sprite standing at screen point (sx, sy), 1 at the projection centre. */
float    gte_ground_scale(float sx, float sy);

/* Widescreen: screen X of RTPS/RTPT is scaled by num/den about OFX (1/1 = off), so a wider view
 * fits the picture. The game sees the squeezed coordinates. Not part of the state. */
void     gte_set_widescreen(int num, int den);
/* Vertices projected by RTPS/RTPT so far (the frontend's "is the game drawing 3D" test). */
uint32_t gte_projections(void);
/* Widescreen: distance v of the 4:3 view widened like the view (v unchanged when off). */
int32_t  gte_widescreen_widen(int32_t v);
/* Widescreen: angle a (4096 = full turn) off the view axis, widened like the view (a side plane
 * of the game's own frustum test); a unchanged when widescreen is off. */
int32_t  gte_widescreen_angle(int32_t a);

size_t   gte_state_size(void);
void     gte_save_state(void *dst);
void     gte_load_state(const void *src);
