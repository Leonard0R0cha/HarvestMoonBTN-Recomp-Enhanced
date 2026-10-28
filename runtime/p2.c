/* A second farmer (prototype): a second copy of the player, moved and drawn by the game's own
 * player routines.
 *
 * The player is the struct at 80071A00 (0x3FC bytes; the next object starts at 80071DFC), and the
 * farm/town code works on it by that address. Farmer 2 keeps a struct of its own outside the
 * game's RAM, and each game tick, once the player has had its turn, it is swapped in for the
 * player's and the player's routines are run once more with farmer 2's buttons:
 *   - 800C401C, the control (reads the pad, starts and runs actions: tools, ...);
 *   - 800CAADC, the walk (look-ahead, collision, sliding), when the player's state routine
 *     (800CD9A0) would let the player walk. That routine itself is not run: it is the one that
 *     takes the player through doors and to other areas.
 * Then farmer 2's struct is copied out and the player's put back, with the buttons and the few
 * globals the player's turn leaves for the rest of the tick. The entry hook of 800DB5CC, which
 * the farm's tick calls right after the player's turn, runs p2_tick.
 *
 * Only some of farmer 2's buttons reach the game: the directions, Square (use the tool), Circle
 * (run), Cross (look, talk, open) and R1 (change tools). Start and the others would open menus
 * that are the whole game's. When farmer 2's turn opens a menu or a talk (8006C844 gets 08000000:
 * the tool chest, a person), farmer 2 has the game until it closes: the farm's whole tick (800C162C,
 * p2_tick_begin / p2_tick_end) runs with farmer 2's struct, globals and pad in place of the
 * player's (and the player's drawn as the second farmer), with all farmer 2's buttons but Start
 * and Select.
 *
 * There is one of each tool: farmer 2 starts with nothing and takes tools from the tool chest
 * (80070EC2) with that menu. The game saves only what the player carries, so the save and load
 * screens put farmer 2's tools back in the chest.
 *
 * The farm/town code draws the player with 800CA24C (a0 = the player's position, x / y / z words
 * at 80071B00, from 800C17D8). Its entry hook calls p2_draw first: the routine is run once more
 * with farmer 2's struct swapped in, then the player's is put back and the game draws the player
 * as usual (so the projection it leaves at 80128B88 is the player's). */
#include "psx.h"
#include "psx_recomp.h"

int g_p2_enabled;               /* --p2 */
int g_p2_demo;                  /* --p2demo: farmer 2 walks around by itself (testing) */
uint16_t g_p2_buttons;          /* farmer 2's pad, set by the frontend each frame */

#define PLAYER 0x80071A00u
#define PLAYER_SIZE 0x3FC
#define POS (PLAYER + 0x100)    /* x / y / z words */
#define FLAGS (PLAYER + 0x6C)
#define PLAYER_DRAW 0x800CA24Cu
#define CONTROL 0x800C401Cu
#define WALK 0x800CAADCu
#define PAD 0x8005E3A8u         /* -> the pad state (port 1) */
#define BUFFER 0x8005E3A4u      /* which of the two screens is being built (0 / 1) */
#define SLOTS 0x80122F9Cu       /* per screen: VRAM places of the player's pictures */
/* The player's sprite builder (80016C08) unpacks each picture of a frame (the body, the tool in
 * hand) and loads it into VRAM at a place of its screen's list (15 places of 8 bytes: where the
 * palette goes at +0 / +2, the pixels at +4 / +6; the pixels at 960-1023 x 256-415, several tools
 * sharing a place; the palettes on rows 485-486 (the body's 256 colours, a palette per frame) and
 * 487-488 (the tools' 16)). The GPU draws a screen while the next is built, so farmer 2 needs
 * places of its own, which nothing else loads, copies to or draws from (logged at the GPU over
 * the farm, town, house, talks and menus): the pixels 256 rows up (960-1023 x 0-159; the letters'
 * page and palette are just left of it, at 896 and on row 252), the palettes 10 rows down
 * (495-498). */
#define P2_SLOTS 15
#define P2_PIXELS_DX 0
#define P2_PIXELS_DY (-256)
#define P2_PALETTE_DY 10
#define START_DX 250            /* where farmer 2 appears: beside the player */
#define GROUND 0x800BDB88u
#define GROUND_STAND 0xC080u    /* the kinds of ground the player cannot stand on */
#define BUTTONS (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT | PAD_SQUARE | PAD_CIRCLE | PAD_CROSS | PAD_R1)

/* what the player carries (6 bytes each): the tool in hand at +084, the item in hand at +08A,
 * 8 tools in the rucksack from +090, 8 items from +0C0 */
