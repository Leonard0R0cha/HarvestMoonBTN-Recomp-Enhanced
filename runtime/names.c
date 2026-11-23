/* Random names (randomnames): on the game's naming screen, Select fills in a name picked at random
 * for what is being named (the farmer, the farm, the dog, the horse, the baby, the other animals);
 * pressed again, another. The name can still be changed letter by letter before it is accepted.
 *
 * The naming screen is the main program's (80023100 sets it up; 80023130, 80023170 and 800231AC
 * open it with a0 = where the name goes, kept at gp+236; 800231E8 runs it each frame and 80023DBC
 * reads the buttons). The name is written straight into its place: 9 halfwords, the game's glyphs
 * (A-Z 0.., a-z 0x20.., 0-9 0x40.., space 0FFD), FFFF after the last; 8 letters at most. gp+208 is
 * the screen's state (1 typing, 2 "is this right?", 3 / 4 left), gp+218 how many letters, gp+220
 * set once a letter is typed (the name can't be accepted without it). Select is not one of the
 * screen's buttons (Circle rubs out, Cross types, L / R the other letters, Start is done).
 *
 * What is named: the farmer (80071A00), the farm (80070D18) and the dog (800759E8) at the start;
 * the rest goes through a buffer of its own and is told apart by who opened the screen: the
 * farm's event routine (GF_FARM 800E729C, its s0 the kind: 0 the horse, copied to 80075AD8; 4 the
 * baby, to 8007AFBC; the others animals) or anything else (the ranch and the poultry farm: animals).
 *
 * The lists are the built-in ones of the game's language (languages[], English for one without
 * its own), or those of names.txt next to the exe
 * (one name per line under [farmer], [farm], [dog], [horse], [baby], [animal]; a list missing
 * there keeps the built-in one). Online both sides must pick the same, so names.txt is not read
 * then. The pick comes from the game's own random seed (gp+28, read, never moved) and how many
 * names were picked, so a recording picks the same again. */
#include "psx.h"
#include "psx_recomp.h"
#include "text_pt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NAME_STATE 0x8005E344u      /* gp+208, halfword: 1 typing */
#define NAME_LEN   0x8005E34Eu      /* gp+218, halfword */
#define NAME_TYPED 0x8005E350u      /* gp+220, word */
#define NAME_DEST  0x8005E360u      /* gp+236 */
#define GAME_SEED  0x8005E290u      /* gp+28: rand()'s seed (80012A34) */
#define PAD_STATE  0x8005E3A8u      /* -> the pad: +0 held, +8 newly pressed */
#define PAD_SELECT 0x0100u
#define SOUND      0x8001FABCu      /* a sound effect (0: a letter typed) */
#define TYPING 1
#define NAME_MAX 8
#define NAME_SLOTS 9

#define FARMER_NAME 0x80071A00u
#define FARM_NAME   0x80070D18u
#define DOG_NAME    0x800759E8u
#define EVENT_NAMING 0x800E729Cu    /* GF_FARM: the event's naming, its s0 the kind */
#define EVENT_NAMING_END 0x800E7494u
#define KIND_HORSE 0
#define KIND_BABY  4

enum { FARMER, FARM, DOG, HORSE, BABY, ANIMAL, LISTS };
static const char *const list_names[LISTS] = { "farmer", "farm", "dog", "horse", "baby", "animal" };

