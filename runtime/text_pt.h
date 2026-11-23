/* Accented letters of the Portuguese translation's font, for the port's own texts (UTF-8 in the
 * sources). The translation draws them over glyphs the English game doesn't use. */
#ifndef TEXT_PT_H
#define TEXT_PT_H
#include <stdint.h>

extern const int g_game_lang;

/* the Spanish translation's font has its own (fewer) letters, drawn over other glyphs */
static inline int es_glyph(const char **s) {
    static const struct { uint8_t lead, utf8; uint16_t glyph; } letters[] = {
        { 0xC3, 0xA1, 0x5A }, { 0xC3, 0xA9, 0x55 }, { 0xC3, 0xAD, 0x56 }, { 0xC3, 0xB3, 0x5B },  /* á é í ó */
        { 0xC3, 0xB1, 0x54 }, { 0xC2, 0xBF, 0x52 }, { 0xC2, 0xA1, 0x53 },                        /* ñ ¿ ¡ */
    };
    for (unsigned i = 0; i < sizeof letters / sizeof *letters; i++)
        if ((uint8_t)(*s)[0] == letters[i].lead && (uint8_t)(*s)[1] == letters[i].utf8) {
            (*s)++;
            return letters[i].glyph;
        }
    return -1;
}

/* the Japanese font (build.py --lang ja) has capitals, digits and some signs of its own: the port's
 * texts in it are in capitals */
static inline int ja_ascii(char ch) {
    if (ch >= 'A' && ch <= 'Z') return 161 + ch - 'A';
    if (ch >= 'a' && ch <= 'z') return 161 + ch - 'a';
    if (ch >= '0' && ch <= '9') return 187 + ch - '0';
    switch (ch) {
    case ' ': case '\'': return 0x0FFD;
    case '.': return 197;  case ',': return 198;  case '?': return 202;  case '!': return 203;
    case ':': return 206;  case '+': return 207;  case '-': return 199;  case '(': return 209;
    default: return -1;
    }
}

/* a digit's glyph */
static inline uint16_t game_digit(int d) {
    return (uint16_t)(g_game_lang == 5 ? 187 + d : 0x40 + d);
}

/* *s at a UTF-8 accented letter: its glyph, *s moved to the sequence's last byte; else -1 (in Japanese:
 * the glyph of every letter, digit and sign it has) */
static inline int pt_glyph(const char **s) {
    if (g_game_lang == 5) return ja_ascii(**s);
    if (g_game_lang == 4) return es_glyph(s);
    static const struct { uint8_t utf8; uint16_t glyph; } letters[] = {
        { 0x82, 0x1A }, { 0x83, 0x1C }, { 0x95, 0x1D },                          /* Â Ã Õ */
        { 0xA1, 0x3A }, { 0xA0, 0x3B }, { 0xA2, 0x3C }, { 0xA3, 0x3D },          /* á à â ã */
        { 0xA9, 0x3E }, { 0xAA, 0x3F }, { 0xAD, 0x4A }, { 0xB3, 0x4B },          /* é ê í ó */
        { 0xB4, 0x4C }, { 0xB5, 0x4D }, { 0xBA, 0x4E }, { 0xA7, 0x4F },          /* ô õ ú ç */
        { 0x89, 0x57 }, { 0x81, 0x59 }, { 0x93, 0x5A }, { 0x87, 0x5B },          /* É Á Ó Ç */
        { 0x8A, 0x5C }, { 0x8D, 0x5D }, { 0x9A, 0x5E }, { 0x94, 0x5F },          /* Ê Í Ú Ô */
    };
    if ((uint8_t)(*s)[0] != 0xC3) return -1;
    for (unsigned i = 0; i < sizeof letters / sizeof *letters; i++)
        if ((uint8_t)(*s)[1] == letters[i].utf8) {
            (*s)++;
            return letters[i].glyph;
        }
    return -1;
}

#endif
