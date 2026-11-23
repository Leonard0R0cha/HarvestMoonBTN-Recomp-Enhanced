/* Marks over the animals (animalmarks): over each cow, sheep, chicken, the horse and the dog, an
 * icon for each care still left today (talk, brush, milk, shear, feed); an animal fully cared for
 * shows nothing. Hooks emitted by tools/recomp.py.
 *
 * The animals: the cows and sheep are 20 records of 184 bytes at 800749F4 (+0 the kind: 354 a grown
 * cow, 357 a grown sheep; +48 the area's kind; +52 the flags; +56 / +60 / +64 x / y / z; +174 and
 * +178 nonzero when it is sick or with young: no milk, no wool; +180 the wool, ready from 7), the
 * chickens 10 of 180 at 800742EC (265 a grown hen; the hen house is area kind 4, its feed boxes the
 * 10 bytes at 8007130E, nonzero when filled), the dog at 800759E4 and the horse at 80075AD4. The
 * game's own flags at +52: 0x2 talked to today (800DFA38, 800E1610, the horse 80109B64, the dog
 * 8010BA38), 0x4 brushed (800DF6F0, 800E129C, 80109EB0), 0x8 milked / sheared (800DF8B8, 800EDB24),
 * 0x1 a hen fed (800E2180), 0x400 not shown; all cleared by the night.
 *
 * Each animal's draw routine ends with its sprite (800CFE40 / 800D0198, a0 = s0 the record); the
 * marks are drawn right after (s0 still the record): the point over its back is projected with the
 * game's object projection (8001DFD4: a0 -> x, y; a1 -> x, y, z halfwords; v0 the depth / 4, 0 when
 * behind), which also records the anchor, so in widescreen the icons stay over the animal (tagged as
 * a sprite at that point). They go in the world's ordering table (*(8005E3C0) + 288) just in front
 * of the animal, from the frame's primitive buffer (80011F5C; never past its end: it stops the game).
 * Not shown while an event, a talk or a menu holds the game (8006C844 & 0x18800000, 8006C904), nor
 * on the horse while it is ridden (80071A6C & 0x100).
 *
 * The icons are our own (12x12, 16 colours), sent to VRAM the game never uses: 928-947 x 32-43, in
 * the HUD font's texture page (896, 4-bit), their palette at 752,480. Sent again each frame they are
 * drawn (a CPU -> VRAM transfer, as the game's loads), in case something cleared it. */
#include "psx.h"
#include "psx_recomp.h"
#include "gpu.h"
#include <string.h>

#define CHICKENS 0x800742ECu
#define CHICKEN_SIZE 180u
#define CHICKEN_COUNT 10u
#define ANIMALS 0x800749F4u
#define ANIMAL_SIZE 184u
#define ANIMAL_COUNT 20u
#define DOG 0x800759E4u
#define HORSE 0x80075AD4u
#define COOP_FEED 0x8007130Eu
#define COOP_KIND 4
#define FARM_KIND 1
#define HEN 265
#define COW 354
#define SHEEP 357
#define WOOL_READY 7
#define HELD 0x8006C844u
#define HELD_MASK 0x18800000u
#define HELD_B 0x8006C904u
#define RIDING 0x80071A6Cu
#define PROJECT_AT 0x8001DFD4u
#define ALLOC_AT 0x80011F5Cu
#define GFX 0x8005E3C0u             /* -> the frame's drawing state: +288 the world's ordering table */
#define PRIM_AT 0x800A4060u         /* where the frame's primitive buffer is filled to */
#define OT_SIZE 2048
#define LIFT 100                    /* the point over the animal's back */
#define SHIFT_X 150                 /* ... from the record's position to the middle of the animal */
#define SHIFT_Z 150
#define ABOVE 38                    /* the icons over that point; a grown cow and the horse are taller */
#define ABOVE_TALL 52

/* the icons in VRAM */
#define ICON 12
#define CELL 16                     /* pixels between icons in VRAM (4 halfwords) */
#define STEP 13                     /* between icons on screen */
#define VRAM_X 928
#define VRAM_Y 32
#define PAGE_X 896
#define CLUT_X 752
#define CLUT_Y 480
#define PRIM_SIZE 24u               /* tag, draw mode, sprite (4 words) */
#define DRAW_MODE 0xE100060Eu       /* page 896,0, 4-bit, dithered, as the HUD's letters */

enum { TALK, BRUSH, MILK, SHEAR, FEED, CARES };