/* the built-in lists: at most 8 letters (an accented letter is one) */
#define END NULL
static const char *const pt_lists[LISTS][40] = {
    { "Ana", "Bruno", "Caio", "Duda", "Enzo", "Gabi", "Heitor", "Iara", "João", "Júlia", "Kauã", "Lara",
      "Luna", "Malu", "Miguel", "Nina", "Otávio", "Pedro", "Rafa", "Sofia", "Théo", "Yuri", "Zeca",
      "Bento", "Cecília", "Davi", "Lívia", "Maitê", "Tomás", "Benício", END },
    { "Aurora", "Girassol", "Ipê", "Jatobá", "Bonança", "Alvorada", "Harmonia", "Sabiá", "Cerrado",
      "Orvalho", "Pomar", "Colina", "Raízes", "Trevo", "Serena", "Boa Vida", "Lua Nova", "Cantinho",
      "Recanto", "Paraíso", "Ventura", "Alegria", "Jardim", "Vale Sol", END },
    { "Pipoca", "Totó", "Bidu", "Rex", "Thor", "Pingo", "Bolinha", "Paçoca", "Biscoito", "Fumaça",
      "Pretinha", "Caramelo", "Faísca", "Mel", "Banzé", "Tobias", "Zeus", "Pituco", "Fred", "Nescau",
      "Costela", "Pandora", "Brigite", "Feijão", END },
    { "Trovão", "Pégaso", "Ventania", "Corisco", "Estrela", "Sereno", "Alazão", "Tufão", "Cometa",
      "Galope", "Baio", "Furacão", "Pampa", "Ouro", "Brisa", "Zorro", "Diamante", "Raio", "Fogo",
      "Tornado", END },
    { "Ana", "Theo", "Alice", "Gael", "Helena", "Bento", "Laura", "Davi", "Clara", "Noah", "Maria",
      "Lucas", "Isis", "Levi", "Lis", "Ravi", "Aurora", "Joaquim", "Liz", "Arthur", END },
    { "Mimosa", "Malhada", "Estrela", "Flor", "Branca", "Neve", "Algodão", "Fofinha", "Pintada",
      "Cocó", "Penélope", "Gigi", "Dondoca", "Mococa", "Bolota", "Docinho", "Quindim", "Cocada",
      "Pamonha", "Canjica", "Nuvem", "Lulu", "Violeta", "Bela", "Jujuba", "Mariola", "Pitanga",
      "Amora", "Cacau", "Manteiga", END },
};
static const char *const en_lists[LISTS][40] = {
    { "Alex", "Sam", "Jamie", "Riley", "Casey", "Morgan", "Taylor", "Jordan", "Avery", "Quinn",
      "Charlie", "Robin", "Skyler", "Emery", "Rowan", "Harper", "Finley", "Sage", "Ellis", "Parker",
      "Logan", "Max", "Lily", "Oliver", "Emma", "Noah", "Mia", "Leo", "Ruby", "Theo", END },
    { "Sunny", "Meadow", "Willow", "Clover", "Maple", "Bluebell", "Harvest", "Oakhill", "Cedar",
      "Hollow", "Brook", "Daisy", "Juniper", "Golden", "Pine", "Hilltop", "Rosebud", "Ivy",
      "Riverbed", "Thistle", END },
    { "Buddy", "Max", "Biscuit", "Rusty", "Scout", "Pepper", "Bandit", "Cookie", "Duke", "Lucky",
      "Shadow", "Toby", "Rocky", "Peanut", "Ziggy", "Bingo", "Bear", "Waffles", "Sparky", "Patch", END },
    { "Thunder", "Comet", "Storm", "Spirit", "Blaze", "Maverick", "Dusty", "Apollo", "Chestnut",
      "Flash", "Rocket", "Bolt", "Majesty", "Pegasus", "Copper", "Ranger", "Tornado", "Midnight", END },
    { "Lily", "Leo", "Ella", "Finn", "Rose", "Jack", "Ivy", "Owen", "Grace", "Eli", "Hazel", "Milo",
      "Nora", "Theo", "Lucy", "Sam", "Ada", "Ben", "Iris", "Jude", END },
    { "Daisy", "Bessie", "Clover", "Molly", "Snowball", "Cotton", "Fluffy", "Nugget", "Henny",
      "Peaches", "Honey", "Muffin", "Pudding", "Marble", "Dotty", "Rosie", "Mabel", "Penny", "Cocoa",
      "Sprout", "Tilly", "Oreo", "Lulu", "Pickles", "Bubbles", END },
};
static const char *const id_lists[LISTS][40] = {
    { "Adi", "Ayu", "Budi", "Dewi", "Eka", "Fajar", "Gita", "Hana", "Indra", "Joko", "Kartika",
      "Lestari", "Made", "Nanda", "Putri", "Rizki", "Sari", "Tono", "Wulan", "Yudi", "Bayu", "Citra",
      "Dimas", "Rina", END },
    { "Melati", "Mawar", "Kenanga", "Cempaka", "Makmur", "Lestari", "Subur", "Damai", "Bahagia",
      "Harapan", "Sentosa", "Asri", "Permai", "Indah", "Bukit", "Sawah", "Pelangi", "Mentari", END },
    { "Bleki", "Molly", "Brownie", "Kopi", "Coklat", "Bobo", "Choco", "Belang", "Gembul", "Kiki",
      "Jojo", "Bruno", "Snowy", "Momo", "Tofu", "Cimol", "Bakso", "Lucky", END },
    { "Kilat", "Petir", "Guntur", "Bintang", "Angin", "Badai", "Garuda", "Satria", "Perkasa", "Jaya",
      "Raja", "Rajawali", "Bara", "Elang", "Topan", "Sakti", "Wira", END },
    { "Aditya", "Bunga", "Cahya", "Dian", "Elok", "Galih", "Intan", "Kirana", "Laras", "Nadia",
      "Raka", "Salsa", "Tirta", "Wisnu", "Zahra", END },
    { "Mimi", "Susu", "Putih", "Belang", "Manis", "Gendut", "Mochi", "Kapas", "Bunga", "Melon",
      "Ciko", "Jeli", "Lala", "Nini", "Pipi", "Tutul", "Cici", "Ndut", "Gula", "Kacang", END },
};

