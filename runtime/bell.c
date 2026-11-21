/* The bell herds the animals (bellherd): ringing it in the barn, every cow and sheep there walks
 * out through the door to the farm; ringing it on the farm, every one there walks in.
 *
 * The animals are the 20 records of 184 bytes at 800749F4: +48 their area's kind (5 the barn, 1
 * the farm), +52 the flags, +56 / +64 the x / z, +88 / +96 where they walk to, +178 nonzero when
 * they may not be led out. The bell asks each one in the player's area whether it heard
 * (800E1654 for the cows, 800DFA7C for the sheep): a roll of 200 under its +22 (v0 back at
 * 800E169C / 800DFAC4); if so it takes the player's position as its goal and the flag 0x2000
 * (walking to it; 800E16E4 / 800DFB0C store it). Its walk (800D12B8) stops once near the goal on
 * one axis (800D1CFC: within the halfword at 80123754 both ways and 50 on one), and at each step
 * asks the doors (800D2B40): with the flag 0x04000000 (a push sets it, 800E0280) the door table at 80122D8C (kind 0: the barn's two doors, 28 bytes
 * each: +0 / +8 x from / to, +4 / +12 z, +16 the area from, +18 to, +20 / +24 where it lands)
 * takes it through to the other area (stored before 800D2CCC / 800D2D0C).
 *
 * With bellherd every animal hears, its goal is the front of the door (where the way in lands
 * one), then straight through the door's box, with 0x04000000 set; it takes the straight way
 * (sprites.c, as a sprite at work) and does not stop on the way. Through the door it does not
 * land on the others: in the barn on the spot it left from, else (or taken) on the first free
 * spot of a grid away from the door. One that comes no nearer for BELL_TIMEOUT frames stays. */
#include "psx.h"
#include "psx_recomp.h"
#include <stdint.h>

#define ANIMALS 0x800749F4u
#define ANIMAL_SIZE 184u
#define ANIMAL_COUNT 20u
#define DOORS 0x80122D8Cu
#define DOOR_SIZE 28u
#define AREA_KIND 0x800A0466u
#define FARM_KIND 1
#define BARN_KIND 5
#define WALKING_TO 0x2000u
#define MAY_PASS 0x04000000u
#define BELL_TIMEOUT 1200        /* frames (20 s) without coming PROGRESS nearer: held up, it stays */
#define PROGRESS 50
#define GRID_STEP 300
#define FRONT_NEAR 100
#define SPOT_NEAR 200

enum { NOT_LED, TO_FRONT, THROUGH };
static int led[ANIMAL_COUNT];               /* led to the door by the bell (to its front, then through) */
static uint64_t led_since[ANIMAL_COUNT];    /* ... nearer to its goal since */
static int32_t best_left[ANIMAL_COUNT];
static int had_spot[ANIMAL_COUNT];          /* where each left the barn from */
static int32_t spot_x[ANIMAL_COUNT], spot_z[ANIMAL_COUNT];

static int index_of(uint32_t a) {
    uint32_t o = a - ANIMALS;
    if (o >= ANIMAL_SIZE * ANIMAL_COUNT || o % ANIMAL_SIZE) return -1;
    return (int)(o / ANIMAL_SIZE);
}

/* the barn's door out of this area (its entry in the table, 0 if none) */
static uint32_t door_from(int kind) {
    for (uint32_t e = 0; e < 2; e++)
        if ((int16_t)MEM_R16(DOORS + e * DOOR_SIZE + 16) == kind) return DOORS + e * DOOR_SIZE;
    return 0;
}

/* in front of the way out of this area: where the way in lands one (the barn's walls stand by
 * the door's box, so the box's middle is not reached straight) */
static int front_of_door(int32_t *x, int32_t *z) {
    uint32_t out = door_from((int16_t)MEM_R16(AREA_KIND));
    if (!out) return 0;
    uint32_t in = door_from((int16_t)MEM_R16(out + 18));
    if (!in) return 0;
    *x = (int32_t)MEM_R32(in + 20);
    *z = (int32_t)MEM_R32(in + 24);
    return 1;
}

static int herding(uint32_t a) {
    if (!g_cfg.bellherd || g_view2_run) return 0;
    int kind = (int16_t)MEM_R16(AREA_KIND);
    if (kind != FARM_KIND && kind != BARN_KIND) return 0;
    if ((int16_t)MEM_R16(a + 48) != kind || MEM_R16(a + 178)) return 0;
    return door_from(kind) != 0;
}

/* v0 at 800E169C / 800DFAC4 (s0 the animal, v1 its +22): the roll; under v1 = heard */
uint32_t bell_heard(CPU *c, uint32_t roll) {
    uint32_t a = c->r[16];
    if (index_of(a) < 0 || !herding(a)) return roll;
    return c->r[3] - 1;
}

/* the goal just taken (800E16E4 / 800DFB0C, s0 the animal): the door instead */
void bell_sent(CPU *c) {
    uint32_t a = c->r[16];
    int i = index_of(a);
    if (i < 0 || !herding(a)) return;
    int32_t x, z;
    if (!front_of_door(&x, &z)) return;
    MEM_W32(a + 88, (uint32_t)x);
    MEM_W32(a + 96, (uint32_t)z);
    MEM_W32(a + 52, MEM_R32(a + 52) | MAY_PASS);
    led[i] = TO_FRONT;
    led_since[i] = frame_count();
    best_left[i] = INT32_MAX;
    if ((int16_t)MEM_R16(a + 48) == BARN_KIND) {
        had_spot[i] = 1;
        spot_x[i] = (int32_t)MEM_R32(a + 56);
        spot_z[i] = (int32_t)MEM_R32(a + 64);
    }
}

