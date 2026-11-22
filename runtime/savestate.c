/* Save states ("save anywhere"): the whole machine in a file, states/slotN.state in the game's
 * folder.
 *
 * The recompiled game runs on the native stack, which can't be saved. But the game's main loop
 * (80011370) never returns; for each scene it calls 80011C24 from one place, and that runs the
 * scene's frames in a loop (80011C80 .. 80011D90). At the top of that loop (the hook
 * savestate_point) the game is between two frames and the native calls under it are always the
 * same (the generated code keeps everything in the CPU struct and the RAM), so a state is saved
 * there and put back there, at any later frame and from any scene (the title too). F8 / F10 and the
 * overlay (F7) only ask; the next frame does it.
 *
 * What is kept: what each module keeps per machine (machine_parts, the second machine's list), the
 * GPU's state (its VRAM: the upscaled copy and Vulkan's are made again from it) and what the
 * platform set for the frame being run (the pads' buttons, the music held). Not: the memory cards'
 * contents (a load must not undo the diary saves made since; no state is made while a save / load
 * screen or a card file is open) and the BIOS's native jump buffer.
 *
 * The file: a header, the list of parts (name, size), the parts. It loads only into a build with
 * the same parts (their sizes change when the runtime's do).
 *
 * --savestate-test N:K (testing): saves at frame N (to states/test.state), runs K frames and keeps
 * everything, loads the state from the file, runs the same K frames again (with the same --input)
 * and compares: exit code 0 when all of it came out the same. */
#include "psx.h"
#include "gpu.h"
#include "overlay.h"
#include <time.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir_one(p) _mkdir(p)
#else
#define mkdir_one(p) mkdir(p, 0755)
#endif
#ifdef HMBTN_VULKAN
#include "vk/vk_renderer.h"
#endif

void view2_gpu_load_state(const void *src);

#define MAGIC "HMBTNSS1"
#define SAVELOAD_FUNC 0x8012E15Cu       /* a function of the save / load screens' overlay */
#define WAIT_FRAMES 300                 /* a request not done by then: the game is busy (a load) */

typedef struct { char name[32]; void *p; uint32_t n; size_t off; } Part;   /* p NULL: the GPU's */
#define MAX_PARTS 200
static Part part[MAX_PARTS];
static int nparts;
static size_t total;
static int music;                       /* quick_music_held, as a part */

typedef struct {
    char magic[8];
    uint32_t build, nparts;
    int64_t saved_at;                   /* time() */
    uint64_t frame;
} Head;
typedef struct { char name[32]; uint32_t n; } Entry;

static int want = -1, want_load;        /* the slot asked for (-1: none), a load */
static uint64_t asked_at;
static uint8_t *buf;                    /* a state: read from the file, or made at the hook */
static int status = SS_NONE, status_slot;
static unsigned serial;
static int slot;
static char info[SS_SLOTS][48];
static int info_read[SS_SLOTS];

static void add(const char *name, void *p, size_t n) {
    static const char *const skip[] = { "exc_jb", "card", "card_dirty", "card_bank", "card_req" };
    for (size_t i = 0; i < sizeof skip / sizeof *skip; i++)
        if (!strcmp(name, skip[i])) return;
    if (nparts == MAX_PARTS) fatal("savestate: too many parts");
    Part *q = &part[nparts++];
    snprintf(q->name, sizeof q->name, "%s", name);
    q->p = p;
    q->n = (uint32_t)n;
    q->off = total;
    total += n;
}

static int init(void) {
    if (nparts) return 1;
    machine_parts(add);
    add("pad_buttons", g_pad_buttons, sizeof g_pad_buttons);
    add("p2_buttons", &g_p2_buttons, sizeof g_p2_buttons);
    add("music_held", &music, sizeof music);
    add("gpu", NULL, gpu_state_size());
    buf = malloc(total);
    if (!buf) { nparts = 0; return 0; }
    return 1;
}

static void snap(uint8_t *b) {
    music = quick_music_held();
    for (int i = 0; i < nparts; i++) {
        if (part[i].p) memcpy(b + part[i].off, part[i].p, part[i].n);
        else gpu_save_state(b + part[i].off);
    }
}