/* the lists of each language of the game (GAME_LANG_CODE); one not here gets the English ones.
 * A new language: its lists above and a line here (letters its font hasn't are left out by
 * glyphs(): such a name is skipped) */
static const struct { const char *code; const char *const (*lists)[40]; } languages[] = {
    { "en", en_lists },
    { "pt", pt_lists },
    { "id", id_lists },
};

/* names.txt's lists (UTF-8, one name a line), read the first time */
#define FILE_NAMES 200
static char *file_text;
static const char *file_lists[LISTS][FILE_NAMES + 1];
static int file_read;

static void read_names_file(void) {
    file_read = 1;
    char path[1100];
    snprintf(path, sizeof path, "%snames.txt", exe_dir_path());
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 1 << 20 || !(file_text = calloc((size_t)n + 1, 1))) { fclose(f); return; }
    if (fread(file_text, 1, (size_t)n, f) != (size_t)n) LOGE("names: short read of %s", path);
    fclose(f);
    int list = -1, count[LISTS] = { 0 }, total = 0;
    for (char *line = file_text, *next; line && *line; line = next) {
        next = strpbrk(line, "\r\n");
        if (next) { *next++ = 0; next += strspn(next, "\r\n"); }
        if ((uint8_t)line[0] == 0xEF && (uint8_t)line[1] == 0xBB && (uint8_t)line[2] == 0xBF) line += 3;
        line += strspn(line, " \t");
        char *e = line + strlen(line);
        while (e > line && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
        if (!*line || *line == '#') continue;
        if (*line == '[') {
            list = -1;
            for (int k = 0; k < LISTS; k++)
                if (!strncmp(line + 1, list_names[k], strlen(list_names[k])) && line[1 + strlen(list_names[k])] == ']') list = k;
            continue;
        }
        if (list >= 0 && count[list] < FILE_NAMES) { file_lists[list][count[list]++] = line; total++; }
    }
    LOGI("names: %d names read from %s", total, path);
}

static const char *const *names_of(int list) {
    if (!netplay_active()) {
        if (!file_read) read_names_file();
        if (file_lists[list][0]) return file_lists[list];
    }
    for (unsigned k = 0; k < sizeof languages / sizeof *languages; k++)
        if (!strcmp(GAME_LANG_CODE, languages[k].code)) return languages[k].lists[list];
    return en_lists[list];
}

