/* The bigger greenhouse (biggreenhouse): a built greenhouse is 12 rows deeper, 7 x 19 fields
 * instead of 7 x 7. Hooks emitted by tools/recomp.py (GF_FARM).
 *
 * The fields: the farm's 45 x 25 (80071DFC) and the greenhouse's 7 x 7 (80074124) are 8 bytes each,
 * row after row, in the saved block; the greenhouse's rows go on towards -z (row y from z = 1220 -
 * 400 y, 8011E090). Rows 7-18 are kept in RAM nothing uses (8009B9F0) and saved in the card's file
 * past the game's own data. A field's address the game works out past the greenhouse's last row
 * (800742AC) is one of them (the farm's never get there).
 *
 * The model: the greenhouse is map 124 + the season; the area's model (pack + 0x2000, at 80128818)
 * is 24 parts of 1600 x 1600 sharing one vertex table, each part's polygons as GPU packets (a word
 * with the mode in its top byte and 08h in its low byte for a field, the texture words, the
 * colours, then the vertex numbers in halfwords), and after the parts a point per part (the
 * middle of its 1600 square) the renderer (8001C0D4) looks at to skip the parts out of sight.
 * The deeper greenhouse is made from it, by zone across x: on the fields and paths what is past
 * z = -1600 (the back edge) moves back 4800 and row 4's strip (z -800 to -400: the middle, no edge
 * in its colours) is repeated 12 times as rows 7-18; on the door's side (the box's column, x from 2000) the cut is at
 * -2000, so the shipping box's corner (the box, a placed thing at 2400 / -1480, and its shadow)
 * stays by the door, and the strip fills from there; the left wall (to x = -1888) is panes of 800
 * and pillars of 120: past -1080 moves back and its pillar + pane (-1080 to -160) is repeated 5
 * times, stretched to 960. Rows 5 and 6 (darker towards the edge) get row 4's colours and give
 * theirs to rows 17 and 18. An old part is split where its polygons move or not (a part is drawn
 * or skipped by one point).
 * The new parts go after the old
 * ones; the old polygons stay where they are (the
 * fields' table, 8009E5F8: 45 a row, a pointer per field to its floor polygon, which the game
 * repaints when the field changes). The new model goes past what the area's data uses
 * (pack + *8005E328, which grows by it). */
#include "psx.h"
#include "psx_recomp.h"
#include <string.h>

#define GH_BUILT 0x80070D0Cu       /* halfword: the greenhouse is built */
#define GH_GRID 0x80074124u        /* 7 x 7 fields of 8 bytes */
#define GH_END 0x800742ACu         /* past its last row */
#define GH_EXTRA 0x8009B9F0u       /* rows 7-18 */
#define GH_COLS 7
#define GH_ROWS 19
#define GH_EXTRA_N (12 * GH_COLS * 8)
#define MAP_NOW 0x800A046Cu        /* the map loaded (word) */
#define AREA_KIND 0x800A0466u      /* halfword: 6 the greenhouse */
#define FIELD_PRIMS 0x8009E5F8u    /* a pointer per field, 45 a row */
#define GRID_SIZES 0x80123E04u     /* (width, height) per grid (halfwords): the greenhouse's at +4 */
#define AREA_MODEL 0x80128818u     /* the area model's object: +0 the model, +4 its parts' points */
#define PACK_AT 0x8007AFF0u        /* -> the area's data */
#define PACK_USED 0x8005E328u      /* how much of it is used */
#define MODEL_INIT 0x8001BFA8u     /* (object, model) */
#define FLOOR_PAINT 0x8011E1E8u    /* (x, y, look): a field's floor polygon */
#define CUT (-1600)                /* past it (the back): moved back */
#define SRC_LO (-800)              /* the strip repeated: row 4 (the middle: no edge in its colours) */
#define SRC_HI (-400)
#define SRC_GAP 800                /* row 4 to row 7: 1200, then 400 each */
/* by x: the left wall (and the strip beside it) to x = -1888, the door's side from x = 2000 (the box's
 * column: from 1600, the old back edge beside the box stayed and stopped the farmer at z -1814) */
