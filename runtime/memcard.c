/* Memory card storage: raw 128 KiB images (same format as common .mcd/.mcr files). */
#include "psx.h"

static uint8_t card[2][0x20000];
static char card_path[2][1024];
static bool card_dirty[2];
static bool card_present[2];

static void frame_checksum(uint8_t *f) {
    uint8_t x = 0;
    for (int i = 0; i < 127; i++) x ^= f[i];
    f[127] = x;
}

static void format_card(uint8_t *c) {
    memset(c, 0, 0x20000);
    c[0] = 'M';
    c[1] = 'C';
    frame_checksum(c);
    for (int i = 1; i < 16; i++) {
        uint8_t *f = c + i * 128;
        f[0] = 0xA0;
        f[8] = 0xFF;
        f[9] = 0xFF;
        frame_checksum(f);
    }
    for (int i = 16; i < 36; i++) {
        uint8_t *f = c + i * 128;
        f[0] = f[1] = f[2] = f[3] = 0xFF;
        f[8] = 0xFF;
        f[9] = 0xFF;
        frame_checksum(f);
    }
    /* frame 63: write test frame (copy of frame 0) */
    memcpy(c + 63 * 128, c, 128);
}

bool memcard_load(int slot, const char *path) {
    snprintf(card_path[slot], sizeof card_path[slot], "%s", path);
    card_present[slot] = true;
    FILE *f = fopen(path, "rb");
    if (f) {
        size_t n = fread(card[slot], 1, 0x20000, f);
        fclose(f);
        if (n == 0x20000 && card[slot][0] == 'M' && card[slot][1] == 'C') {
            LOGI("memory card %d: %s", slot + 1, path);
            return true;
        }
        LOGW("memory card %s is not a valid 128KB image; formatting a new one", path);
    }
    format_card(card[slot]);
    card_dirty[slot] = true;
    memcard_flush();
    return true;
}

static uint64_t last_access = UINT64_MAX;   /* frame of the last card access (BIOS or SIO) */

uint8_t *memcard_data(int slot) {
    return card_present[slot] ? card[slot] : NULL;
}

/* a real read or write of the card's contents (not the presence checks the game makes all
 * the time): the card counts as in use for a while after it */
void memcard_touch(void) {
    last_access = frame_count();
}
void memcard_mark_dirty(int slot) { card_dirty[slot] = true; }

void memcard_flush(void) {
    for (int s = 0; s < 2; s++) {
        if (!card_dirty[s] || !card_path[s][0]) continue;
        char tmp[1100];
        snprintf(tmp, sizeof tmp, "%s.tmp", card_path[s]);
        FILE *f = fopen(tmp, "wb");
        if (!f) { LOGE("cannot write memory card %s", tmp); continue; }
        fwrite(card[s], 1, 0x20000, f);
        fclose(f);
        remove(card_path[s]);
        if (rename(tmp, card_path[s]) != 0) LOGE("cannot replace memory card %s", card_path[s]);
        card_dirty[s] = false;
    }
}

/* The save/load screens (saveload overlay, hooks from tools/recomp.py) ask "Select a MEMORY
 * CARD slot" with a menu that returns 0/1 for a slot, -1 for cancel and anything else while
 * the player is still choosing. With autoslot the first question of a visit is answered with
 * slot 1. Asked again in the same visit (the player backed out of the file list, or an error
 * sent the screen back) it answers cancel, which asks "Aborting ... Is that OK?": Yes leaves
 * the screen, No comes back here and gets slot 1 again. */
static bool slot_answered;

void memcard_slot_visit(void) {
    slot_answered = false;
}

static bool bank_has_diary(int b);
static int bank_with_diary(int from, int step);

/* With autoslot the menu (80136990, which draws its window and cursor on each call) is not
 * run at all: it says "still choosing" and the hook after the call answers. */
int memcard_slot_auto(void) {
    return g_cfg.autoslot != 0;
}

uint32_t memcard_slot_menu(uint32_t result, int loading) {
    if (!g_cfg.autoslot) return result;
    slot_answered = !slot_answered;
    if (!slot_answered) return (uint32_t)-1;
    /* nothing to load on this card: take the first card that has a diary, if any */
    if (loading && !bank_has_diary(memcard_bank() - 1)) {
        int b = bank_with_diary(memcard_bank() - 1, 1);
        if (b >= 0) memcard_switch_bank(b, true);
    }
    return 0;
}

/* Every text the save/load screens show goes through the hook after their calls of the
 * message box (8001AC10, a2 = text). While one of the "busy" texts is up and the card is
 * being accessed, fastcard runs the game at full speed, like holding Tab: the card itself
 * takes its time (one sector per frame, as on the console), so this shortens the wait. */
static bool busy_text;

