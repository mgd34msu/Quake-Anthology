/*
 * Port of src/persistence/q3.ts. C accommodations: TS byte strings
 * are ptr+length (sourceBuffer cuts at the first NUL and at 1023
 * bytes); Q3SessionCvars becomes get/set callbacks + ctx (get
 * returns ptr+length). GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q3_H
#define QA_PERSISTENCE_Q3_H

#include <stddef.h>
#include <stdint.h>

typedef struct qa_q3_client_session {
  int32_t team, spectator_time, spectator_state, spectator_client;
  int32_t wins, losses, team_leader;
} qa_q3_client_session;

qa_q3_client_session qa_decode_q3_client_session(const char *value, size_t length);
/* Malloc'd "a b c ..." text; NULL only on OOM. */
char *qa_encode_q3_client_session(qa_q3_client_session session);

typedef struct qa_q3_session_cvars {
  void *ctx;
  const char *(*get)(void *ctx, const char *name, size_t *out_length);
  void (*set)(void *ctx, const char *name, const char *value, size_t length);
} qa_q3_session_cvars;

typedef struct qa_q3_session_save_client {
  int64_t slot;
  qa_q3_client_session session;
} qa_q3_session_save_client;
typedef struct qa_q3_session_save {
  int32_t game_type;
  /* Malloc'd array (caller frees); NULL with count 0 on OOM only
   * when client_count is 0. */
  qa_q3_session_save_client *clients;
  size_t client_count;
} qa_q3_session_save;

qa_q3_session_save qa_read_q3_sessions(qa_q3_session_cvars cvars,
                                       const int64_t *client_slots, size_t slot_count);
void qa_write_q3_sessions(qa_q3_session_cvars cvars, qa_q3_session_save save);

#endif