#define WALL_X (-1888)
#define DOOR_X 2000
#define WALL_CUT (-1080)           /* the wall: past its pillar at -1080 moved back ... */
#define WALL_UNIT 920              /* ... and the pillar + pane from -1080 to -160 repeated */
#define WALL_STEP 960
#define DOOR_CUT (-2000)           /* the door's side: the shipping box's corner stays */
#define DEEPER 4800

static int big(void) {
    return g_cfg.biggreenhouse && MEM_R16(GH_BUILT) != 0 && build_has(2);
}

static uint32_t field(int x, int y) {
    return y < 7 ? GH_GRID + (uint32_t)((y * GH_COLS + x) * 8)
                 : GH_EXTRA + (uint32_t)(((y - 7) * GH_COLS + x) * 8);
}

uint32_t gh_addr(uint32_t a) {
    if (a >= GH_END && a < GH_END + GH_EXTRA_N && big()) return GH_EXTRA + (a - GH_END);
    return a;
}

/* 800DAF60 (grid, x, y): a field's address, 0 out of the grid */
int gh_field_at(CPU *c) {
    if (c->r[4] == 0 || !big()) return 0;
    int x = (int16_t)c->r[5], y = (int16_t)c->r[6];
    c->r[2] = x >= 0 && x < GH_COLS && y >= 0 && y < GH_ROWS ? field(x, y) : 0;
    return 1;
}

/* the greenhouse's rows: 8011E090's test (slti v1, a2, 7) and the night's (the height at sp+24) */
uint32_t gh_row_ok(uint32_t y, uint32_t ok) {
    return big() ? (int32_t)(int16_t)y < GH_ROWS : ok;
}

/* a field drawn by lot among the greenhouse's (49), its address worked out from the number */
uint32_t gh_fields(uint32_t n) {
    return big() && n == 49 ? GH_COLS * GH_ROWS : n;
}

void gh_night_rows(CPU *c) {
    if (big()) MEM_W32(c->r[29] + 24, GH_ROWS);
}

static void empty_rows(void) {
    for (uint32_t a = GH_EXTRA; a < GH_EXTRA + GH_EXTRA_N; a += 8) {
        MEM_W16(a, 359);
        MEM_W16(a + 2, 359);
        MEM_W8(a + 4, 0);
        MEM_W8(a + 5, 0);
        MEM_W16(a + 6, 0);
    }
}

/* 800BE5A0 (entry): people (the sprites, 0x10 of 800BEB28's mask) walk only inside rectangles kept
 * per kind of area (20 bytes: x, z from, x, z to as words, +16 the kind); the greenhouse's (kind 6,
 * x -1900 to 1750) ended at z -1900, the old back edge: a sprite watering a field of rows 7-18
 * walked to and fro there. With the bigger greenhouse it reaches the new back edge. */
#define WALK_RECT 0x801229E0u      /* 801228DC + 13 * 20 */
#define WALK_Z_OLD (-1900)
void gh_walk_area(void) {
    if (MEM_R16(WALK_RECT + 16) != 6 || (int32_t)MEM_R32(WALK_RECT) != -1900 || (int32_t)MEM_R32(WALK_RECT + 8) != 1750)
        return;
    MEM_W32(WALK_RECT + 4, (uint32_t)(big() ? WALK_Z_OLD - DEEPER : WALK_Z_OLD));
}

/* 800D7B24: the greenhouse's fields are emptied (broken, a new game) */
void gh_clear(void) {
    if (g_cfg.biggreenhouse) empty_rows();
}

static uint32_t call3(CPU *c, uint32_t at, uint32_t a0, uint32_t a1, uint32_t a2) {
    uint32_t regs[32], hi = c->hi, lo = c->lo;
    memcpy(regs, c->r, sizeof regs);
    c->r[29] = regs[29] - 0x80;
    c->r[4] = a0;
    c->r[5] = a1;
    c->r[6] = a2;
    call_addr(c, at);
    uint32_t v = c->r[2];
    memcpy(c->r, regs, sizeof regs);
    c->hi = hi;
    c->lo = lo;
    return v;
}