/* "Memory card N" on a line of its own above the game's text (game encoding: A-Z = 0-25,
 * a-z = 0x20-0x39, digits = 0x40-0x49, space = 0FFD, new line = FFFE, end = FFFF) */
static size_t put_text(size_t n, const char *s) {
    for (; *s; s++)
        g_port_text[n++] = *s == ' ' ? 0x0FFD : *s == '\n' ? 0xFFFE : *s == '.' ? 0x6C :
                           *s >= '0' && *s <= '9' ? (uint16_t)(0x40 + *s - '0') :
                           *s >= 'a' ? (uint16_t)(0x20 + *s - 'a') : (uint16_t)(*s - 'A');
    return n;
}

static int deleting = -1;   /* diary (0-2) whose deletion is being held for, or -1 */

static uint32_t with_card_line(uint32_t text) {
    char line[64];
    size_t n;
    snprintf(line, sizeof line, "Memory card %d\n", memcard_bank());
    n = put_text(0, line);
    if (deleting >= 0) {   /* instead of the game's prompt */
        snprintf(line, sizeof line, "Keep holding TRIANGLE\nto delete Diary %d.", deleting + 1);
        n = put_text(n, line);
        g_port_text[n] = 0xFFFF;
        return 0x80000000u | PORT_TEXT_ADDR;
    }
    while (n < sizeof g_port_text / sizeof *g_port_text - 1) {
        uint16_t ch = (uint16_t)MEM_R16(text);
        g_port_text[n++] = ch;
        if (ch == 0xFFFF) return 0x80000000u | PORT_TEXT_ADDR;
        text += 2;
    }
    return text;   /* too long: leave the game's text as it is */
}

uint32_t memcard_text(uint32_t text) {
    switch (text - ram_r32(0x80054BBCu)) {   /* the game adds a text bank offset */
    case 0x80053566u:   /* Select a diary to save. */
    case 0x80053596u:   /* Select a diary to load. */
        busy_text = false;
        return with_card_line(text);
    }
    /* with autoslot the slot question is never seen: keep showing the search */
    if (g_cfg.autoslot && text - ram_r32(0x80054BBCu) == 0x80053530u)   /* Select a MEMORY CARD slot. */
        text = text - 0x80053530u + 0x80052FC4u;
    switch (text - ram_r32(0x80054BBCu)) {
    case 0x80052FC4u:   /* Searching the MEMORY CARD. */
    case 0x8005316Cu:   /* Formatting the MEMORY CARD ... */
    case 0x80053350u:   /* Loading from memory card slot ... */
    case 0x8005346Au:   /* Saving to memory card slot ... */
        busy_text = true;
        break;
    default:
        busy_text = false;
    }
    return text;
}

bool memcard_busy(void) {
    return g_cfg.fastcard && busy_text && last_access != UINT64_MAX && frame_count() - last_access <= 8;
}

/* Card banks: the game keeps three diaries on a card, so slot 1 can hold one of CARD_BANKS
 * card files, chosen with F1-F4: card1.mcd (bank 1, the one there always was), card1_2.mcd,
 * card1_3.mcd, card1_4.mcd. The save/load screens read the card's directory afresh on each
 * visit, so swapping the file between visits is like swapping the card in the console. It is
 * refused while the card was accessed in the last two seconds (a save/load screen is busy
 * with it). The bank last used is remembered in card_bank.txt next to the cards. */
static int bank;   /* 0-based */

static void bank_path(char *out, size_t n, int b) {
    if (b == 0) snprintf(out, n, "%s/card1.mcd", g_cfg.save_dir);
    else snprintf(out, n, "%s/card1_%d.mcd", g_cfg.save_dir, b + 1);
}

int memcard_bank(void) {
    return bank + 1;
}

/* the file of the card in slot 1 */
void memcard_bank_file(char *out, size_t n) {
    bank_path(out, n, bank);
}

void memcard_open_banks(int requested) {
    char p[1200];
    int b = requested - 1;
    if (requested <= 0) {   /* the bank last used */
        snprintf(p, sizeof p, "%s/card_bank.txt", g_cfg.save_dir);
        FILE *f = fopen(p, "r");
        b = 0;
        if (f) { if (fscanf(f, "%d", &b) != 1) b = 1; fclose(f); b--; }
    }
    if (b < 0 || b >= CARD_BANKS) b = 0;
    bank = b;
    bank_path(p, sizeof p, bank);
    memcard_load(0, p);
}

bool memcard_switch_bank(int b, bool force) {
    if (b < 0 || b >= CARD_BANKS || b == bank) return false;
    if (!force && (bios_card_files_open() || (last_access != UINT64_MAX && frame_count() - last_access < 120))) {
        LOGW("memory card in use: not switching to card %d now", b + 1);
        return false;
    }
    memcard_flush();
    char p[1200];
    bank = b;
    bank_path(p, sizeof p, bank);
    memcard_load(0, p);
    sio_card_changed(0);
    snprintf(p, sizeof p, "%s/card_bank.txt", g_cfg.save_dir);
    FILE *f = fopen(p, "w");
    if (f) { fprintf(f, "%d\n", bank + 1); fclose(f); }
    LOGI("memory card %d in slot 1", bank + 1);
    return true;
}

