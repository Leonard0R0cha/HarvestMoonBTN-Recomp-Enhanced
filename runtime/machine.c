/* The second machine (experiment, step 1 of "the farmers in different places"): a second copy of
 * the game, with RAM, CPU, timers, CD, BIOS, pads, cards and sound chip of its own, that will run
 * the area farmer 2 is in while the game runs the player's.
 *
 * The recompiled game runs on the native stack (the platform's frame is called from inside the
 * game's code, at each vblank), so each machine has a stack of its own: the second runs on a
 * fiber. At each vblank the first machine stops, its state is put aside and the second's put in
 * its place, and the second runs till its own vblank; then the other way round. What a machine
 * keeps is listed by each module (*_machine: the RAM, the CPU, ...); what both share stays (the
 * disc, the windows, the pads' buttons, the configuration). The second machine draws on the
 * second GPU (gpu_view2.c, g_gpu_route 2), has no window and makes no sound.
 *
 * --mirror (testing): the second machine starts at the boot as the first and gets the same
 * buttons; at each vblank the two are compared: everything must be the same.
 *
 * Parked (step 2): the second machine can stop at the entry of the farm's frame (800C14A0, the
 * hook machine_farm_frame) and wait there while the first goes on alone. To take it up again,
 * the first, at the entry of its own farm's frame, gives it all its state: the second goes on
 * from there as the first does. That is right only if the native calls under the two hooks are
 * the same ones (the game's functions keep everything in the CPU struct and the RAM, but each
 * returns to the native code that called it), so the two chains of return addresses are
 * compared first (RtlCaptureStackBackTrace); if they differ, the next farm's frame is tried.
 * --mirror-park F / --mirror-resume F (testing): park at the first farm's frame after frame F,
 * take it up again at the first after the other F; the comparison goes on after that. */
#include "psx.h"
#include "gpu.h"
#include "gte.h"
#ifdef _WIN32
#include <windows.h>
#endif

int g_machine;
int g_mirror;
int g_split;
int g_machine_probe;
int g_heard;
static int route_first;                 /* the first's GPU route (1 with the second window) */
static int split, split_asked;          /* the farmers apart: farmer 2 is the second's player */
static int join_asked;                  /* the farmers are in the same place again: the second parks */
static int bring_beside;                /* the join is the player leaving the farm: farmer 2 goes along */
#define MAP 0x8009F790u                 /* the loaded area's map number (fixes.c) */
#define FRAME_STATE 0x800A0490u         /* 0: no door or way out being taken */
#define MENU_FLAGS 0x8006C844u          /* 08000000: a menu or a talk has the game */
static int farm_ran[2];                 /* each machine ran the farm's frame since the last world sync */
static int armed;                       /* this farm's frame's start kept for the player's door */
static uint64_t armed_frame;
static void *gs_armed;

/* The world (step 4): what the save keeps, the two machines' each farm's frame. Found by comparing
 * the two RAMs with the farmers in different places: in 80070000-80080000 they differ only in the
 * player's struct, the farm module's state and a pointer at 70C8C, the clock and the places of the
 * things that move (the dog, ...); the places' own data is elsewhere, but for 80070000-80070C88,
 * which an area's load writes (and the two had the same there then). So a byte there that only one
 * machine changed since the last time goes to the other; changed by both, the first's stays. Left
 * out: the player's struct (each machine's own farmer: its money is the two's, added up, and its
 * rucksack the bigger), the farm module's state (70CC0), 70C88-70C8F, the player's extra animations.
 * The clock (the date, the hour, the minutes): see clock_seen. */
#define WORLD_LO 0x70000u
#define WORLD_N 0x10000u
enum { W_MERGE, W_SKIP, W_FIRST };
static uint8_t *world_kind, *world_seen;
#define GH_ROWS_AT 0x9B9E0u              /* the building picked at the carpenter's (buildorder.c, 16 bytes) and the
                                          * bigger greenhouse's rows 7-18 (greenhouse.c), out of the block */
#define GH_ROWS_N (16 + 672)
static uint8_t gh_seen[GH_ROWS_N];
static int32_t money_seen;
/* The clock: the minute goes on when a counter (a word at 8006C90C) reaches the hour's pace; in the
 * houses neither moves. Each machine has its own, so the clock is the one of a machine whose clock
 * went on since the last time: the first's if both (then the second's counter takes the first's,
 * the two in step: had both gone on, the time went twice as fast), else the second's (the player
 * in a house, farmer 2 out on the farm: the time goes on). */
