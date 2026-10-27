/*
 * Port of src/persistence/q2-containers.ts. Decoded strings and
 * arrays are malloc'd (qa_*_free helpers release a whole record);
 * encoders return malloc'd bytes with *out_length set (NULL only on
 * OOM). Timestamps (TS bigint) are uint64_t. GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q2_CONTAINERS_H
#define QA_PERSISTENCE_Q2_CONTAINERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct qa_saved_cvar { char *name; char *value; } qa_saved_cvar;
typedef struct qa_q2_classic_server_save {
  char *comment; char *map_command;
  qa_saved_cvar *cvars; size_t cvar_count;
} qa_q2_classic_server_save;
typedef struct qa_q2_rerelease_server_save {
  char *comment; char *map_command;
  qa_saved_cvar *cvars; size_t cvar_count;
  uint64_t timestamp; int kind; /* 0 | 1 | 2 */
} qa_q2_rerelease_server_save;
typedef struct qa_q2_configstring { int64_t index; char *value; } qa_q2_configstring;
typedef struct qa_q2_level_metadata {
  qa_q2_configstring *configstrings; size_t configstring_count;
  uint8_t *portal_bytes; size_t portal_length;
} qa_q2_level_metadata;

bool qa_decode_q2_classic_server(const uint8_t *bytes, size_t length,
                                 qa_q2_classic_server_save *out);
uint8_t *qa_encode_q2_classic_server(const qa_q2_classic_server_save *save,
                                     size_t *out_length);
/* TS defaults configstringCount = 2080, width = 64. */
bool qa_decode_q2_classic_level_metadata(const uint8_t *bytes, size_t length,
                                         int64_t configstring_count, int64_t width,
                                         qa_q2_level_metadata *out);
uint8_t *qa_encode_q2_classic_level_metadata(const qa_q2_level_metadata *save,
                                             int64_t configstring_count, int64_t width,
                                             size_t *out_length);
bool qa_decode_q2_rerelease_server(const uint8_t *bytes, size_t length,
                                   qa_q2_rerelease_server_save *out);
uint8_t *qa_encode_q2_rerelease_server(const qa_q2_rerelease_server_save *save,
                                       size_t *out_length);
bool qa_decode_q2_rerelease_level_metadata(const uint8_t *bytes, size_t length,
                                           int64_t configstring_end,
                                           qa_q2_level_metadata *out);
uint8_t *qa_encode_q2_rerelease_level_metadata(const qa_q2_level_metadata *save,
                                               int64_t configstring_end,
                                               size_t *out_length);

void qa_q2_classic_server_save_free(qa_q2_classic_server_save *save);
void qa_q2_rerelease_server_save_free(qa_q2_rerelease_server_save *save);
void qa_q2_level_metadata_free(qa_q2_level_metadata *save);

#endif
