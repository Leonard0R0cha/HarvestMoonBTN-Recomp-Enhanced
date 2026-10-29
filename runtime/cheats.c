/* Cheats (the launcher's Cheats page, [cheats] in the ini), applied at the start of each farm tick
 * (entry of 800C162C, before farmer 2's part):
 *   - cheat_money N: on the farm for the first time since the game started, the money is N (once:
 *     then it goes up and down as usual);
 *   - cheat_stamina: the farmers never tire (stamina full, no tiredness: no fainting);
 *   - cheat_clock: the clock stands still (sleeping, fainting and events still set the time);
 *   - cheat_bag: the biggest rucksack (8 places);
 *   - cheat_items: lumber, fodder, fish food and chicken feed at 999;
 *   - cheat_tools: the five tools' levels at their most (both farmers).
 * They change what the game does: recordings keep them (also when changed with F5, which opens the
 * launcher's Cheats page: the ini is looked at twice a second) and the co-op host sends them (F5 is
 * off in co-op). */
#include "psx.h"
#include "psx_recomp.h"

#define PLAYER 0x80071A00u
#define STAMINA 0x12             /* halfwords: what is left, the most at +014, the tiredness at +016 */
#define STAMINA_MAX 0x14
#define TIRED 0x16
#define RUCKSACK 0x1E            /* 0, 1, 2: 2, 4, 8 places */
#define BIGGEST_RUCKSACK 2
#define MONEY 0x5C               /* the money shown (+048 is something else) */
#define MONEY_MOST 9999999        /* the game's most (GameShark "max gold": 0098967F) */
#define DAY 0x80070CF4u
#define HOUR 0x80070CF8u         /* halfwords: the hour, then the minute */
#define MINUTE 0x80070CFAu
#define TOOL_LEVELS 0x40       /* halfwords: sickle, hoe, axe, hammer, watering can (FFFF = the most) */
#define TOOLS 5
#define MATERIALS 0x80070D38u    /* halfwords: lumber, fodder, (+4 not this), fish food, chicken feed */
#define MATERIALS_MOST 999
#define TIME 0x80070D02u         /* the time itself, minutes since midnight: the hour and minute only show it */

static int money_done;           /* the money was set (once a run) */
static int frozen;               /* the clock's time kept (cheat_clock) */
static uint16_t frozen_day, frozen_hour, frozen_minute, frozen_time;

/* changed while the game runs (the ini, or a recording's "cheats@FRAME=..."): new money is set at
 * the next farm tick */
void cheats_set(const int v[CHEATS]) {
    if (v[0] > 0 && v[0] != g_cfg.cheat_money) money_done = 0;
    if (!v[2]) frozen = 0;
    g_cfg.cheat_money = v[0];
    g_cfg.cheat_stamina = v[1];
    g_cfg.cheat_clock = v[2];
    g_cfg.cheat_bag = v[3];
    g_cfg.cheat_items = v[4];
    g_cfg.cheat_tools = v[5];
}

/* the time (80070D02) and the clock shown: stopping only the clock shown left the day going on
 * (shops closing, nightfall) */
static void clock_stands(void) {
    uint16_t day = (uint16_t)MEM_R16(DAY), now = (uint16_t)MEM_R16(TIME);
    /* the time's own step is a minute forward: anything else (a new day, a jump) is kept instead */
    if (!frozen || day != frozen_day || now < frozen_time || now > frozen_time + 1) {
        frozen = 1;
        frozen_day = day;
        frozen_time = now;
        frozen_hour = (uint16_t)MEM_R16(HOUR);
        frozen_minute = (uint16_t)MEM_R16(MINUTE);
        return;
    }
    MEM_W16(TIME, frozen_time);
    MEM_W16(HOUR, frozen_hour);
    MEM_W16(MINUTE, frozen_minute);
}

void cheats_tick(void) {
    if (g_view2_run) return;    /* the second window's run: its RAM is thrown away */
    if (g_cfg.cheat_money > 0 && !money_done)
        MEM_W32(PLAYER + MONEY, (uint32_t)(g_cfg.cheat_money > MONEY_MOST ? MONEY_MOST : g_cfg.cheat_money));
    money_done = 1;
    if (g_cfg.cheat_stamina) {
        MEM_W16(PLAYER + STAMINA, MEM_R16(PLAYER + STAMINA_MAX));
        MEM_W16(PLAYER + TIRED, 0);
    }
    if (g_cfg.cheat_bag) MEM_W16(PLAYER + RUCKSACK, BIGGEST_RUCKSACK);
    if (g_cfg.cheat_clock) clock_stands();
    if (g_cfg.cheat_items) {
        static const uint32_t at[] = { 0, 2, 6, 8 };
        for (int k = 0; k < 4; k++) MEM_W16(MATERIALS + at[k], MATERIALS_MOST);
    }
    if (g_cfg.cheat_tools)
        for (uint32_t k = 0; k < TOOLS; k++) MEM_W16(PLAYER + TOOL_LEVELS + k * 2, 0xFFFF);
    p2_cheats(g_cfg.cheat_stamina, g_cfg.cheat_bag, g_cfg.cheat_tools);
}
