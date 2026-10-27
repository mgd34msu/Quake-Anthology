/*
 * Port of src/persistence/q2-typescript.ts. Save structs borrow the
 * parsed source-json tree; the caller frees `root`. Encoders build a
 * fresh flat object mirroring the TS spread (root keys first, the
 * replaced key keeps its position) and return malloc'd text.
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q2_TYPESCRIPT_H
#define QA_PERSISTENCE_Q2_TYPESCRIPT_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/persistence/source_json.h"

typedef struct qa_q2_typescript_game_save {
  qa_save_json *root; /* owned */
  qa_save_json **clients; /* borrowed nodes in root; malloc'd array */
  size_t client_count;
} qa_q2_typescript_game_save;
typedef struct qa_q2_typescript_level_edict {
  double index;
  qa_save_json *data; /* borrowed */
} qa_q2_typescript_level_edict;
typedef struct qa_q2_typescript_level_save {
  qa_save_json *root; /* owned */
  qa_save_json *level; /* borrowed */
  qa_q2_typescript_level_edict *edicts; /* malloc'd array */
  size_t edict_count;
} qa_q2_typescript_level_save;

/* False only on OOM. */
bool qa_decode_q2_typescript_game(const char *text, qa_q2_typescript_game_save *out);
char *qa_encode_q2_typescript_game(const qa_q2_typescript_game_save *save);
bool qa_decode_q2_typescript_level(const char *text, qa_q2_typescript_level_save *out);
char *qa_encode_q2_typescript_level(const qa_q2_typescript_level_save *save);

#endif
