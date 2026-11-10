/* The carpenter's building order (buildorder): Gotz lets the player pick which building to
 * extend next, out of those still not built. Hooks emitted by tools/recomp.py (GF_FARM, saveload).
 *
 * The game: the carpenter's shop is shop 24 (80118A68 opens a shop: init 80117F70, the talk's
 * frame 801180F0, its state at 8012D7CC, the state after a text at 8012D7D0). The buildings are
 * the event flags 58-62 (bits in words from 8006C870; 8001AF44 reads one), in a fixed order: 0
 * the chicken coop, 1 the kitchen (the house, 80070D06 = 1), 2 the barn, 3 the bigger house
 * (80070D06 = 2), 4 the greenhouse. The next one is the first flag not set (801186FC), kept at
 * 8012D7D4 when the shop opens. Picking "extension" in the shop's menu offers it: the text
 * 316 + the building (its price and lumber in words) is put at 8012828C and the state after it
 * (5: the yes / no) at 80128290; state 9 shows the text at 8012828C and goes on to state 8
 * (waits for the text to close), then to the state stored. Yes checks the money and the lumber
 * of the table at 80128278 (a halfword each, 4 bytes a building) for 8012D7D4, takes them, sets
 * flag 56 (building) and 3 days (80111EE0). The night (80111F88) counts them off and builds the
 * first flag not set again (801120xx: s0 = 58 + the building, s1 the flag it sets).
 *
 * With the option, when more than one building is left the offer is first a menu of their names
 * (the bigger house only once the kitchen is built; at most 4 answers, a page of the text window):
 * the state after the menu's text is 9 again, and the answer puts the building picked at
 * 8012D7D4 and its text at 8012828C, so the game's own offer, yes / no, price and lumber follow.
 * The building paid for is kept in RAM nothing uses (8009B9E0, next to the bigger greenhouse's
 * rows: machine.c keeps it the same in both machines) and in the card's file (past those rows,
 * with a mark); the night builds it in place of the first one not built. Without the option,
 * the game's own.
 *
 * The greenhouse's own works (with the option, once the greenhouse is built): the sturdy
 * greenhouse (sturdygreenhouse: 10000G and 700 lumber) and after it the bigger one (biggreenhouse:
 * 30000G and 500 lumber) are works 5 and 6 of the menu. One picked is offered as the greenhouse
 * (8012D7D4 = 4, its text 320 written over with ours, its price at 80128288 the work's until the
 * offer is left), paid the same (flag 56 and 3 days), and the night it is done sets a bit at
 * 8009B9E4 in place of a building (s1 = 62, the greenhouse's flag, set again). Those options then
 * work only with their bit (build_has). With nothing left to build the shop still offers the
 * extension (80117F90) while a work is left. */
#include "psx.h"
#include "psx_recomp.h"

#define BUILDINGS 5
#define WORKS 7                    /* + the sturdy greenhouse (5) and the bigger (6) */
#define STURDY 5
#define BIGGER 6
#define FIRST_FLAG 58
#define KITCHEN 1
#define BIG_HOUSE 3
#define GREENHOUSE 4
#define GH_BUILT 0x80070D0Cu       /* halfword: the greenhouse stands */
#define FLAGS 0x8006C870u          /* the event flags: 32 a word */
#define OFFERED 0x8012D7D4u        /* the building offered (word; -1 none left) */
#define PRICES 0x80128278u         /* money, lumber (halfwords) a building */
#define GH_PRICE (PRICES + 4u * GREENHOUSE)
#define TEXT_ID 0x8012828Cu        /* the text state 9 shows (bank 2) ... */
#define AFTER_TEXT 0x80128290u     /* ... and the state after it */
#define OFFER_TEXT 316             /* + the building */
#define OFFER_NEXT 5               /* the state after the offer's text: its yes / no */
#define SHOW_TEXT 9
#define PICKED 0x8009B9E0u         /* word: 1 + the work paid for until it is done, else 0 */
#define OWNED 0x8009B9E4u          /* word: bit 0 the sturdy greenhouse, bit 1 the bigger, bit 31
                                    * a save from before them (settled once the game is in) */