/* rows 7-18's floor polygons (made with the model) into the fields' table: the game fills rows
 * 0-6 after the model is loaded and leaves the rest */
#define FIRST_NEW 7
static uint32_t new_floor[GH_ROWS - FIRST_NEW][GH_COLS];
static uint32_t floor_model;

static void set_floor(void) {
    for (int y = FIRST_NEW; y < GH_ROWS; y++)
        for (int x = 0; x < GH_COLS; x++)
            if (new_floor[y - FIRST_NEW][x]) MEM_W32(FIELD_PRIMS + (uint32_t)(y * 45 + x) * 4, new_floor[y - FIRST_NEW][x]);
}

/* 8011E45C paints rows 0-6's floor polygons as the fields are (once the greenhouse is loaded);
 * at its end, rows 7-18 the same way */
void gh_floor_paint(CPU *c) {
    if (!big() || (int16_t)MEM_R16(AREA_KIND) != 6 || !floor_model || MEM_R32(0x80128818u) != floor_model) return;
    set_floor();
    for (int y = FIRST_NEW; y < GH_ROWS; y++)
        for (int x = 0; x < GH_COLS; x++) {
            uint32_t f = field(x, y);
            int look = (int)MEM_R16(f + 2) - 359;
            if (look < 0 || look > 4) look = 0;
            if (MEM_R16(f) == 54 && MEM_R8(f + 4)) look = ((MEM_R8(f + 4) - 1) << 3 | 5) + (MEM_R16(f + 6) & 15);
            call3(c, FLOOR_PAINT, (uint32_t)x, (uint32_t)y, (uint32_t)(int16_t)look);
        }
}

/* ---- the model ---- */
static int prim_words(uint32_t mode) {
    int n = mode & 8 ? 4 : 3;
    return 1 + (mode & 4 ? n : 0) + (mode & 0x10 ? n : 1) + (n + 1) / 2;
}

#define MAX_PARTS 160
#define MAX_VERTS 4096
static int16_t vx[MAX_VERTS][4];
static int nv;

static int vert(int16_t x, int16_t y, int16_t z) {
    for (int i = 0; i < nv; i++)
        if (vx[i][0] == x && vx[i][1] == y && vx[i][2] == z) return i;
    if (nv == MAX_VERTS) return -1;
    vx[nv][0] = x;
    vx[nv][1] = y;
    vx[nv][2] = z;
    vx[nv][3] = 0;
    return nv++;
}

static int cell(int v) {                /* the middle of the 1600 square v is in */
    return (v >= 0 ? v / 1600 : (v - 1599) / 1600) * 1600 + 800;
}

typedef struct { uint32_t at; int words; int nidx; int idx[4]; int zlo, zhi, xlo, xhi; } Prim;

static void read_prim(uint32_t p, uint32_t vtab, Prim *q) {
    uint32_t mode = MEM_R32(p) >> 24;
    q->at = p;
    q->words = prim_words(mode);
    q->nidx = mode & 8 ? 4 : 3;
    uint32_t iw = p + (uint32_t)(q->words - (q->nidx + 1) / 2) * 4;
    q->zlo = q->xlo = 1 << 30;
    q->zhi = q->xhi = -(1 << 30);
    for (int k = 0; k < q->nidx; k++) {
        q->idx[k] = MEM_R16(iw + (uint32_t)k * 2);
        int z = (int16_t)MEM_R16(vtab + (uint32_t)q->idx[k] * 8 + 4);
        int x = (int16_t)MEM_R16(vtab + (uint32_t)q->idx[k] * 8);
        if (z < q->zlo) q->zlo = z;
        if (z > q->zhi) q->zhi = z;
        if (x < q->xlo) q->xlo = x;
        if (x > q->xhi) q->xhi = x;
    }
}

/* a vertex's z for the left wall's k-th copy of its unit (z -1080 to -160, a pillar and a pane,
 * 920): stretched to 960, so 5 fill the 4800 */
