/* Shorter waits in menus and windows (quickui). Hooks emitted by tools/recomp.py. */
#include "psx.h"

/* The message/help window (the box at the bottom of the rucksack, the toolbox, the bookshelf,
 * ...) grows open and shrinks closed over 6 + steps game ticks, counted in the word at
 * `counter`; once it reaches 6 + steps the window is drawn whole (or gone) and the caller
 * goes on. Starting it there makes the window open and close at once. */
void quick_window(uint32_t counter, uint32_t steps) {
    if (!g_cfg.quickui) return;
    int32_t end = (int32_t)steps + 6;
    if ((int32_t)MEM_R32(counter) < end) MEM_W32(counter, (uint32_t)end);
}

/* Fades to and from black (doors, the map, menus, going to bed, ...) last the given number of
 * game ticks, and the game waits for them to finish: a quarter of it still shows the fade. */
uint32_t quick_fade(uint32_t ticks) {
    int16_t n = (int16_t)ticks;
    if (!g_cfg.quickui || n <= 2) return ticks;
    n = (int16_t)((n + 3) / 4);
    return (uint32_t)(int32_t)(n < 2 ? 2 : n);
}

/* "Now Loading" (the dog in the middle of a black screen while an area is read from the disc):
 * the game sets gp+88 (8005E2CC) while it is up, and its draw callback (8001B118) only draws
 * it then. The wait is the disc and the data setup, so fastload runs those frames at full
 * speed, like holding Tab. */
bool quick_loading(void) {
    return g_cfg.fastload && ram_r32(0x8005E2CCu) != 0;
}