#define CARRIED 0x84
#define CARRIED_SIZE 0x6C
#define TOOL_CHEST 0x80070EC2u  /* the tool chest: 64 places (8 pages of 8), 6 bytes each as carried */
#define TOOL_CHEST_PLACES 64
#define MENU_FLAGS 0x8006C844u  /* 08000000: a menu or talk has the game (the control skips the player) */
#define MENU_OPEN 0x08000000u

/* the globals the player's turn keeps for itself from tick to tick (found by what farmer 2's turn
 * changed): farmer 2 has its own copy of them too */
static const struct { uint32_t at, size; } GLOBALS[] = {
    { 0x80123150u, 0x0C },      /* counters (80123158: the look-ahead's) */
    { 0x801231F4u, 8 },         /* the buttons */
    { 0x80128B60u, 4 },         /* the state routine's answer */
    { 0x80128C4Cu, 0x28 },      /* what the look-ahead and the actions found */
    { 0x80128CB0u, 0x1C },      /* the actions' timers */
    { 0x80128CF8u, 4 },
    { 0x80128B90u, 8 },         /* left by the control for the player's projection */
    { 0x80128D30u, 4 },         /* how long a direction has been held (walking starts after it) */
};
#define GLOBALS_SIZE 0x80       /* at least the sizes above added up */
/* the pad state: +00 newly pressed, +04 held, +08 pressed with repeats (after +18 ticks held,
 * every +1C), +0C held, +10 ticks since the buttons last changed, +24 / +28 as +00 / +10 */
#define PAD_SIZE 0x38
/* the view: the walk (800CAADC) aims it at the player (800A03A4, x / y / z halfwords) and
 * centres it (800A0398 / 039A: 160 - screen x, 120 - screen y). It follows the player only: farmer
 * 2's walk would turn it to farmer 2 for as long as farmer 2 walks. */
#define VIEW 0x800A0384u
#define VIEW_SIZE 0x2C
#define INPUT_LOG 0x8006C854u   /* -> the control's input log: a count at +0 that each turn moves on */
#define INPUT_LOG_SIZE 8

static uint8_t me[PLAYER_SIZE], saved[PLAYER_SIZE];
static uint8_t my_globals[GLOBALS_SIZE], saved_globals[GLOBALS_SIZE], my_pad[PAD_SIZE], saved_pad[PAD_SIZE];
static uint8_t my_log[INPUT_LOG_SIZE], saved_log[INPUT_LOG_SIZE], saved_view[VIEW_SIZE];

/* copies the globals out to "out", and "in" (if any) in */
static void swap_globals(uint8_t *out, const uint8_t *in) {
    uint32_t k = 0;
    for (size_t g = 0; g < sizeof GLOBALS / sizeof *GLOBALS; g++)
        for (uint32_t i = 0; i < GLOBALS[g].size; i++, k++) {
            out[k] = (uint8_t)MEM_R8(GLOBALS[g].at + i);
            if (in) MEM_W8(GLOBALS[g].at + i, in[k]);
        }
}

/* copies n bytes at "at" out to "out", and "in" (if any) in */
static void swap_at(uint32_t at, uint32_t n, uint8_t *out, const uint8_t *in) {
    for (uint32_t i = 0; i < n; i++) {
        out[i] = (uint8_t)MEM_R8(at + i);
        if (in) MEM_W8(at + i, in[i]);
    }
}

static int placed, inside, ticks;
static int owner, exchanged;    /* farmer 2 opened the menu that has the game / its turn is the tick's */
static int loaded;              /* the farm's tick was not run for a while: a new area */
static uint64_t last_tick;
static uint16_t buttons;

static void swap_in(void) {     /* farmer 2's struct in place of the player's */
    for (uint32_t i = 0; i < PLAYER_SIZE; i++) {
        saved[i] = (uint8_t)MEM_R8(PLAYER + i);
        MEM_W8(PLAYER + i, me[i]);
    }
}

static void swap_out(void) {    /* ... and back */
    for (uint32_t i = 0; i < PLAYER_SIZE; i++) {
        me[i] = (uint8_t)MEM_R8(PLAYER + i);
        MEM_W8(PLAYER + i, saved[i]);
    }
}

/* the game's ground query at x, z (on the stack below the caller's, which the routine's own frame
 * keeps clear of): 1 = the player can stand there, *y = the ground's height */
static int ground(CPU *c, uint32_t sp, int32_t x, int32_t *y, int32_t z) {
    uint32_t v = sp - 32;
    MEM_W32(v, (uint32_t)x);
    MEM_W32(v + 4, (uint32_t)*y);
    MEM_W32(v + 8, (uint32_t)z);
    MEM_W32(v + 12, 0);
    c->r[29] = sp - 64;
    c->r[4] = v;
    c->r[5] = GROUND_STAND;
    call_addr(c, GROUND);
    if (c->r[2]) return 0;
    *y = (int32_t)MEM_R32(v + 4);
    return 1;
}

