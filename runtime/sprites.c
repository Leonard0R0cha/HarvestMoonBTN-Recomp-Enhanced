/* Hiring the Harvest Sprites through the Chef (spritehire). Hooks emitted by tools/recomp.py.
 *
 * The seven sprites' records (0xF8 bytes each from 8007A638, in the saved part of the RAM): +22
 * friendship (0-255), +40 the hire menu's state, +172 which sprite (0 = the Chef), +174/+176/+178
 * the skill at each job (+1 a day worked, up to 255: the game's own, at night in 8010CB6C), +180
 * the days left (+1: the night takes one off and the sprite works while it is not 0), +184 the job
 * (bits 1, 2, 4; 0x40 hired, 8 talked to once).
 *
 * The talk (GF_FARM 8010DA04) asks to hire only a sprite already talked to (+184 bit 8) with a
 * friendship of 50 or more, and in spring (and the last days of winter) not before the sixth talk
 * (+188). The hire menu (8010E6E8, state +40: 0 the job's answer, 2 shows the days question, 3 its
 * answer, 5 "never mind") asks the job, then how many days.
 *
 * With the option, sprites work in spring too, their skills are kept at the highest (255), they
 * work WORK_SPEED times as fast, and the Chef always asks: after the job he asks how
 * many of the sprites still free (left / right in his menu) and for how long, 1 week (200G a sprite)
 * or 30 days (450G a sprite), takes the money and sends that many to the same job (the others
 * first, the Chef himself last, so he stays home for the rest while any are left). The questions
 * are the game's own texts with new words: a text is written over where it is loaded (80022A94).
 * The rest of the hiring stays the game's own.
 *
 * Talking to a sprite under contract on the farm, or in the barn or the coop (a day or more left: the last day it works too), says what it does and asks whether it should do another job: the answer changes its job,
 * the days stay. The same menu renews the contract of every sprite under one (left / right: 7 or 30
 * more days, the hire's price a sprite), so it need not run out and be made again at their house. The talk picks its text in 8010DC94 / 8010DC98 (bank in a0, number in a1); the text
 * system (80022B98) stores a menu's answer at gp+836 (80022CD8) and closes the window. */
#include "psx.h"
#include "psx_recomp.h"
#include "gpu.h"
#include "text_pt.h"
#include <string.h>

#define SPRITES 0x8007A638u
#define SPRITE_SIZE 0xF8u
#define SPRITE_COUNT 7
#define CHEF 0
#define MONEY 0x80071A5Cu
#define MAP_NOW 0x800A046Cu        /* the map loaded (word): the farm is 20 + the season (the area's
                                    * number, 800A036A, is not always 2 on the farm) */
#define FARM_MAP 20
#define AREA_KIND 0x800A0466u      /* halfword: 5 the barn, 4 the coop (where the animals' sprites
                                    * work), 40 the sprites' house, 1 the farm */
#define ANIMALS_KIND 5
#define COOP_KIND 4
#define FARM_KIND 1
#define GREENHOUSE_KIND 6          /* the greenhouse (the waterers work its beds there too) */
#define HOUR 0x80070CF8u           /* halfword: the hour */
#define TEXT_BUF 0x8006E110u        /* the text being shown (gp+828) */
#define TEXT_MAX 0x800u             /* halfwords there (the 8 KB block's last 2 KB) */
#define TEXT_HEAD 5                 /* the window's codes before the words, kept */
#define PAD_STATE 0x8005E3A8u       /* -> the pad: +0 held */
#define PAD_LEFT 0x8000u
#define PAD_RIGHT 0x2000u
#define START_ACTION 0x8010CE80u   /* a sprite's new action (a0 the sprite, a1 the action) */
#define SPRITE_UPDATE 0x8010CD80u  /* a sprite's frame: its action (a0 the sprite) ... */
#define SPRITE_MOVE 0x8010CAA8u    /* ... then its move (position +56 += speed +72) */
#define WORK_SPEED 3                /* frames a sprite at work runs in one */
#define FRAME_LIST_COUNT 0x80128810u /* things put on the frame's list (800BE208, up to 50) */
#define SOUND 0x8001FABCu           /* a sound effect (5: the number window's) */
#define SPRITE_TEXTS 0x80127CF4u   /* each sprite's hire question (bank 2); +1 its "working" one */
#define FONT_X 896                  /* the font: 4-bit, 8x12 cells, 16 a row, at VRAM 896,0 */
#define ARROW_LEFT 0x9E             /* empty cells of the font: our arrows are drawn there */
#define ARROW_RIGHT 0x9F
#define WEEK_PRICE 200
#define MONTH_PRICE 450

enum { NONE, COUNT, DAYS, HIRE, NO_MONEY };
enum { T_NONE, T_COUNT, T_DAYS, T_DONE, T_NO_MONEY, T_JOBS };

static int phase, want_text, how_many, most, hire_days, cost, count_at, job_menu;
/* the job menu (the window shows 4 lines and its cursor goes no further: the first line and 3
 * answers): the job line (left / right: which job) and the renew line (1 week / 30 days) */