static int wall_z(int z, int k) {
    return WALL_CUT - WALL_STEP * k + (z - WALL_CUT) * WALL_STEP / WALL_UNIT;
}

/* the vertex numbers of a polygon at p (in place), each vertex moved dz (or, wall > 0, to the
 * wall's copy number wall) */
static int move_prim_to(uint32_t p, uint32_t vtab, int dz, int wall);

static int move_prim(uint32_t p, uint32_t vtab, int dz) {
    return move_prim_to(p, vtab, dz, 0);
}

static int move_prim_to(uint32_t p, uint32_t vtab, int dz, int wall) {
    Prim q;
    read_prim(p, vtab, &q);
    uint32_t iw = p + (uint32_t)(q.words - (q.nidx + 1) / 2) * 4;
    for (int k = 0; k < q.nidx; k++) {
        uint32_t v = vtab + (uint32_t)q.idx[k] * 8;
        int z = (int16_t)MEM_R16(v + 4);
        /* (the grass beside the wall is trapezoids from the pillars, at the wall, to the fields'
         * 400s, at x = -1888: in a copy that side lines up with the wall's) */
        if (wall && (int16_t)MEM_R16(v) == WALL_X)    /* (wall < 0: moved back, lined up too) */
            z = z == -1200 ? -1080 : z == -800 ? -960 : z == -400 ? -160 : z == 0 ? -40 : z;
        z = wall > 0 ? wall_z(z, wall) : z + dz;
        int n = vert((int16_t)MEM_R16(v), (int16_t)MEM_R16(v + 2), (int16_t)z);
        if (n < 0) return 0;
        MEM_W16(iw + (uint32_t)k * 2, (uint16_t)n);
    }
    return 1;
}

/* a polygon's colours: 1 (flat) or one per vertex (shaded), after the texture words */
static int colour_at(uint32_t p, int *count) {
    uint32_t mode = MEM_R32(p) >> 24;
    int n = mode & 8 ? 4 : 3;
    *count = mode & 0x10 ? n : 1;
    return 1 + (mode & 4 ? n : 0);
}

static uint32_t get_colours(uint32_t p, uint32_t c[4]) {
    int n, at = colour_at(p, &n);
    for (int i = 0; i < 4; i++) c[i] = MEM_R32(p + (uint32_t)(at + (i < n ? i : 0)) * 4);
    return (uint32_t)n;
}

static void put_colours(uint32_t p, const uint32_t c[4], uint32_t from) {
    int n, at = colour_at(p, &n);
    for (int i = 0; i < n; i++) MEM_W32(p + (uint32_t)(at + i) * 4, c[i < (int)from ? i : 0]);
}

typedef struct { uint32_t prims; int n; int16_t pt[4]; uint32_t entry; } Part;
static Part part[MAX_PARTS];
static int nparts;