static const uint8_t palette[16][3] = {
    { 0, 0, 0 },                    /* . clear */
    { 24, 24, 32 },                 /* # outline */
    { 248, 248, 248 },              /* w white */
    { 184, 188, 200 },              /* l light grey */
    { 120, 124, 140 },              /* m grey */
    { 112, 64, 32 },                /* B dark wood */
    { 184, 120, 64 },               /* b wood */
    { 232, 204, 128 },              /* t bristles */
    { 64, 120, 232 },               /* U blue */
    { 152, 200, 255 },              /* u light blue */
    { 224, 56, 56 },                /* r red */
    { 248, 216, 72 },               /* y yellow */
    { 96, 184, 72 },                /* g green */
    { 255, 144, 176 },              /* p pink */
    { 216, 180, 124 },              /* s sack */
    { 40, 120, 40 },                /* G dark green */
};

static const char *const icons[CARES][ICON] = {
    {   /* talk: a speech bubble */
        "..########..",
        ".#wwwwwwww#.",
        "#wwwwwwwwww#",
        "#wwwwwwwwww#",
        "#w##w##w##w#",
        "#w##w##w##w#",
        "#wwwwwwwwww#",
        ".#wwwwwwww#.",
        "..###ww###..",
        "....#w#.....",
        "....##......",
        "............" },
    {   /* brush */
        "............",
        "..########..",
        ".#bbbbbbbb#.",
        ".#b#bbbb#b#.",
        ".#bbbbbbbb#.",
        ".##########.",
        ".#tt#tt#tt#.",
        ".#tt#tt#tt#.",
        ".#tt#tt#tt#.",
        ".#tt#tt#tt#.",
        "..#t##t##t..",
        "............" },
    {   /* milk: a bottle */
        "....####....",
        "....#UU#....",
        "....####....",
        "....#ww#....",
        "...#wwww#...",
        "..#wwwwww#..",
        "..#wUUUUw#..",
        "..#wUUUUw#..",
        "..#wwwwww#..",
        "..#wwwwww#..",
        "..#lwwwwl#..",
        "...######..." },
    {   /* shear: scissors */
        ".#........#.",
        ".#l#....#l#.",
        "..#l#..#l#..",
        "...#l##l#...",
        "....#ll#....",
        ".....##.....",
        "...##..##...",
        "..#rr##rr#..",
        ".#r#r##r#r#.",
        ".#rr#..#rr#.",
        "..##....##..",
        "............" },
    {   /* feed: a sack of grain */
        "....#..#....",
        "...#s##s#...",
        "....#ss#....",
        "...#ssss#...",
        "..#ssssss#..",
        ".#ssssssss#.",
        ".#ssyyyyss#.",
        ".#syyyyyys#.",
        ".#ssyyyyss#.",
        ".#ssssssss#.",
        ".#ssssssss#.",
        "..########.." },
};

static int colour_of(char ch) {
    static const char keys[] = ".#wlmBbtUurygpsG";      /* by the palette */
    const char *k = strchr(keys, ch);
    return k && ch ? (int)(k - keys) : 0;
}

#define VRAM_W (CARES * CELL / 4)   /* halfwords */
static uint16_t pixels[ICON][VRAM_W], clut[16];
static int made;

static void make_icons(void) {
    for (int i = 0; i < 16; i++)
        clut[i] = i ? (uint16_t)(palette[i][0] >> 3 | (palette[i][1] >> 3) << 5 | (palette[i][2] >> 3) << 10) : 0;
    memset(pixels, 0, sizeof pixels);
    for (int k = 0; k < CARES; k++)
        for (int y = 0; y < ICON; y++)
            for (int x = 0; x < ICON; x++) {
                int px = k * CELL + x;
                pixels[y][px / 4] |= (uint16_t)(colour_of(icons[k][y][x]) << (px % 4 * 4));
            }
    made = 1;
}

/* the icons into VRAM, once a frame for each run (the second window's has its own GPU) */
static void send_icons(void) {
    static uint64_t sent[3] = { ~0ull, ~0ull, ~0ull };
    int s = g_machine ? 2 : g_view2_run ? 1 : 0;
    if (sent[s] == frame_count()) return;
    sent[s] = frame_count();
    if (!made) make_icons();
    gpu_write_gp0(0xA0000000u);
    gpu_write_gp0((uint32_t)VRAM_Y << 16 | VRAM_X);
    gpu_write_gp0((uint32_t)ICON << 16 | VRAM_W);
    for (int y = 0; y < ICON; y++)
        for (int x = 0; x < VRAM_W; x += 2)
            gpu_write_gp0(pixels[y][x] | (uint32_t)pixels[y][x + 1] << 16);
    gpu_write_gp0(0xA0000000u);
    gpu_write_gp0((uint32_t)CLUT_Y << 16 | CLUT_X);
    gpu_write_gp0(1u << 16 | 16);
    for (int i = 0; i < 16; i += 2)
        gpu_write_gp0(clut[i] | (uint32_t)clut[i + 1] << 16);
}