static int renew_at, renew_month, renew_count, job_at, job_pick;
#define MENU_CURSOR 0x8005E4A4u     /* halfword: the menu's line under the cursor (0 the first) */
static uint32_t job_sprite;
static uint32_t pad_was;

/* the arrows (1 the glyphs' ink), loaded into the font's empty cells with a VRAM write (GP0 A0) */
static const char *const arrow_rows[12] = {
    "........", ".....1..", "....11..", "...111..", "..1111..", ".11111..",
    "..1111..", "...111..", "....11..", ".....1..", "........", "........" };

static void put_glyph(int code, int mirror) {
    gpu_write_gp0(0xA0000000u);
    gpu_write_gp0((uint32_t)((code >> 4) * 12) << 16 | (uint32_t)(FONT_X + (code & 15) * 2));
    gpu_write_gp0(12u << 16 | 2u);
    for (int y = 0; y < 12; y++) {
        uint32_t w = 0;
        for (int x = 0; x < 8; x++)
            if (arrow_rows[y][mirror ? 7 - x : x] == '1') w |= 1u << (x * 4);
        gpu_write_gp0(w);
    }
}

static void load_arrows(void) {
    put_glyph(ARROW_LEFT, 0);
    put_glyph(ARROW_RIGHT, 1);
}

static uint32_t sprite(int i) { return SPRITES + (uint32_t)i * SPRITE_SIZE; }

static int is_chef(uint32_t rec) { return g_cfg.spritehire && MEM_R16(rec + 172) == CHEF; }

/* the free sprites in hiring order, the Chef last (he is free while he talks: the game has already
 * stored his days when the hire is made) */
static int free_sprites(int order[SPRITE_COUNT]) {
    int n = 0;
    for (int k = 1; k <= SPRITE_COUNT; k++) {
        int i = k % SPRITE_COUNT;
        if (i == CHEF || MEM_R16(sprite(i) + 180) < 2) order[n++] = i;
    }
    return n;
}

static uint32_t game_call(CPU *c, uint32_t at, uint32_t a0, uint32_t a1) {
    uint32_t regs[32], hi = c->hi, lo = c->lo;
    memcpy(regs, c->r, sizeof regs);
    c->r[29] = regs[29] - 0x80;
    c->r[4] = a0;
    c->r[5] = a1;
    call_addr(c, at);
    uint32_t v = c->r[2];
    memcpy(c->r, regs, sizeof regs);
    c->hi = hi;
    c->lo = lo;
    return v;
}

/* the night (8010CB6C, a0 the sprite): every sprite's skill at the three jobs is the highest (the
 * routine adds a day's and keeps it at 255) */
void sprite_night(CPU *c) {
    if (!g_cfg.spritehire || g_view2_run) return;
    for (int i = 0; i < SPRITE_COUNT; i++)
        for (uint32_t o = 174; o <= 178; o += 2) MEM_W16(sprite(i) + o, 255);
}

/* a sprite's frame (entry of 8010CD80, a0 the sprite; its caller moves it next with 8010CAA8): one
 * at work (actions 8-11: harvest, water, animals, coming to the farm) runs WORK_SPEED frames */
void sprite_update(CPU *c) {
    static int busy;
    uint32_t rec = c->r[4];
    uint16_t action = MEM_R16(rec + 38);
    if (busy || !g_cfg.spritehire || g_view2_run || action < 8 || action > 11) return;
    busy = 1;
    /* each frame puts the sprite on the frame's list of things (count at 80128810, up to 50,
     * cleared each frame by 800BE2B8): the extra frames' entries are dropped, or the list is
     * full early and the cows and sheep out on the farm walk a few steps, stop and turn, again
     * and again. The real frame, run next, puts the sprite on it as the game does. */
    uint32_t listed = MEM_R32(FRAME_LIST_COUNT);
    for (int k = 1; k < WORK_SPEED; k++) {
        game_call(c, SPRITE_UPDATE, rec, 0);
        game_call(c, SPRITE_MOVE, rec, 0);
    }
    MEM_W32(FRAME_LIST_COUNT, listed);
    busy = 0;
}

/* the bed a sprite at work goes to next: 801105C8 (a0 the sprite) picks it with 80110658 (a0/a1 ->
 * the bed's column/row, a2 1 the farm / 0 the greenhouse, a3 and the 5th argument which test:
 * 80110790 ready to harvest, 80110838 / 80110890 to water), which starts at a bed drawn by lot and
 * takes the first one that wants the job: the sprite crosses the whole farm to it, then draws
 * another far away, and walks about more than it works. With the option the nearest bed that
 * wants the job is taken (steps of the 4 ways: x + z), not one another sprite is going to or
 * working. Farm beds: x = 400 col - 4200, z = 1400 - 400 row (45 a row); the greenhouse's:
 * x = 400 col - 1420, z = 1020 - 400 row (7 a row; 800D812C). */
