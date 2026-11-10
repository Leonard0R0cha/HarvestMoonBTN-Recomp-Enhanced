/* Fixes to the original game. Hooks emitted by tools/recomp.py. */
#include "psx.h"
#include "psx_recomp.h"
#include <string.h>

/* The farm score on the summary page (the "%" next to the name, 800FB908) adds up to 100:
 * buildings 10, recipes 8, the hearts of wife, child, dog and horse 10 each, chickens 10, cows
 * and sheep 20, Power Berries 10 and the Mystic Berry 2. The page counts the Power Berries (its
 * berry row) but never adds them, nor the Mystic Berry, so a complete farm stops at 88. The sum
 * is in v0 just before it is stored (800FBDF0); this adds them. The score is worked out each
 * time the page opens, so old saves read right at once. */
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
 * nothing; it opens the cabinet (kind 23). An area's things to look at are
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
 * looks ahead, 0 for the step itself, 24 for what X faces; the mask in t2) and the farm's field
 * answers at 800BE6E4 (the query in s1, its mask at +20; the point tested made a field, x / y at
 * sp+32 / sp+34, by 8011E090 in the greenhouse (s0 = 1) or 800D7EF8): a field thing is solid
 * when its picture says so (v0 after 800BE818, with the item's record in v1 and its class byte,
 * +3, in t1). Here the walk's mask gets 0x100000 and a planted crop (record flags 4040h, a class)
 * is not solid then, nor a weed (field 64: record flags 8000h, +14 the item it gives, 91h; the
 * records are 40 bytes from 8004B1A4, stones 58 and branches 62 give 92h / 93h and stay solid).
 * What X faces is tested close to the player: standing on a crop, the crop under the player was
 * what X faced, and a Harvest Sprite or the shipping basket beside could not be reached. That
 * test gets 0x200000: a crop or weed in the player's own field is not solid for it (the one in
 * front still is, to pick it). */
#define CROP_WALK 0x100000u
#define CROP_FACE 0x200000u
#define CROP_FLAGS 0x4040u
#define WEED_FLAGS 0x8000u
#define WEED_ITEM 0x91u
#define PLAYER_POS 0x80071B00u     /* the player's x, y, z (words) */

uint32_t crop_walk_mask(uint32_t mask, uint32_t why) {
    if (!g_cfg.cropwalk) return mask;
    if (why == 56 || why == 0) return mask | CROP_WALK;
    if (why == 24) return mask | CROP_FACE;
    return mask;
}

/* the player's own field, made as the point tested was (0 out of the grid) */
static int own_field(CPU *c) {
    uint32_t regs[32], hi = c->hi, lo = c->lo;
    memcpy(regs, c->r, sizeof regs);
    uint32_t p = regs[29] - 0x40u;             /* below the field's frame, above the callee's */
    for (int k = 0; k < 3; k++) MEM_W32(p + 4u * k, MEM_R32(PLAYER_POS + 4u * k));
    c->r[29] = regs[29] - 0x80u;
    c->r[4] = p;
    c->r[5] = p + 16u;
    c->r[6] = p + 18u;
    call_addr(c, regs[16] ? 0x8011E090u : 0x800D7EF8u);
    int out = c->r[2] != 0;
    int same = !out && MEM_R16(p + 16u) == MEM_R16(regs[29] + 32u) && MEM_R16(p + 18u) == MEM_R16(regs[29] + 34u);
    memcpy(c->r, regs, sizeof regs);
    c->hi = hi;
    c->lo = lo;
    return same;
}

uint32_t crop_walk_solid(CPU *c, uint32_t solid, uint32_t query, uint32_t record, uint32_t cls) {
    uint32_t mask = MEM_R32(query + 20);
    if (!solid || !(mask & (CROP_WALK | CROP_FACE))) return solid;
    int crop = ((int8_t)cls != -1 && MEM_R16(record) == CROP_FLAGS) ||
               (MEM_R16(record) == WEED_FLAGS && MEM_R16(record + 14) == WEED_ITEM);
    if (!crop) return solid;
    if (mask & CROP_WALK) return 0;
    return own_field(c) ? 0 : solid;
}