static uint32_t call4(CPU *c, uint32_t at, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    c->r[4] = a0;
    c->r[5] = a1;
    c->r[6] = a2;
    c->r[7] = a3;
    call_addr(c, at);
    return c->r[2];
}

static int in_table(uint32_t rec, uint32_t at, uint32_t size, uint32_t count) {
    uint32_t o = rec - at;
    return o < size * count && o % size == 0;
}

/* the hen house: fewer feed boxes filled than hens in it */
static int coop_hungry(void) {
    int hens = 0, fed = 0;
    for (uint32_t i = 0; i < CHICKEN_COUNT; i++) {
        uint32_t r = CHICKENS + i * CHICKEN_SIZE;
        if (MEM_R16(r) == HEN && (int16_t)MEM_R16(r + 48) == COOP_KIND && !(MEM_R32(r + 52) & 0x401)) hens++;
    }
    for (uint32_t i = 0; i < 10; i++)
        if (MEM_R8(COOP_FEED + i)) fed++;
    return fed < hens;
}

/* the cares still left today (bits by the enum), *tall for the taller animals */
static unsigned cares_left(uint32_t rec, int *tall) {
    uint32_t f = MEM_R32(rec + 52);
    unsigned need = 0;
    *tall = 0;
    if (rec == DOG) return f & 6 ? 0 : 1u << TALK;
    if (rec == HORSE) {
        *tall = 1;
        need = 1u << TALK | 1u << BRUSH;
    } else if (in_table(rec, CHICKENS, CHICKEN_SIZE, CHICKEN_COUNT)) {
        if (MEM_R16(rec) != HEN || f & 1) return 0;
        int area = (int16_t)MEM_R16(rec + 48);
        return (area == COOP_KIND ? coop_hungry() : area == FARM_KIND) ? 1u << FEED : 0;
    } else if (in_table(rec, ANIMALS, ANIMAL_SIZE, ANIMAL_COUNT)) {
        uint16_t kind = MEM_R16(rec);
        need = 1u << TALK | 1u << BRUSH;
        *tall = kind == COW;
        if (!MEM_R16(rec + 174) && !MEM_R16(rec + 178)) {
            if (kind == COW && !(f & 8)) need |= 1u << MILK;
            if (kind == SHEEP && MEM_R16(rec + 180) >= WOOL_READY) need |= 1u << SHEAR;
        }
    } else
        return 0;
    if (f & 2) need &= ~(1u << TALK);
    if (f & 4) need &= ~(1u << BRUSH);
    return need;
}

/* one icon (a sprite from the HUD's page, at vx, vy in VRAM) first in the table's entry ot */
static void icon(uint32_t ot, uint32_t p, int vx, int vy, int x, int y, int sx, int sy) {
    MEM_W32(p, 5u << 24 | (MEM_R32(ot) & 0xFFFFFFu));
    MEM_W32(ot, (MEM_R32(ot) & 0xFF000000u) | (p & 0xFFFFFFu));
    MEM_W32(p + 4, DRAW_MODE);
    MEM_W32(p + 8, 0x64808080u);
    MEM_W32(p + 12, (uint32_t)(uint16_t)y << 16 | (uint16_t)x);
    uint32_t u = (uint32_t)(vx - PAGE_X) * 4;     /* vx, vy: the icon in VRAM (4-bit: 4 pixels a halfword) */
    MEM_W32(p + 16, (uint32_t)(CLUT_Y << 6 | CLUT_X >> 4) << 16 | (uint32_t)vy << 8 | u);
    MEM_W32(p + 20, (uint32_t)ICON << 16 | ICON);
    if (g_sprite_tags) {            /* placed, scaled and squeezed with the animal's point */
        gpu_sprite_begin((int16_t)sx, (int16_t)sy);
        SPRITE_TAG(p + 12);
    }
}