/* the halfword at 80123754 just loaded (800D1D90, s4 the walker): 0 = never near enough, so one
 * led to the door keeps walking till it is through */
uint32_t bell_keep_going(CPU *c, uint32_t near) {
    uint32_t a = c->r[20];
    int i = index_of(a);
    if (i < 0 || !led[i] || g_view2_run) return near;
    if (!(MEM_R32(a + 52) & WALKING_TO)) { led[i] = 0; return near; }
    int32_t gx = (int32_t)MEM_R32(a + 88) - (int32_t)MEM_R32(a + 56), gz = (int32_t)MEM_R32(a + 96) - (int32_t)MEM_R32(a + 64);
    int32_t left = (gx < 0 ? -gx : gx) + (gz < 0 ? -gz : gz);
    if (left < best_left[i] - PROGRESS) { best_left[i] = left; led_since[i] = frame_count(); }
    if (frame_count() > led_since[i] + BELL_TIMEOUT) {       /* held up: stays where it is */
        led[i] = 0;
        MEM_W32(a + 52, MEM_R32(a + 52) & ~(WALKING_TO | MAY_PASS));
        return near;
    }
    /* the animal's frame clears 0x04000000 each time (800E0E38, the sheep's 800DF240); a push sets
     * it again before the step, and so does this (the step asks the doors after it) */
    MEM_W32(a + 52, MEM_R32(a + 52) | MAY_PASS);
    int32_t x, z;
    if (led[i] == TO_FRONT && front_of_door(&x, &z)) {
        int32_t dx = (int32_t)MEM_R32(a + 56) - x, dz = (int32_t)MEM_R32(a + 64) - z;
        if (dx > -FRONT_NEAR && dx < FRONT_NEAR && dz > -FRONT_NEAR && dz < FRONT_NEAR) {
            uint32_t d = door_from((int16_t)MEM_R16(AREA_KIND));         /* then straight through */
            MEM_W32(a + 96, (uint32_t)(((int32_t)MEM_R32(d + 4) + (int32_t)MEM_R32(d + 12)) / 2));
            led[i] = THROUGH;
            best_left[i] = INT32_MAX;
        }
    }
    return 0;
}

/* the walk's way (sprites.c, sprite_walk_way): a led animal takes the straight way to its goal,
 * as a sprite at work does (the game's own picks wander round about) */
int bell_walker(uint32_t rec) {
    int i = index_of(rec);
    if (i < 0 || !g_cfg.bellherd || g_view2_run || !led[i]) return -1;
    return i;
}

/* no other animal in that area stands within SPOT_NEAR of x, z */
static int spot_free(int self, int kind, int32_t x, int32_t z) {
    for (uint32_t j = 0; j < ANIMAL_COUNT; j++) {
        uint32_t b = ANIMALS + j * ANIMAL_SIZE;
        if ((int)j == self || (int16_t)MEM_R16(b + 48) != kind) continue;
        int32_t dx = (int32_t)MEM_R32(b + 56) - x, dz = (int32_t)MEM_R32(b + 64) - z;
        if (dx > -SPOT_NEAR && dx < SPOT_NEAR && dz > -SPOT_NEAR && dz < SPOT_NEAR) return 0;
    }
    return 1;
}

/* through the door (800D2CCC / 800D2D0C, s1 the animal, +48 its new area, +56 / +64 the landing) */
void bell_through(CPU *c) {
    uint32_t a = c->r[17];
    int i = index_of(a);
    if (i < 0 || !led[i] || g_view2_run) return;
    led[i] = 0;
    int kind = (int16_t)MEM_R16(a + 48);
    if (kind == BARN_KIND && had_spot[i] && spot_free(i, kind, spot_x[i], spot_z[i])) {
        MEM_W32(a + 56, (uint32_t)spot_x[i]);
        MEM_W32(a + 64, (uint32_t)spot_z[i]);
        return;
    }
    /* a grid away from the door it came through (the way back's box): 5 across, then a row
     * further; the first spot no other animal there stands on */
    uint32_t back = door_from(kind);
    if (!back) return;
    int32_t x = (int32_t)MEM_R32(a + 56), z = (int32_t)MEM_R32(a + 64);
    int32_t door_z = ((int32_t)MEM_R32(back + 4) + (int32_t)MEM_R32(back + 12)) / 2;
    int away_z = z > door_z ? 1 : -1;
    /* across: on the farm both ways of the landing (open grass, measured 700..2700), in the barn
     * towards its middle (the landing is by its right wall) */
    static const int farm_cols[5] = { 0, 1, -1, 2, 3 };
    for (int n = 0; n < (int)ANIMAL_COUNT; n++) {
        int col = kind == BARN_KIND ? -(n % 5) : farm_cols[n % 5];
        int32_t sx = x + col * GRID_STEP, sz = z + away_z * (n / 5) * GRID_STEP;
        if (!spot_free(i, kind, sx, sz)) continue;
        MEM_W32(a + 56, (uint32_t)sx);
        MEM_W32(a + 64, (uint32_t)sz);
        return;
    }
}