#define UNSETTLED 0x80000000u
#define TEXT_BUF 0x8006E110u       /* the text being shown (gp+828) */
#define TEXT_MAX 0x800u
#define TEXT_HEAD 5                /* the window's codes before the words, kept */
#define MENU_MAX 4                 /* answers on a page of the text window */
#define CARD_FILE 0x80137830u      /* the card's file, 32 KB (greenhouse.c's rows at +0x7300) */
#define CARD_PICKED (CARD_FILE + 0x7600u)
#define PICKED_MARK 0x3130424Fu    /* "OB01": the work picked */
#define OWNED_MARK 0x3230424Fu     /* "OB02": and the greenhouse's works done (+8) */

static const char *const names[WORKS] = { "Hen House", "Kitchen", "Barn", "Bigger house", "Greenhouse",
                                          "Sturdy greenhouse", "Bigger greenhouse" };
static const uint16_t work_price[WORKS][2] = { [STURDY] = { 10000, 700 }, [BIGGER] = { 30000, 500 } };

enum { NONE, WANT_MENU, MENU };
static int phase, n_lines, work, work_text;
static int lines[WORKS];
static uint32_t gh_price_orig;
static int gh_price_set;

static int built(int b) {
    uint32_t f = FIRST_FLAG + (uint32_t)b;
    return (int)((MEM_R32(FLAGS + (f >> 5) * 4u) >> (f & 31u)) & 1u);
}

static uint32_t owned_bit(int w) { return w == STURDY ? 1u : 2u; }

/* a save from before the works keeps what its options gave it: with the greenhouse built, the
 * ones on are done (the card is read before the game's data is back: settled when first asked) */
static uint32_t owned_now(void) {
    uint32_t owned = MEM_R32(OWNED);
    if (!(owned & UNSETTLED)) return owned;
    owned = 0;
    if (MEM_R16(GH_BUILT) != 0) {
        if (g_cfg.sturdygreenhouse) owned |= 1u;
        if (g_cfg.biggreenhouse) owned |= 2u;
    }
    MEM_W32(OWNED, owned);
    return owned;
}

/* a greenhouse's work wanted (its option on) and not done yet: the bigger after the sturdy */
static int work_left(int w) {
    if (!(w == STURDY ? g_cfg.sturdygreenhouse : g_cfg.biggreenhouse)) return 0;
    if (!built(GREENHOUSE) || MEM_R16(GH_BUILT) == 0) return 0;
    uint32_t owned = owned_now();
    if (owned & owned_bit(w)) return 0;
    return w == STURDY || !g_cfg.sturdygreenhouse || (owned & owned_bit(STURDY));
}

/* the sturdy (1) / bigger (2) greenhouse: always without buildorder, else once done */
int build_has(int bit) {
    return !g_cfg.buildorder || (owned_now() & (uint32_t)bit) != 0;
}

static int list_lines(void) {
    n_lines = 0;
    for (int b = 0; b < BUILDINGS; b++)
        if (!built(b) && (b != BIG_HOUSE || built(KITCHEN))) lines[n_lines++] = b;
    for (int w = STURDY; w < WORKS; w++)
        if (n_lines < MENU_MAX && work_left(w)) lines[n_lines++] = w;
    return n_lines;
}

/* the greenhouse's price back (the offer left, another picked, paid) */
static void price_back(void) {
    if (gh_price_set) MEM_W32(GH_PRICE, gh_price_orig);
    gh_price_set = 0;
    work = 0;
    work_text = 0;
}

/* a greenhouse's work offered in the greenhouse's place: its price and its text */
static void offer_work(int w) {
    if (!gh_price_set) gh_price_orig = MEM_R32(GH_PRICE);
    gh_price_set = 1;
    MEM_W16(GH_PRICE, work_price[w][0]);
    MEM_W16(GH_PRICE + 2u, work_price[w][1]);
    MEM_W32(OFFERED, GREENHOUSE);
    MEM_W32(TEXT_ID, OFFER_TEXT + GREENHOUSE);
    work = w;
    work_text = 1;
    LOGI("buildorder: %s offered", names[w]);
}

/* the shop opens (80117F90, v0 the first building not built, -1 none): with a work left the
 * extension stays on offer */
uint32_t build_next(uint32_t next) {
    if (g_view2_run || !g_cfg.buildorder) return next;
    price_back();
    phase = NONE;
    if (next == 0xFFFFFFFFu && (work_left(STURDY) || work_left(BIGGER))) return GREENHOUSE;
    return next;
}

