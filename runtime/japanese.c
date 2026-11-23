/* The game built from the Japanese disc (build.py --lang ja, g_game_lang 5): its font.
 *
 * The Japanese font is two pages of 12x12 cells, 21 a row: codes 0-440 at VRAM 896,0 (63x252) and
 * 441-734 at 960,0 (63x168), with its palettes at 896-959 x 253/254. It is the start of the Japanese
 * A_FILE's subfile 00 (13 entries of the game's packed-image list, 0xCAE0 bytes), which the build puts
 * at the start of DUMMY.DAT on the disc image it makes (tools/transplant.py, behind a 16-byte mark). It
 * is loaded here in place of the USA one (10 entries: the 8x12 font at 896,0, its palettes and the
 * HUD's icons). The glyph routine (80019070) is the USA one, its sprite then given the Japanese cell,
 * size, palette and texture page; the text advances 12 pixels instead of 8. */
#include "psx.h"
#include "psx_recomp.h"
#include <stdio.h>
#include <string.h>


#define GAME_JA (g_game_lang == 5)
#define FONT_BYTES 0xCAE0u
#define MAGIC "HMBTN JA FONT 1"     /* + a zero: 16 bytes */
#define FONT_BUF 0x800A5000u        /* zero in every state seen (800A4070-800B5D60), see friends.c */
#define PAGE_CODES 441
#define ROW 21
#define CELL 12

static uint8_t font[FONT_BYTES];
static int state;                   /* 0 not looked at, 1 on, -1 off */
static int busy;
int g_ja_icons;                     /* the icons moved (dma.c) */

static int on(void) {
    if (state) return state > 0;
    state = -1;
    if (!GAME_JA) return 0;
    uint32_t lba, size;
    if (!iso_find_file("DUMMY.DAT", &lba, &size) || size < 16 + FONT_BYTES) {
        LOGW("japanese: no DUMMY.DAT on the disc");
        return 0;
    }
    static uint8_t data[16 + FONT_BYTES + 2048];
    uint8_t sec[2352];
    for (uint32_t k = 0; k < 16 + FONT_BYTES; k += 2048) {
        if (!cdrom_read_sector_raw(lba + k / 2048u, sec)) return 0;
        memcpy(data + k, sec + 24, 2048);
    }
    if (memcmp(data, MAGIC, 16) || data[16] != 0x0D) {
        LOGW("japanese: the font is not on this disc (build again)");
        return 0;
    }
    memcpy(font, data + 16, FONT_BYTES);
    state = 1;
    return 1;
}

/* What the USA game keeps where the Japanese font goes (896-958 x 0-251), moved to VRAM it never uses
 * (784-831 x 256-470, measured in _testes/p2/vm_*): the HUD's four icons of the font's list (6x24 at
 * 928-946,0, palettes 896,250-253; moved in the list, ja_unpack), the clock's season icons (3x24 at 953,0
 * and 956,0, palettes 944,24-27) and an image at 928,32 (20x12), both sent to VRAM by the game (moved as
 * they are sent, ja_upload). What is drawn with them is moved too (ja_dma_packet), told apart by its
 * palette. The Japanese red ink (its palette 4 at 896,253) goes to 928,253, for 896,250-253 to be the
 * icons' only. */
#define ENTRY_HEAD 12
#define RED_TO_X 928
#define NEW_PAGE 0x1C               /* the texture page 768,256 */

static const struct { int16_t x, y, w, h, to_x, to_y; } moves[] = {
    { 928, 0, 24, 24, 784, 256 },   /* the four icons */
    { 896, 250, 16, 4, 816, 256 },  /* ... their palettes */
    { 953, 0, 6, 24, 808, 256 },    /* the season icons */
    { 944, 24, 16, 4, 816, 260 },   /* ... their palettes */
    { 928, 32, 20, 12, 784, 288 },  /* the 20x12 image */
};
#define N_MOVES (int)(sizeof moves / sizeof *moves)

/* the move a rectangle of VRAM is in (-1: none) */
static int move_of(int x, int y, int w, int h) {
    for (int i = 0; i < N_MOVES; i++)
        if (x >= moves[i].x && y >= moves[i].y && x + w <= moves[i].x + moves[i].w && y + h <= moves[i].y + moves[i].h)
            return i;
    return -1;
}

static uint32_t entry_size(uint32_t at) { return ENTRY_HEAD + ((ram_r32(at) & 0xFFFFFFu) + 3u) / 4u * 4u; }