/* new area (the farm's tick was not run for a while: a load), or left far behind: a copy of the
 * player, beside it (on it if there is no ground beside) */
static void place(CPU *c, uint32_t sp) {
    int32_t x = (int32_t)MEM_R32(POS), y = (int32_t)MEM_R32(POS + 4), z = (int32_t)MEM_R32(POS + 8);
    if (placed && !loaded) {
        int32_t mx, mz;
        memcpy(&mx, me + 0x100, 4);
        memcpy(&mz, me + 0x108, 4);
        if (abs(x - mx) <= 4000 && abs(z - mz) <= 4000) return;
    }
    /* there is one of each tool: farmer 2 starts with nothing, takes them from the tool chest and
     * keeps what it carries */
    uint8_t carried[CARRIED_SIZE] = { 0 };
    if (placed) memcpy(carried, me + CARRIED, CARRIED_SIZE);
    for (uint32_t i = 0; i < PLAYER_SIZE; i++) me[i] = (uint8_t)MEM_R8(PLAYER + i);
    memcpy(me + CARRIED, carried, CARRIED_SIZE);
    swap_globals(my_globals, NULL);
    swap_at(MEM_R32(PAD), PAD_SIZE, my_pad, NULL);
    swap_at(MEM_R32(INPUT_LOG), INPUT_LOG_SIZE, my_log, NULL);
    loaded = 0;
    if (ground(c, sp, x + START_DX, &y, z)) {
        x += START_DX;
        memcpy(me + 0x100, &x, 4);
        memcpy(me + 0x104, &y, 4);
    }
    placed = 1;
}

static uint16_t demo_buttons(void) {    /* a square, 2 seconds a side, then 2 seconds standing */
    static const uint16_t sides[5] = { PAD_RIGHT, PAD_UP, PAD_LEFT, PAD_DOWN, 0 };
    return sides[(ticks / 60) % 5];
}

/* the game's pad bits are the PlayStation's with the two bytes swapped */
static uint32_t game_bits(uint16_t b) { return (uint32_t)(uint16_t)(b << 8 | b >> 8); }

/* farmer 2's pad state for buttons b, as the game makes the player's */
static void make_pad(uint16_t b) {
    static uint32_t held, count;
    uint32_t now = game_bits(b), fresh = now & ~held, repeat = fresh;
    count = now == held ? count + 1 : 0;
    uint32_t delay = 20, rate = 7;
    memcpy(&delay, my_pad + 0x18, 4);
    memcpy(&rate, my_pad + 0x1C, 4);
    if (!delay || !rate) { delay = 20; rate = 7; }
    if (now && count >= delay && (count - delay) % rate == 0) repeat = now;
    held = now;
    memcpy(my_pad + 0x00, &fresh, 4);
    memcpy(my_pad + 0x04, &now, 4);
    memcpy(my_pad + 0x08, &repeat, 4);
    memcpy(my_pad + 0x0C, &now, 4);
    memcpy(my_pad + 0x10, &count, 4);
    memcpy(my_pad + 0x24, &fresh, 4);
    memcpy(my_pad + 0x28, &count, 4);
}

void p2_tick(CPU *c) {
    if (!g_p2_enabled || inside || exchanged) return;
    uint32_t regs[32], hi = c->hi, lo = c->lo, sp = c->r[29];
    memcpy(regs, c->r, sizeof regs);
    ticks++;
    place(c, sp);
    uint32_t pad = MEM_R32(PAD), log = MEM_R32(INPUT_LOG);
    swap_at(pad, PAD_SIZE, saved_pad, my_pad);
    swap_at(log, INPUT_LOG_SIZE, saved_log, my_log);
    swap_globals(saved_globals, my_globals);
    swap_at(VIEW, VIEW_SIZE, saved_view, NULL);
    swap_in();
    inside = 1;
    uint32_t menu = MEM_R32(MENU_FLAGS) & MENU_OPEN;
    c->r[29] = sp;
    call_addr(c, CONTROL);
    if (!menu && (MEM_R32(MENU_FLAGS) & MENU_OPEN)) owner = 1;
    /* as the player's state routine (800CD9A0) lets it walk: not in an action */
    uint32_t f = MEM_R32(FLAGS);
    if (!(f & 0x50) && (!(f & 0x20000000) || (f & 0x40000008))) {
        c->r[29] = sp;
        call_addr(c, WALK);
    }
    inside = 0;
    swap_out();
    swap_globals(my_globals, saved_globals);
    swap_at(log, INPUT_LOG_SIZE, my_log, saved_log);
    swap_at(pad, PAD_SIZE, my_pad, saved_pad);
    {
        uint8_t t[VIEW_SIZE];
        swap_at(VIEW, VIEW_SIZE, t, saved_view);
    }
    memcpy(c->r, regs, sizeof regs);
    c->hi = hi;
    c->lo = lo;
}