#define PICK_FROM 0x801105C8u
#define FARM_FIELD 0x80071DFCu
#define GH_FIELD (0x8006C838u + 30956u)
#define GH_FLAG (0x8006C838u + 17620u)  /* halfword: the greenhouse built */
uint32_t gh_addr(uint32_t a);
uint32_t gh_fields(uint32_t n);

static uint32_t pick_sprite;

void sprite_pick_from(CPU *c) {
    pick_sprite = g_cfg.spritehire && !g_view2_run ? c->r[4] : 0;
}

static int taken(uint32_t rec, int32_t x, int32_t z) {
    for (int i = 0; i < SPRITE_COUNT; i++) {
        uint32_t o = sprite(i);
        uint16_t a = MEM_R16(o + 38);
        int16_t st = (int16_t)MEM_R16(o + 40);
        if (o == rec || a < 8 || a > 10 || (st != 1 && st != 2) || MEM_R16(o + 48) != MEM_R16(rec + 48)) continue;
        if ((int32_t)MEM_R32(o + 88) == x && (int32_t)MEM_R32(o + 96) == z) return 1;
    }
    return 0;
}

int sprite_pick_bed(CPU *c) {
    uint32_t rec = pick_sprite;
    pick_sprite = 0;
    if (!rec) return 0;
    int farm = c->r[6] != 0;
    uint32_t test = c->r[7] ? 0x80110790u : MEM_R32(c->r[29] + 16) ? 0x80110838u : 0x80110890u;
    uint32_t base = farm ? FARM_FIELD : GH_FIELD;
    int cols = farm ? 45 : 7;
    int n = farm ? 1125 : (int)gh_fields(49);
    if (!farm && MEM_R16(GH_FLAG) == 0) { c->r[2] = 1; return 1; }
    int32_t sx = (int32_t)MEM_R32(rec + 56), sz = (int32_t)MEM_R32(rec + 64);
    int best = -1, best_free = -1;
    int64_t bd = 0, bfd = 0;
    for (int i = 0; i < n; i++) {
        if (!game_call(c, test, gh_addr(base + (uint32_t)i * 8u), 0)) continue;
        int col = i % cols, row = i / cols;
        int32_t x = farm ? 400 * col - 4200 : 400 * col - 1420;
        int32_t z = farm ? 1400 - 400 * row : 1020 - 400 * row;
        int64_t d = (int64_t)(x > sx ? x - sx : sx - x) + (z > sz ? z - sz : sz - z);
        if (best < 0 || d < bd) { best = i; bd = d; }
        if (!taken(rec, x, z) && (best_free < 0 || d < bfd)) { best_free = i; bfd = d; }
    }
    if (best_free >= 0) best = best_free;
    if (best < 0) { c->r[2] = 1; return 1; }       /* none: as the game's (v0 1) */
    MEM_W16(c->r[4], (uint16_t)(best % cols));
    MEM_W16(c->r[5], (uint16_t)(best / cols));
    MEM_W16(rec + 46, 1000);        /* the walk picks its way at once (sprite_walk_way): past the
                                     * count (1080 / speed at most), not 7FFF, which the +1 turns
                                     * negative and the way is never picked again */
    c->r[2] = 0;
    return 1;
}

/* The walk of the characters (800D12B8, a1 the speed) picks a way every 720 / speed calls (1080 every
 * other frame; the toggle at 80128D58) and keeps it until then: it tests the way ahead, to the left
 * (way - 1) and to the right (way + 1), 400 on (bits 1, 4, 2 of s3 when free), and 800D196C draws
 * one of them by lot, the lot leaning to the target's side when the walker has one (+52 0x2000,
 * x / z at +88 / +96; 800D1CFC, which also ends the walk when both are within 230 and one within
 * 50). A sprite at work goes far past its bed and round about. With the option a sprite at work
 * with a target takes the way that brings it nearest instead (the one of the axis still farther
 * off first, then the other; one blocked: a free one across, else back), and walks it only as far
 * as the target is on that axis (the count at +46 set so the next pick comes then). Ways: 0 z down,
 * 1 x down, 2 z up, 3 x up. The hook comes after the sll by 16 at 800D1744: the way is in the
 * upper half. */
#define WALK_TOGGLE 0x80128D58u

static int way_free(int way, int now, uint32_t free_bits) {
    if (way == now) return (free_bits & 1u) != 0;
    if (way == ((now + 3) & 3)) return (free_bits & 4u) != 0;
    if (way == ((now + 1) & 3)) return (free_bits & 2u) != 0;
    return 1;                       /* back: where it came from (tested as "ahead" next time) */
}

/* the cows and sheep the bell leads to the barn's door (bellherd, bell.c) walk the same way */
#define WALKERS (SPRITE_COUNT + BELL_WALKERS)

