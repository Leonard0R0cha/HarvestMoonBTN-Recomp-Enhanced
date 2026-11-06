/* Fixes to the original game. Hooks emitted by tools/recomp.py. */
#include "psx.h"

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
