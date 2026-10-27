/*
 * Port of src/persistence/q1.ts: Quake Host_Savegame_f / Host_Loadgame_f
 * version 5 text saves, plus Ironwail's KEX version 6 header.
 * C accommodations:
 *  - TS QcTextPair (from compat/qc/save.ts, type-only there) is carried
 *    here as qa_q1_text_pair; the compat port reconciles the alias.
 *  - All strings/arrays in a decoded qa_q1_save_data are malloc'd and
 *    released by qa_q1_save_data_free. qa_encode_q1_save borrows its
 *    input. Decode/encode return false / NULL only on allocation
 *    failure; malformed saves abort via qa_save_format_error with the
 *    exact TS SaveFormatError path ("q1" or "q1:<offset>") and message.
 *  - TS strings are byte strings here (byteText is Latin-1, one char
 *    per byte), so scanner offsets equal byte offsets. Strings are
 *    NUL-terminated: a quoted field containing an interior NUL byte is
 *    truncated at that NUL on later C-string use (the TS encode path
 *    rejects such strings anyway). extension_text carries an explicit
 *    length because trailing extension bytes are copied verbatim.
 *  - Number formatting: decimal fields use "%.6f" (TS toFixed(6));
 *    for magnitudes >= 1e21 JS switches to exponent spelling where
 *    this port prints full digits (cosmetic, same class of note as in
 *    contracts/presentation.c).
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q1_H
#define QA_PERSISTENCE_Q1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct qa_q1_text_pair {
  char *key;   /* owned in decoded data */
  char *value; /* owned in decoded data */
} qa_q1_text_pair;

typedef struct qa_q1_text_record {
  qa_q1_text_pair *pairs; /* owned in decoded data */
  size_t pair_count;
} qa_q1_text_record;

typedef struct qa_q1_save_data {
  int format_version;       /* 5 or 6 */
  char *game_directories;   /* owned; version 6 only, NULL for 5 */
  char *comment;            /* owned */
  double *spawn_parameters; /* owned; 16 entries */
  size_t spawn_parameter_count;
  double skill;
  char *map;    /* owned */
  double time;
  char **light_styles; /* owned array of owned strings; 64 entries */
  size_t light_style_count;
  qa_q1_text_pair *globals; /* owned */
  size_t global_count;
  /* Array order is the original edict number; an empty record is a
   * free slot. */
  qa_q1_text_record *entities; /* owned */
  size_t entity_count;
  char *extension_text; /* owned, NUL-terminated for convenience */
  size_t extension_length;
} qa_q1_save_data;

/* Fills *out (zeroed first). Returns false only on OOM; malformed
 * input aborts as the TS throws SaveFormatError. */
bool qa_decode_q1_save(const uint8_t *bytes, size_t length, qa_q1_save_data *out);
/* Malloc'd save bytes; *out_length set. NULL only on OOM; invalid
 * saves abort as the TS throws SaveFormatError. */
uint8_t *qa_encode_q1_save(const qa_q1_save_data *save, size_t *out_length);
void qa_q1_save_data_free(qa_q1_save_data *save);

#endif