/* the way to the target for walker i (0.. the sprites, then the bell's animals) */
static uint32_t walk_toward(CPU *c, uint32_t rec, uint32_t way, int i) {
    int32_t speed = (int32_t)c->r[21];
    if (speed <= 0) return way;
    int now = MEM_R16(rec + 34) & 3;
    uint32_t free_bits = c->r[19];
    int32_t dx = (int32_t)MEM_R32(rec + 88) - (int32_t)MEM_R32(rec + 56);
    int32_t dz = (int32_t)MEM_R32(rec + 96) - (int32_t)MEM_R32(rec + 64);
    int32_t ax = dx < 0 ? -dx : dx, az = dz < 0 ? -dz : dz;
    /* no nearer after a few picks (an animal or a wall across the way, gone round to and fro):
     * the game's own picks for a while, which go round it, then ours again */
    {
        static int32_t tx[WALKERS], tz[WALKERS], best[WALKERS];
        static uint8_t stall[WALKERS], game[WALKERS];
        int32_t x = (int32_t)MEM_R32(rec + 88), z = (int32_t)MEM_R32(rec + 96);
        if (x != tx[i] || z != tz[i]) { tx[i] = x; tz[i] = z; best[i] = ax + az; stall[i] = game[i] = 0; }
        if (game[i]) { game[i]--; best[i] = ax + az; return way; }
        if (ax + az < best[i] - 100) { best[i] = ax + az; stall[i] = 0; }
        else if (++stall[i] >= 4) { stall[i] = 0; game[i] = 6; return way; }
    }
    int wx = dx < 0 ? 1 : 3, wz = dz < 0 ? 0 : 2;
    int want[4], n = 0;
    int x_first = ax >= az;
    if (ax >= 50 && az >= 50) { want[n++] = x_first ? wx : wz; want[n++] = x_first ? wz : wx; }
    else if (ax >= 50 || az < 50) want[n++] = wx;
    else want[n++] = wz;
    if (az < 50 && ax < 50) want[n++] = wz;
    /* then the ways across (to go round), then back */
    int across[2] = { (want[0] + 1) & 3, (want[0] + 3) & 3 };
    int pick = -1;
    for (int k = 0; k < n && pick < 0; k++) if (way_free(want[k], now, free_bits)) pick = want[k];
    for (int k = 0; k < 2 && pick < 0; k++)
        if (across[k] != ((now + 2) & 3) && way_free(across[k], now, free_bits)) pick = across[k];
    /* the way to the target blocked, the target near on that axis and the other lined up: the
     * test 400 on hits what the target is at (the trough in the barn, the greenhouse's wall by its
     * door), and the sprite went to and fro across it for good. It goes on that way: the move
     * itself is not stopped, and the walk ends 230 short of the target. */
    if (pick < 0 || (pick != want[0] && n == 1)) {
        int32_t on = want[0] == 1 || want[0] == 3 ? ax : az, other = want[0] == 1 || want[0] == 3 ? az : ax;
        if (on < 800 && other < 50) pick = want[0];
    }
    if (pick < 0) return way;       /* only back, or nothing: the game's own pick */
    int32_t left = pick == 1 || pick == 3 ? ax : az;
    if (pick == want[0] || (n > 1 && pick == want[1])) {
        int32_t every = (MEM_R32(WALK_TOGGLE) == 1 ? 1080 : 720) / speed;
        int32_t calls = left / speed;
        if (calls < 1) calls = 1;
        if (calls < every) MEM_W16(rec + 46, (uint16_t)(every - calls));
    }
    return (uint32_t)pick << 16;
}

uint32_t sprite_walk_way(CPU *c, uint32_t way) {
    uint32_t rec = c->r[18];
    /* not in the second view's extra run of the frame (p2view.c): it puts the RAM back, but not the
     * counters below, and the co-op player with that window would walk the sprites another way */
    if (g_view2_run || !(MEM_R32(rec + 52) & 0x2000u)) return way;
    int animal = bell_walker(rec);
    if (animal >= 0) return walk_toward(c, rec, way, SPRITE_COUNT + animal);
    if (!g_cfg.spritehire || rec < SPRITES || rec >= SPRITES + SPRITE_COUNT * SPRITE_SIZE
        || (rec - SPRITES) % SPRITE_SIZE) return way;
    uint16_t action = MEM_R16(rec + 38);
    if (action < 8 || action > 11) return way;
    return walk_toward(c, rec, way, (int)((rec - SPRITES) / SPRITE_SIZE));
}

/* a sprite at work waters (801103E8) or harvests (8011031C) the bed 100 ahead of it (a0 the
 * sprite): the walk ends within 230 of the bed, and come in from the side the sprite faced the
 * next bed, already done, and went back to the same one again and again. With the option it
 * faces its bed first, along the axis it is farther off on (then 100 on is inside the bed). */
void sprite_face_bed(CPU *c) {
    uint32_t rec = c->r[4];
    if (!g_cfg.spritehire || g_view2_run || rec < SPRITES || rec >= SPRITES + SPRITE_COUNT * SPRITE_SIZE
        || (rec - SPRITES) % SPRITE_SIZE) return;
    uint16_t action = MEM_R16(rec + 38);
    if (action != 8 && action != 9) return;
    int32_t dx = (int32_t)MEM_R32(rec + 88) - (int32_t)MEM_R32(rec + 56);
    int32_t dz = (int32_t)MEM_R32(rec + 96) - (int32_t)MEM_R32(rec + 64);
    int32_t ax = dx < 0 ? -dx : dx, az = dz < 0 ? -dz : dz;
    if (ax < 50 && az < 50) return;
    if (ax > 400 || az > 400) return;      /* not at its bed */
    int way = ax >= az ? (dx < 0 ? 1 : 3) : (dz < 0 ? 0 : 2);
    MEM_W16(rec + 34, (uint16_t)((MEM_R16(rec + 34) & ~3u) | (uint16_t)way));
}