/* On a diary list (save or load), L1 / R1 put the previous / next card in slot 1 (when
 * loading, the previous / next card that has a diary; Continue on an empty card takes the
 * first one that has one): the list
 * reports "nothing chosen yet" and the screen goes back to its state 4, which reads the card
 * in the chosen slot and lists its diaries, as after the slot menu. The screen is only waiting
 * for a button there, so the card can be swapped although it was read a moment ago. */
#define SAVELOAD_STATE 0x80137584u

/* the card file of bank b has a diary of this game */
static bool bank_has_diary(int b) {
    uint8_t dir[16 * 128];
    if (b == bank) memcpy(dir, card[0], sizeof dir);
    else {
        char p[1200];
        bank_path(p, sizeof p, b);
        FILE *f = fopen(p, "rb");
        if (!f) return false;
        size_t n = fread(dir, 1, sizeof dir, f);
        fclose(f);
        if (n != sizeof dir) return false;
    }
    for (int i = 1; i < 16; i++)
        if (dir[i * 128] == 0x51 && !memcmp(dir + i * 128 + 10, "BASLUS-01115-FARM", 17)) return true;
    return false;
}

/* the next bank after `from` going by `step` that has a diary, or -1 */
static int bank_with_diary(int from, int step) {
    for (int k = 1; k < CARD_BANKS; k++) {
        int b = (from + step * k + CARD_BANKS * 2) % CARD_BANKS;
        if (bank_has_diary(b)) return b;
    }
    return -1;
}

/* this game's diary n (0-2) on the card in slot 1: its first directory frame, or -1 */
static int diary_frame(int n) {
    char name[24];
    snprintf(name, sizeof name, "BASLUS-01115-FARM%d", n);
    for (int b = 1; b < 16; b++)
        if (card[0][b * 128] == 0x51 && !strncmp((const char *)card[0] + b * 128 + 10, name, 20)) return b;
    return -1;
}

/* erase a diary like the BIOS erase does: each of its blocks marked free in the directory */
static void delete_diary(int n) {
    int b = diary_frame(n);
    while (b > 0) {
        uint8_t *f = card[0] + b * 128;
        uint16_t nx = (uint16_t)(f[8] | (f[9] << 8));
        f[0] = (uint8_t)(0xA0 | (f[0] & 0x0F));
        frame_checksum(f);
        b = nx == 0xFFFF ? -1 : nx + 1;
    }
    card_dirty[0] = true;
    memcard_flush();
    LOGI("memory card %d: diary %d deleted", bank + 1, n + 1);
}

#define DIARY_CURSOR   0x80137587u
#define DELETE_HOLD    120   /* frames TRIANGLE is held to delete (2 s) */

uint32_t memcard_diary_menu(uint32_t result, int loading) {
    static uint16_t prev;
    static uint64_t held_since;
    static bool wait_release;
    uint16_t pad = g_pad_buttons[0], down = (uint16_t)(pad & ~prev);
    prev = pad;
    /* TRIANGLE held on a diary for DELETE_HOLD frames deletes it; the prompt says so meanwhile */
    int cur = MEM_R8(DIARY_CURSOR);
    if (!(pad & PAD_TRIANGLE)) wait_release = false;
    if ((pad & PAD_TRIANGLE) && !wait_release && (int32_t)result >= 3 && cur < 3 && diary_frame(cur) > 0) {
        if (deleting != cur) { deleting = cur; held_since = frame_count(); }
        if (frame_count() - held_since < DELETE_HOLD) return result;
        delete_diary(cur);
        deleting = -1;
        wait_release = true;
        /* nothing left to load here: the next card that has a diary, as on Continue */
        if (loading && !bank_has_diary(bank)) {
            int b = bank_with_diary(bank, 1);
            if (b >= 0) memcard_switch_bank(b, true);
        }
        MEM_W8(SAVELOAD_STATE, 4);   /* read the card and list it again */
        return 3;
    }
    deleting = -1;
    int step = (down & PAD_R1) ? 1 : (down & PAD_L1) ? -1 : 0;
    if (!step || (int32_t)result < 3) return result;
    /* loading: only cards with something to load; saving: any card */
    int b = loading ? bank_with_diary(bank, step) : (bank + step + CARD_BANKS) % CARD_BANKS;
    if (b < 0 || !memcard_switch_bank(b, true)) return result;
    MEM_W8(SAVELOAD_STATE, 4);
    return 3;
}