#define CLOCK_LO 0x70CF4u
#define CLOCK_N 0x10u
#define SUBMINUTE 0x6C90Cu
static uint8_t clock_seen[CLOCK_N];
static uint32_t sub_seen;
#define PLAYER_AT 0x71A00u
#define MONEY_AT (PLAYER_AT + 0x5C)
#define BAG_AT (PLAYER_AT + 0x1E)
static void world_init(void);
static void world_sync(uint8_t *b, int first_time);
void view2_gpu_load_state(const void *src);
extern int view2_follows_player;    /* gpu.c (the second GPU): its in-between pictures follow the player */

typedef struct { const char *name; void *p; size_t n, off; int compare; } Region;
#define MAX_REGIONS 192
static Region reg[MAX_REGIONS];
static int nreg;
static size_t total;
static uint8_t *state[3];          /* what each machine keeps while the other runs; machine_hold's */
static void (*boot_fn)(void);
static int started;
static uint64_t compared, differed;
static int reported;
int g_mirror_park, g_mirror_resume;     /* testing: frames */
enum { RUNS, PARKING, PARKED, RESUMED };
static int second = RUNS;               /* what the second machine does */
#define CHAIN 128
static void *chain[CHAIN];               /* the native return addresses under the parked hook */
static int chain_n;                      /* ... the game's part of them (above the fiber's start) */
#ifdef _WIN32
static void *fiber[2];
#endif

/* the first's GPU state into the second GPU */
static void gpu_to_second(void) {
    static void *gs;
    if (!gs && !(gs = malloc(gpu_state_size()))) fatal("machine: not enough memory");
    gpu_save_state(gs);
    view2_gpu_load_state(gs);
}

static void add(const char *name, void *p, size_t n) {
    if (nreg == MAX_REGIONS) fatal("machine: too many regions");
    /* jump buffers hold each machine's own stack, and only the first writes the cards' files:
     * not the same in the two */
    int cmp = strcmp(name, "exc_jb") != 0 && strcmp(name, "card_dirty") != 0;
    reg[nreg++] = (Region){ name, p, n, total, cmp };
    total += n;
}

static void save(uint8_t *b) {
    for (int i = 0; i < nreg; i++) memcpy(b + reg[i].off, reg[i].p, reg[i].n);
}

static void load(const uint8_t *b) {
    for (int i = 0; i < nreg; i++) memcpy(reg[i].p, b + reg[i].off, reg[i].n);
}

#ifdef _WIN32
static void CALLBACK second_main(void *arg) {
    (void)arg;
    boot_fn();
    fatal("the second machine's game returned");
}

/* the native return addresses above this function's caller (inlined: the caller's frame is skipped
 * wherever it is called from in it) */
static inline __attribute__((always_inline)) int capture(void **out) {
    return RtlCaptureStackBackTrace(1, CHAIN, out, NULL);
}
/* the native calls under this hook are those under the parked second's (machine_farm_frame) */
static inline __attribute__((always_inline)) int chain_same(void) {
    void *t[CHAIN];
    int n = capture(t);
    return n >= chain_n && !memcmp(t, chain, (size_t)chain_n * sizeof *t);
}

#endif

/* the second GPU, as the second view sets it up (p2view.c) */
static void second_gpu(void) {
    int up = gpu_upscale();
    if (g_cfg.p2view_upscale > 0 && g_cfg.p2view_upscale < up) up = g_cfg.p2view_upscale;
    view2_gpu_init();
    view2_g_pgxp_cracks = g_pgxp_cracks;
    view2_gpu_set_upscale(up);
    view2_gpu_set_threads(gpu_threads());
    view2_g_pgxp = g_pgxp && view2_gpu_upscale() > 1;
    view2_g_pgxp_persp = g_pgxp_persp && view2_g_pgxp;
    view2_g_sprite_depth = g_sprite_depth && view2_g_pgxp;
    view2_g_sprite_ws = g_sprite_ws;
    view2_g_sprite_tags = g_sprite_tags;
    view2_g_scene_3d = g_scene_3d;
}