/* the talk's checks, each hook after the instruction: v0 = +184 & 8 (talked to once), v0 =
 * friendship < 50, v0 = the spring talks < 6 */
uint32_t sprite_talked(CPU *c, uint32_t talked) {
    return is_chef(c->r[18]) ? 8u : talked;
}

uint32_t sprite_friendship(CPU *c, uint32_t too_low) {
    return is_chef(c->r[18]) ? 0u : too_low;
}

uint32_t sprite_spring(uint32_t too_few) {
    return g_cfg.spritehire ? 0u : too_few;
}

/* entry of the hire menu (a0 the sprite): the days question is about to be shown */
void sprite_menu(CPU *c) {
    uint32_t rec = c->r[4];
    int16_t state = (int16_t)MEM_R16(rec + 40);
    if (g_view2_run || !is_chef(rec)) return;
    if (state == 0) phase = NONE;
    if (state != 2) return;
    if (phase == NONE) {
        int order[SPRITE_COUNT];
        most = free_sprites(order);
        if (most < 1) return;
        how_many = most;
        phase = COUNT;
        want_text = T_COUNT;
    } else if (phase == DAYS) {
        want_text = T_DAYS;
    }
}

/* the days question's answer (v1, copied from v0 by the hook's instruction: 0 none yet, 1.. the
 * line), s0 the sprite */
uint32_t sprite_days_answer(CPU *c, uint32_t answer) {
    if (g_view2_run || !is_chef(c->r[16]) || answer == 0) return answer;
    uint32_t rec = c->r[16];
    if (phase == COUNT) {
        if (answer != 1) { phase = NONE; return 4; }
        phase = DAYS;
        MEM_W16(rec + 40, 2);       /* the days question next */
        return 0;
    }
    if (phase != DAYS) return answer;
    if (answer != 1 && answer != 2) { phase = NONE; return 4; }
    hire_days = answer == 1 ? 8 : 31;
    cost = how_many * (answer == 1 ? WEEK_PRICE : MONTH_PRICE);
    if ((int32_t)MEM_R32(MONEY) < cost) {
        phase = NO_MONEY;
        want_text = T_NO_MONEY;     /* in place of the "never mind" one */
        return 4;
    }
    MEM_W32(MONEY, MEM_R32(MONEY) - (uint32_t)cost);
    phase = HIRE;
    want_text = T_DONE;
    return 1;                       /* the game's own "OK" (it stores 2 days, put right below) */
}

/* after the game stored the Chef's days (8010E98C): the hire */
void sprite_hired(CPU *c) {
    if (g_view2_run || !is_chef(c->r[16]) || phase != HIRE) return;
    int order[SPRITE_COUNT], n = free_sprites(order);
    uint32_t job = MEM_R16(sprite(CHEF) + 184) & 7u;
    int chef_goes = 0;
    if (how_many > n) how_many = n;
    for (int k = 0; k < how_many; k++) {
        uint32_t rec = sprite(order[k]);
        MEM_W16(rec + 180, (uint16_t)hire_days);
        MEM_W16(rec + 184, (uint16_t)((MEM_R16(rec + 184) & ~0x47u) | 0x40u | job));
        if (order[k] == CHEF) chef_goes = 1;
    }
    if (!chef_goes) {
        uint32_t rec = sprite(CHEF);
        MEM_W16(rec + 180, 0);
        MEM_W16(rec + 184, (uint16_t)(MEM_R16(rec + 184) & ~0x47u));
    }
    LOGI("sprites: %d hired for %d days (%dG)", how_many, hire_days - 1, cost);
    phase = NONE;
}

/* the words: A-Z 0.., a-z 0x20.., 0-9 0x40..; '\n' a new line, '|' the start of the answers,
 * '#' the number of sprites (its place kept in count_at) */
static uint32_t put(uint32_t at, uint16_t h) {
    if (at < TEXT_MAX) MEM_W16(TEXT_BUF + at * 2, h);
    return at + 1;
}

static uint32_t put_number(uint32_t at, int v) {
    char d[12];
    int n = 0;
    do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) at = put(at, (uint16_t)(0x40 + d[--n] - '0'));
    return at;
}

