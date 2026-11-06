/* Fixes to the original game. Hooks emitted by tools/recomp.py. */
#include "psx.h"
#include "psx_recomp.h"
#include <string.h>

/* The farm score on the summary page (the "%" next to the name, 800FB908) adds up to 100:
 * buildings 10, recipes 8, the hearts of wife, child, dog and horse 10 each, chickens 10, cows
 * and sheep 20, Power Berries 10 and the Mystic Berry 2. The page counts the Power Berries (its
 * berry row) but never adds them, nor the Mystic Berry, so a complete farm stops at 88. The sum
 * is in v0 just before it is stored (800FBDF0); this adds them, as Back to nature+'s farm score
 * fix does. The score is worked out each time the page opens, so old saves read right at once. */
#define POWER_BERRIES 0x800712BCu   /* halfword: Power Berries eaten (10 in all) */
#define BERRY_FLAGS 0x80071A6Cu     /* word: 0x200 = the Mystic Berry eaten */

uint32_t farm_score_berries(uint32_t score) {
    int16_t power = (int16_t)MEM_R16(POWER_BERRIES);
    if (power > 10) power = 10;
    if (power > 0) score += (uint32_t)power;
    if (MEM_R32(BERRY_FLAGS) & 0x200u) score += 2;
    return score;
}

/* The cabinet in the first house: the pile of boxes there is a thing X looks at (kind 16) that does
 * nothing; it opens the cabinet (kind 23), as in Back to nature+. An area's things to look at are
 * copied to 8009F78C when it loads (800BCF54; the pointer to them is stored at 800BD0B4): its map
 * number, then from +16 up to 50 of 44 bytes, the kind at +2. Maps 80-83 are the first house. */
#define AREA_THINGS 0x8009F78Cu
#define FIRST_HOUSE 80u
#define NOTHING_KIND 16
#define CABINET_KIND 23

void first_house_cabinet(void) {
    if (!g_cfg.earlycabinet || MEM_R32(AREA_THINGS + 4) - FIRST_HOUSE >= 4) return;
    for (uint32_t i = 0; i < 50; i++) {
        uint32_t kind = AREA_THINGS + 16 + i * 44 + 2;
        if (MEM_R16(kind) == NOTHING_KIND) MEM_W16(kind, CABINET_KIND);
    }
}

/* Walking through crops: the player's walk asks for collisions at 800CB114 (a2 in s2: 56 when it
 * looks ahead, 0 for the step itself; the mask in t2) and the farm's field answers at 800BE6E4
 * (the query in s1, its mask at +20): a field thing is solid when its picture says so (v0 after
 * 800BE818, with the item's record in v1 and its class byte, +3, in t1). As in Back to nature+, the
 * walk's mask gets 0x100000 and a planted crop (record flags 4040h, a class) is not solid then. */
#define CROP_WALK 0x100000u
#define CROP_FLAGS 0x4040u

uint32_t crop_walk_mask(uint32_t mask, uint32_t why) {
    return g_cfg.cropwalk && (why == 56 || why == 0) ? mask | CROP_WALK : mask;
}

uint32_t crop_walk_solid(uint32_t solid, uint32_t query, uint32_t record, uint32_t cls) {
    if (solid && (MEM_R32(query + 20) & CROP_WALK) && (int8_t)cls != -1 && MEM_R16(record) == CROP_FLAGS) return 0;
    return solid;
}

/* The stamina and tiredness under the clock (staminabar): the game never shows them. Every window
 * goes through 80014FAC (a0 the ordering table's entry, a1-a3 x / y / width, the height and the
 * colours on the stack from +16); the clock is the one at 230,18, 74x28, with two rows of text
 * (the glyph routine 80019070: a0 the entry before the window's, a1 where the primitive goes, a2 the
 * letter, a3 x, then y, r, g, b, the palette and semi-transparency (+36: GP0 66h instead of 64h; the
 * clock turns it on and off every 5 minutes, so it is never copied) on the stack; it returns where
 * the next goes). When the
 * clock opens, a window of the same size opens under it with the game's own routines: a row for the
 * stamina (S, a bar of what is left of the most, the number) and one for the tiredness (F, of 100).
 * The primitives come from the game's allocator (80011F5C, the size in a0). In the second window
 * (p2view.c) the numbers are farmer 2's. */
#define CLOCK_X 230
#define CLOCK_Y 18
#define CLOCK_W 74
#define CLOCK_H 28
#define WINDOW_AT 0x80014FACu
#define GLYPH_AT 0x80019070u
#define ALLOC_AT 0x80011F5Cu
#define PLAYER_STAMINA 0x80071A12u      /* what is left, the most, the tiredness (halfwords) */
#define LETTER(c) ((c) - 'A')           /* the font: A-Z from 0, a-z from 0x20, 0-9 from 0x40 */
#define DIGIT(d) (0x40 + (d))
#define RGB(r, g, b) ((uint32_t)(b) << 16 | (uint32_t)(g) << 8 | (uint32_t)(r))
#define BAR_X (CLOCK_X + 12)
#define BAR_W 28
#define BAR_H 6

