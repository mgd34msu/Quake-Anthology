/* Port of src/network/q3/rcon.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q3_RCON_H
#define QA_NETWORK_Q3_RCON_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/network/q3/admission.h"
#include "qa/network/q3/connectionless.h"
typedef const char *(*qa_q3_rcon_password_fn)(void *ctx);
typedef void (*qa_q3_rcon_flush_fn)(void *ctx, const char *text);
typedef void (*qa_q3_rcon_execute_fn)(void *ctx, const char *command);
typedef void (*qa_q3_rcon_thunk_fn)(void *ctx);
typedef void (*qa_q3_rcon_redirect_fn)(void *ctx, int64_t capacity,
                                       qa_q3_rcon_flush_fn flush, void *flush_ctx,
                                       qa_q3_rcon_thunk_fn execute, void *execute_ctx);
typedef void (*qa_q3_rcon_send_fn)(void *ctx, const qa_q3_address *to, const uint8_t *bytes, size_t len);
/* TS Q3RconBindings. */
typedef struct qa_q3_rcon_bindings {
  qa_q3_rcon_password_fn password;
  qa_q3_message_print_fn print; void *print_ctx;
  qa_q3_rcon_redirect_fn redirect;
  qa_q3_rcon_execute_fn execute;
  qa_q3_rcon_send_fn send;
  void *ctx;
} qa_q3_rcon_bindings;
/* TS Q3RconState kept across replacements. */
typedef struct qa_q3_rcon_state { double last_time; } qa_q3_rcon_state;
typedef struct qa_q3_rcon qa_q3_rcon;
bool qa_q3_rcon_state_init(qa_q3_rcon_state *s);
bool qa_q3_rcon_init(qa_q3_rcon **out, qa_q3_rcon_state *state, const qa_q3_rcon_bindings *bindings);
void qa_q3_rcon_destroy(qa_q3_rcon *r);
qa_q3_rcon_state *qa_q3_rcon_get_state(const qa_q3_rcon *r);
const qa_q3_rcon_bindings *qa_q3_rcon_get_bindings(const qa_q3_rcon *r);
void qa_q3_rcon_receive(qa_q3_rcon *r, const qa_q3_address *from, const qa_q3_connectionless_packet *packet, double milliseconds);
bool qa_q3_encode_rcon(const char *password, const char *command, uint8_t **out, size_t *out_len);
#endif
