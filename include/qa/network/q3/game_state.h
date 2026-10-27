/* Port of src/network/q3/game-state.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q3_GAME_STATE_H
#define QA_NETWORK_Q3_GAME_STATE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/network/q3/message.h"
/* TS SourceGameStateRecord. Arrays owned (free). */
typedef struct qa_q3_source_game_state_record {
  int32_t *string_offsets; /* 1024 */
  uint8_t *string_data;    /* 16000 */
  int64_t data_count;
} qa_q3_source_game_state_record;
void qa_q3_source_game_state_record_destroy(qa_q3_source_game_state_record *r);
/* drop closure (message)->never becomes fn+ctx; NULL drop uses qa_common_error(drop). */
typedef void (*qa_q3_game_state_drop_fn)(void *ctx, const char *message);
typedef struct qa_q3_client_game_state_storage qa_q3_client_game_state_storage;
bool qa_q3_client_game_state_storage_init(qa_q3_client_game_state_storage **out, qa_q3_game_state_drop_fn drop, void *drop_ctx);
void qa_q3_client_game_state_storage_destroy(qa_q3_client_game_state_storage *s);
void qa_q3_client_game_state_storage_clear(qa_q3_client_game_state_storage *s);
void qa_q3_client_game_state_storage_begin_entries(qa_q3_client_game_state_storage *s);
bool qa_q3_client_game_state_storage_copy_source_record(const qa_q3_client_game_state_storage *s, qa_q3_source_game_state_record *out);
/* copyStrings: out[1024] malloc'd strings (caller frees each + array). */
bool qa_q3_client_game_state_storage_copy_strings(const qa_q3_client_game_state_storage *s, char ***out, size_t *count);
/* get: *out malloc'd or NULL when TS null. */
bool qa_q3_client_game_state_storage_get(const qa_q3_client_game_state_storage *s, int64_t index, char **out);
void qa_q3_client_game_state_storage_append(qa_q3_client_game_state_storage *s, int64_t index, const char *value);
bool qa_q3_client_game_state_storage_modify(qa_q3_client_game_state_storage *s, int64_t index, const char *value);
#endif