/* before the boot: the second machine starts from the state the first boots from */
void machine_init(void (*boot)(void)) {
    /* on one PC: with farmer 2's window; online (step 6): both PCs run both machines in step, the
     * host shows the first, the guest the second (its window shows the second GPU, g_view2 2) */
    if (g_split && (!g_p2_enabled || (!netplay_role() && g_view2 != 1))) {
        LOGW("--split: only with farmer 2 (and its window, on one PC)");
        g_split = 0;
    }
    /* on one PC the second machine costs too much: farmer 2 follows farmer 1 there; the split is
     * for the online co-op only (a replay of an online session still gets it) */
    if (g_split && !netplay_role() && !g_cfg.input_script[0]) {
        LOGW("split: online co-op only");
        g_split = 0;
    }
    if (g_mirror && g_split) g_mirror = 0;
    if (!g_mirror && !g_split) return;
#ifndef _WIN32
    LOGW("the second machine: only on Windows for now");
    g_mirror = g_split = 0;
    return;
#else
    if (g_mirror && g_view2 > 0) {
        LOGW("--mirror: not with the second window");
        g_mirror = 0;
        return;
    }
    mem_machine(add);
    add("cpu", &g_cpu, sizeof g_cpu);
    gte_machine(add);
    sched_machine(add);
    dma_machine(add);
    cdrom_machine(add);
    sio_machine(add);
    bios_machine(add);
    memcard_machine(add);
    spu_machine(add);
    quick_machine(add);
    cheats_machine(add);
    p2_machine(add);
    add("gp0_addr", &g_gp0_addr, sizeof g_gp0_addr);
    add("hud_call", &g_hud_call, sizeof g_hud_call);
    add("held_call", &g_held_call, sizeof g_held_call);
    add("item_call", &g_item_call, sizeof g_item_call);
    add("menu_tile", &g_menu_tile, sizeof g_menu_tile);
    state[0] = malloc(total);
    state[1] = malloc(total);
    state[2] = malloc(total);
    if (!state[0] || !state[1] || !state[2]) fatal("machine: not enough memory");
    if (g_view2 <= 0) second_gpu();     /* else the second view set it up (p2view.c) */
    route_first = g_gpu_route;
    /* farmers in different places: the second boots as the first and waits at the first farm's
     * frame till farmer 2 goes through a door */
    if (g_split) {
        second = PARKING;
        /* till then only the second feeds the second GPU: it sends what the first does, and both
         * feeding it mixed their drawing states (shadows came out as squares at the start) */
        g_gpu_route = 0;
    }
    save(state[1]);
    boot_fn = boot;
    fiber[0] = ConvertThreadToFiber(NULL);
    fiber[1] = CreateFiberEx(64 * 1024, 16 * 1024 * 1024, FIBER_FLAG_FLOAT_SWITCH, second_main, NULL);
    if (!fiber[0] || !fiber[1]) fatal("machine: no fiber (%lu)", (unsigned long)GetLastError());
    started = 1;
    LOGI("%s: a second machine runs as the first (%zu KB of state, %d parts)", g_split ? "split" : "mirror", total / 1024, nreg);
#endif
}

static void compare(void) {
    compared++;
    int same = 1;
    for (int i = 0; i < nreg; i++) {
        const Region *r = &reg[i];
        if (!r->compare) continue;
        const uint8_t *a = state[0] + r->off, *b = state[1] + r->off;
        if (!memcmp(a, b, r->n)) continue;
        same = 0;
        if (reported >= 12) continue;
        size_t first = 0, n = 0;
        for (size_t k = 0; k < r->n; k++)
            if (a[k] != b[k]) { if (!n) first = k; n++; }
        reported++;
        if (r->p == (void *)g_ram)
            LOGI("mirror: DIFFERENT at frame %llu: ram, %zu bytes, first %08X (%02X / %02X)",
                 (unsigned long long)frame_count(), n, 0x80000000u + (unsigned)first, a[first], b[first]);
        else
            LOGI("mirror: DIFFERENT at frame %llu: %s, %zu bytes, first +%zX (%02X / %02X)",
                 (unsigned long long)frame_count(), r->name, n, first, a[first], b[first]);
    }
    if (!same) differed++;
    if (compared % 600 == 0)
        LOGI("mirror: %llu frames compared, %llu different", (unsigned long long)compared, (unsigned long long)differed);
}

