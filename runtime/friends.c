/* The town's friendships (friendpage): one more page in the START menu, after the last, with the
 * 32 people of the town: the name, the hearts and whether you talked to them / gave them a gift
 * today. Hooks emitted by tools/recomp.py.
 *
 * The START menu (GF_FARM 800FC1AC, each frame): its windows (80022580: a0 the ordering table's
 * entry, a1 1, a2 / a3 x / y, the width and height on the stack), the title (80017B3C: a0 the entry,
 * a1 the text, a2 / a3 x / y; the text is the farm's name, built when the menu opens at 800FB908 in
 * 8012CDC8), Left / Right (the pad's new presses, *(8005E3A8) + 0: 0x8000 / 0x2000) to the page
 * before / after (800FEA00 / 800FE928: the page at 8012720E, 0-7, some skipped), then the page's
 * routine from the table at 80127308. The new page is 8: Right on the last page goes to it and
 * Left on the first comes back to it; its routine is an empty one (80011270) and the page is drawn
 * here, with the game's window, text and glyph routines.
 *
 * The people: a record of 0x1D4 bytes each from 80076220 (by the person's number): +2 the
 * friendship (0-255), +4 a bachelorette's love (0-65535: the heart's colour), +30 bits: 0x400
 * talked to today, 0x200 given a gift today. Up / Down move the cursor (the list scrolls).
 *
 * The person under the cursor is shown with their portrait, the game's own: the conversations' 274
 * portraits are A_FILE's file 1 (where it starts: 80048B1C + 40 + 36), 6144 bytes each (0x0D: a
 * palette and a packed 80x80 8-bit picture, with where they go in VRAM: 960,175 and the palette at
 * 0,490); 80017300 unpacks one and loads it, 80019E80 (a0 the entry, a1 / a2 x / y, a3 not -1) draws
 * it. It is read from the disc here, put for the call in RAM nothing uses (800A5000), and the game's
 * portrait number (gp+492) is forgotten, so the next conversation loads its own again. Drawn as the
 * conversations' are, the redrawn portraits (textures/portraits) take its place. */
#include "psx.h"
#include "psx_recomp.h"
#include "gpu.h"
#include "text_pt.h"
#include <stdio.h>
#include <string.h>

#define PAGE 0x8012720Eu
#define FRIEND_PAGE 8
#define PAGES 8                     /* the game's */
#define PEOPLE 0x80076220u
#define PERSON_SIZE 0x1D4u
#define PAD_NEW 0x8005E3A8u         /* -> the pad's new presses (+0) */
#define GFX 0x8005E3C0u             /* -> the frame's drawing state: +216 the ordering table */
#define WINDOW_AT 0x80022580u
#define TEXT_AT 0x80017B3Cu
#define GLYPH_AT 0x80019070u
#define ALLOC_AT 0x80011F5Cu
#define SOUND_AT 0x8001FABCu
#define NOTHING 0x80011270u         /* jr ra */
#define PAD_UP 0x1000u
#define PAD_DOWN 0x4000u
#define HEART_FULL 0x80
#define HEART_EMPTY 0x81
#define GLYPH_SIZE 28
/* the texts are written where the game keeps nothing (zero in every state seen: 8009BC90-8009CBF0,
 * after the bigger greenhouse's rows) and what was there is put back after each is drawn; the
 * stack is the scratchpad's, too small */
#define TEXT_BUF 0x8009BE00u
#define TEXT_MAX 64

/* the menu's page window: 16,32 288x192 */
#define LIST_X 28
#define LIST_Y 54                    /* under the title's window (60,16 200x32) */
#define ROW_H 14
#define ROWS 12
#define HEARTS_X 86
#define PANEL_X 212
#define PORTRAIT_Y 56
#define PORTRAIT_AT 0x80019E80u
#define UNPACK_AT 0x80017300u
#define FILE_TABLE 0x80048B1Cu      /* A_FILE's files: 40 bytes each, +36 where it starts */
#define PORTRAIT_SIZE 6144u
#define PORTRAIT_BUF 0x800A5000u    /* zero in every state seen (800A4070-800B5D60) */
#define GP_PORTRAIT 492u            /* the portrait the conversations have loaded (halfword) */

