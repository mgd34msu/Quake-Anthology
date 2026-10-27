/*
 * Port of src/persistence/kex-q2-save.ts. C accommodations: the
 * async file readers are synchronous (comment at the definitions);
 * gunzip uses zlib; the parsed game.ssv root (TS unknown) is a
 * qa_save_value tree from qa_parse_json_value (NULL when absent).
 * Strings and arrays are malloc'd; qa_*_free releases a record.
 * Timestamps (TS bigint) are uint64_t. GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_KEX_Q2_SAVE_H
#define QA_PERSISTENCE_KEX_Q2_SAVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/persistence/value.h"

typedef struct qa_kex_cvar { char *name; char *value; } qa_kex_cvar;
typedef struct qa_kex_q2_save_info {
  char *comment;
  char *map_command;
  uint64_t timestamp;
  char *game_directory; /* NULL when absent */
  qa_kex_cvar *cvars; /* insertion order; later duplicates win on
                           lookup, as in the TS Map */
  size_t cvar_count;
} qa_kex_q2_save_info;
typedef struct qa_kex_q2_save_archive {
  qa_kex_q2_save_info info;
  char **files; /* lowercase names in archive order */
  size_t file_count;
  qa_save_value *game; /* owned; NULL when game.ssv is absent */
} qa_kex_q2_save_archive;

bool qa_decode_kex_q2_server_save(const uint8_t *bytes, size_t length,
                                  qa_kex_q2_save_info *out);
/* TS async readKexQ2SaveArchive/readKexQ2SaveInfo: synchronous here. */
bool qa_read_kex_q2_save_archive(const char *path, qa_kex_q2_save_archive *out);
bool qa_read_kex_q2_save_info(const char *path, qa_kex_q2_save_info *out);
bool qa_kex_q2_save_restorable(const qa_kex_q2_save_archive *save);

void qa_kex_q2_save_info_free(qa_kex_q2_save_info *info);
void qa_kex_q2_save_archive_free(qa_kex_q2_save_archive *archive);

#endif
