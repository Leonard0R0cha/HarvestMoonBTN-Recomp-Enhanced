/* The farm as built (with buildorder): the buildings built out of the game's order show outside.
 * Hooks emitted by tools/recomp.py (GF_FARM, 800BCF54: the area loader).
 *
 * The game: the farm outside is a whole area for each stage of the buildings, in their fixed
 * order (0 nothing, 1 + the chicken coop, 2 + the kitchen, 3 + the barn, 4 + the bigger house,
 * 5 + the greenhouse): area = stage * 4 + the season (800DD274). The areas are in the disc's
 * A_FILE.BIN, file 0, at the offsets of the table at 801217A8 (16 bytes an area: start, the end
 * of part 0 = the start of part 1, ...); 800BCF54 reads part 0 to the buffer at *8007AFF0
 * (800BD374 reads it ahead), keeps its size at 8005E328 (what the game puts after the buffer is
 * placed by it) and copies the buffer's first 0xBDC bytes to 8009F78C.
 *
 * Part 0: 0x000 the area's events (0x2C each from 0x10: doors 0x9000, things to look at 0x4000),
 * 0x940 the things placed (0x2C each, 14: their models are offsets from 0x2000 into the small
 * models after the big one; a free one has 0x8000 at +8), 0x1000 the people's paths (Gotz's to
 * the next building: 0x1260-0x145F, the only part the stages differ in), 0x2000 the big model:
 * a TMD (0x41-like id, flags, n objects; 7 words an object, offsets from 0x200C; one vertex
 * pool for all), a word (1600) and a point for each object, then each object's polygons (4 bytes
 * of header, mode in the 4th: the polygons with 08 in the 1st byte are the field's squares the
 * game finds by that mark), the vertices (8 bytes), and the small models up to the end.
 *
 * With the option, an area of the farm is built from the six of its season: the farm is cut in
 * squares of 1600 (by each object's middle), and a square takes its objects from the stage that
 * agrees most with the buildings standing on the ones that change something there (weighed by
 * how many polygons each changes). The events and the things placed come from stage 0 (stage 4
 * with the bigger house: its door), plus the greenhouse's door when it stands; Gotz's paths go to
 * the building paid for. Built in the game's order, the game's own area. */
#include "psx.h"
#include "psx_recomp.h"
#include <stdlib.h>

#define STAGES 6
#define FARM_AREAS 24
#define AREA_TABLE 0x801217A8u
#define BUFFER_PTR 0x8007AFF0u
#define PICKED 0x8009B9E0u          /* buildorder.c: 1 + the work paid for, else 0 */
#define FLAGS 0x8006C870u
#define BUILDING_FLAG 56            /* a building under way */
#define HOUSE 0x80070D06u           /* 1 the kitchen, 2 the bigger house */
#define BARN 0x80070D08u
#define COOP 0x80070D0Au
#define GREENHOUSE 0x80070D0Cu
#define CELL 1600
#define GRID 32                     /* squares from -16 to 15 each way */
#define TMD 0x2000u
#define BASE 0x200Cu
#define EVENTS 0x10u
#define EVENT_SIZE 0x2Cu
#define GH_EVENT 0x3Cu              /* the greenhouse's door (stage 5) */
#define PLACED 0x940u
#define PLACED_N 14
#define PLACED_END 0xBDCu
#define GH_PLACED 9                 /* the greenhouse's door model (stage 5) */
#define FREE_PLACED 0x8000u
#define PATHS 0x1260u
#define PATHS_END 0x1460u

enum { COOP_B, KITCHEN_B, BARN_B, BIG_HOUSE_B, GREENHOUSE_B, BUILDINGS };

typedef struct {
    uint32_t ptop, nprim;
    int cell;                       /* -1: no polygons */
} Obj;

typedef struct {
    uint8_t *b;
    uint32_t len;                   /* part 0 */
    uint32_t n, vtop, nvert, ntop, tail, tail_len;
    Obj *obj;
    uint64_t *geo;                  /* (cell << 40 | polygon's hash) sorted */
    uint32_t ngeo;
} Area;

static uint32_t a_lba;              /* A_FILE.BIN */
static int a_found;
static int cached_season = -1;
static Area stage[STAGES];
static uint16_t weight[GRID * GRID][BUILDINGS];