/* 80017300 (a0 a packed-image list): the USA font's list is swapped for the Japanese one with the icons */
int ja_unpack(CPU *c) {
    if (busy || !GAME_JA || !on()) return 0;
    uint32_t a = c->r[4];
    if (ram_r8(a) != 0x0D || ram_r16(a + 2) != 10 || ram_r16(a + 8) != 896 || ram_r16(a + 10) != 251)
        return 0;
    static uint8_t aside[FONT_BYTES + 0x1000];
    uint8_t *ram = ram_ptr(FONT_BUF);
    memcpy(aside, ram, sizeof aside);
    memcpy(ram, font, sizeof font);
    uint32_t end = FONT_BUF + FONT_BYTES, n = ram_r16(FONT_BUF + 2);
    for (uint32_t i = 0, at = FONT_BUF + 4; i < n; i++, at += entry_size(at))
        if (ram_r16(at + 4) == 896 && ram_r16(at + 6) == 253 && ram_r16(at + 8) == 16)
            ram_w16(at + 4, RED_TO_X);                      /* the red ink */
    for (uint32_t i = 0, at = a + 4; i < 10; i++, at += entry_size(at)) {
        int x = ram_r16(at + 4), y = ram_r16(at + 6), m = move_of(x, y, ram_r16(at + 8), ram_r16(at + 10));
        uint32_t size = entry_size(at);
        if (m < 0 || end + size > FONT_BUF + sizeof aside) continue;
        memcpy(ram_ptr(end), ram_ptr(at), size);
        ram_w16(end + 4, (uint16_t)(x - moves[m].x + moves[m].to_x));
        ram_w16(end + 6, (uint16_t)(y - moves[m].y + moves[m].to_y));
        end += size;
        n++;
    }
    ram_w16(FONT_BUF + 2, (uint16_t)n);
    g_ja_icons = 1;
    busy = 1;
    c->r[4] = FONT_BUF;
    call_addr(c, 0x80017300u);
    busy = 0;
    memcpy(ram, aside, sizeof aside);
    return 1;
}

/* a VRAM write (GP0 A0, gpu.c) of something moved */
void ja_upload(int32_t *x, int32_t *y, int32_t w, int32_t h) {
    if (!GAME_JA) return;
    int m = move_of(*x, *y, w, h);
    if (m < 0 || moves[m].w == 24) return;          /* the four icons come in the font's list */
    *x += moves[m].to_x - moves[m].x;
    *y += moves[m].to_y - moves[m].y;
}

/* Every textured primitive the GPU is given (dma.c, the ordering table) that samples something moved:
 * its palette, texture coordinates and page patched (a rectangle's page is the drawing mode's: dma.c sets
 * it around). page: the drawing mode's texture page. w: the packet's words (cnt). -1 untouched, -2 a
 * patched polygon, -3 a drawing mode patched (the game's to be given back after), else the page a
 * rectangle needs */
int ja_dma_packet(uint32_t *w, uint32_t cnt, uint32_t page) {
    uint32_t cmd = w[0] >> 24, tpw = 0;
    if (cmd == 0xE1 && cnt >= 4) {          /* the drawing mode, then what it is for */
        int r = ja_dma_packet(w + 1, cnt - 1, w[0] & 0x1F);
        if (r >= 0) w[0] = (w[0] & ~0x1Fu) | (uint32_t)r;
        return r >= 0 ? -3 : r;
    }
    int rect = 0;
    if ((cmd & 0xE4) == 0x64 && cnt >= 3) rect = 1;                         /* 64-67, 6C-6F, 74-77, 7C-7F */
    else if ((cmd & 0xF4) == 0x24 && cnt >= 7) tpw = 4;                     /* flat textured polygons */
    else if ((cmd & 0xF4) == 0x34 && cnt >= 9) tpw = 5;                     /* shaded textured polygons */
    else return -1;
    if (!rect) page = (w[tpw] >> 16) & 0x1F;
    if (page != 14) return -1;                      /* 896,0 */
    uint32_t cl = w[2] >> 16, cx = (cl & 0x3F) << 4, cy = cl >> 6, u = w[2] & 0xFF, v = (w[2] >> 8) & 0xFF;
    int m;
    if (cx == 896 && cy >= 250 && cy <= 253) m = 0;                         /* an icon */
    else if (cx == 944 && cy >= 24 && cy <= 27) m = 2;                      /* a season icon */
    else if (u >= 128 && u < 208 && v >= 32 && v < 44 && !(cy >= 253 && cx >= 896)) m = 4;   /* the image */
    else return -1;
    int pal = m == 4 ? -1 : m + 1;
    int du = (moves[m].to_x - 768) * 4 - (moves[m].x - 896) * 4;            /* in the new page: 768,256 */
    if (pal >= 0)
        cl = (uint32_t)(cy - moves[pal].y + moves[pal].to_y) << 6 | (uint32_t)moves[pal].to_x >> 4;
    uint32_t step = (cmd & 0x10) ? 3 : 2, n = rect ? 1 : (cmd & 8) ? 4 : 3;
    for (uint32_t k = 0; k < n && 2 + k * step < cnt; k++) {
        uint32_t i = 2 + k * step, x = w[i], hi = x & 0xFFFF0000u;
        if (k == 0) hi = cl << 16;
        if (i == tpw) hi = (hi & ~(0x1Fu << 16)) | (uint32_t)NEW_PAGE << 16;
        w[i] = hi | (x & 0xFF00u) | ((uint32_t)((int)(x & 0xFF) + du) & 0xFF);   /* v: the same in 256 */
    }
    return rect ? NEW_PAGE : -2;
}