/* a name in the game's glyphs; how many (0 if it has a letter the font hasn't) */
static int glyphs(const char *s, uint16_t out[NAME_MAX]) {
    int n = 0;
    for (; *s && n < NAME_MAX; s++) {
        char ch = *s;
        int g = GAME_PT || g_game_lang == 5 ? pt_glyph(&s) : -1;
        if (g >= 0) out[n++] = (uint16_t)g;
        else if (ch >= 'A' && ch <= 'Z') out[n++] = (uint16_t)(ch - 'A');
        else if (ch >= 'a' && ch <= 'z') out[n++] = (uint16_t)(0x20 + ch - 'a');
        else if (ch >= '0' && ch <= '9') out[n++] = (uint16_t)(0x40 + ch - '0');
        else if (ch == ' ') out[n++] = 0x0FFD;
        else return 0;
    }
    while (n > 0 && out[n - 1] == 0x0FFD) n--;
    return n;
}

static int opener_kind = ANIMAL;
static uint32_t picks;

/* the screen is opened (80023130, 80023170, 800231AC; entry, ra the caller's, s0 still its) */
void names_open(CPU *c) {
    uint32_t ra = c->r[31];
    if (ra >= EVENT_NAMING && ra < EVENT_NAMING_END)
        opener_kind = (int16_t)c->r[16] == KIND_HORSE ? HORSE : (int16_t)c->r[16] == KIND_BABY ? BABY : ANIMAL;
    else opener_kind = ANIMAL;
}

static int what_is_named(uint32_t dest) {
    if (dest == FARMER_NAME) return FARMER;
    if (dest == FARM_NAME) return FARM;
    if (dest == DOG_NAME) return DOG;
    return opener_kind;
}

static void play_sound(CPU *c, uint32_t id) {
    uint32_t regs[32], hi = c->hi, lo = c->lo;
    memcpy(regs, c->r, sizeof regs);
    c->r[29] = regs[29] - 0x80;
    c->r[4] = id;
    call_addr(c, SOUND);
    memcpy(c->r, regs, sizeof regs);
    c->hi = hi;
    c->lo = lo;
}

/* the screen reads the buttons (80023DBC, entry) */
void names_buttons(CPU *c) {
    if (!g_cfg.randomnames || g_view2_run || MEM_R16(NAME_STATE) != TYPING) return;
    if (!(MEM_R32(MEM_R32(PAD_STATE) + 8u) & PAD_SELECT)) return;
    uint32_t dest = MEM_R32(NAME_DEST);
    if ((dest & 0x1FFFFFu) > 0x1FFFF0u) return;
    int list = what_is_named(dest);
    const char *const *names = names_of(list);
    int n = 0;
    while (names[n]) n++;
    if (!n) return;
    uint16_t now[NAME_SLOTS], name[NAME_MAX];
    for (int k = 0; k < NAME_SLOTS; k++) now[k] = MEM_R16(dest + 2u * k);
    uint32_t x = MEM_R32(GAME_SEED) ^ (++picks * 0x9E3779B9u);
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15;
    for (int tries = 0; tries < n; tries++) {
        int len = glyphs(names[(x + (uint32_t)tries) % (uint32_t)n], name);
        if (!len) continue;
        int same = 1;
        for (int k = 0; k < NAME_SLOTS; k++)
            if (now[k] != (k < len ? name[k] : 0xFFFFu)) same = 0;
        if (same && n > 1) continue;            /* another than the one shown */
        for (int k = 0; k < NAME_SLOTS; k++) MEM_W16(dest + 2u * k, k < len ? name[k] : 0xFFFFu);
        MEM_W16(NAME_LEN, (uint16_t)len);
        MEM_W32(NAME_TYPED, 1);
        LOGI("names: %s %s", list_names[list], names[(x + (uint32_t)tries) % (uint32_t)n]);
        play_sound(c, 0);
        return;
    }
}