static void unsnap(const uint8_t *b) {
    const uint8_t *gs = NULL;
    for (int i = 0; i < nparts; i++) {
        if (part[i].p) memcpy(part[i].p, b + part[i].off, part[i].n);
        else gpu_load_state(gs = b + part[i].off);
    }
    quick_hold_music(music != 0);
    if (g_view2 > 0) view2_gpu_load_state(gs);     /* farmer 2's window: the same VRAM */
#ifdef HMBTN_VULKAN
    if (g_vk) {                                     /* Vulkan's VRAM from the GPU's */
        int o = vkr_enter(0);
        vkr_upload(0, 0, 1024, 512, gpu_vram());
        vkr_target(o);
    }
#endif
    platform_state_loaded();
}

static void path_of(int s, char *out, size_t n) {     /* s -1: --savestate-test's */
    if (s < 0) snprintf(out, n, "%sstates/test.state", exe_dir_path());
    else snprintf(out, n, "%sstates/slot%d.state", exe_dir_path(), s + 1);
}

static void done(int st) {
    status = st;
    status_slot = want >= 0 ? want : status_slot;
    serial++;
    want = -1;
}

static int write_state(int s) {
    char path[1200], tmp[1210];
    snprintf(path, sizeof path, "%sstates", exe_dir_path());
    mkdir_one(path);
    path_of(s, path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return 0;
    Head h;
    memset(&h, 0, sizeof h);
    memcpy(h.magic, MAGIC, 8);
    h.build = netplay_build_id();
    h.nparts = (uint32_t)nparts;
    h.saved_at = (int64_t)time(NULL);
    h.frame = frame_count();
    int ok = fwrite(&h, sizeof h, 1, f) == 1;
    for (int i = 0; i < nparts && ok; i++) {
        Entry e;
        memset(&e, 0, sizeof e);
        memcpy(e.name, part[i].name, sizeof e.name);
        e.n = part[i].n;
        ok = fwrite(&e, sizeof e, 1, f) == 1;
    }
    ok = ok && fwrite(buf, 1, total, f) == total;
    ok = fclose(f) == 0 && ok;
    if (ok) {
        remove(path);                   /* (Windows: rename doesn't replace) */
        ok = rename(tmp, path) == 0;
    }
    if (!ok) remove(tmp);
    if (s >= 0) info_read[s] = 0;
    return ok;
}

/* the state in slot s into buf: SS_NONE when it's there and fits this build */
static int read_state(int s, Head *h) {
    char path[1200];
    path_of(s, path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) return SS_EMPTY;
    int st = SS_NONE;
    if (fread(h, sizeof *h, 1, f) != 1 || memcmp(h->magic, MAGIC, 8)) st = SS_ERROR;
    else if (h->nparts != (uint32_t)nparts) st = SS_OTHER;
    for (int i = 0; i < nparts && st == SS_NONE; i++) {
        Entry e;
        if (fread(&e, sizeof e, 1, f) != 1) st = SS_ERROR;
        else if (strncmp(e.name, part[i].name, sizeof e.name) || e.n != part[i].n) st = SS_OTHER;
    }
    if (st == SS_NONE && fread(buf, 1, total, f) != total) st = SS_ERROR;
    fclose(f);
    return st;
}

/* ------------------------------------------------------------------ asked by the player */
void savestate_request(int load, int s) {
    if (s < 0 || s >= SS_SLOTS || want >= 0) return;
    status_slot = s;
    if (!init()) { done(SS_ERROR); return; }
    if (g_split) { done(SS_APART); return; }
    if (load && netplay_active()) { done(SS_ONLINE); return; }
    if (load) {
        Head h;
        int st = read_state(s, &h);     /* read now: the game is not touched if it can't be */
        if (st != SS_NONE) { done(st); return; }
    }
    want = s;
    want_load = load;
    asked_at = frame_count();
    status = SS_WAITING;
}

static void check_wait(void) {
    if (want >= 0 && frame_count() - asked_at > WAIT_FRAMES) done(SS_TIMEOUT);
}

int savestate_pending(void) { check_wait(); return want >= 0; }

int savestate_status(int *s, unsigned *n) {
    check_wait();
    if (s) *s = status_slot;
    if (n) *n = serial;
    return status;
}

int savestate_slot(void) { return slot; }
void savestate_set_slot(int s) { if (s >= 0 && s < SS_SLOTS) slot = s; }

int savestate_slot_info(int s, char *out, int n) {
    if (s < 0 || s >= SS_SLOTS) return 0;
    if (!info_read[s]) {
        info_read[s] = 1;
        info[s][0] = 0;
        char path[1200];
        path_of(s, path, sizeof path);
        FILE *f = fopen(path, "rb");
        Head h;
        if (f) {
            if (fread(&h, sizeof h, 1, f) == 1 && !memcmp(h.magic, MAGIC, 8)) {
                time_t t = (time_t)h.saved_at;
                struct tm *lt = localtime(&t);
                if (lt) strftime(info[s], sizeof info[s], "%Y-%m-%d  %H:%M", lt);
                if (h.build != netplay_build_id()) strncat(info[s], "  *", sizeof info[s] - strlen(info[s]) - 1);
            }
            fclose(f);
        }
    }
    snprintf(out, (size_t)n, "%s", info[s]);
    return info[s][0] != 0;
}

/* ------------------------------------------------------------------ testing */
static void test_step(void) {
    static int phase;
    static uint64_t end;
    static uint8_t *a, *b;
    uint64_t f = frame_count();
    if (phase == 0 && f >= (uint64_t)g_cfg.state_test_at) {
        if (!init() || !(a = malloc(total)) || !(b = malloc(total))) fatal("savestate test: not enough memory");
        snap(buf);
        if (!write_state(-1)) fatal("savestate test: can't write states/test.state");
        end = f + (uint64_t)g_cfg.state_test_frames;
        phase = 1;
        LOGI("savestate test: saved at frame %llu (%zu KB, %d parts)", (unsigned long long)f, total / 1024, nparts);
    } else if (phase == 1 && f >= end) {
        snap(a);
        end = f;
        Head h;
        memset(buf, 0, total);
        int st = read_state(-1, &h);
        if (st != SS_NONE) fatal("savestate test: states/test.state didn't load (%d)", st);
        unsnap(buf);
        phase = 2;
        LOGI("savestate test: frame %llu kept, the state loaded: frame %llu again", (unsigned long long)f,
             (unsigned long long)frame_count());
    } else if (phase == 2 && f >= end) {
        snap(b);
        int bad = 0;
        for (int i = 0; i < nparts; i++) {
            const uint8_t *x = a + part[i].off, *y = b + part[i].off;
            if (!memcmp(x, y, part[i].n)) continue;
            size_t k = 0, n = 0;
            for (size_t j = 0; j < part[i].n; j++)
                if (x[j] != y[j]) { if (!n) k = j; n++; }
            LOGI("savestate test: DIFFERENT %s: %zu bytes, first +%zX (%02X / %02X)", part[i].name, n, k, x[k], y[k]);
            bad++;
        }
        LOGI("savestate test: frame %llu, %s", (unsigned long long)f, bad ? "FAILED" : "all the same: PASSED");
        char path[1200];
        path_of(-1, path, sizeof path);
        remove(path);
        fflush(NULL);
        exit(bad ? 1 : 0);
    }
}

/* ------------------------------------------------------------------ the hook */
/* the top of the game's frame loop (80011C80, the delay slot of its first jal) */
void savestate_point(CPU *c) {
    (void)c;
    if (g_machine || g_view2_run || g_machine_probe || g_in_exception) return;
    if (g_cfg.state_test_at) { test_step(); return; }
    if (want < 0) return;
    if (want_load) {
        unsnap(buf);
        done(SS_LOADED);
        LOGI("save state: slot %d loaded (frame %llu)", status_slot + 1, (unsigned long long)frame_count());
        return;
    }
    const char *mod = NULL;
    if (memcard_busy() || bios_card_files_open() || (dispatch_lookup(SAVELOAD_FUNC, &mod) && !strcmp(mod, "saveload"))) {
        done(SS_BUSY);
        return;
    }
    snap(buf);
    int ok = write_state(want);
    LOGI("save state: slot %d %s (frame %llu)", want + 1, ok ? "saved" : "NOT saved", (unsigned long long)frame_count());
    done(ok ? SS_SAVED : SS_ERROR);
}