/* 80019070 (a1 the sprite, a2 the code, 32(sp) the palette): the Japanese cell. The palettes are the
 * Japanese game's (its table at 8004AFE8): 0-3 at 896-944,254, 4-5 at 896-912,253 (4 moved, above) */
static const uint16_t clut_xy[6][2] = {
    { 896, 254 }, { 912, 254 }, { 928, 254 }, { 944, 254 }, { RED_TO_X, 253 }, { 912, 253 } };

int ja_glyph(CPU *c) {
    if (busy || !GAME_JA || state <= 0) return 0;
    uint32_t pkt = c->r[5], code = c->r[6] & 0xFFF, pal = ram_r32(c->r[29] + 32);
    if (pal >= 6) pal = 0;
    busy = 1;
    call_addr(c, 0x80019070u);
    busy = 0;
    int page = code >= PAGE_CODES;
    uint32_t i = code - (page ? PAGE_CODES : 0);
    ram_w8(pkt + 12, (uint8_t)(i % ROW * CELL));
    ram_w8(pkt + 13, (uint8_t)(i / ROW * CELL));
    ram_w16(pkt + 14, (uint16_t)(clut_xy[pal][1] << 6 | clut_xy[pal][0] >> 4));
    ram_w16(pkt + 16, CELL);
    ram_w16(pkt + 18, CELL);
    ram_w32(pkt + 24, 0xE1000600u | (page ? 15u : 14u));
    return 1;
}

/* the text's advance (80018FC0: 8 pixels a glyph) */
int ja_advance(void) {
    return GAME_JA && state > 0 ? CELL - 8 : 0;
}

/* The clock's time (GF_FARM 800FB674): [A/P][M][ ][h][h][ ][m][m] at 8 pixels a glyph, its colon apart
 * at 272. Twelve pixels a glyph do not fit in its window: in Japanese it is the time of day in 24 hours,
 * [h][h][ ][m][m], the colon at 256 (recomp.py) */
void ja_clock(uint32_t buf) {
    if (!GAME_JA || state <= 0) return;
    uint16_t t[9];
    for (int i = 0; i < 9; i++) t[i] = ram_r16(buf + 2u * (uint32_t)i);
    if (t[8] != 0xFFFF) return;
    int h = (t[4] >= 187 && t[4] < 197 ? t[4] - 187 : 0) + (t[3] >= 187 && t[3] < 197 ? 10 * (t[3] - 187) : 0);
    if (t[0] == 161 + 15) h += 12;          /* P */
    uint16_t out[6] = { h >= 10 ? (uint16_t)(187 + h / 10) : 0x0FFD, (uint16_t)(187 + h % 10), 0x0FFD,
                        t[6], t[7], 0xFFFF };
    for (int i = 0; i < 6; i++) ram_w16(buf + 2u * (uint32_t)i, out[i]);
}

/* A text the game's text puts in by number ({4000}-{4009}: 80018EE4, the text at v0) that is a number in
 * the USA font's digits (0x40-0x49: the memory card screens' slot and diary numbers, texts of their own),
 * in the Japanese ones */
void ja_macro(uint32_t str) {
    if (!GAME_JA || state <= 0) return;
    int n = 0;
    for (; n < 8; n++) {
        uint16_t w = ram_r16(str + 2u * (uint32_t)n);
        if (w == 0xFFFF) break;
        if (w < 0x40 || w > 0x49) return;
    }
    if (n == 0 || n == 8) return;
    for (int i = 0; i < n; i++) ram_w16(str + 2u * (uint32_t)i, (uint16_t)(187 + ram_r16(str + 2u * (uint32_t)i) - 0x40));
}