/* the farm's frame begins (800C14A0, before the second view's run) */
void machine_farm_frame(CPU *c) {
    (void)c;
    if (!started || g_view2_run) return;    /* not the second window's run of the frame (p2view.c) */
    farm_ran[g_machine] = 1;
#ifdef _WIN32
    if (g_machine == 1) {
        if (second != PARKING && !join_asked) return;
        /* parked here: the game's part of the chain is what lies above second_main */
        void *t[CHAIN];
        int n = capture(t);
        chain_n = -1;
        for (int i = 0; i < n; i++)
            if ((uintptr_t)t[i] - (uintptr_t)second_main < 256) { chain_n = i; break; }
        if (chain_n < 0) fatal("machine: the second machine's stack has no start");
        memcpy(chain, t, sizeof t);
        second = PARKED;
        LOGI("mirror: parked at frame %llu (%d native calls)", (unsigned long long)frame_count(), chain_n);
        save(state[1]);
        load(state[0]);
        g_machine = 0;
        g_gpu_route = route_first;
        /* the second GPU has the first's again (the rest of this frame never reached it; the first
         * fed it nothing while the second ran) */
        if (g_split) gpu_to_second();
        SwitchToFiber(fiber[0]);
        /* taken up again: the first's state was put in place (machine_vblank) */
        LOGI("mirror: going on from the first's state at frame %llu", (unsigned long long)frame_count());
        if (split) p2_become_player();      /* farmer 2 is this machine's player now */
        return;
    }
    if (second == RUNS && g_mirror_park && frame_count() >= (uint64_t)g_mirror_park) {
        second = PARKING;
        g_mirror_park = 0;              /* once */
        return;
    }
    if (second != PARKED) return;
    armed = 0;
    if (!split_asked && (!g_mirror_resume || frame_count() < (uint64_t)g_mirror_resume)) {
        /* the player at a door with farmer 2 about: this frame's start is kept, in case the door
         * takes the player through (then farmer 2 stays here, in the second: machine_split_player) */
        if (g_split && p2_player_at_door() && chain_same()) {
            save(state[1]);
            if (!gs_armed && !(gs_armed = malloc(gpu_state_size()))) fatal("machine: not enough memory");
            gpu_save_state(gs_armed);
            armed = 1;
            armed_frame = frame_count();
        }
        return;
    }
    void *t[CHAIN];
    int n = capture(t);
    if (n < chain_n || memcmp(t, chain, (size_t)chain_n * sizeof *t)) {
        static uint64_t told;
        if (frame_count() - told >= 600) {
            told = frame_count();
            LOGI("mirror: the native calls differ (%d / %d): not taken up", n, chain_n);
        }
        return;
    }
    save(state[1]);                  /* the second goes on from here, as the first */
    /* ... with the first's GPU too: its own got nothing while it was parked (the game reads the
     * GPU's status, and draws over what is in the VRAM) */
    static void *gs;
    if (!gs && !(gs = malloc(gpu_state_size()))) fatal("machine: not enough memory");
    gpu_save_state(gs);
    view2_gpu_load_state(gs);
    second = RESUMED;
    LOGI("mirror: taken up at frame %llu", (unsigned long long)frame_count());
    if (split_asked) {
        split_asked = 0;
        split = 1;
        if (!world_kind) world_init();
        world_sync(g_ram, 1);           /* the two have the same world now */
        p2_away();                      /* farmer 2 is the second's now */
        view2_follows_player = 1;
        g_gpu_route = 0;                /* the second window shows the second machine's GPU */
        LOGI("split: farmer 2 goes through a door: the second machine is farmer 2's (frame %llu)", (unsigned long long)frame_count());
    }
#endif
}