/* farmer 2's struct, globals, pad and input log for the player's, both ways */
static void exchange(void) {
    uint8_t t[GLOBALS_SIZE > PAD_SIZE ? GLOBALS_SIZE : PAD_SIZE];
    for (uint32_t i = 0; i < PLAYER_SIZE; i++) {
        uint8_t v = (uint8_t)MEM_R8(PLAYER + i);
        MEM_W8(PLAYER + i, me[i]);
        me[i] = v;
    }
    swap_globals(t, my_globals);
    memcpy(my_globals, t, GLOBALS_SIZE);
    swap_at(MEM_R32(PAD), PAD_SIZE, t, my_pad);
    memcpy(my_pad, t, PAD_SIZE);
    swap_at(MEM_R32(INPUT_LOG), INPUT_LOG_SIZE, t, my_log);
    memcpy(my_log, t, INPUT_LOG_SIZE);
}

/* the farm's tick begins (entry of 800C162C): farmer 2's buttons for this tick; while a menu
 * farmer 2 opened is up, the whole tick is farmer 2's, with all its buttons */
void p2_tick_begin(void) {
    if (!g_p2_enabled) return;
    uint64_t now = frame_count();
    if (now - last_tick > 8) loaded = 1;
    last_tick = now;
    buttons = g_p2_demo ? demo_buttons() : g_p2_buttons;
    if (owner && !(MEM_R32(MENU_FLAGS) & MENU_OPEN)) owner = 0;
    /* in farmer 2's own menu all its buttons but Start and Select reach the game */
    make_pad(buttons & (owner ? ~(PAD_START | PAD_SELECT) : BUTTONS));
    if (!owner || !placed) return;
    exchange();
    exchanged = 1;
}

/* ... and ends (800C1874) */
void p2_tick_end(void) {
    if (!exchanged) return;
    exchange();
    exchanged = 0;
}

void p2_draw(CPU *c) {
    if (!g_p2_enabled || inside || !placed) return;
    uint32_t a = c->r[4];
    uint32_t regs[32], hi = c->hi, lo = c->lo;
    memcpy(regs, c->r, sizeof regs);
    uint32_t x = MEM_R32(a), y = MEM_R32(a + 4), z = MEM_R32(a + 8);
    uint32_t slot = SLOTS + (MEM_R32(BUFFER) & 1) * P2_SLOTS * 8;
    uint16_t places[P2_SLOTS * 4];
    for (int i = 0; i < P2_SLOTS; i++) {
        uint32_t at = slot + (uint32_t)i * 8;
        for (int k = 0; k < 4; k++) places[i * 4 + k] = (uint16_t)MEM_R16(at + (uint32_t)k * 2);
        MEM_W16(at + 2, (uint32_t)(places[i * 4 + 1] + P2_PALETTE_DY));
        MEM_W16(at + 4, (uint32_t)(places[i * 4 + 2] + P2_PIXELS_DX));
        MEM_W16(at + 6, (uint32_t)(places[i * 4 + 3] + P2_PIXELS_DY));
    }
    swap_in();
    MEM_W32(a, MEM_R32(POS));
    MEM_W32(a + 4, MEM_R32(POS + 4));
    MEM_W32(a + 8, MEM_R32(POS + 8));
    inside = 1;
    call_addr(c, PLAYER_DRAW);
    inside = 0;
    swap_out();
    for (int i = 0; i < P2_SLOTS * 4; i++) MEM_W16(slot + (uint32_t)i * 2, places[i]);
    MEM_W32(a, x);
    MEM_W32(a + 4, y);
    MEM_W32(a + 8, z);
    memcpy(c->r, regs, sizeof regs);
    c->hi = hi;
    c->lo = lo;
}

int p2_drawing(void) { return inside; }

/* the save and load screens open: farmer 2's tools go back to the tool chest, so that a save
 * keeps them (the game saves only what the player carries) and a load does not make two */
void p2_return_tools(void) {
    if (!placed) return;
    for (int k = 0; k < 9; k++) {
        uint8_t *tool = me + (k ? 0x90 + (k - 1) * 6 : CARRIED);
        if (!(tool[0] | tool[1])) continue;
        for (uint32_t at = TOOL_CHEST; at < TOOL_CHEST + TOOL_CHEST_PLACES * 6; at += 6)
            if (!MEM_R16(at)) {
                for (uint32_t i = 0; i < 6; i++) MEM_W8(at + i, tool[i]);
                memset(tool, 0, 6);
                break;
            }
    }
}