static uint32_t put_words(uint32_t at, const char *s) {
    for (; *s; s++) {
        char ch = *s;
        int g = pt_glyph(&s);
        if (g >= 0) at = put(at, (uint16_t)g);
        else if (ch >= 'A' && ch <= 'Z') at = put(at, (uint16_t)(ch - 'A'));
        else if (ch >= 'a' && ch <= 'z') at = put(at, (uint16_t)(0x20 + ch - 'a'));
        else if (ch >= '0' && ch <= '9') at = put(at, (uint16_t)(0x40 + ch - '0'));
        else if (ch == ' ') at = put(at, 0x0FFD);
        else if (ch == ',') at = put(at, 0x8C);
        else if (ch == '.') at = put(at, 0x6C);
        else if (ch == '?') at = put(at, 0x62);
        else if (ch == '!') at = put(at, 0x63);
        else if (ch == '<') at = put(at, ARROW_LEFT);
        else if (ch == '>') at = put(at, ARROW_RIGHT);
        else if (ch == '\'' && (s[1] == 'm' || s[1] == 'l' || s[1] == 's')) {
            /* 'm 'l 's are glyphs of their own (the game writes I'll as I, 'l, l) */
            s++;
            at = put(at, *s == 'm' ? 0x54 : *s == 'l' ? 0x52 : 0x51);
        } else if (ch == '\'') at = put(at, 0x8D);
        else if (ch == '\n') at = put(at, 0xFFFE);
        else if (ch == '|') {
            at = put(at, 0x0FFC);
            at = put(at, 0x0FF8);
            at = put(at, 0xFFFE);
            at = put(at, 0x0FF6);
        } else if (ch == '#') {
            count_at = (int)at;
            at = put(at, (uint16_t)(0x40 + how_many));
        }
    }
    return at;
}

/* the sprites under contract (a day or more left and a job): the ones a renewal is for */
static int contracted(void) {
    int n = 0;
    for (int i = 0; i < SPRITE_COUNT; i++)
        if (MEM_R16(sprite(i) + 180) >= 1 && (MEM_R16(sprite(i) + 184) & 7u)) n++;
    return n;
}

#define RENEW_FIELD 26             /* the renew line's glyphs (padded): rewritten in place by left / right */

static uint32_t put_renew(uint32_t at) {
    uint32_t start = at;
    int price = renew_count * (renew_month ? MONTH_PRICE : WEEK_PRICE);
    if (GAME_PT) at = put_words(at, renew_month ? "Renovar < 30 dias > " : "Renovar < 1 semana > ");
    else at = put_words(at, renew_month ? "Renew < 30 days > " : "Renew < 1 week > ");
    at = put_number(at, price);
    at = put_words(at, "G");
    while (at - start < RENEW_FIELD) at = put(at, 0x0FFD);
    return at;
}

#define JOB_FIELD 24                /* the job line's glyphs (padded), as the renew line's */

/* the job the first line offers (left / right go through the three) */
static uint32_t put_job_pick(uint32_t at) {
    uint32_t start = at;
    static const char *const pt[3] = { "Colher", "Regar", "Animais" };
    static const char *const en[3] = { "Harvest", "Water", "Animals" };
    at = put_words(at, GAME_PT ? "Trocar para < " : "Switch to < ");
    at = put_words(at, (GAME_PT ? pt : en)[job_pick - 1]);
    at = put_words(at, " >");
    while (at - start < JOB_FIELD) at = put(at, 0x0FFD);
    return at;
}

/* the job menu's first line: the job and the days left (today too: the last day it works) */
static uint32_t put_job_line(uint32_t at, uint32_t rec) {
    uint32_t job = MEM_R16(rec + 184);
    int days = MEM_R16(rec + 180);
    if (GAME_PT) {
        at = put_words(at, job & 1u ? "Colhendo, " : job & 2u ? "Regando, " : "Nos animais, ");
        at = put_words(at, days == 1 ? "falta " : "faltam ");
        at = put_number(at, days);
        at = put_words(at, days == 1 ? " dia." : " dias.");
    } else {
        at = put_words(at, job & 1u ? "Harvesting, " : job & 2u ? "Watering, " : "With animals, ");
        at = put_number(at, days);
        at = put_words(at, days == 1 ? " day left." : " days left.");
    }
    return at;
}