int machine_vblank(void) {
    if (!started) return 0;
#ifdef _WIN32
    if (g_machine == 0 && second == PARKED) return 0;     /* the first goes on alone */
    if (g_machine == 0 && second == RESUMED) second = RUNS;
    if (g_machine == 0) {
        uint16_t mine = g_pad_buttons[0];
        if (split) g_pad_buttons[0] = g_p2_buttons;     /* the second's player is farmer 2 */
        save(state[0]);
        load(state[1]);
        g_machine = 1;
        g_gpu_route = 2;
        /* farmer 2's window: on one PC its effects, mixed into the first's sound; online the guest
         * hears all of it (and not the first's), the host none of it */
        g_spu_effects_only = split && !netplay_role();
        g_heard = split && netplay_role() == 2;
        SwitchToFiber(fiber[1]);
        g_spu_effects_only = 0;
        /* the second machine reached the same vblank and put the first back */
        g_pad_buttons[0] = mine;
        if (g_mirror && second == RUNS) compare();
        /* both ran the farm's frame since the last time (it runs every other vblank, and the two
         * need not be in step) */
        if (split && second == PARKED && join_asked) {
            /* the second parked at its farm's frame: farmer 2 comes back here */
            world_sync(state[1], 0);
            p2_come_back(state[1], bring_beside);
            bring_beside = 0;
            p2view_rejoin();
            static void *gs;            /* the second window: the second view's again (p2view.c) */
            if (!gs && !(gs = malloc(gpu_state_size()))) fatal("machine: not enough memory");
            gpu_save_state(gs);
            view2_gpu_load_state(gs);
            split = join_asked = 0;
            view2_follows_player = 0;
            farm_ran[0] = farm_ran[1] = 0;
            g_gpu_route = route_first;
            LOGI("split: the farmers meet again on map %u: farmer 2 is back (frame %llu)", ram_r32(MAP), (unsigned long long)frame_count());
            return 0;
        }
        if (split && farm_ran[0] && farm_ran[1]) {
            world_sync(state[1], 0);    /* the RAM is the state's first part */
            farm_ran[0] = farm_ran[1] = 0;
            /* the same map, no door being taken, no menu or talk in the second, its farmer not led */
            const uint8_t *b = state[1];
            uint32_t mb, fb, nb, lb;
            memcpy(&mb, b + (MAP & 0x1FFFFF), 4);
            memcpy(&fb, b + (FRAME_STATE & 0x1FFFFF), 4);
            memcpy(&nb, b + (MENU_FLAGS & 0x1FFFFF), 4);
            memcpy(&lb, b + 0x71A6C, 4);
            if (!join_asked && mb == ram_r32(MAP) && !fb && !ram_r32(FRAME_STATE) && !(nb & 0x08000000u) &&
                !(lb & 0x00800000u)) {
                join_asked = 1;
                LOGI("split: both on map %u (frame %llu)", mb, (unsigned long long)frame_count());
            }
        }
        return 0;
    }
    save(state[1]);
    load(state[0]);
    g_machine = 0;
    g_gpu_route = split || second == PARKING ? 0 : route_first;
    SwitchToFiber(fiber[0]);
    return 1;
#else
    return 0;
#endif
}

static void world_init(void) {
    world_kind = calloc(WORLD_N, 1);
    world_seen = malloc(WORLD_N);
    if (!world_kind || !world_seen) fatal("machine: not enough memory");
    static const struct { uint32_t lo, hi; uint8_t k; } R[] = {
        { 0x71A00, 0x71DFC, W_SKIP },       /* the player */
        /* written by an area's load (a machine that loads one wrote 1.7 KB there: in the other,
         * which was elsewhere, an animation was not found and the game froze; nothing of the
         * world is known below the date) and a pointer into the area's data */
        { 0x70000, 0x70C88, W_SKIP },
        { 0x75B60, 0x75B68, W_SKIP },
        { 0x70CC0, 0x70CC4, W_SKIP },       /* the farm module's state (800BBC34's table) */
        { 0x70C88, 0x70C90, W_SKIP },
        { 0x7BFF8, 0x7F7F8, W_SKIP },       /* the player's extra animations (p2.c ANIMS) */
        { 0x70CF4, 0x70D04, W_FIRST },      /* the date and the clock */
    };
    for (size_t i = 0; i < sizeof R / sizeof *R; i++)
        memset(world_kind + (R[i].lo - WORLD_LO), R[i].k, R[i].hi - R[i].lo);
}