/* the person's number, name and first portrait (the conversations' numbering) */
static const struct { uint8_t id; const char *name; uint16_t face; } people[] = {
    { 3, "Karen", 11 }, { 7, "Ann", 37 }, { 9, "Elli", 53 }, { 14, "Mary", 84 }, { 20, "Popuri", 121 },
    { 1, "Jeff", 0 }, { 2, "Sasha", 6 }, { 4, "Saibara", 19 }, { 5, "Gray", 24 }, { 6, "Doug", 32 },
    { 8, "Doctor", 46 }, { 10, "Carter", 62 }, { 11, "Cliff", 67 }, { 12, "Basil", 74 }, { 13, "Anna", 79 },
    { 15, "Barley", 94 }, { 16, "May", 99 }, { 17, "Thomas", 104 }, { 18, "Lillia", 109 }, { 19, "Rick", 114 },
    { 21, "Duke", 130 }, { 22, "Manna", 136 }, { 23, "Ellen", 141 }, { 24, "Gotz", 146 }, { 25, "Kai", 152 },
    { 26, "Zack", 159 }, { 28, "Harris", 169 }, { 29, "Kano", 174 }, { 31, "Louis", 185 }, { 32, "Greg", 190 },
    { 34, "Stu", 200 }, { 35, "Won", 247 },
};
#define PEOPLE_N ((int)(sizeof people / sizeof *people))
#define GIRLS 5                     /* the first five: their hearts are the love's colour */

static int top[3], sel[3], here[3]; /* the first row shown, the cursor, the page shown last frame (per run) */
static int face_loaded[3] = { -1, -1, -1 };    /* the portrait in VRAM (per run: each has its GPU) */
static uint32_t glyph_args[6];      /* a glyph's stack arguments seen last (+16 to +36) */
static int glyph_known, title_kept;
static uint8_t kept[TEXT_MAX * 2];

static void keep(void) { memcpy(kept, &g_ram[TEXT_BUF & 0x1FFFFF], sizeof kept); }
static void put_back(void) { memcpy(&g_ram[TEXT_BUF & 0x1FFFFF], kept, sizeof kept); }


static int slot(void) { return g_machine ? 2 : g_view2_run ? 1 : 0; }