static uint32_t rd32(const uint8_t *b, uint32_t o) { uint32_t v; memcpy(&v, b + o, 4); return v; }
static void wr32(uint8_t *b, uint32_t o, uint32_t v) { memcpy(b + o, &v, 4); }
static int floordiv(int a, int d) { return a >= 0 ? a / d : -((-a + d - 1) / d); }

static int read_file0(uint32_t off, uint32_t len, uint8_t *out) {
    uint8_t s[2352];
    for (uint32_t k = 0; k < len; k += 2048) {
        if (!cdrom_read_sector_raw(a_lba + (off + k) / 2048u, s)) return 0;
        memcpy(out + k, s + 24, len - k < 2048 ? len - k : 2048);
    }
    return 1;
}

static uint32_t prim_size(uint8_t mode, int *nv) {
    *nv = mode & 8 ? 4 : 3;
    return (uint32_t)((mode & 4 ? *nv * 4 : 0) + (mode & 16 ? *nv * 4 : 4) + 8);
}

static uint64_t fnv(uint64_t h, const void *p, size_t n) {
    const uint8_t *q = p;
    while (n--) { h ^= *q++; h *= 0x100000001B3ull; }
    return h;
}

static int cmp64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

static void free_area(Area *a) {
    free(a->b); free(a->obj); free(a->geo);
    memset(a, 0, sizeof *a);
}

/* an area read and cut up; its objects' squares and their polygons' shapes (the textures left out) */
static int load_area(Area *a, int area) {
    uint32_t start = MEM_R32(AREA_TABLE + 16u * (uint32_t)area), end = MEM_R32(AREA_TABLE + 16u * (uint32_t)area + 4u);
    if (end <= start || end - start > 0x80000u) return 0;
    a->len = end - start;
    a->b = malloc(a->len);
    if (!a->b || !read_file0(start, a->len, a->b)) return 0;
    a->n = rd32(a->b, TMD + 8);
    if (a->n == 0 || BASE + 28u * a->n > a->len) return 0;
    a->vtop = rd32(a->b, BASE);
    a->nvert = rd32(a->b, BASE + 4);
    a->ntop = rd32(a->b, BASE + 8);
    a->tail = BASE + a->ntop;
    if (a->tail > a->len || BASE + a->vtop + 8u * a->nvert > a->len) return 0;
    a->tail_len = a->len - a->tail;
    a->obj = calloc(a->n, sizeof *a->obj);
    uint32_t total = 0;
    for (uint32_t i = 0; i < a->n; i++) total += rd32(a->b, BASE + 28u * i + 20);
    a->geo = malloc(sizeof *a->geo * (total + 1));
    if (!a->obj || !a->geo) return 0;
    for (uint32_t i = 0; i < a->n; i++) {
        Obj *o = &a->obj[i];
        o->ptop = rd32(a->b, BASE + 28u * i + 16);
        o->nprim = rd32(a->b, BASE + 28u * i + 20);
        uint32_t p = BASE + o->ptop, first = a->ngeo;
        int x0 = 1 << 30, x1 = -(1 << 30), z0 = 1 << 30, z1 = -(1 << 30);
        for (uint32_t k = 0; k < o->nprim; k++) {
            int nv;
            if (p + 4 > a->len) return 0;
            uint8_t mode = a->b[p + 3];
            if ((mode >> 5) != 1) return 0;
            uint32_t sz = prim_size(mode, &nv);
            if (p + 4 + sz > a->len) return 0;
            uint8_t m = mode & (uint8_t)~3u;
            uint64_t h = fnv(0xCBF29CE484222325ull, &m, 1);
            for (int v = 0; v < nv; v++) {
                uint16_t ix; memcpy(&ix, a->b + p + 4 + sz - 8 + 2 * v, 2);
                if (ix >= a->nvert) return 0;
                int16_t xyz[3]; memcpy(xyz, a->b + BASE + a->vtop + 8u * ix, 6);
                h = fnv(h, xyz, 6);
                if (xyz[0] < x0) x0 = xyz[0];
                if (xyz[0] > x1) x1 = xyz[0];
                if (xyz[2] < z0) z0 = xyz[2];
                if (xyz[2] > z1) z1 = xyz[2];
            }
            a->geo[a->ngeo++] = h & 0xFFFFFFFFFFull;
            p += 4 + sz;
        }
        o->cell = -1;
        if (o->nprim) {
            int cx = floordiv(floordiv(x0 + x1, 2), CELL) + GRID / 2, cz = floordiv(floordiv(z0 + z1, 2), CELL) + GRID / 2;
            if (cx < 0 || cx >= GRID || cz < 0 || cz >= GRID) return 0;
            o->cell = cx * GRID + cz;
            for (uint32_t g = first; g < a->ngeo; g++) a->geo[g] |= (uint64_t)o->cell << 40;
        }
    }
    qsort(a->geo, a->ngeo, sizeof *a->geo, cmp64);
    return 1;
}