static uint32_t glyph_args[6];          /* the last glyph's stack arguments (+16 to +36) */
static int fade_now;                    /* the clock is see-through this frame */
static int glyph_known, stamina_drawing;

void stamina_glyph_seen(CPU *c) {
    if (stamina_drawing) return;
    for (int i = 0; i < 6; i++) glyph_args[i] = MEM_R32(c->r[29] + 16 + (uint32_t)i * 4);
    glyph_known = 1;
}

static uint32_t call4(CPU *c, uint32_t at, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    c->r[4] = a0;
    c->r[5] = a1;
    c->r[6] = a2;
    c->r[7] = a3;
    call_addr(c, at);
    return c->r[2];
}

static uint32_t glyph(CPU *c, uint32_t ot, uint32_t p, int letter, int x, int y) {
    uint32_t sp = c->r[29];
    MEM_W32(sp + 16, (uint32_t)y);
    for (int i = 1; i < 5; i++) MEM_W32(sp + 16 + (uint32_t)i * 4, glyph_args[i]);
    MEM_W32(sp + 36, (uint32_t)fade_now);    /* +36 non-zero: a semi-transparent letter (the clock's blinks every 5 minutes) */
    return call4(c, GLYPH_AT, ot, p, (uint32_t)letter, (uint32_t)x);
}

/* an untextured rectangle (GP0 60h) at p, put first in the table's entry ot (drawn before what is
 * already there) */
static uint32_t tile(uint32_t ot, uint32_t p, int x, int y, int w, int h, uint32_t rgb) {
    MEM_W32(p, 3u << 24 | (MEM_R32(ot) & 0xFFFFFFu));
    MEM_W32(ot, (MEM_R32(ot) & 0xFF000000u) | (p & 0xFFFFFFu));
    MEM_W32(p + 4, (fade_now ? 0x62000000u : 0x60000000u) | rgb);     /* 62h: semi-transparent */
    MEM_W32(p + 8, (uint32_t)(uint16_t)y << 16 | (uint16_t)x);
    MEM_W32(p + 12, (uint32_t)h << 16 | (uint32_t)w);
    HUD_BEGIN();                        /* widescreen: with the clock */
    SPRITE_TAG(p + 8);
    return p + 16;
}

static uint32_t stamina_row(CPU *c, uint32_t ot, uint32_t p, int y, int letter, int value, int most, uint32_t rgb) {
    p = glyph(c, ot, p, letter, CLOCK_X + 2, y);
    if (value < 0) value = 0;
    if (value > 999) value = 999;
    int fill = most > 0 ? (value > most ? BAR_W : value * BAR_W / most) : 0;
    if (fill) p = tile(ot, p, BAR_X, y + 4, fill, BAR_H, rgb);          /* first: drawn last */
    p = tile(ot, p, BAR_X, y + 4, BAR_W, BAR_H, RGB(40, 32, 24));
    int x = BAR_X + BAR_W + 4 + 16;
    do {
        p = glyph(c, ot, p, DIGIT(value % 10), x, y);
        value /= 10;
        x -= 8;
    } while (value);
    return p;
}

/* The clock (GF_FARM 800FB674: its window through 80022580 / 800136D4 at 228,16, 78x32, the date,
 * the time and the weather icon) sets g_clock_draw while it builds: in widescreen what it builds
 * keeps to the right edge (HUD_BEGIN), the stamina window under it too.
 * The clock see-through over the farmer: the game projects the farmer (the anchor at 8001E02C,
 * its feet, kind 1; farmer 2 is kind 3, the one the second window follows) after it builds the HUD,
 * so the clock uses where the farmer was in the frame before. While the farmer is behind the clock
 * (and the stamina window under it), every part of them is built semi-transparent: the window
 * builders and the glyph routine have a flag for it on the stack (hud_fade). The blending of
 * untextured parts is the drawing mode's in effect (a cave's or a lake's can subtract): the window
 * gets one of its own, the glyphs' (E100060Eh, 50% / 50%), drawn first. Kept apart for the game,
 * the second window's run and the second machine. */
int g_clock_draw;
static uint32_t clock_ot;
#define FADE_BODY 40                    /* how high the farmer is above its feet */
#define FADE_HOLD 20                    /* frames it stays see-through after (no blinking) */
static struct { int x0, y0, x1, y1, seen, on, hold; } fade_at[3];

static int fade_slot(void) { return g_machine ? 2 : g_view2_run ? 1 : 0; }