/* a text was loaded (80022A94): written over when one of ours is wanted */
void sprite_text_loaded(void) {
    if (g_view2_run) return;
    if (want_text != T_JOBS) job_menu = renew_at = job_at = 0;  /* another text: the job menu was left */
    if (!want_text) return;
    int which = want_text;
    want_text = T_NONE;
    uint32_t at = TEXT_HEAD;
    if (GAME_PT) switch (which) {   /* the Portuguese translation's game: the same texts in Portuguese */
    case T_COUNT:
        load_arrows();
        at = put_words(at, "Quantos de nós devem ir?\nUse esquerda e direita.| Mandar < # > de nós\n Ah, deixa para lá.");
        break;
    case T_DAYS:
        at = put_words(at, "Por quanto tempo vamos\ntrabalhar, budum?| 1 semana, ");
        at = put_number(at, how_many * WEEK_PRICE);
        at = put_words(at, "G\n 30 dias, ");
        at = put_number(at, how_many * MONTH_PRICE);
        at = put_words(at, "G\n Ah, deixa para lá.");
        break;
    case T_DONE:
        at = put_words(at, "Certo. ");
        at = put_number(at, how_many);
        at = put_words(at, how_many == 1 ? " de nós vai ajudar a\npartir de amanhã às 6h, budum."
                                         : " de nós vão ajudar a\npartir de amanhã às 6h, budum.");
        break;
    case T_NO_MONEY:
        at = put_words(at, "Vai custar ");
        at = put_number(at, cost);
        at = put_words(at, "G, budum.\nVolte quando tiver o dinheiro.");
        phase = NONE;
        break;
    case T_JOBS:
        load_arrows();
        at = put_job_line(at, job_sprite);
        at = put_words(at, "| ");
        job_at = (int)at;
        at = put_job_pick(at);
        at = put_words(at, "\n ");
        renew_at = (int)at;
        at = put_renew(at);
        at = put_words(at, "\n Ah, deixa para lá.");
        job_menu = 2;
        break;
    } else switch (which) {
    case T_COUNT:
        load_arrows();
        at = put_words(at, "How many of us should go?\nPress left or right to pick.| Send < # > of us\n Oh, never mind.");
        break;
    case T_DAYS:
        at = put_words(at, "How long should we work,\nbudum?| 1 week, ");
        at = put_number(at, how_many * WEEK_PRICE);
        at = put_words(at, "G\n 30 days, ");
        at = put_number(at, how_many * MONTH_PRICE);
        at = put_words(at, "G\n Oh, never mind.");
        break;
    case T_DONE:
        at = put_words(at, "OK. ");
        at = put_number(at, how_many);
        at = put_words(at, " of us will help from\n6AM tomorrow, budum.");
        break;
    case T_NO_MONEY:
        at = put_words(at, "That would be ");
        at = put_number(at, cost);
        at = put_words(at, "G, budum.\nCome back when you have it.");
        phase = NONE;
        break;
    case T_JOBS:
        load_arrows();
        at = put_job_line(at, job_sprite);
        at = put_words(at, "| ");
        job_at = (int)at;
        at = put_job_pick(at);
        at = put_words(at, "\n ");
        renew_at = (int)at;
        at = put_renew(at);
        at = put_words(at, "\n Oh, never mind.");
        job_menu = 2;
        break;
    }
    put(at, 0xFFFF);
}

/* the farm: the kind of area (the map's number is not always 20 + the season there: 19 and 0
 * were seen on the farm) */
static int on_farm(void) {
    return MEM_R16(AREA_KIND) == FARM_KIND || MEM_R32(MAP_NOW) - FARM_MAP <= 3u;
}

/* the barn or the coop (a sprite's +48 there is the same kind) */
static int animals_kind(uint32_t kind) {
    return kind == ANIMALS_KIND || kind == COOP_KIND;
}

static int in_greenhouse(void) {
    return MEM_R16(AREA_KIND) == GREENHOUSE_KIND;
}

/* the talk's text (after 8010DC98: a0 bank, a1 number; s2 the sprite): one under contract asks
 * whether to do another job */
void sprite_talk_text(CPU *c) {
    uint32_t rec = c->r[18];
    if (g_view2_run || !g_cfg.spritehire || (!on_farm() && !in_greenhouse() && !animals_kind(MEM_R16(AREA_KIND))) || MEM_R16(rec + 180) < 1
        || !(MEM_R16(rec + 184) & 7u)) return;
    job_sprite = rec;
    job_menu = 1;
    renew_month = 0;
    renew_count = contracted();
    {   /* the job offered first: the next one after the sprite's */
        uint32_t job = MEM_R16(rec + 184);
        job_pick = job & 1u ? 2 : job & 2u ? 3 : 1;
    }
    c->r[4] = 2;
    c->r[5] = (uint32_t)(int32_t)(int16_t)MEM_R16(SPRITE_TEXTS + MEM_R16(rec + 172) * 2u);
    want_text = T_JOBS;
}

/* a menu answered (80022CDC, a0 = 1.. the line): the job menu's (1 the job picked on that line, as
 * the hire menu's: 1-3 = bits 1, 2, 4; 2 renew; 3 never mind) */