/* how many polygons each building changes in each square (stage i to i + 1) */
static void weigh(void) {
    memset(weight, 0, sizeof weight);
    for (int i = 0; i < BUILDINGS; i++) {
        const Area *a = &stage[i], *b = &stage[i + 1];
        uint32_t x = 0, y = 0;
        while (x < a->ngeo || y < b->ngeo) {
            uint64_t u = x < a->ngeo ? a->geo[x] : UINT64_MAX, v = y < b->ngeo ? b->geo[y] : UINT64_MAX;
            if (u == v) { x++; y++; continue; }
            uint64_t w = u < v ? u : v;
            if (u < v) x++; else y++;
            int c = (int)(w >> 40);
            if (weight[c][i] < 0xFFFF) weight[c][i]++;
        }
    }
}

static int load_season(int season) {
    if (season == cached_season) return 1;
    for (int k = 0; k < STAGES; k++) free_area(&stage[k]);
    cached_season = -1;
    if (!a_found) {
        uint32_t size;
        if (!iso_find_file("A_FILE.BIN", &a_lba, &size)) return 0;
        a_found = 1;
    }
    for (int k = 0; k < STAGES; k++)
        if (!load_area(&stage[k], k * 4 + season)) {
            LOGE("farmmix: area %d not read", k * 4 + season);
            for (int j = 0; j < STAGES; j++) free_area(&stage[j]);
            return 0;
        }
    weigh();
    cached_season = season;
    return 1;
}

static int flag(int f) { return (int)((MEM_R32(FLAGS + (uint32_t)(f >> 5) * 4u) >> (f & 31)) & 1u); }

static unsigned standing(void) {
    unsigned t = 0;
    if (MEM_R16(COOP)) t |= 1u << COOP_B;
    if (MEM_R16(HOUSE) >= 1) t |= 1u << KITCHEN_B;
    if (MEM_R16(BARN)) t |= 1u << BARN_B;
    if (MEM_R16(HOUSE) >= 2) t |= 1u << BIG_HOUSE_B;
    if (MEM_R16(GREENHOUSE)) t |= 1u << GREENHOUSE_B;
    return t;
}

/* the stage Gotz's paths come from: the building under way's (the greenhouse's works: its) */
static int gotz_stage(void) {
    uint32_t p = MEM_R32(PICKED);
    if (!flag(BUILDING_FLAG) || p == 0) return -1;
    return p - 1 < BUILDINGS ? (int)p - 1 : GREENHOUSE_B;
}

/* the farm needs building: buildorder, a farm area, the buildings (or Gotz's way) out of order */
static int wanted(uint32_t area, unsigned *t, int *g) {
    if (!g_cfg.buildorder || area >= FARM_AREAS) return 0;
    *t = standing();
    *g = gotz_stage();
    int k = 0;
    while (k < BUILDINGS && (*t >> k & 1u)) k++;
    int in_order = *t == (1u << k) - 1u;
    return !(in_order && (*g < 0 || *g == k));
}

static int pick(int cell, unsigned t) {
    int best = 0, best_score = -1, best_dist = 99;
    for (int s = 0; s < STAGES; s++) {
        unsigned have = (1u << s) - 1u;
        int score = 0;
        for (int i = 0; i < BUILDINGS; i++)
            if (((have >> i) & 1u) == ((t >> i) & 1u)) score += weight[cell][i];
        int dist = __builtin_popcount(have ^ t);
        if (score > best_score || (score == best_score && dist < best_dist)) { best = s; best_score = score; best_dist = dist; }
    }
    return best;
}