/* the offer's text and next state just stored (801182EC, v1 the state: 5 after the offer only) */
void build_offer(CPU *c) {
    if (g_view2_run || !g_cfg.buildorder || c->r[3] != OFFER_NEXT) return;
    price_back();
    list_lines();
    if (n_lines == 1 && lines[0] >= BUILDINGS) { offer_work(lines[0]); return; }
    if (n_lines < 2) return;
    MEM_W32(AFTER_TEXT, SHOW_TEXT);
    phase = WANT_MENU;
    LOGI("buildorder: a menu of %d buildings", n_lines);
}

static uint32_t put_base = TEXT_BUF, put_max = TEXT_MAX;   /* where put writes */

static uint32_t put(uint32_t at, uint16_t h) {
    if (at < put_max) MEM_W16(put_base + at * 2, h);
    return at + 1;
}

/* the game's glyphs (as sprites.c): A-Z 0.., a-z 0x20.., 0-9 0x40..; '\n' a new line, '|' the
 * start of the answers (a page of their own) */
static uint32_t put_words(uint32_t at, const char *s) {
    for (; *s; s++) {
        char ch = *s;
        if (ch >= 'A' && ch <= 'Z') at = put(at, (uint16_t)(ch - 'A'));
        else if (ch >= 'a' && ch <= 'z') at = put(at, (uint16_t)(0x20 + ch - 'a'));
        else if (ch >= '0' && ch <= '9') at = put(at, (uint16_t)(0x40 + ch - '0'));
        else if (ch == ' ') at = put(at, 0x0FFD);
        else if (ch == ',') at = put(at, 0x8C);
        else if (ch == '.') at = put(at, 0x6C);
        else if (ch == '?') at = put(at, 0x62);
        else if (ch == '!') at = put(at, 0x63);
        else if (ch == '\'' && (s[1] == 'l' || s[1] == 's')) {
            s++;                                    /* 'l 's are glyphs of their own */
            at = put(at, *s == 'l' ? 0x52 : 0x51);
        } else if (ch == '\'') at = put(at, 0x8D);
        else if (ch == '\n') at = put(at, 0xFFFE);
        else if (ch == '|') {
            at = put(at, 0x0FFC);
            at = put(at, 0x0FF8);
            at = put(at, 0xFFFE);
            at = put(at, 0x0FF6);
        }
    }
    return at;
}

/* The shop's item names (bank 0 at 80051DBE, each at a place of its own): the extension's is
 * at the offset 80127D64 + 4 x the building gives (80118040): "Enlarge hen house", "Enlarge own house", "Enlarge barn",
 * "Hothouse" at 80054984 (58 halfwords). With the menu (or a greenhouse's work) the first says
 * "Enlarge build" (in its 18 halfwords) and the shop shows it for any building. */
#define SHOP_NAMES 0x80054984u
#define SHOP_NAMES_LEN 58u
static uint16_t shop_orig[SHOP_NAMES_LEN];
static int shop_saved, shop_generic;

static void shop_names(void) {
    if (!shop_saved) {
        for (uint32_t i = 0; i < SHOP_NAMES_LEN; i++) shop_orig[i] = MEM_R16(SHOP_NAMES + i * 2u);
        shop_saved = 1;
    }
    int generic = 0;
    if (g_cfg.buildorder) {
        int keep_n = n_lines, keep[WORKS];
        for (int k = 0; k < WORKS; k++) keep[k] = lines[k];
        generic = list_lines() >= 2 || (n_lines == 1 && lines[0] >= BUILDINGS);
        n_lines = keep_n;
        for (int k = 0; k < WORKS; k++) lines[k] = keep[k];
    }
    shop_generic = generic;
    if (!generic) {
        for (uint32_t i = 0; i < SHOP_NAMES_LEN; i++) MEM_W16(SHOP_NAMES + i * 2u, shop_orig[i]);
        return;
    }
    put_base = SHOP_NAMES;
    put_max = 18u;
    put(put_words(0, "Enlarge build"), 0xFFFF);
    put_base = TEXT_BUF;
    put_max = TEXT_MAX;
}

/* the extension's name (80118040, s4 the building): the first's when it says "Enlarge build" */
uint32_t build_label(uint32_t b) {
    if (g_view2_run || !g_cfg.buildorder) return b;
    shop_names();
    return shop_generic ? 0u : b;
}

/* a text was loaded (80022A94): the offer's is written over with the menu, or with a
 * greenhouse's work */
