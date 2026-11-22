/* The jukebox (F7): the farm's music of any season (or of a rainy day), whatever the season.
 *
 * The farm's music is picked by 800BCB5C (its number back in v0 at 800BC888, in 800BC868) each
 * time the player comes to an area: the festivals have their own; then the day's weather
 * (8007AD00): a typhoon or a blizzard (3, 4) 23, rain (1) 22, snow (2) the season's; a fine day
 * (0) the season's on the farm (area 999), others elsewhere. The season's is season + 1 (1
 * spring, 2 summer, 3 autumn, 4 winter; the song table at 80049296 gives each song its own sound
 * bank and sequence, so any of them plays right anywhere). 800BC868 then loads and plays it
 * unless it is the one playing already (the number at 800A048C).
 *
 * With a song chosen, a season's or the rain's number is replaced by it: it plays from the next
 * time the player comes to the farm. Not at once: the bank is read from the CD through a buffer
 * (801C2440) that only the loading between areas leaves free; while the farm runs it holds the
 * area's data (a song loaded then reads a broken bank, and writes over that data). Not online
 * (the two games would differ). A recording keeps the choice ("jukebox=N" first,
 * "jukebox@FRAME=N" when changed), as the replay needs it. */
#include "psx.h"
#include "psx_recomp.h"

#define SEASON_FIRST 1u             /* spring; summer, autumn, winter after it */
#define SEASON_LAST 4u
#define RAIN_SONG 22u

int g_jukebox;              /* 0: the game's own; 1..4 spring, summer, autumn, winter; 22 rain */

static int farm_song(uint32_t s) { return (s >= SEASON_FIRST && s <= SEASON_LAST) || s == RAIN_SONG; }

void jukebox_set(int song)
{
    g_jukebox = song > 0 && farm_song((uint32_t)song) ? song : 0;
}

/* the area's music just picked (v0 at 800BC88C) */
uint32_t jukebox_song(uint32_t v)
{
    if (g_jukebox && farm_song(v) && !netplay_active()) return (uint32_t)g_jukebox;
    return v;
}