/* the area's size the game keeps (8005E328, after 800BCF88: a2): the biggest of the season's */
uint32_t farm_area_size(uint32_t entry, uint32_t size) {
    uint32_t area = (entry - AREA_TABLE) / 16u, t;
    int g;
    if (!wanted(area, &t, &g)) return size;
    uint32_t biggest = size;
    for (int k = 0; k < STAGES; k++) {
        uint32_t a = AREA_TABLE + 16u * (uint32_t)(k * 4 + (int)(area & 3));
        uint32_t s = MEM_R32(a + 4) - MEM_R32(a);
        if (s > biggest) biggest = s;
    }
    return biggest;
}

/* part 0 is in the buffer (800BCFAC): the farm built in its place */
void farm_area_loaded(uint32_t entry) {
    uint32_t area = (entry - AREA_TABLE) / 16u, t;
    int g;
    if (!wanted(area, &t, &g)) return;
    int season = (int)(area & 3);
    if (!load_season(season)) return;
    int gh = (t >> GREENHOUSE_B) & 1u, big = (t >> BIG_HOUSE_B) & 1u;
    int hs = big ? (gh ? 5 : 4) : 0;
    const Area *h = &stage[hs];
    uint8_t choice[GRID * GRID];
    for (int c = 0; c < GRID * GRID; c++) choice[c] = (uint8_t)pick(c, t);

    /* the objects: the header stage's in its order (the field's squares keep theirs), then the
     * squares taken from other stages */
    uint32_t cap = 0;
    for (int k = 0; k < STAGES; k++) cap += stage[k].n;
    uint8_t *os = malloc(cap); uint32_t *oi = malloc(cap * sizeof *oi);
    uint32_t n = 0;
    for (uint32_t i = 0; i < h->n; i++)
        if (h->obj[i].cell < 0 || choice[h->obj[i].cell] == hs) { os[n] = (uint8_t)hs; oi[n++] = i; }
    for (int c = 0; c < GRID * GRID; c++) {
        if (choice[c] == hs) continue;
        const Area *a = &stage[choice[c]];
        for (uint32_t i = 0; i < a->n; i++)
            if (a->obj[i].cell == c) { os[n] = choice[c]; oi[n++] = i; }
    }

    /* polygons and one pool of vertices (the same point once) */
    uint32_t psz = 0, maxv = 0;
    for (uint32_t j = 0; j < n; j++) {
        const Area *a = &stage[os[j]];
        const Obj *o = &a->obj[oi[j]];
        uint32_t p = BASE + o->ptop;
        for (uint32_t k = 0; k < o->nprim; k++) { int nv; uint32_t s = 4 + prim_size(a->b[p + 3], &nv); psz += s; p += s; maxv += (uint32_t)nv; }
    }
    uint32_t head = 28u * n + 4u + 8u * n;
    uint8_t *prims = malloc(psz ? psz : 1);
    uint64_t *vkey = malloc(sizeof *vkey * (maxv + 1));        /* hash table of points */
    uint32_t hsize = 1; while (hsize < maxv * 2 + 16) hsize <<= 1;
    int32_t *slot = malloc(sizeof *slot * hsize);
    int16_t *vert = malloc(6 * (size_t)(maxv + 1));
    uint32_t *ptop = malloc(sizeof *ptop * (n + 1));
    for (uint32_t k = 0; k < hsize; k++) slot[k] = -1;
    uint32_t pp = 0, nvert = 0;
    for (uint32_t j = 0; j < n; j++) {
        const Area *a = &stage[os[j]];
        const Obj *o = &a->obj[oi[j]];
        uint32_t p = BASE + o->ptop;
        ptop[j] = head + pp;
        for (uint32_t k = 0; k < o->nprim; k++) {
            int nv; uint32_t s = 4 + prim_size(a->b[p + 3], &nv);
            memcpy(prims + pp, a->b + p, s);
            for (int v = 0; v < nv; v++) {
                uint16_t ix; memcpy(&ix, a->b + p + s - 8 + 2 * v, 2);
                int16_t xyz[3]; memcpy(xyz, a->b + BASE + a->vtop + 8u * ix, 6);
                uint64_t key = (uint64_t)(uint16_t)xyz[0] | (uint64_t)(uint16_t)xyz[1] << 16 | (uint64_t)(uint16_t)xyz[2] << 32;
                uint32_t hk = (uint32_t)(fnv(0xCBF29CE484222325ull, &key, 8) & (hsize - 1));
                while (slot[hk] >= 0 && vkey[slot[hk]] != key) hk = (hk + 1) & (hsize - 1);
                if (slot[hk] < 0) { slot[hk] = (int32_t)nvert; vkey[nvert] = key; memcpy(vert + 3 * nvert, xyz, 6); nvert++; }
                uint16_t nix = (uint16_t)slot[hk];
                memcpy(prims + pp + s - 8 + 2 * v, &nix, 2);
            }
            pp += s; p += s;
        }
    }
    uint32_t vtop = head + psz, ntop = vtop + 8u * nvert;
    uint32_t tail = BASE + ntop;
    uint32_t tail_len = h->tail_len;
    uint32_t biggest = 0;
    for (int k = 0; k < STAGES; k++) if (stage[k].len > biggest) biggest = stage[k].len;
    while (tail_len && h->b[h->tail + tail_len - 1] == 0) tail_len--;   /* its padding */
    int ok = nvert <= 0xFFFF && tail + tail_len <= biggest;
    if (ok) {
        uint8_t *out = calloc(1, biggest);
        memcpy(out, h->b, TMD);
        wr32(out, 4, area);
        wr32(out, TMD, rd32(h->b, TMD));
        wr32(out, TMD + 4, rd32(h->b, TMD + 4));
        wr32(out, TMD + 8, n);
        for (uint32_t j = 0; j < n; j++) {
            uint32_t r = BASE + 28u * j;
            const Area *a = &stage[os[j]];
            wr32(out, r, vtop); wr32(out, r + 4, nvert); wr32(out, r + 8, ntop); wr32(out, r + 12, 0);
            wr32(out, r + 16, ptop[j]); wr32(out, r + 20, a->obj[oi[j]].nprim); wr32(out, r + 24, 0);
            memcpy(out + BASE + 28u * n + 4u + 8u * j, a->b + BASE + 28u * a->n + 4u + 8u * oi[j], 8);
        }
        wr32(out, BASE + 28u * n, rd32(h->b, BASE + 28u * h->n));
        memcpy(out + BASE + head, prims, psz);
        for (uint32_t v = 0; v < nvert; v++) memcpy(out + BASE + vtop + 8u * v, vert + 3 * v, 6);
        memcpy(out + tail, h->b + h->tail, tail_len);

        /* the things placed: their models moved with the small models */
        uint32_t old_t = h->tail - TMD, new_t = tail - TMD;
        for (uint32_t o = PLACED; o < PLACED_END; o += 4) {
            uint32_t w = rd32(out, o);
            if (w >= old_t && w < old_t + h->tail_len) wr32(out, o, w - old_t + new_t);
        }
        /* the greenhouse's door with the house of stage 0 */
        if (gh && hs == 0) {
            const Area *s5 = &stage[5];
            int free_ = -1;
            for (int k = 0; k < PLACED_N && free_ < 0; k++)
                if (rd32(out, PLACED + EVENT_SIZE * (uint32_t)k + 8) == FREE_PLACED) free_ = k;
            if (free_ >= 0) {
                uint32_t dst = PLACED + EVENT_SIZE * (uint32_t)free_, t5 = s5->tail - TMD;
                memcpy(out + dst, s5->b + PLACED + EVENT_SIZE * GH_PLACED, EVENT_SIZE);
                for (uint32_t o = dst; o < dst + EVENT_SIZE; o += 4) {
                    uint32_t w = rd32(out, o);
                    if (w >= t5 && w < t5 + s5->tail_len) wr32(out, o, w - t5 + new_t);
                }
                memcpy(out + GH_EVENT, s5->b + GH_EVENT, EVENT_SIZE);
                uint16_t fs = (uint16_t)free_;
                memcpy(out + GH_EVENT + 12, &fs, 2);
            }
        }
        /* Gotz goes to the building paid for */
        if (g >= 0) memcpy(out + PATHS, stage[g].b + PATHS, PATHS_END - PATHS);

        uint32_t buf = MEM_R32(BUFFER_PTR);
        for (uint32_t k = 0; k < tail + tail_len; k += 4) { uint32_t w; memcpy(&w, out + k, 4); MEM_W32(buf + k, w); }
        LOGI("farmmix: area %u built (buildings %02X, Gotz %d): %u objects, %u bytes", area, t, g, n, tail + tail_len);
        free(out);
    } else LOGE("farmmix: area %u too big, the game's own", area);
    free(os); free(oi); free(prims); free(vkey); free(slot); free(vert); free(ptop);
}