/* after both ran their farm's frame: the world's changes each way (b: the second's RAM) */
static void world_sync(uint8_t *b, int first_time) {
    uint8_t *a = g_ram + WORLD_LO, *s = world_seen;
    b += WORLD_LO;
    int32_t ma, mb;
    memcpy(&ma, g_ram + MONEY_AT, 4);
    memcpy(&mb, b - WORLD_LO + MONEY_AT, 4);
    uint8_t *ca = g_ram + CLOCK_LO, *cb = b - WORLD_LO + CLOCK_LO;
    uint32_t sa, sb;
    memcpy(&sa, g_ram + SUBMINUTE, 4);
    memcpy(&sb, b - WORLD_LO + SUBMINUTE, 4);
    if (first_time) {               /* b: the RAM the two start from */
        memcpy(s, b, WORLD_N);
        memcpy(gh_seen, b - WORLD_LO + GH_ROWS_AT, GH_ROWS_N);
        money_seen = mb;
        memcpy(clock_seen, cb, CLOCK_N);
        sub_seen = sb;
        return;
    }
    {
        int a_on = memcmp(ca, clock_seen, CLOCK_N) || sa != sub_seen;
        int b_on = memcmp(cb, clock_seen, CLOCK_N) || sb != sub_seen;
        const uint8_t *c = a_on || !b_on ? ca : cb;
        uint32_t sub = a_on || !b_on ? sa : sb;
        memcpy(clock_seen, c, CLOCK_N);
        sub_seen = sub;
        memcpy(ca, clock_seen, CLOCK_N);
        memcpy(cb, clock_seen, CLOCK_N);
        memcpy(g_ram + SUBMINUTE, &sub, 4);
        memcpy(b - WORLD_LO + SUBMINUTE, &sub, 4);
    }
    for (uint32_t i = 0; i < WORLD_N; i += 64) {     /* most of it changes in neither */
        if (!memcmp(a + i, s + i, 64) && !memcmp(b + i, s + i, 64)) continue;
        for (uint32_t k = i; k < i + 64; k++) {
            uint8_t kind = world_kind[k], v;
            if (kind == W_SKIP) continue;
            if (kind == W_FIRST) v = a[k];             /* the clock: set above */
            else if (a[k] != s[k]) v = a[k];
            else v = b[k];
            a[k] = b[k] = s[k] = v;
        }
    }
    {
        uint8_t *ga = g_ram + GH_ROWS_AT, *gb = b - WORLD_LO + GH_ROWS_AT;
        for (uint32_t k = 0; k < GH_ROWS_N; k++) {
            uint8_t v = ga[k] != gh_seen[k] ? ga[k] : gb[k];
            ga[k] = gb[k] = gh_seen[k] = v;
        }
    }
    int32_t m = money_seen + (ma - money_seen) + (mb - money_seen);
    memcpy(g_ram + MONEY_AT, &m, 4);
    memcpy(b - WORLD_LO + MONEY_AT, &m, 4);
    money_seen = m;
    int16_t ga, gb;
    memcpy(&ga, g_ram + BAG_AT, 2);
    memcpy(&gb, b - WORLD_LO + BAG_AT, 2);
    if (gb > ga) memcpy(g_ram + BAG_AT, &gb, 2);
    else if (ga > gb) memcpy(b - WORLD_LO + BAG_AT, &ga, 2);
}

/* The player went through a door (p2.c, right after the player's turn, this farm's frame): farmer
 * 2 stays where it was. The second machine goes on from this frame's start (kept when the player
 * was at the door, before the door was taken) with farmer 2 as its player; here farmer 2 is away.
 * 1: done */
int machine_split_player(void) {
    if (g_view2_run || !armed || armed_frame != frame_count() || split || g_machine || second != PARKED) return 0;
    armed = 0;
    view2_gpu_load_state(gs_armed);
    second = RESUMED;
    split = 1;
    if (!world_kind) world_init();
    world_sync(state[1], 1);            /* the world as the second starts it (this tick's changes are the first's) */
    p2_away();
    view2_follows_player = 1;
    g_gpu_route = 0;
    LOGI("split: the player goes through a door, farmer 2 stays: the second machine is farmer 2's (frame %llu)",
         (unsigned long long)frame_count());
    return 1;
}

/* a question to the game with everything put back after (p2.c: would the doors take farmer 2
 * through?): no events run meanwhile */
void machine_hold(void) {
    if (!started) return;
    save(state[2]);
    g_machine_probe = 1;
}

void machine_unhold(void) {
    if (!started) return;
    load(state[2]);
    g_machine_probe = 0;
}

/* farmer 2 goes through a door: at the next farm's frame the second machine takes the game */
void machine_split_request(void) {
    if (!started || g_view2_run || split || second != PARKED || g_machine) return;
    if (!split_asked) LOGI("split: farmer 2 at a door (frame %llu)", (unsigned long long)frame_count());
    split_asked = 1;
}

int machine_split(void) { return split; }

/* the player takes a way out of the farm while farmer 2 is in the second machine (p2.c): the
 * second parks at its next farm's frame and farmer 2 comes back, to be placed beside the player
 * where it arrives (farmer 2 is on its own on the farm only) */
void machine_bring_back(void) {
    if (!started || !split || g_machine || g_view2_run || join_asked) return;
    join_asked = 1;
    bring_beside = 1;
    LOGI("split: the player leaves the farm: farmer 2 comes along (frame %llu)", (unsigned long long)frame_count());
}

uint32_t machine_hash(uint32_t h) {
    if (!split) return h;
    const uint32_t *w = (const uint32_t *)state[1];     /* the RAM is the state's first part */
    for (int i = 0; i < 0x200000 / 4; i++) h = (h ^ w[i]) * 16777619u;
    return h;
}