/* after an animal's sprite is built (s0 the record) */
void animal_marks(CPU *c) {
    if (!g_cfg.animalmarks) return;
    uint32_t rec = c->r[16];
    if (rec != DOG && rec != HORSE && !in_table(rec, CHICKENS, CHICKEN_SIZE, CHICKEN_COUNT) &&
        !in_table(rec, ANIMALS, ANIMAL_SIZE, ANIMAL_COUNT)) return;
    if (!MEM_R16(rec) || MEM_R32(rec + 52) & 0x400) return;
    if (MEM_R32(HELD) & HELD_MASK || MEM_R8(HELD_B)) return;
    if (rec == HORSE && MEM_R32(RIDING) & 0x100) return;
    int tall;
    unsigned need = cares_left(rec, &tall);
    int n = 0;
    for (int i = 0; i < CARES; i++) n += need >> i & 1;
    if (!n) return;

    uint32_t regs[32], hi = c->hi, lo = c->lo;
    memcpy(regs, c->r, sizeof regs);
    uint32_t fsp = c->r[29] - 0x100;    /* the calls' frames below; the point and the screen spot above */
    uint32_t pos = fsp + 0x40, spot = fsp + 0x48;
    MEM_W16(pos, (uint16_t)(MEM_R16(rec + 56) + SHIFT_X));
    MEM_W16(pos + 2, (uint16_t)(MEM_R16(rec + 60) - LIFT));
    MEM_W16(pos + 4, (uint16_t)(MEM_R16(rec + 64) - SHIFT_Z));
    MEM_W16(pos + 6, 0);
    c->r[29] = fsp;
    int32_t depth = (int32_t)call4(c, PROJECT_AT, spot, pos, 0, 0);
    int sx = (int32_t)MEM_R32(spot), sy = (int32_t)MEM_R32(spot + 4);
    int width = n * STEP - (STEP - ICON);
    int x = sx - width / 2, y = sy - (tall ? ABOVE_TALL : ABOVE);
    uint32_t base = MEM_R32(c->r[28] + 332);
    uint32_t end = MEM_R32(base + 300) + MEM_R32(base + 296);
    if (depth > 0 && x >= 4 && x + width <= 316 && y >= 4 && y <= 212 &&
        MEM_R32(PRIM_AT) + (uint32_t)n * PRIM_SIZE + 64 <= end) {
        send_icons();
        int at = depth / 4 - 1;
        at = at < 0 ? 0 : at >= OT_SIZE ? OT_SIZE - 1 : at;
        uint32_t ot = MEM_R32(MEM_R32(GFX) + 288) + (uint32_t)at * 4;
        c->r[29] = fsp;
        uint32_t p = call4(c, ALLOC_AT, (uint32_t)n * PRIM_SIZE, 0, 0, 0);
        for (int i = 0; i < CARES; i++) {
            if (!(need >> i & 1)) continue;
            icon(ot, p, VRAM_X + i * CELL / 4, VRAM_Y, x, y, sx, sy);
            p += PRIM_SIZE;
            x += STEP;
        }
    }
    memcpy(c->r, regs, sizeof regs);
    c->hi = hi;
    c->lo = lo;
}

/* A letter over the mailbox (mailmark): while a letter waits in it, an envelope over the farm's
 * mailbox. The mail waiting is a queue of 20 entries of 4 bytes at 8007121C (+0 the text, +2 -1 when
 * empty); X on the mailbox (800F24B0, from the X routine 800C7AC0) reads the first and moves the
 * rest up (800F2724). The mailbox is the area's X spot (8009F78C + 16, 50 x 44 bytes) of kind 4000
 * and number 6; the envelope goes over the middle of its box (+14 / +18 .. +20 / +24). Drawn after the
 * farmer is drawn (800C9E90, on the horse 800CA580), once a frame for each run, as the animals' marks. Its picture is in
 * VRAM the game never uses (924-927 x 13-24, the HUD font's page as the others), with their palette. */
#define MAIL_QUEUE 0x8007121Cu
#define SPOTS 0x8009F79Cu
#define SPOT_SIZE 44u
#define SPOT_COUNT 50u
#define SPOT_KIND 0x4000
#define MAILBOX 6
#define MAIL_LIFT 260               /* the point over the mailbox */
#define MAIL_ABOVE 12
#define MAIL_X 924
#define MAIL_Y 13

static const char *const envelope[ICON] = {
    "............",
    "############",
    "##wwwwwwww##",
    "#w#wwwwww#w#",
    "#ww#wwww#ww#",
    "#www#ww#www#",
    "#wwww##wwww#",
    "#wwwwrrwwww#",
    "#lwwwrrwwwl#",
    "#llwwwwwwll#",
    "############",
    "............" };

