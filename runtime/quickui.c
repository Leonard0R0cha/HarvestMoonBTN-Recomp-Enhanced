/* Shorter waits: windows and fades (quickui), loading (fastload), typing (textspeed) and
 * cutscenes (skipscenes). Hooks emitted by tools/recomp.py. */
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
 * speed, like holding Tab; so are the black pauses between places. */
bool quick_loading(bool black) {
    if (!g_cfg.fastload) return false;
    if (ram_r32(0x8005E2CCu) != 0) return true;
    /* After a fade to black (doors, going to bed, between the intro screens) the fade kind at
     * gp+32 (8005E294) is 0 until the next fade from black starts, while the game loads the
     * next place. Only frames that really are black count, in case something is drawn then. */
    return black && (int16_t)MEM_R16(0x8005E294u) == 0;
}

/* The music goes on through those pauses (the next place's music often starts in them), and
 * at full speed it would run on unheard: once the picture is back it would be well ahead. So
 * while fastload runs a pause at full speed the music sequencer (8002A6E4, libsnd's
 * SsSeqCalledTbyT, called each tick) is not run: the music waits and goes on from where it
 * was. The platform says per frame whether the frame being run is such a pause. */
static bool music_held;

void quick_hold_music(bool hold) {
    music_held = hold;
}

int quick_music_held(void) {
    return music_held;
}

/* Typed text (dialogue, the TV, signs, ...): gp+464 (8005E444) counts the characters of the
 * text shown so far; the printer adds one each time its timer runs out and handles every
 * control code itself as the count reaches it (line ends, waits for the button, names,
 * choices, ...). textspeed adds more characters at that moment, but only plain letters and
 * spaces: the count never passes a control code, so the printer still meets each one as in
 * the original and only the typing is faster.
 * The count is not an index into the text: an inserted name (a 4xxx code: the farm's, the
 * player's, an animal's) counts as all of its letters. So the characters are looked at where
 * the printer's walk through the text ends (80018FF4, s0 = the next character), once per
 * character it added (quick_text, at 80018C94). Inserted names are walked by a call of the
 * printer of its own (the flag at sp+112 set) and are typed faster too: the walk then ends
 * right after the name's code, and the characters checked are the ones after the name, of
 * which no more are shown than are added. */
#define TEXT_SHOWN 0x8005E444u
#define TEXT_STATE 0x8005E446u
#define TEXT_TYPING 0
#define INSERTED_NAME 0x4000u   /* its letters are plain too */

static bool plain_char(uint16_t ch) {
    return ch < 0x0E00u || ch == 0x0FFDu;   /* letters, digits, punctuation; 0FFD = space */
}

static bool text_added;

void quick_text(void) {
    text_added = true;
}

void quick_text_walked(uint32_t next, uint32_t inserted) {
    if (inserted || !text_added) return;
    text_added = false;
    if (g_cfg.textspeed <= 1 || MEM_R16(TEXT_STATE) != TEXT_TYPING) return;
    uint16_t last = (uint16_t)MEM_R16(next - 2u);
    if (!plain_char(last) && (last & 0xF000u) != INSERTED_NAME) return;
    uint32_t n = MEM_R16(TEXT_SHOWN);
    for (int k = 1; k < g_cfg.textspeed && n < 4096; k++, n++, next += 2u)
        if (!plain_char((uint16_t)MEM_R16(next))) break;
    MEM_W16(TEXT_SHOWN, n);
}

/* The printer's state at gp+466 (8005E446) is 3 while it waits for the button (the count
 * stops at the wait code). 80017AE8 ends the wait (and shows the next character); it is
 * called when the button is pressed (return address 80018604) and when the box has scrolled
 * up a line (80018798). If the wait was reached while the box was about to scroll, the end
 * of the scroll ended it too and the text went on without the player: more likely with
 * fast text, as the wait is reached before the scroll starts. Only the button ends a wait. */
#define TEXT_WAITING    3
#define WAIT_PRESSED    0x80018604u

int quick_text_wait_kept(uint32_t ra) {
    return ra != WAIT_PRESSED && MEM_R16(TEXT_STATE) == TEXT_WAITING;
}

/* Cutscenes: the event scripts lock the player (bit 23 of the player state word, 80071A6C,
 * set and cleared by the script command at 800E63C8) for as long as they run. Holding START
 * then runs the game at full speed; the game never sees that START (it opens the farm summary
 * in normal play), not even if the scene ends while it is still held. Whenever a message
 * window is up (8006C904, set while the dialogue box is open) the scene runs at normal speed
 * and waits for the player, since some dialogue asks for a choice: once the box closes,
 * holding START goes on skipping. */
#define PLAYER_STATE   0x80071A6Cu
#define PLAYER_LOCKED  0x00800000u
#define DIALOGUE_OPEN  0x8006C904u

static bool skipping, start_swallowed;

uint16_t quick_skip(uint16_t pad) {
    bool start = (pad & PAD_START) != 0;
    bool scene = g_cfg.skipscenes && (MEM_R32(PLAYER_STATE) & PLAYER_LOCKED) != 0;
    if (!start) start_swallowed = false;
    else if (scene) start_swallowed = true;
    if (start_swallowed) pad &= (uint16_t)~PAD_START;
    skipping = scene && start && MEM_R8(DIALOGUE_OPEN) == 0;
    return pad;
}

bool quick_skipping(void) {
    return skipping;
}

/* R1 switches tools and R2 items; each press makes the player stop for a moment, even with
 * nothing to switch to, so pressing them while running with an empty rucksack stutters the
 * run. What the player carries (the list 800F9BE4 builds): the tool in hand at 80071A84 and
 * up to 8 more in the rucksack from 80071A90, the item in hand at 80071A8A and up to 8 more
 * from 80071AC0 (6 bytes each, 0 = empty). With none of a kind, its button is dropped from
 * the pad state the farm/town control reads (801231F4 held, 801231F8 newly pressed;
 * game pad bits: R1 = 8, R2 = 2). */
#define PLAYER_PAD_HELD    0x801231F4u
#define PLAYER_PAD_PRESSED 0x801231F8u
#define PAD_GAME_R1        0x0008u
#define PAD_GAME_R2        0x0002u

static bool carries(uint32_t in_hand, uint32_t slots) {
    if (MEM_R16(in_hand)) return true;
    for (int i = 0; i < 8; i++) if (MEM_R16(slots + 6u * (uint32_t)i)) return true;
    return false;
}

void quick_bag_filter(void) {
    if (!g_cfg.emptybag) return;
    uint32_t drop = 0;
    if (!carries(0x80071A84u, 0x80071A90u)) drop |= PAD_GAME_R1;
    if (!carries(0x80071A8Au, 0x80071AC0u)) drop |= PAD_GAME_R2;
    if (!drop) return;
    MEM_W32(PLAYER_PAD_HELD, MEM_R32(PLAYER_PAD_HELD) & ~drop);
    MEM_W32(PLAYER_PAD_PRESSED, MEM_R32(PLAYER_PAD_PRESSED) & ~drop);
}
