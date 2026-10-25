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
    last_access = frame_count();
    return card_present[slot] ? card[slot] : NULL;
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

uint32_t memcard_slot_menu(uint32_t result) {
    if (!g_cfg.autoslot) return result;
    slot_answered = !slot_answered;
    return slot_answered ? 0 : (uint32_t)-1;
}

/* Every text the save/load screens show goes through the hook after their calls of the
 * message box (8001AC10, a2 = text). While one of the "busy" texts is up and the card is
 * being accessed, fastcard runs the game at full speed, like holding Tab: the card itself
 * takes its time (one sector per frame, as on the console), so this shortens the wait. */
static bool busy_text;

void memcard_text(uint32_t text) {
    switch (text - ram_r32(0x80054BBCu)) {   /* the game adds a text bank offset */
    case 0x80052FC4u:   /* Searching the MEMORY CARD. */
    case 0x8005316Cu:   /* Formatting the MEMORY CARD ... */
    case 0x80053350u:   /* Loading from memory card slot ... */
    case 0x8005346Au:   /* Saving to memory card slot ... */
        busy_text = true;
        break;
    default:
        busy_text = false;
    }
}

bool memcard_busy(void) {
    return g_cfg.fastcard && busy_text && last_access != UINT64_MAX && frame_count() - last_access <= 8;
}