void sprite_menu_answered(CPU *c, uint32_t answer) {
    if (g_view2_run || job_menu != 2) return;
    job_menu = 0;
    renew_at = job_at = 0;
    if (answer == 1) answer = (uint32_t)job_pick;
    else if (answer == 2) answer = 4;
    else return;
    if (answer == 4) {
        /* renew: every sprite under contract gets 7 or 30 more days for the price shown (talk
         * again to see the days: no text can follow the answer) */
        int price = renew_count * (renew_month ? MONTH_PRICE : WEEK_PRICE);
        if (renew_count < 1 || (int32_t)MEM_R32(MONEY) < price) {
            LOGI("sprites: renewing %d needs %dG", renew_count, price);
            return;
        }
        MEM_W32(MONEY, MEM_R32(MONEY) - (uint32_t)price);
        for (int i = 0; i < SPRITE_COUNT; i++) {
            uint32_t r = sprite(i);
            if (MEM_R16(r + 180) >= 1 && (MEM_R16(r + 184) & 7u))
                MEM_W16(r + 180, (uint16_t)(MEM_R16(r + 180) + (renew_month ? 30 : 7)));
        }
        LOGI("sprites: %d renewed for %d days (%dG)", renew_count, renew_month ? 30 : 7, price);
        return;
    }
    if (answer < 1 || answer > 3) return;
    uint32_t rec = job_sprite;
    MEM_W16(rec + 184, (uint16_t)((MEM_R16(rec + 184) & ~7u) | (1u << (answer - 1))));
    /* the job is chosen once, when the sprite reaches the farm: action 11 starts action 8, 9 or 10
     * (harvest, water, animals; 8010F324) with 8010CE80, and a talk on the farm doesn't stop it. A
     * sprite at work starts the new job's action the same way (the action it would go back to,
     * +190, too). In the barn or the coop (a job of the fields) the sprite leaves at once for the
     * farm: a sprite is where its +48 (the kind of area, as 800A0466) says, and is moved and drawn
     * only there (8010C800); on the farm it arrives (action 11) and starts the new job; with 0x40
     * clear it works the fields meanwhile, as one out of sight (8010CAD4) */
    uint16_t saved = MEM_R16(rec + 38);
    int work_hours = saved != 12 && MEM_R16(HOUR) - 6u < 15u;
    if (in_greenhouse() && MEM_R16(rec + 48) == GREENHOUSE_KIND) {
        /* in the greenhouse: watering goes on there (action 9 works the greenhouse's beds with +48
         * 6); another job sends the sprite to the farm at once, as from the barn (action 11) */
        if (answer == 2 && saved != 9 && work_hours) {
            MEM_W16(rec + 184, (uint16_t)(MEM_R16(rec + 184) & ~0x40u));
            MEM_W16(rec + 190, 0);
            game_call(c, START_ACTION, rec, 9);
        } else if (answer != 2 && work_hours) {
            MEM_W16(rec + 48, FARM_KIND);
            MEM_W16(rec + 184, (uint16_t)(MEM_R16(rec + 184) & ~0x40u));
            MEM_W16(rec + 190, 0);
            game_call(c, START_ACTION, rec, 11);
        }
    } else if (!on_farm() && answer != 3 && animals_kind(MEM_R16(rec + 48))) {
        MEM_W16(rec + 48, FARM_KIND);
        MEM_W16(rec + 184, (uint16_t)(MEM_R16(rec + 184) & ~0x40u));
        MEM_W16(rec + 190, 0);
        game_call(c, START_ACTION, rec, 11);
    } else if (on_farm() && saved >= 8 && saved <= 10) {
        if (answer == 3) MEM_W16(rec + 34, 3);
        game_call(c, START_ACTION, rec, 7 + answer);
        if (MEM_R16(rec + 190) >= 8 && MEM_R16(rec + 190) <= 10) MEM_W16(rec + 190, (uint16_t)(7 + answer));
    } else if (on_farm() && work_hours) {
        /* done for the day (0x40, walking about): back to work at the new job, in work hours
         * (from 21h the sprites go home: action 12, 8010CE00) */
        MEM_W16(rec + 184, (uint16_t)(MEM_R16(rec + 184) & ~0x40u));
        MEM_W16(rec + 190, 0);
        if (answer == 3) MEM_W16(rec + 34, 3);
        game_call(c, START_ACTION, rec, 7 + answer);
    }
    LOGI("sprites: sprite %u now on job %u (action %u -> %u)", MEM_R16(rec + 172), answer, saved,
         MEM_R16(rec + 38));
}

/* each tick the text window is drawn (80022B98): left / right change how many, or in the job
 * menu the renewal's length */
void sprite_text_tick(CPU *c) {
    if (g_view2_run) return;
    if (job_menu == 2 && renew_at) {
        uint32_t held = MEM_R32(MEM_R32(PAD_STATE));
        uint32_t pressed = held & ~pad_was;
        pad_was = held;
        if (!(pressed & (PAD_LEFT | PAD_RIGHT))) return;
        uint16_t line = MEM_R16(MENU_CURSOR);
        if (line == 0 && job_at) {
            job_pick = pressed & PAD_RIGHT ? job_pick % 3 + 1 : (job_pick + 1) % 3 + 1;
            put_job_pick((uint32_t)job_at);
        } else if (line == 1) {
            renew_month = !renew_month;
            put_renew((uint32_t)renew_at);
        } else return;
        game_call(c, SOUND, 5, 0);
        return;
    }
    if (phase != COUNT || !count_at) return;
    uint32_t held = MEM_R32(MEM_R32(PAD_STATE));
    uint32_t pressed = held & ~pad_was;
    pad_was = held;
    int was = how_many;
    if (pressed & PAD_RIGHT) how_many = how_many >= most ? 1 : how_many + 1;
    if (pressed & PAD_LEFT) how_many = how_many <= 1 ? most : how_many - 1;
    if (how_many == was) return;
    MEM_W16(TEXT_BUF + (uint32_t)count_at * 2, (uint16_t)(0x40 + how_many));
    game_call(c, SOUND, 5, 0);
}
