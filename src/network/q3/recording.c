/* Port of src/network/q3/recording.ts. GPL-2.0-or-later. */
#include "qa/network/q3/recording.h"
#include "qa/foundation/support.h"
#include <stdlib.h>
#include <string.h>
static qa_q3_entity_state_record *recording_baseline(void *ctx, int64_t number) {
  qa_q3_client_connection *client = ctx;
  if (number < 0 || number >= 1024) return NULL;
  size_t count;
  return &qa_q3_client_connection_baselines(client, &count)[number];
}
bool qa_q3_demo_gamestate(qa_q3_client_connection *client, int64_t *sequence_out, uint8_t **message_out, size_t *message_len_out) {
  int64_t sequence = (int32_t)(qa_q3_client_connection_server_message_sequence(client) - 1);
  qa_q3_gamestate state;
  if (!qa_q3_client_connection_copy_gamestate(client, &state)) return false;
  qa_q3_server_operation op; memset(&op, 0, sizeof op);
  op.kind = QA_Q3_OP_GAMESTATE; op.gamestate = state;
  qa_q3_server_message_context ctx;
  ctx.product = qa_q3_client_connection_get_product(client);
  ctx.message_number = sequence;
  qa_q3_client_reliable_commands *reliable = qa_q3_client_connection_reliable(client);
  ctx.reliable_sequence = qa_q3_reliable_ring_sequence(reliable);
  ctx.server_command_sequence = qa_q3_client_connection_server_command_sequence(client);
  ctx.parse_entities_number = 0;
  ctx.baseline = recording_baseline; ctx.baseline_ctx = client;
  ctx.history = NULL; ctx.history_ctx = NULL;
  bool ok = qa_q3_encode_server_message(qa_q3_reliable_ring_sequence(reliable), &op, 1, &ctx,
                                        qa_q3_client_connection_source_state(client), message_out, message_len_out);
  qa_q3_gamestate_destroy(&state);
  *sequence_out = sequence;
  return ok;
}
struct qa_q3_demo_recording {
  qa_q3_client_connection *client;
  qa_q3_demo_sink file;
  bool ended;
};
static void recording_write(qa_q3_demo_recording *r, int64_t sequence, const uint8_t *bytes, size_t len) {
  uint8_t header[8];
  uint32_t s = (uint32_t)(int32_t)sequence, l = (uint32_t)len;
  header[0] = (uint8_t)s; header[1] = (uint8_t)(s >> 8); header[2] = (uint8_t)(s >> 16); header[3] = (uint8_t)(s >> 24);
  header[4] = (uint8_t)l; header[5] = (uint8_t)(l >> 8); header[6] = (uint8_t)(l >> 16); header[7] = (uint8_t)(l >> 24);
  r->file.write_bytes(r->file.ctx, header, 8);
  r->file.write_bytes(r->file.ctx, bytes, len);
}
bool qa_q3_demo_recording_init(qa_q3_demo_recording **out, qa_q3_client_connection *client, const qa_q3_demo_sink *file) {
  qa_q3_demo_recording *r = calloc(1, sizeof *r);
  if (!r) return false;
  r->client = client; r->file = *file;
  qa_q3_client_connection_set_demo_waiting(client, true);
  int64_t sequence = 0; uint8_t *message = NULL; size_t len = 0;
  if (!qa_q3_demo_gamestate(client, &sequence, &message, &len)) { free(r); return false; }
  recording_write(r, sequence, message, len);
  free(message);
  *out = r; return true;
}
void qa_q3_demo_recording_destroy(qa_q3_demo_recording *r) { free(r); }
qa_q3_client_connection *qa_q3_demo_recording_client(const qa_q3_demo_recording *r) { return r->client; }
void qa_q3_demo_recording_append(qa_q3_demo_recording *r, const uint8_t *bytes, size_t len) {
  if (r->ended) qa_raise("Error", 0, 0, "Q3 demo recording is closed");
  if (!qa_q3_client_connection_demo_waiting(r->client))
    recording_write(r, qa_q3_client_connection_server_message_sequence(r->client), bytes, len);
}
typedef struct terminator_scope { qa_q3_demo_recording *r; } terminator_scope;
static void terminator_body(void *ctx) {
  qa_q3_demo_recording *r = ((terminator_scope *)ctx)->r;
  uint8_t terminator[8]; memset(terminator, 255, 8);
  r->file.write_bytes(r->file.ctx, terminator, 8);
}
void qa_q3_demo_recording_stop(qa_q3_demo_recording *r) {
  if (r->ended) return;
  r->ended = true;
  /* TS `try { writeBytes(terminator) } finally { close() }`. */
  terminator_scope scope = { r };
  qa_caught_error caught;
  bool ok = qa_try(terminator_body, &scope, &caught);
  r->file.close(r->file.ctx);
  if (!ok) qa_rethrow(&caught);
}
void qa_q3_demo_recording_close(qa_q3_demo_recording *r) {
  if (!r->ended) { r->ended = true; r->file.close(r->file.ctx); }
}