void build_text_loaded(void) {
    if (g_view2_run) return;
    shop_names();
    if (work_text && MEM_R32(TEXT_ID) == OFFER_TEXT + GREENHOUSE) {
        work_text = 0;
        uint32_t at = put_words(TEXT_HEAD, work == STURDY
            ? "A sturdy greenhouse? Storms\nwon't break it anymore.\n10000G and 700 lumber. OK?"
            : "A bigger greenhouse? I'll make\nit 12 rows deeper.\n30000G and 500 lumber. OK?");
        put(at, 0xFFFF);
        return;
    }
    if (phase == MENU) phase = NONE;           /* another text: the menu was left */
    if (phase != WANT_MENU) return;
    uint32_t at = put_words(TEXT_HEAD, "Which one should I build?|");
    for (int k = 0; k < n_lines; k++) {
        if (k) at = put_words(at, "\n");
        at = put_words(at, " ");
        at = put_words(at, names[lines[k]]);
    }
    put(at, 0xFFFF);
    phase = MENU;
}

/* a menu answered (80022CDC, 1.. the line): the game's offer of the building picked comes next */
void build_menu_answered(uint32_t line) {
    if (g_view2_run || phase != MENU) return;
    phase = NONE;
    MEM_W32(AFTER_TEXT, OFFER_NEXT);
    if (line < 1 || line > (uint32_t)n_lines) return;     /* the game's own offer */
    int b = lines[line - 1];
    if (b >= BUILDINGS) { offer_work(b); return; }
    MEM_W32(OFFERED, (uint32_t)b);
    MEM_W32(TEXT_ID, (uint32_t)(OFFER_TEXT + b));
    LOGI("buildorder: %s offered", names[b]);
}

/* paid: the building starts (801185E4, s0 = 4 x the building) */
void build_started(CPU *c) {
    if (g_view2_run || !g_cfg.buildorder) return;
    int b = work && (c->r[16] >> 2) == GREENHOUSE ? work : (int)((c->r[16] >> 2) % BUILDINGS);
    price_back();
    MEM_W32(PICKED, (uint32_t)b + 1u);
    LOGI("buildorder: %s paid for", names[b]);
}

/* a greenhouse built now (not from a save from before the works): its works not done yet */
static uint32_t settle_new(uint32_t b) {
    if (b == GREENHOUSE && (MEM_R32(OWNED) & UNSETTLED)) MEM_W32(OWNED, 0);
    return b;
}

/* the night the building is done (8011205C, v1 = s0 - 58: the first not built) */
uint32_t build_night(CPU *c, uint32_t first) {
    uint32_t picked = MEM_R32(PICKED);
    if (g_view2_run || !g_cfg.buildorder) return first;
    MEM_W32(PICKED, 0);
    if (picked == STURDY + 1u || picked == BIGGER + 1u) {
        int w = (int)picked - 1;
        MEM_W32(OWNED, owned_now() | owned_bit(w));
        if (w == BIGGER) gh_clear();               /* the new rows: empty fields */
        c->r[16] = FIRST_FLAG + GREENHOUSE;        /* no building: its flag is set again */
        c->r[17] = FIRST_FLAG + GREENHOUSE;
        LOGI("buildorder: %s done", names[w]);
        return BUILDINGS;                          /* past the buildings' switch */
    }
    if (picked < 1 || picked > BUILDINGS || built((int)picked - 1)) return settle_new(first);
    uint32_t b = picked - 1;
    c->r[16] = FIRST_FLAG + b;
    c->r[17] = FIRST_FLAG + b;
    LOGI("buildorder: %s built (the first not built: %u)", names[b], first);
    return settle_new(b);
}

/* the card's file: written (the serialization, 80134C48) and read (80135380) */
void build_card_save(void) {
    if (!g_cfg.buildorder) return;
    MEM_W32(CARD_PICKED, OWNED_MARK);
    MEM_W32(CARD_PICKED + 4u, MEM_R32(PICKED));
    MEM_W32(CARD_PICKED + 8u, owned_now());
}

void build_card_load(void) {
    if (!g_cfg.buildorder) return;
    uint32_t mark = MEM_R32(CARD_PICKED);
    MEM_W32(PICKED, mark == OWNED_MARK || mark == PICKED_MARK ? MEM_R32(CARD_PICKED + 4u) : 0u);
    MEM_W32(OWNED, mark == OWNED_MARK ? MEM_R32(CARD_PICKED + 8u) & 3u : UNSETTLED);
}
