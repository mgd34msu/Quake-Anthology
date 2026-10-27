/* Port of src/network/q3/state-delta.ts (msg.c delta codecs). GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q3_STATE_DELTA_H
#define QA_NETWORK_Q3_STATE_DELTA_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/network/q3/message.h"
#include "qa/network/q3/state/entity.h"
#include "qa/network/q3/state/player.h"
#define QA_Q3_ENTITY_NUMBER_BITS 10
#define QA_Q3_MAX_ENTITIES 1024
typedef int64_t (*qa_q3_delta_shownet_fn)(void *ctx);
/* TS DeltaMessageDiagnostics. */
typedef struct qa_q3_delta_message_diagnostics {
  qa_q3_delta_shownet_fn shownet; void *shownet_ctx;
  qa_q3_message_print_fn print; void *print_ctx;
  int64_t offset;
} qa_q3_delta_message_diagnostics;
typedef struct qa_q3_state_delta_field { const char *name; int64_t bits; } qa_q3_state_delta_field;
/* TS stateDeltaFields(kind): kind 0=entity 1=player. Returns static table. */
const qa_q3_state_delta_field *qa_q3_state_delta_fields(int kind, size_t *count);
void qa_q3_write_delta_entity(qa_q3_message_writer *w, const qa_q3_entity_state_record *from,
                              const qa_q3_entity_state_record *to, bool force); /* NULL from/to allowed */
void qa_q3_read_delta_entity(qa_q3_message_reader *r, const qa_q3_entity_state_record *from, int64_t number,
                             const qa_q3_delta_message_diagnostics *diagnostics, qa_q3_entity_state_record *out);
void qa_q3_write_delta_player_state(qa_q3_message_writer *w, const qa_q3_player_state_record *from,
                                    const qa_q3_player_state_record *to); /* NULL from allowed */
void qa_q3_read_delta_player_state(qa_q3_message_reader *r, const qa_q3_player_state_record *from, qa_q3_product product,
                                   const qa_q3_delta_message_diagnostics *diagnostics, qa_q3_player_state_record *out);
#endif