void gh_model(CPU *c) {
    uint32_t map = MEM_R32(MAP_NOW);
    if (map < 124 || map > 127) return;
    MEM_W16(GRID_SIZES + 6, (uint16_t)(big() ? GH_ROWS : 7));
    if (!big()) return;
    uint32_t m = MEM_R32(AREA_MODEL);
    if (MEM_R32(m + 8) != 24) return;
    uint32_t base = m + 12, n = 24;
    uint32_t vtab = base + MEM_R32(base), vcount = MEM_R32(base + 4);
    uint32_t points = base + n * 28;
    if (vcount > MAX_VERTS) return;
    /* the new model: past what the area's data uses */
    uint32_t pack = MEM_R32(PACK_AT), used = MEM_R32(PACK_USED);
    uint32_t out = (pack + used + 3) & ~3u, start = out;

    nv = 0;
    for (uint32_t i = 0; i < vcount; i++) {
        uint32_t v = vtab + i * 8;
        vx[nv][0] = (int16_t)MEM_R16(v);
        vx[nv][1] = (int16_t)MEM_R16(v + 2);
        vx[nv][2] = (int16_t)MEM_R16(v + 4);
        vx[nv][3] = (int16_t)MEM_R16(v + 6);
        nv++;
    }
    nparts = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t o = base + i * 28;
        part[nparts].prims = base + MEM_R32(o + 16);
        part[nparts].n = (int)MEM_R32(o + 20);
        part[nparts].entry = o;
        for (int k = 0; k < 4; k++) part[nparts].pt[k] = (int16_t)MEM_R16(points + 4 + i * 8 + (uint32_t)k * 2);
        nparts++;
    }
    /* by zone (x): what is past its cut moves back (in place; an old part is split into runs of
     * moved or not), and the gap is filled with new parts: the fields and paths with row 4's strip
     * (12 x 400), the door's side the same from its cut, the left wall with its unit (5 x 960) */
    int nold = nparts;
    Part old[24];
    memcpy(old, part, sizeof old);
    nparts = 0;
    uint32_t pw = out;
    for (int i = 0; i < nold; i++) {
        uint32_t p = old[i].prims;
        Prim q[64];
        int zone[64], back[64];
        int np = old[i].n < 64 ? old[i].n : 64;
        for (int k = 0; k < np; k++) {
            read_prim(p, vtab, &q[k]);
            p += (uint32_t)q[k].words * 4;
            zone[k] = q[k].xhi <= WALL_X ? 1 : q[k].xlo >= DOOR_X ? 2 : 0;
            int cut = zone[k] == 1 ? WALL_CUT : zone[k] == 2 ? DOOR_CUT : CUT;
            /* (the wall's pillar face standing at its cut stays: it is its unit's, repeated) */
            back[k] = zone[k] == 1 ? q[k].zhi < cut || (q[k].zhi == cut && q[k].zlo < cut) : q[k].zhi <= cut;
        }
        for (int k = 0; k < np;) {      /* runs of the same: moved or not */
            int e = k;
            while (e < np && back[e] == back[k]) e++;
            if (nparts == MAX_PARTS) return;
            part[nparts] = old[i];
            part[nparts].prims = q[k].at;
            part[nparts].n = e - k;
            if (back[k]) {
                for (int j = k; j < e; j++)
                    if (!move_prim_to(q[j].at, vtab, -DEEPER, zone[j] == 1 ? -1 : 0)) return;
                part[nparts].pt[2] = (int16_t)(old[i].pt[2] - DEEPER);
            }
            nparts++;
            k = e;
        }
        for (int rep = 0; rep < 2; rep++) {
            int times = rep ? DEEPER / WALL_STEP : DEEPER / 400;
            for (int t = 1; t <= times; t++) {
                uint32_t first = pw;
                int count = 0, xlo = 1 << 30, xhi = -(1 << 30), ylo = 1 << 30, zsum = 0;
                for (int k = 0; k < np; k++) {
                    int take, off = 0;
                    if (rep) {          /* the wall's unit (not the next pillar's face at -160; the
                                         * grass under the pillar is a piece from -1200 to -800) */
                        take = zone[k] == 1 && q[k].zlo >= WALL_CUT - 120 && q[k].zhi <= -160 &&
                               !(q[k].zlo == q[k].zhi && q[k].zlo == -160);
                    } else {            /* row 4's strip (not a face standing on its edge, like the
                                         * door step's end at -400) */
                        take = zone[k] != 1 && q[k].zlo >= SRC_LO && q[k].zhi <= SRC_HI &&
                               !(q[k].zlo == q[k].zhi && (q[k].zlo == SRC_LO || q[k].zlo == SRC_HI));
                        off = -(SRC_GAP + 400 * t) - (zone[k] == 2 ? CUT - DOOR_CUT : 0);
                    }
                    if (!take) continue;
                    for (int w = 0; w < q[k].words; w++) MEM_W32(pw + (uint32_t)w * 4, MEM_R32(q[k].at + (uint32_t)w * 4));
                    if (!move_prim_to(pw, vtab, off, rep ? t : 0)) return;
                    if (q[k].xlo < xlo) xlo = q[k].xlo;
                    if (q[k].xhi > xhi) xhi = q[k].xhi;
                    for (int v = 0; v < q[k].nidx; v++)
                        if (vx[q[k].idx[v]][1] < ylo) ylo = vx[q[k].idx[v]][1];
                    zsum += rep ? wall_z((q[k].zlo + q[k].zhi) / 2, t) : (q[k].zlo + q[k].zhi) / 2 + off;
                    pw += (uint32_t)q[k].words * 4;
                    count++;
                }
                if (!count) continue;
                if (nparts == MAX_PARTS) return;
                part[nparts].prims = first;
                part[nparts].n = count;
                part[nparts].pt[0] = (int16_t)cell((xlo + xhi) / 2);
                part[nparts].pt[1] = (int16_t)(ylo < -100 ? -800 : 800);
                part[nparts].pt[2] = (int16_t)cell(zsum / count);
                part[nparts].pt[3] = old[i].pt[3];
                part[nparts].entry = old[i].entry;
                nparts++;
            }
        }
    }
    /* the header */
    uint32_t hdr = pw, nb = hdr + 12;
    MEM_W32(hdr, MEM_R32(m));
    MEM_W32(hdr + 4, MEM_R32(m + 4));
    MEM_W32(hdr + 8, (uint32_t)nparts);
    uint32_t pts = nb + (uint32_t)nparts * 28;
    uint32_t vt = pts + 4 + (uint32_t)nparts * 8;
    vt = (vt + 7) & ~7u;
    for (int i = 0; i < nparts; i++) {
        uint32_t o = nb + (uint32_t)i * 28, old = part[i].entry;
        MEM_W32(o, vt - nb);
        MEM_W32(o + 4, (uint32_t)nv);
        MEM_W32(o + 8, MEM_R32(old + 8));
        MEM_W32(o + 12, MEM_R32(old + 12));
        MEM_W32(o + 16, part[i].prims - nb);
        MEM_W32(o + 20, (uint32_t)part[i].n);
        MEM_W32(o + 24, MEM_R32(old + 24));
    }
    MEM_W32(pts, MEM_R32(points));
    for (int i = 0; i < nparts; i++)
        for (int k = 0; k < 4; k++) MEM_W16(pts + 4 + (uint32_t)i * 8 + (uint32_t)k * 2, (uint16_t)part[i].pt[k]);
    for (int i = 0; i < nv; i++)
        for (int k = 0; k < 4; k++) MEM_W16(vt + (uint32_t)i * 8 + (uint32_t)k * 2, (uint16_t)vx[i][k]);
    uint32_t end = vt + (uint32_t)nv * 8;
    MEM_W32(PACK_USED, end - pack);
    call3(c, MODEL_INIT, AREA_MODEL, hdr, 0);
    /* every field's floor polygon (a field's mark in their low byte), from where it lies now (x from
     * -1600, z down from 1200, 400 a field): rows 7-18 into the fields' table; the edge's darker
     * colours of rows 5 and 6 go to rows 17 and 18, and rows 5 and 6 get row 4's */
    static uint32_t floor_at[GH_ROWS][GH_COLS];
    memset(floor_at, 0, sizeof floor_at);
    memset(new_floor, 0, sizeof new_floor);
    int floors = 0;
    for (int i = 0; i < nparts; i++) {
        uint32_t p = part[i].prims;
        for (int k = 0; k < part[i].n; k++) {
            Prim a;
            read_prim(p, vt, &a);
            if ((MEM_R32(p) & 0xFF) == 8) {
                int xlo = 1 << 30, zhi = -(1 << 30);
                for (int v = 0; v < a.nidx; v++) {
                    if (vx[a.idx[v]][0] < xlo) xlo = vx[a.idx[v]][0];
                    if (vx[a.idx[v]][2] > zhi) zhi = vx[a.idx[v]][2];
                }
                int x = (xlo + 1600) / 400, y = (1200 - zhi) / 400;
                if (x >= 0 && x < GH_COLS && y >= 0 && y < GH_ROWS) {
                    floor_at[y][x] = p;
                    if (y >= FIRST_NEW) {
                        new_floor[y - FIRST_NEW][x] = p;
                        floors++;
                    }
                }
            }
            p += (uint32_t)a.words * 4;
        }
    }
    for (int x = 0; x < GH_COLS; x++) {
        uint32_t c5[4], c6[4], m5, m6;
        if (!floor_at[4][x] || !floor_at[5][x] || !floor_at[6][x] || !floor_at[17][x] || !floor_at[18][x]) continue;
        m5 = get_colours(floor_at[5][x], c5);
        m6 = get_colours(floor_at[6][x], c6);
        uint32_t c4[4], m4 = get_colours(floor_at[4][x], c4);
        put_colours(floor_at[5][x], c4, m4);
        put_colours(floor_at[6][x], c4, m4);
        put_colours(floor_at[17][x], c5, m5);
        put_colours(floor_at[18][x], c6, m6);
    }
    floor_model = hdr;
    set_floor();
    LOGI("greenhouse: %d parts, %d vertices, %d fields of rows 7-18, %u bytes at %08X", nparts, nv, floors, end - start, start);
}