static void send_envelope(void) {
    static uint64_t sent[3] = { ~0ull, ~0ull, ~0ull };
    static uint16_t pic[ICON][4];
    static int drawn;
    int s = g_machine ? 2 : g_view2_run ? 1 : 0;
    if (sent[s] == frame_count()) return;
    sent[s] = frame_count();
    if (!made) make_icons();
    if (!drawn) {
        for (int y = 0; y < ICON; y++)
            for (int x = 0; x < ICON; x++)
                pic[y][x / 4] |= (uint16_t)(colour_of(envelope[y][x]) << (x % 4 * 4));
        drawn = 1;
    }
    gpu_write_gp0(0xA0000000u);
    gpu_write_gp0((uint32_t)MAIL_Y << 16 | MAIL_X);
    gpu_write_gp0((uint32_t)ICON << 16 | 4);
    for (int y = 0; y < ICON; y++)
        for (int x = 0; x < 4; x += 2)
            gpu_write_gp0(pic[y][x] | (uint32_t)pic[y][x + 1] << 16);
    gpu_write_gp0(0xA0000000u);
    gpu_write_gp0((uint32_t)CLUT_Y << 16 | CLUT_X);
    gpu_write_gp0(1u << 16 | 16);
    for (int i = 0; i < 16; i += 2)
        gpu_write_gp0(clut[i] | (uint32_t)clut[i + 1] << 16);
}

/* after the farmer is drawn */
void mail_mark(CPU *c) {
    static uint64_t done[3] = { ~0ull, ~0ull, ~0ull };
    if (!g_cfg.mailmark || MEM_R8(MAIL_QUEUE + 2) == 0xFF) return;
    if (MEM_R32(HELD) & HELD_MASK || MEM_R8(HELD_B)) return;
    int s = g_machine ? 2 : g_view2_run ? 1 : 0;
    if (done[s] == frame_count()) return;  /* the second farmer's sprite too */
    uint32_t e = 0;
    for (uint32_t i = 0; i < SPOT_COUNT && !e; i++) {
        uint32_t at = SPOTS + i * SPOT_SIZE;
        if (MEM_R16(at) == SPOT_KIND && MEM_R16(at + 2) == MAILBOX) e = at;
    }
    if (!e) return;
    done[s] = frame_count();

    uint32_t regs[32], hi = c->hi, lo = c->lo;
    memcpy(regs, c->r, sizeof regs);
    uint32_t fsp = c->r[29] - 0x100;
    uint32_t pos = fsp + 0x40, spot = fsp + 0x48;
    MEM_W16(pos, (uint16_t)(((int16_t)MEM_R16(e + 14) + (int16_t)MEM_R16(e + 20)) / 2));
    MEM_W16(pos + 2, (uint16_t)((int16_t)MEM_R16(e + 16) - MAIL_LIFT));
    MEM_W16(pos + 4, (uint16_t)(((int16_t)MEM_R16(e + 18) + (int16_t)MEM_R16(e + 24)) / 2));
    MEM_W16(pos + 6, 0);
    c->r[29] = fsp;
    int32_t depth = (int32_t)call4(c, PROJECT_AT, spot, pos, 0, 0);
    int sx = (int32_t)MEM_R32(spot), sy = (int32_t)MEM_R32(spot + 4);
    int x = sx - ICON / 2, y = sy - MAIL_ABOVE - ICON;
    uint32_t base = MEM_R32(c->r[28] + 332);
    uint32_t end = MEM_R32(base + 300) + MEM_R32(base + 296);
    if (depth > 0 && x >= 4 && x + ICON <= 316 && y >= 4 && y <= 212 &&
        MEM_R32(PRIM_AT) + PRIM_SIZE + 64 <= end) {
        send_envelope();
        int at = depth / 4 - 1;
        at = at < 0 ? 0 : at >= OT_SIZE ? OT_SIZE - 1 : at;
        uint32_t ot = MEM_R32(MEM_R32(GFX) + 288) + (uint32_t)at * 4;
        c->r[29] = fsp;
        uint32_t p = call4(c, ALLOC_AT, PRIM_SIZE, 0, 0, 0);
        icon(ot, p, MAIL_X, MAIL_Y, x, y, sx, sy);
    }
    memcpy(c->r, regs, sizeof regs);
    c->hi = hi;
    c->lo = lo;
}