void hud_fade_anchor(int x, int y, int kind) {
    int i = fade_slot();
    if (kind != (i == 1 ? 3 : 1))
        return;
    if (!fade_at[i].seen) {
        fade_at[i].x0 = fade_at[i].x1 = x;
        fade_at[i].y0 = fade_at[i].y1 = y;
        fade_at[i].seen = 1;
    }
    if (x < fade_at[i].x0) fade_at[i].x0 = x;
    if (x > fade_at[i].x1) fade_at[i].x1 = x;
    if (y < fade_at[i].y0) fade_at[i].y0 = y;
    if (y > fade_at[i].y1) fade_at[i].y1 = y;
    fade_at[i].on = 0;                  /* the HUD of this frame is built */
}

void hud_fade(CPU *c, uint32_t flag) {
    if (g_clock_draw && fade_at[fade_slot()].on)
        MEM_W16(c->r[29] + flag, 1);
}

void clock_begin(CPU *c) {
    (void)c;
    int f = fade_slot(), bottom = CLOCK_Y + CLOCK_H + (g_cfg.staminabar && glyph_known ? 2 + CLOCK_H : 0);
    g_clock_draw = 1;
    clock_ot = 0;
    if (fade_at[f].seen && fade_at[f].x0 < CLOCK_X + CLOCK_W + 8 && fade_at[f].x1 > CLOCK_X - 8
        && fade_at[f].y0 - FADE_BODY < bottom && fade_at[f].y1 > CLOCK_Y - 2)
        fade_at[f].hold = FADE_HOLD;
    else if (fade_at[f].hold > 0)
        fade_at[f].hold--;
    fade_at[f].on = fade_at[f].hold > 0;
    fade_at[f].seen = 0;
}

void clock_window(CPU *c) {
    if (g_clock_draw)
        clock_ot = c->r[4];
}

void stamina_window(CPU *c) {
    uint32_t sp = c->r[29];
    if (stamina_drawing || (int16_t)c->r[5] != CLOCK_X || (int16_t)c->r[6] != CLOCK_Y
        || (int16_t)c->r[7] != CLOCK_W || (int16_t)MEM_R32(sp + 16) != CLOCK_H)
        return;
    if (!g_cfg.staminabar || !glyph_known)
        return;
    fade_now = g_clock_draw && fade_at[fade_slot()].on;
    int16_t now, most, tired;
    if (!g_view2_run || !p2_stamina(&now, &most, &tired)) {
        now = (int16_t)MEM_R16(PLAYER_STAMINA);
        most = (int16_t)MEM_R16(PLAYER_STAMINA + 2);
        tired = (int16_t)MEM_R16(PLAYER_STAMINA + 4);
    }
    uint32_t regs[32], hi = c->hi, lo = c->lo, ot = c->r[4];
    memcpy(regs, c->r, sizeof regs);
    stamina_drawing = 1;
    uint32_t fsp = sp - 0x80, y = CLOCK_Y + CLOCK_H + 2;
    for (uint32_t i = 0; i < 6; i++) MEM_W32(fsp + 16 + i * 4, MEM_R32(sp + 16 + i * 4));   /* the clock's colours */
    c->r[29] = fsp;
    call4(c, WINDOW_AT, ot, CLOCK_X, y, CLOCK_W);
    c->r[29] = fsp;
    uint32_t p = call4(c, ALLOC_AT, 8 * 28 + 4 * 16, 0, 0, 0);
    c->r[29] = fsp;
    p = stamina_row(c, ot - 4, p, (int)y + 1, LETTER('S'), now, most, RGB(72, 200, 80));
    stamina_row(c, ot - 4, p, (int)y + 15, LETTER('F'), tired, 100, RGB(232, 104, 48));
    stamina_drawing = 0;
    fade_now = 0;
    memcpy(c->r, regs, sizeof regs);
    c->hi = hi;
    c->lo = lo;
}

/* A call of a game routine from a hook: every register is kept (the routine's frame goes below the
 * hook's), only v0 comes back */
static uint32_t game_call(CPU *c, uint32_t at, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    uint32_t regs[32], hi = c->hi, lo = c->lo;
    memcpy(regs, c->r, sizeof regs);
    c->r[29] = regs[29] - 0x80;
    uint32_t v = call4(c, at, a0, a1, a2, a3);
    memcpy(c->r, regs, sizeof regs);
    c->hi = hi;
    c->lo = lo;
    return v;
}

/* After the clock's window is built (back from 800136D4 at 800225A8): see-through, its drawing mode
 * (50% / 50%, the glyphs' own) goes first in its table entry, drawn before it */
void clock_window_done(CPU *c) {
    if (!g_clock_draw || !clock_ot || !fade_at[fade_slot()].on)
        return;
    uint32_t p = game_call(c, ALLOC_AT, 8, 0, 0, 0);
    MEM_W32(p, 1u << 24 | (MEM_R32(clock_ot) & 0xFFFFFFu));
    MEM_W32(p + 4, 0xE100060Eu);
    MEM_W32(clock_ot, (MEM_R32(clock_ot) & 0xFF000000u) | (p & 0xFFFFFFu));
    clock_ot = 0;
}