/* the card's file (the 32 KB buffer at 80137830): rows 7-18 past the game's own data, with a mark.
 * The check sum (80135AE0, after the serialization) puts a byte at +127 of each block of 128 from
 * +640 (the xor of the block's other 127): the rows go round those bytes ("GH92"). The first
 * version ("GH91") wrote the rows straight on: 5 bytes of theirs (+0x737F, +0x73FF, +0x747F,
 * +0x74FF, +0x757F, each the high byte of a field's second halfword) were the block's check sum
 * once saved; read back, each is taken from the field beside it in the row. */
#define CARD_FILE 0x80137830u
#define CARD_ROWS (CARD_FILE + 0x7300u)
#define ROWS_MARK 0x32394847u      /* "GH92" */
#define OLD_MARK 0x31394847u       /* "GH91" */
#define SUM_BYTE(o) (((o) & 0x7Fu) == 0x7Fu)   /* the block check sum's byte (blocks from +640) */

/* (only with the option: without it the card's file and the RAM are the game's own; a field of
 * zeros, RAM never set, is an empty one) */
static void fix_empty(void) {
    for (uint32_t a = GH_EXTRA; a < GH_EXTRA + GH_EXTRA_N; a += 8)
        if (!MEM_R32(a) && !MEM_R32(a + 4)) {
            MEM_W16(a, 359);
            MEM_W16(a + 2, 359);
        }
}