/* The yellow Harvest Sprite at home (spritechair): the sprites stand at their spots in their house,
 * which 80110E34 copies each tick from a table of 7 (80127EE0, 16 bytes: x, y, z, facing) to the
 * sprite (a0; its spot at +172, its x, y, z at +56). The yellow one's (spot 3, -240, 0, -146) puts
 * its flat picture on the last chair behind the table, so the chair cuts in and out of its feet as
 * the camera moves (on the console too). Its z (a2 after 80110E78) gets SPRITE_BACK: up the screen,
 * away from the table; 90 is the least that clears the seat in the sessions tried. */
#define SPRITE_YELLOW_SPOT 3
#define SPRITE_BACK 100

uint32_t sprite_spot_z(uint32_t z, uint32_t actor) {
    return g_cfg.spritechair && MEM_R16(actor + 172) == SPRITE_YELLOW_SPOT ? z + SPRITE_BACK : z;
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

/* An item's picture (800D6A94: in the hands, the rucksack, the menus) comes from the area's own copy
 * when the area has one (800D688C: a crop of the field or the hothouse), else from the disc. That copy
 * draws the crop in the field: its two halves can sit on different pictures, and the item shown with
 * them gets wrong colours (the hothouse eggplant held up). Such a copy (and
 * always item 235's) is not used: the picture is read from the disc */
#define AREA_PICTURES 0x800A0378u   /* -> the area: +10 its list of pictures (-1 none) */
#define PICTURE_IDS 0x80126254u     /* -> the list's items: a count, then the item numbers (halfwords) */
#define PICTURE_AT 0x80125528u      /* -> where each is, from 800957F8 (words, from the second) */
#define PICTURES_BASE 0x800957F8u
#define PICTURE_CALLER 0x800D6B20u  /* where 800D6A94's call of 800D688C returns */

/* the picture's part (its frame, from the header's tables; 0: none) as 800D6A94 finds it */
static uint32_t picture_part(uint32_t p, uint32_t which) {
    uint32_t frames = p + MEM_R32(p + 24), parts = p + MEM_R32(p + 20);
    uint32_t list = p + MEM_R32(p + 28), cells = p + MEM_R32(p + 32);
    uint32_t h = MEM_R16(cells + MEM_R16(list + which));
    if (h > 0xFFFDu) return 0;
    uint32_t e = parts + h * 4;
    return frames + MEM_R16(e + 2) + (uint32_t)((int16_t)MEM_R16(e) * 12 - 12);
}

static int area_picture_mixed(int16_t item) {
    uint32_t area = MEM_R32(AREA_PICTURES);
    if (!area) return 0;
    int16_t k = (int16_t)MEM_R16(area + 10);
    if (k == -1) return 0;
    uint32_t ids = MEM_R32(PICTURE_IDS + (uint32_t)k * 4), at = MEM_R32(PICTURE_AT + (uint32_t)k * 4);
    if (!ids) return 0;
    int16_t n = (int16_t)MEM_R16(ids);
    for (int i = 1; i <= n; i++) {
        if ((int16_t)MEM_R16(ids + (uint32_t)i * 2) != item) continue;
        uint32_t p = MEM_R32(at + (uint32_t)i * 4) + PICTURES_BASE;
        uint32_t a = picture_part(p, 2), b = picture_part(p, 6);
        if (!a || !b) return 0;
        uint32_t rects = p + MEM_R32(p + 16);
        uint32_t ra = rects + MEM_R16(a + 2) * 8, rb = rects + MEM_R16(b + 2) * 8;
        return MEM_R16(ra) != MEM_R16(rb) || MEM_R16(ra + 2) != MEM_R16(rb + 2);
    }
    return 0;
}

/* entry of 800D688C (a0 = the item): 1 = answer "no copy here" */
int item_picture_from_disc(CPU *c) {
    if (c->r[31] != PICTURE_CALLER) return 0;
    int16_t item = (int16_t)c->r[4];
    return item == 235 || area_picture_mixed(item);
}

/* Bulk food (bulkfood): a thing on a shop's shelf is taken in the hands and 8011C298 asks whether to
 * buy it (its state at 8012D524, the next one at 8012D528; a1 / a2 the question, a3 how it is sold:
 * 0 the shelf's; s1 the item, s3 the price). A yes for food asks how many with
 * the game's own number window (the fodder's: 8011CF40 the number, the text 77; 8012D538: +4 the
 * window's text, +8 the number, +10 the price; 8012D52C = 1 shows it), as many as the rucksack's empty
 * places and the fridge (once the house has the kitchen) can take: one stays in the hands, the rest
 * go to the rucksack, then to the fridge (64 places of 6 bytes at 80070D42: the item, 0, how many;
 * up to 99 in a place). States 15 and 16 are new: the question and the number */
#define SHOP_STATE 0x8012D524u
#define SHOP_NEXT 0x8012D528u
#define SHOP_SHOWN 0x8012D52Cu
#define NUMBER_BOX 0x8012D538u
#define MONEY 0x80071A5Cu
#define BAG_SIZE 0x80071A1Eu        /* the rucksack: 2 << it places, at 80071AC0 */
#define BAG_ITEMS 0x80071AC0u
#define FRIDGE 0x80070D42u
#define HAS_KITCHEN 0x80070D06u
#define ITEMS_TABLE 0x8006C858u     /* -> 40 bytes an item: flags at +24 */
#define FOOD_FLAGS 0xC0000000u      /* food, kept in the fridge */
#define STATE_ASK 15
#define STATE_NUMBER 16
#define TEXT_HOW_MANY 77
#define TEXT_THANKS 187
#define TEXT_NO_MONEY 79
#define TEXT_NOT_BOUGHT 216
#define SET_TEXT 0x8002261Cu
#define TEXT_CLOSE 0x80022738u
#define TEXT_DONE 0x800226D8u
#define TEXT_SOUND 0x80017B18u
#define NUMBER_PICK 0x8011CF40u
#define ITEM_PICTURE 0x800D6A94u

static int bulk_on, bulk_price;
static int16_t bulk_item;
/* the second window's run of the frame (p2view.c) throws its RAM away: so are these */
#define BULK_KEEP int keep_on = bulk_on, keep_price = bulk_price; int16_t keep_item = bulk_item
#define BULK_BACK if (g_view2_run) { bulk_on = keep_on; bulk_price = keep_price; bulk_item = keep_item; }

static void shop_go(uint32_t state, uint32_t next) {
    MEM_W32(SHOP_STATE, state);
    MEM_W32(SHOP_NEXT, next);
}

static int is_food(int16_t item) {
    uint32_t f = MEM_R32(MEM_R32(ITEMS_TABLE) + (uint32_t)item * 40 + 24);
    return (f & 0xF0000000u) == FOOD_FLAGS;
}

/* how many more of the item the rucksack and the fridge take; with put, puts that many there */
static int bulk_room(CPU *c, int16_t item, int put) {
    int room = 0, places = 2 << MEM_R16(BAG_SIZE);
    for (int i = 0; i < places; i++) {
        uint32_t e = BAG_ITEMS + (uint32_t)i * 6;
        if (MEM_R16(e)) continue;
        room++;
        if (put > 0) {
            game_call(c, ITEM_PICTURE, (uint32_t)(int32_t)item, 0, 0, 0);
            MEM_W16(e, (uint16_t)item);
            MEM_W16(e + 2, 0);
            MEM_W16(e + 4, 1);
            put--;
        }
    }
    if (!MEM_R16(HAS_KITCHEN) || !is_food(item)) return room;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < 64; i++) {
            uint32_t e = FRIDGE + (uint32_t)i * 6;
            int16_t id = (int16_t)MEM_R16(e);
            if (pass == 0 ? id != item : id != 0) continue;
            int n = pass == 0 ? 99 - (int16_t)MEM_R16(e + 4) : 99;
            if (n <= 0) continue;
            room += n;
            if (put > 0) {
                if (n > put) n = put;
                if (pass == 1) {
                    MEM_W16(e, (uint16_t)item);
                    MEM_W16(e + 2, 0);
                    MEM_W16(e + 4, 0);
                }
                MEM_W16(e + 4, (uint16_t)(MEM_R16(e + 4) + n));
                put -= n;
            }
        }
    return room;
}

