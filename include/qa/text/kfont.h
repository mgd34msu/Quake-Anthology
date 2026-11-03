/*
 * Port of src/text/kfont.ts: Quake rerelease .kfont parsing and glyph
 * lookup.
 * C accommodations:
 *  - TS Map<number, KfontCharT> becomes an insertion-ordered entry
 *    array (qa_kfont_glyph_entry); setting an existing codepoint
 *    replaces in place, as a JS Map does. Codepoints are doubles
 *    (parseInt results), compared with the same equality a Map uses.
 *  - The fixed ASCII table is chars[QA_KFONT_NUM_CHARS] with a
 *    parallel present[] (TS null slots).
 *  - ParsedKfontT owns its texture token and glyph entries; KfontT
 *    borrows its (the TS shares the parsed objects), which
 *    qa_kfont_from_parsed sets up. Parse returns false for TS null
 *    (malformed input) and on OOM; *out is left zeroed either way.
 * GPL-2.0-or-later.
 */
#ifndef QA_TEXT_KFONT_H
#define QA_TEXT_KFONT_H

#include <stdbool.h>
#include <stddef.h>

#define QA_KFONT_ASCII_MIN 32
#define QA_KFONT_ASCII_MAX 126
#define QA_KFONT_NUM_CHARS (QA_KFONT_ASCII_MAX - QA_KFONT_ASCII_MIN + 1)

typedef struct qa_kfont_char {
  double x, y, w, h;
  bool has_color; /* false is TS color === undefined */
  bool color;
} qa_kfont_char;

typedef struct qa_kfont_glyph_entry {
  double codepoint;
  qa_kfont_char glyph;
} qa_kfont_glyph_entry;

/* TS ParsedKfontT. Owns texture_token and glyphs. */
typedef struct qa_parsed_kfont {
  char *texture_token;
  qa_kfont_char chars[QA_KFONT_NUM_CHARS];
  bool present[QA_KFONT_NUM_CHARS];
  qa_kfont_glyph_entry *glyphs;
  size_t glyph_count;
  size_t glyph_capacity;
  double line_height;
} qa_parsed_kfont;

/* TS KfontT. Borrows pic, chars and glyphs. */
typedef struct qa_kfont {
  const char *pic;
  const qa_kfont_char *chars;
  const bool *present;
  const qa_kfont_glyph_entry *glyphs;
  size_t glyph_count;
  double line_height;
} qa_kfont;

/* TS ParseKfont. Input is NUL-terminated bytes (the TS decodes UTF-8;
 * token bytes compare identically for the ASCII grammar). */
bool qa_parse_kfont(const char *text, qa_parsed_kfont *out);
void qa_parsed_kfont_destroy(qa_parsed_kfont *parsed);
/* Borrowed glyph for a codepoint, or NULL (TS Map.get undefined). */
const qa_kfont_char *qa_parsed_kfont_glyph(const qa_parsed_kfont *parsed,
                                           double codepoint);
/* Views a parsed font as a KfontT borrowing its storage. */
qa_kfont qa_kfont_from_parsed(const qa_parsed_kfont *parsed, const char *pic);

/* TS SCR_KFontLookup / kfontGlyph. NULL is the TS null (absent or
 * zero-width glyph). */
const qa_kfont_char *qa_scr_kfont_lookup(const qa_kfont *font,
                                         double codepoint);
const qa_kfont_char *qa_kfont_glyph(const qa_kfont *font, double codepoint);

#endif