void gh_card_save(void) {
    if (!g_cfg.biggreenhouse) return;
    fix_empty();
    MEM_W32(CARD_ROWS, ROWS_MARK);
    uint32_t o = 0x7304u;
    for (uint32_t i = 0; i < GH_EXTRA_N; i++, o++) {
        if (SUM_BYTE(o)) o++;
        MEM_W8(CARD_FILE + o, MEM_R8(GH_EXTRA + i));
    }
}

void gh_card_load(void) {
    if (!g_cfg.biggreenhouse) return;
    uint32_t mark = MEM_R32(CARD_ROWS);
    if (mark == ROWS_MARK) {
        uint32_t o = 0x7304u;
        for (uint32_t i = 0; i < GH_EXTRA_N; i++, o++) {
            if (SUM_BYTE(o)) o++;
            MEM_W8(GH_EXTRA + i, MEM_R8(CARD_FILE + o));
        }
    } else if (mark == OLD_MARK) {
        for (uint32_t i = 0; i < GH_EXTRA_N; i++) MEM_W8(GH_EXTRA + i, MEM_R8(CARD_ROWS + 4 + i));
        for (uint32_t o = 0x7304u; o < 0x7304u + GH_EXTRA_N; o++) {
            if (!SUM_BYTE(o)) continue;
            uint32_t i = o - 0x7304u, field = i / 8, col = field % GH_COLS;
            uint32_t beside = col ? i - 8 : i + 8;           /* the same byte of the field beside */
            MEM_W8(GH_EXTRA + i, MEM_R8(GH_EXTRA + beside));
        }
        LOGI("greenhouse: rows 7-18 read from a card of the first version (5 bytes from the fields beside)");
    } else {
        empty_rows();
        return;
    }
    fix_empty();
}
