/*
 * Port of src/compat/qvm/mod-clients.ts. The q3CommandForControls /
 * relativeQ3SourceCommand helpers (TS owner
 * src/app/bootstrap/simulation/q3-commands.ts, unlanded) are mirrored
 * as static functions in the .c file. Application identity in TS is
 * object identity; the C applications list compares pointers.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_MOD_CLIENTS_H
#define QA_COMPAT_QVM_MOD_CLIENTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/protocol.h"
#include "qa/contracts/qvm_mod_callbacks.h"
#include "qa/network/q3/message.h"
#include "qa/world/session/mod_clients.h"

/* TS QvmModClientSlot. */
typedef struct qa_qvm_mod_client_slot {
  qa_actor_id actor;
  int64_t slot;
  bool admitted;
} qa_qvm_mod_client_slot;

typedef void (*qa_qvm_client_invoke_fn)(void *run_ctx);

typedef struct qa_qvm_mod_client_operations {
  qa_content_id content;
  qa_mod_client_services *services;
  const qa_qvm_mod_clients *declaration;
  void *ctx;
  void (*project)(void *ctx, qa_actor_id actor);
  void (*admitted)(void *ctx, qa_actor_id actor); /* optional */
  void (*release)(void *ctx, qa_actor_id actor);
  void (*invoke)(void *ctx, const qa_qvm_mod_source_call *call, qa_actor_id actor,
                 qa_mod_client_application *application);
  /* Returns an optional cleanup closure through the out params. */
  void (*open_input)(void *ctx, qa_mod_client_application *application,
                     void (**cleanup)(void *cleanup_ctx), void **cleanup_ctx);
  /* Fills a malloc'd filtered output array. Optional. */
  void (*output)(void *ctx, const qa_qvm_mod_input_output *outputs, size_t output_count,
                 qa_mod_client_application *application, qa_qvm_client_invoke_fn run, void *run_ctx,
                 qa_mod_client_input_output **out_outputs, size_t *out_count);
  size_t (*reserved_slots)(void *ctx, int64_t *out, size_t capacity); /* optional */
  void (*player_state)(void *ctx, qa_actor_id actor, qa_q3_player_state *out);
  bool (*requested_weapon)(void *ctx, qa_actor_id actor, double *out); /* optional */
  void (*send)(void *ctx, const char *text, const qa_actor_id *recipient);
} qa_qvm_mod_client_operations;

typedef struct qa_qvm_mod_client_bindings {
  qa_qvm_mod_client_operations operations;
  void *entries; /* opaque */
  size_t entry_count;
  size_t entry_capacity;
  bool has_unsubscribe;
  qa_mod_client_unsubscribe unsubscribe;
  bool has_unsubscribe_input;
  qa_mod_client_unsubscribe unsubscribe_input;
  qa_mod_client_application **applications; /* borrowed pointers */
  size_t application_count;
  size_t application_capacity;
} qa_qvm_mod_client_bindings;

void qa_qvm_mod_client_bindings_init(qa_qvm_mod_client_bindings *bindings,
                                     const qa_qvm_mod_client_operations *operations);
void qa_qvm_mod_client_bindings_destroy(qa_qvm_mod_client_bindings *bindings);
bool qa_qvm_mod_client_bindings_has(const qa_qvm_mod_client_bindings *bindings, qa_actor_id actor);
bool qa_qvm_mod_client_bindings_live(qa_qvm_mod_client_bindings *bindings, qa_actor_id actor);
bool qa_qvm_mod_client_bindings_admitted(qa_qvm_mod_client_bindings *bindings, qa_actor_id actor);
/* False is TS null. */
bool qa_qvm_mod_client_bindings_slot(qa_qvm_mod_client_bindings *bindings, qa_actor_id actor, int64_t *out);
void qa_qvm_mod_client_bindings_start(qa_qvm_mod_client_bindings *bindings);
void qa_qvm_mod_client_bindings_restore(qa_qvm_mod_client_bindings *bindings,
                                        const qa_qvm_mod_client_slot *entries, size_t count);
void qa_qvm_mod_client_bindings_frame(qa_qvm_mod_client_bindings *bindings,
                                      void (*invoke)(void *ctx, const qa_qvm_mod_source_call *call, qa_actor_id actor),
                                      void *invoke_ctx);
/* Fills out; returns the total count. */
size_t qa_qvm_mod_client_bindings_players(qa_qvm_mod_client_bindings *bindings,
                                          qa_qvm_mod_client_slot *out, size_t capacity);
size_t qa_qvm_mod_client_bindings_checkpoint(qa_qvm_mod_client_bindings *bindings,
                                             qa_qvm_mod_client_slot *out, size_t capacity);
void qa_qvm_mod_client_bindings_forget(qa_qvm_mod_client_bindings *bindings, qa_actor_id actor);
void qa_qvm_mod_client_bindings_close(qa_qvm_mod_client_bindings *bindings);
/* Borrowed string ("" when unbound). */
const char *qa_qvm_mod_client_bindings_get_userinfo(qa_qvm_mod_client_bindings *bindings, int64_t slot);
void qa_qvm_mod_client_bindings_set_userinfo(qa_qvm_mod_client_bindings *bindings, int64_t slot, const char *value);
void qa_qvm_mod_client_bindings_drop_client(qa_qvm_mod_client_bindings *bindings, int64_t slot, const char *reason);
void qa_qvm_mod_client_bindings_send_server_command(qa_qvm_mod_client_bindings *bindings, int64_t slot, const char *text);
qa_q3_wire_user_command qa_qvm_mod_client_bindings_get_user_command(qa_qvm_mod_client_bindings *bindings, int64_t slot);

#endif