/* right after the yes / no of 8011C298 (8011C470: s0 the answer, 1 yes): a yes for food that more of
 * fit and the money is enough for asks how many; the answer becomes 0 (still asking) */
static uint32_t food_yes(CPU *c, uint32_t answer) {
    int16_t item = (int16_t)c->r[17];
    int price = (int16_t)c->r[19];
    if (!g_cfg.bulkfood || answer != 1 || (int16_t)c->r[18] != 0 || !is_food(item)
        || (int32_t)MEM_R32(MONEY) < price || bulk_room(c, item, 0) < 1)
        return answer;
    bulk_on = 1;
    bulk_item = item;
    bulk_price = price;
    MEM_W32(NUMBER_BOX, 0);
    MEM_W16(NUMBER_BOX + 8, 1);
    MEM_W16(NUMBER_BOX + 10, (uint16_t)price);
    game_call(c, TEXT_CLOSE, 0, 0, 0, 0);
    MEM_W32(NUMBER_BOX + 4, MEM_R32(0x80054BC0u) + 0x8005181Au);
    shop_go(STATE_ASK, 0);
    return 0;
}

/* entry of 8011C298: the new states (1: done here, the routine answers 0, not finished) */
static int food_state(CPU *c) {
    uint32_t state = MEM_R32(SHOP_STATE);
    if (state == 0) bulk_on = 0;
    if (!bulk_on || (state != STATE_ASK && state != STATE_NUMBER)) return 0;
    if (state == STATE_ASK) {
        game_call(c, TEXT_DONE, 0, 0, 0, 0);
        game_call(c, TEXT_SOUND, 0, 0, 0, 0);
        game_call(c, SET_TEXT, 2, TEXT_HOW_MANY, 0, 0);
        shop_go(3, STATE_NUMBER);       /* 3: waits for the text, then the next state */
        return 1;
    }
    MEM_W32(SHOP_SHOWN, 1);
    int most = bulk_room(c, bulk_item, 0) + 1;
    if (most > 99) most = 99;
    uint32_t r = game_call(c, NUMBER_PICK, (uint32_t)most, NUMBER_BOX + 8, 0, 0);
    if (r == 1) {                       /* chosen */
        game_call(c, TEXT_CLOSE, 0, 0, 0, 0);
        if ((int32_t)MEM_R32(MONEY) >= (int16_t)MEM_R16(NUMBER_BOX + 8) * bulk_price) {
            MEM_W32(SHOP_SHOWN, 3);
            game_call(c, SET_TEXT, 2, TEXT_THANKS, 0, 0);
            shop_go(2, 6);              /* 2: waits for the text; 6: pays */
        } else {
            game_call(c, SET_TEXT, 2, TEXT_NO_MONEY, 0, 0);
            shop_go(2, STATE_ASK);
        }
    } else if (r == 3) {                /* cancelled: 7 puts the thing back */
        MEM_W32(SHOP_SHOWN, 3);
        game_call(c, TEXT_CLOSE, 0, 0, 0, 0);
        game_call(c, SET_TEXT, 2, TEXT_NOT_BOUGHT, 0, 0);
        shop_go(2, 7);
        bulk_on = 0;
    }
    return 1;
}

/* 8011C514 (state 6, a0 = minus the price, for the money): the price of all, the rest are put away */
static uint32_t food_pay(CPU *c, uint32_t minus_price) {
    if (!bulk_on) return minus_price;
    bulk_on = 0;
    int n = (int16_t)MEM_R16(NUMBER_BOX + 8);
    if (n < 2) return minus_price;
    bulk_room(c, bulk_item, n - 1);
    MEM_W16(NUMBER_BOX + 8, 1);
    return (uint32_t)(-(n * bulk_price));
}

uint32_t bulk_food_yes(CPU *c, uint32_t answer) {
    BULK_KEEP;
    answer = food_yes(c, answer);
    BULK_BACK;
    return answer;
}

int bulk_food_state(CPU *c) {
    BULK_KEEP;
    int r = food_state(c);
    BULK_BACK;
    return r;
}

uint32_t bulk_food_pay(CPU *c, uint32_t minus_price) {
    BULK_KEEP;
    minus_price = food_pay(c, minus_price);
    BULK_BACK;
    return minus_price;
}