void friends_glyph_seen(CPU *c) {
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

/* the game's letters: A-Z 0.., a-z 0x20.., 0-9 0x40.., space 0FFD, the end FFFF */
static uint32_t put_text(uint32_t at, const char *s) {
    for (int n = 0; *s && n < TEXT_MAX - 1; s++, n++) {
        char ch = *s;
        int g = pt_glyph(&s);
        uint16_t h = g >= 0 ? (uint16_t)g
            : ch >= 'A' && ch <= 'Z' ? (uint16_t)(ch - 'A')
            : ch >= 'a' && ch <= 'z' ? (uint16_t)(0x20 + ch - 'a')
            : ch >= '0' && ch <= '9' ? (uint16_t)(0x40 + ch - '0')
            : ch == '-' ? 0x69 : ch == '+' ? 0x68 : ch == '.' ? 0x6C : 0x0FFD;
        MEM_W16(at, h);
        at += 2;
    }
    MEM_W16(at, 0xFFFF);
    return at + 2;
}

static const char *word(int i) {
    static const char *const en[] = { "Friendships", "Talked", "Gift", "Doctor", "of" };
    static const char *const pt[] = { "Amizades da cidade", "Falou", "Presente", "Doutor", "de" };
    static const char *const id[] = { "Pertemanan", "Bicara", "Hadiah", "Dokter", "dari" };
    static const char *const es[] = { "Amistades", "Habló", "Regalo", "Doctor", "de" };
    return GAME_PT ? pt[i] : g_game_lang == 2 ? id[i] : g_game_lang == 4 ? es[i] : en[i];
}

/* the title's text, where the menu's routine is about to draw it (a1 at 800FC220) */
uint32_t friend_title(CPU *c, uint32_t text) {
    if (!g_cfg.friendpage || (int16_t)MEM_R16(PAGE) != FRIEND_PAGE) return text;
    keep();
    title_kept = 1;
    put_text(TEXT_BUF, word(0));
    return TEXT_BUF;
}

/* ... back from drawing it (800FC23C) */
void friend_title_drawn(void) {
    if (title_kept) put_back();
    title_kept = 0;
}

/* Right past the last page (v0 = page < 8 at 800FE9E0) and Left before the first (v0 = the page,
 * at 800FEAB0, before it is made 7 when below 0) */
uint32_t friend_next_wrap(uint32_t lt) {
    if (!g_cfg.friendpage) return lt;
    return (int16_t)MEM_R16(PAGE) < PAGES + 1;
}

uint32_t friend_prev_wrap(uint32_t page) {
    if (!g_cfg.friendpage || (int32_t)page >= 0) return page;
    MEM_W16(PAGE, FRIEND_PAGE);
    return FRIEND_PAGE;
}

static void text(CPU *c, uint32_t ot, uint32_t buf, const char *s, int x, int y) {
    (void)buf;
    keep();
    put_text(TEXT_BUF, s);
    call4(c, TEXT_AT, ot, TEXT_BUF, (uint32_t)x, (uint32_t)y);
    put_back();
}

static uint32_t glyph(CPU *c, uint32_t ot, uint32_t p, int letter, int x, int y, uint32_t pal, uint32_t rgb) {
    uint32_t sp = c->r[29];
    MEM_W32(sp + 16, (uint32_t)y);
    MEM_W32(sp + 20, rgb & 0xFF);
    MEM_W32(sp + 24, rgb >> 8 & 0xFF);
    MEM_W32(sp + 28, rgb >> 16 & 0xFF);
    MEM_W32(sp + 32, pal);
    MEM_W32(sp + 36, 0);
    stamina_glyph_others(1);        /* the stamina window copies the game's last letter's colour */
    uint32_t next = call4(c, GLYPH_AT, ot, p, (uint32_t)letter, (uint32_t)x);
    stamina_glyph_others(0);
    return next;
}

#define RGB(r, g, b) ((uint32_t)(b) << 16 | (uint32_t)(g) << 8 | (uint32_t)(r))

/* a bachelorette's love: the heart's colour (black, purple, blue, green, yellow, orange, red), the
 * love at which each colour starts */
static int love_colour(uint32_t love) {
    static const uint32_t at[] = { 5000, 20000, 30000, 40000, 50000, 60000 };
    int i = 0;
    while (i < 6 && love >= at[i]) i++;
    return i;
}

/* the font's palettes (80049044): 3 the menus' dark ink, 4 red, 0 black, 1 a light ink that takes
 * the colour it is drawn with (the glyph's colour is a multiplier, 128 = as it is) */
#define PAL_RED 4
#define PAL_BLACK 0
#define PAL_LIGHT 1
#define PAL_INK 3
#define INK 206                     /* the light ink's fill (206, 239, 222) */
#define TINT(v) ((uint32_t)((v) * 128 / INK > 255 ? 255 : (v) * 128 / INK))

static void hearts_of(int who, int *hearts, uint32_t *pal, uint32_t *rgb) {
    static const uint8_t colours[][3] = { { 0, 0, 0 }, { 176, 96, 224 }, { 72, 136, 248 },
                                          { 72, 208, 96 }, { 248, 224, 64 }, { 248, 152, 56 },
                                          { 0, 0, 0 } };
    static const int girl_hearts[] = { 0, 2, 3, 5, 7, 8, 10 };
    uint32_t rec = PEOPLE + people[who].id * PERSON_SIZE;
    *pal = PAL_RED;
    *rgb = RGB(128, 128, 128);
    if (who < GIRLS) {
        int col = love_colour(MEM_R16(rec + 4));
        *hearts = girl_hearts[col];
        if (col == 0) {
            *pal = PAL_BLACK;
            *hearts = 1;            /* the black heart: one, so its colour shows */
        } else if (col < 6) {
            *pal = PAL_LIGHT;
            *rgb = RGB(TINT(colours[col][0]), TINT(colours[col][1]), TINT(colours[col][2]));
        }
    } else {
        int f = (int16_t)MEM_R16(rec + 2);
        if (f < 0) f = 0;
        if (f > 255) f = 255;
        *hearts = f / 25;
    }
}

static void row(CPU *c, uint32_t ot, uint32_t buf, int who, int y) {
    const char *name = people[who].id == 8 ? word(3) : people[who].name;
    text(c, ot, buf, name, LIST_X, y);
    int hearts;
    uint32_t pal, rgb;
    hearts_of(who, &hearts, &pal, &rgb);
    if (glyph_known) {
        uint32_t p = call4(c, ALLOC_AT, 10 * GLYPH_SIZE, 0, 0, 0);
        for (int i = 0; i < 10; i++)
            p = i < hearts ? glyph(c, ot, p, HEART_FULL, HEARTS_X + i * 9, y, pal, rgb)
                           : glyph(c, ot, p, HEART_EMPTY, HEARTS_X + i * 9, y, PAL_INK, RGB(128, 128, 128));
    }
}

/* an untextured rectangle (GP0 60h) first in the table's entry ot (drawn before what is there) */
static void tile(CPU *c, uint32_t ot, int x, int y, int w, int h, uint32_t rgb) {
    uint32_t p = call4(c, ALLOC_AT, 16, 0, 0, 0);
    MEM_W32(p, 3u << 24 | (MEM_R32(ot) & 0xFFFFFFu));
    MEM_W32(ot, (MEM_R32(ot) & 0xFF000000u) | (p & 0xFFFFFFu));
    MEM_W32(p + 4, 0x60000000u | rgb);
    MEM_W32(p + 8, (uint32_t)(uint16_t)y << 16 | (uint16_t)x);
    MEM_W32(p + 12, (uint32_t)h << 16 | (uint32_t)w);
    HUD_BEGIN();
    SPRITE_TAG(p + 8);
}

/* the portrait from the disc into VRAM (where the conversations' goes) */
static uint32_t a_file_lba;

static int face_load(CPU *c, int face) {
    int s = slot();
    if (face_loaded[s] == face) return 1;
    if (!a_file_lba) {
        uint32_t lba, size;
        if (!iso_find_file("A_FILE.BIN", &lba, &size)) return 0;
        a_file_lba = lba;
    }
    static uint8_t data[PORTRAIT_SIZE], aside[PORTRAIT_SIZE];
    uint32_t at = MEM_R32(FILE_TABLE + 40 + 36) + (uint32_t)face * PORTRAIT_SIZE;
    uint8_t sec[2352];
    for (uint32_t k = 0; k < PORTRAIT_SIZE; k += 2048) {
        if (!cdrom_read_sector_raw(a_file_lba + (at + k) / 2048u, sec)) return 0;
        memcpy(data + k, sec + 24, 2048);
    }
    if (data[0] != 0x0D) return 0;
    uint8_t *ram = &g_ram[PORTRAIT_BUF & 0x1FFFFF];
    memcpy(aside, ram, sizeof aside);
    memcpy(ram, data, sizeof data);
    uint32_t sp = c->r[29];
    MEM_W32(sp + 16, 0);
    MEM_W32(sp + 20, 0);
    call4(c, UNPACK_AT, PORTRAIT_BUF, 0, 0, 0);
    memcpy(ram, aside, sizeof aside);
    MEM_W16(c->r[28] + GP_PORTRAIT, 0xFFFF);    /* the conversations load theirs again */
    face_loaded[s] = face;
    return 1;
}

/* the page's routine, loaded at 800FC2D4 (v1): on the new page, the page is drawn and the routine
 * called is an empty one */
uint32_t friend_page(CPU *c, uint32_t routine) {
    int s = slot();
    if (!g_cfg.friendpage || (int16_t)MEM_R16(PAGE) != FRIEND_PAGE) {
        here[s] = 0;
        return routine;
    }
    uint32_t regs[32], hi = c->hi, lo = c->lo, sp = c->r[29];
    memcpy(regs, c->r, sizeof regs);
    if (!here[s]) {
        top[s] = sel[s] = 0;
        face_loaded[s] = -1;        /* a conversation may have had the place since */
    }
    here[s] = 1;
    uint32_t pressed = MEM_R32(MEM_R32(PAD_NEW));
    int was = sel[s];
    if (pressed & PAD_UP) sel[s]--;
    if (pressed & PAD_DOWN) sel[s]++;
    if (sel[s] < 0) sel[s] = 0;
    if (sel[s] > PEOPLE_N - 1) sel[s] = PEOPLE_N - 1;
    if (sel[s] < top[s]) top[s] = sel[s];
    if (sel[s] > top[s] + ROWS - 1) top[s] = sel[s] - ROWS + 1;
    uint32_t fsp = sp - 0x100, buf = TEXT_BUF;
    if (sel[s] != was) {
        c->r[29] = fsp;
        call4(c, SOUND_AT, 5, 0, 0, 0);
    }
    uint32_t base = MEM_R32(MEM_R32(GFX) + 216), ot = base + 84;
    for (int i = 0; i < ROWS; i++) {
        c->r[29] = fsp;
        row(c, ot, buf, top[s] + i, LIST_Y + ROW_H * i);
    }
    c->r[29] = fsp;
    tile(c, base + 100, LIST_X - 6, LIST_Y + ROW_H * (sel[s] - top[s]) - 1, HEARTS_X + 90 - LIST_X + 10, ROW_H,
         RGB(232, 196, 136));
    int who = sel[s];
    c->r[29] = fsp;
    if (face_load(c, people[who].face)) {
        c->r[29] = fsp;
        call4(c, PORTRAIT_AT, ot, PANEL_X, PORTRAIT_Y, people[who].face);
    }
    uint16_t today = MEM_R16(PEOPLE + people[who].id * PERSON_SIZE + 30);
    char line[24];
    snprintf(line, sizeof line, "%s %s", word(1), today & 0x400 ? "+" : "-");
    c->r[29] = fsp;
    text(c, ot, buf, line, PANEL_X, PORTRAIT_Y + 86);
    snprintf(line, sizeof line, "%s %s", word(2), today & 0x200 ? "+" : "-");
    c->r[29] = fsp;
    text(c, ot, buf, line, PANEL_X, PORTRAIT_Y + 100);
    snprintf(line, sizeof line, "%d %s %d", who + 1, word(4), PEOPLE_N);
    c->r[29] = fsp;
    text(c, ot, buf, line, PANEL_X, LIST_Y + ROW_H * (ROWS - 1));
    memcpy(c->r, regs, sizeof regs);
    c->hi = hi;
    c->lo = lo;
    return NOTHING;
}

/* Birthdays on the calendar (birthdays): the house's calendar (X on it: GF_FARM 800F9D5C) shows
 * this season's 30 days; a heart by the days someone has a birthday, and X on a day (its events,
 * 800FA408: the text 988 "nothing special", a festival's or 989 your birthday, shown with
 * 8002261C) names them too. The birthdays are the game's own: the people's records +36 the
 * season, +38 the day (0-29); the Harvest Sprites' (8007A638, 0xF8 each) +196 / +198.
 *
 * The calendar (80126F7C open, 80126F84 the cursor's day): its routine at 800F9FF4 moves the
 * cursor and draws it (800FA4F4: a0 the ordering table's entry + 80) each frame; the days are on
 * a grid of 41 x 14 from 30,80 (the week day of day d: (80070CF6 + d - 80070CF4) mod 7). */
#define CAL_OPEN 0x80126F7Cu
#define CAL_CURSOR 0x80126F84u
#define SEASON 0x80070CF2u
#define DAY 0x80070CF4u
#define WEEKDAY 0x80070CF6u
#define SPRITES 0x8007A638u
#define SPRITE_SIZE 0xF8u
#define EVENTS_TEXT_RA 0x800FA4C8u  /* 8002261C called for the day's events */
#define NOTHING_TEXT 988
#define SHOWN_TEXT 0x8006E110u      /* the text being shown (gp+828), halfwords */
#define SHOWN_MAX 0x800u
#define MARK_DX 19                  /* the heart from the day's cell */
#define MARK_DY (-5)

static const char *const sprite_names[] = { "Chef", "Nappy", "Hoggy", "Timid", "Aqua", "Bold", "Staid" };

/* who has a birthday on day d (0-29) of season s: their names, at most max */
static int born_on(int s, int d, const char **names, int max) {
    int n = 0;
    for (int i = 0; i < PEOPLE_N && n < max; i++) {
        uint32_t rec = PEOPLE + people[i].id * PERSON_SIZE;
        if ((int16_t)MEM_R16(rec + 36) == s && (int16_t)MEM_R16(rec + 38) == d)
            names[n++] = people[i].id == 8 ? word(3) : people[i].name;
    }
    for (int i = 0; i < 7 && n < max; i++) {
        uint32_t rec = SPRITES + (uint32_t)i * SPRITE_SIZE;
        if ((int16_t)MEM_R16(rec + 196) == s && (int16_t)MEM_R16(rec + 198) == d) names[n++] = sprite_names[i];
    }
    return n;
}

static int first_weekday(void) {
    int w = ((int16_t)MEM_R16(WEEKDAY) - (int16_t)MEM_R16(DAY)) % 7;
    return w < 0 ? w + 7 : w;
}

/* after the cursor is drawn (800FA1B8): the hearts */
void birthday_marks(CPU *c) {
    if (!g_cfg.birthdays || !MEM_R32(CAL_OPEN)) return;
    int s = (int16_t)MEM_R16(SEASON), first = first_weekday();
    uint32_t regs[32], hi = c->hi, lo = c->lo;
    memcpy(regs, c->r, sizeof regs);
    uint32_t fsp = c->r[29] - 0x100;
    uint32_t ot = MEM_R32(MEM_R32(GFX) + 216) + 80;
    for (int d = 0; d < 30; d++) {
        const char *who[1];
        if (!born_on(s, d, who, 1)) continue;
        int cell = d + first;
        c->r[29] = fsp;
        uint32_t p = call4(c, ALLOC_AT, GLYPH_SIZE, 0, 0, 0);
        c->r[29] = fsp;
        glyph(c, ot, p, HEART_FULL, 30 + cell % 7 * 41 + MARK_DX, 80 + cell / 7 * 14 + MARK_DY, PAL_RED,
              RGB(128, 128, 128));
    }
    memcpy(c->r, regs, sizeof regs);
    c->hi = hi;
    c->lo = lo;
}

/* the day's events asked for (8002261C's entry, a1 the text): the names are put in once it is
 * loaded */
static int bday_n[3], bday_alone[3];
static const char *bday_names[3][8];

void birthday_text_asked(CPU *c) {
    int k = slot();
    bday_n[k] = 0;
    if (!g_cfg.birthdays || c->r[31] != EVENTS_TEXT_RA) return;
    bday_n[k] = born_on((int16_t)MEM_R16(SEASON), (int)MEM_R32(CAL_CURSOR), bday_names[k], 8);
    bday_alone[k] = c->r[5] == NOTHING_TEXT;
}

static uint32_t put_bday(uint32_t at, const char *s) {
    for (; *s && at < SHOWN_MAX - 2; s++) {
        char ch = *s;
        int g = pt_glyph(&s);
        uint16_t h;
        if (g >= 0) h = (uint16_t)g;
        else if (ch >= 'A' && ch <= 'Z') h = (uint16_t)(ch - 'A');
        else if (ch >= 'a' && ch <= 'z') h = (uint16_t)(0x20 + ch - 'a');
        else if (ch == '\'' && s[1] == 's') { s++; h = 0x51; }   /* 's is a glyph of its own */
        else if (ch == '\'') h = 0x8D;
        else if (ch == '\n') h = 0xFFFE;
        else h = 0x0FFD;
        MEM_W16(SHOWN_TEXT + at * 2, h);
        at++;
    }
    return at;
}

/* a text was loaded (80022A94): the birthdays in place of "nothing special", or after the events */
void birthday_text_loaded(void) {
    int k = slot();
    int n = bday_n[k];
    bday_n[k] = 0;
    if (!n) return;
    uint32_t at = 0;                /* past the window's codes */
    for (uint16_t h; at < SHOWN_MAX && (h = MEM_R16(SHOWN_TEXT + at * 2)) >= 0x0FF0 && h != 0x0FFD && h != 0xFFFF;) at++;
    if (!bday_alone[k]) {
        while (at < SHOWN_MAX - 2 && MEM_R16(SHOWN_TEXT + at * 2) != 0xFFFF) at++;
        at = put_bday(at, "\n");
    }
    for (int i = 0; i < n; i++) {
        char line[48];
        const char *name = bday_names[k][i];
        if (GAME_PT) snprintf(line, sizeof line, "Anivers\xC3\xA1rio de %s", name);
        else if (g_game_lang == 2) snprintf(line, sizeof line, "Ulang tahun %s", name);
        else if (g_game_lang == 3) snprintf(line, sizeof line, "%s hat Geburtstag", name);
        else if (g_game_lang == 4) snprintf(line, sizeof line, "Cumpleaños de %s", name);
        else snprintf(line, sizeof line, "%s's birthday", name);
        if (i) at = put_bday(at, "\n");
        at = put_bday(at, line);
    }
    MEM_W16(SHOWN_TEXT + at * 2, 0xFFFF);
}
