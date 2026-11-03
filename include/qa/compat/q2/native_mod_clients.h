/* Port of src/compat/q2/native-mod-clients.ts. Source row numbers are private;
 * every external operation resolves the destination generation. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_MOD_CLIENTS_H
#define QA_COMPAT_Q2_NATIVE_MOD_CLIENTS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/native_mod_callbacks.h"
#include "qa/core/commands/index.h"
#include "qa/foundation/support.h"
#include "qa/world/session/mod_clients.h"

typedef struct qa_compat_q2_native_mod_client_slot { qa_actor_id actor; double slot; bool admitted; } qa_compat_q2_native_mod_client_slot;
/* TS Operations. admitted may be NULL. invoke's false is the TS null result.
 * open_input fills its cleanup (cleanup may stay NULL). input_output runs
 * run(run_ctx) and returns a malloc'd output list. */
typedef struct qa_compat_q2_native_mod_client_operations {
  qa_mod_client_services *services;
  const qa_native_mod_clients *declaration;
  qa_content_id content;
  void *ctx;
  void (*project)(void *ctx, qa_actor_id actor);
  void (*admitted)(void *ctx, qa_actor_id actor);
  void (*release)(void *ctx, qa_actor_id actor);
  bool (*invoke)(void *ctx, const qa_native_mod_source_call *call, qa_actor_id actor, double *out);
  void (*invoke_input)(void *ctx, const qa_native_mod_source_call *call, qa_mod_client_application *application);
  void (*open_input)(void *ctx, qa_mod_client_application *application, void (**cleanup)(void *), void **cleanup_ctx);
  void (*input_output)(void *ctx, const qa_native_mod_input_output *outputs, size_t output_count, qa_mod_client_application *application,
                       void (*run)(void *), void *run_ctx, qa_mod_client_input_output **out_outputs, size_t *out_count);
  void (*with_command)(void *ctx, qa_command_invocation *command, void (*invoke)(void *), void *invoke_ctx);
} qa_compat_q2_native_mod_client_operations;
typedef struct qa_compat_q2_native_client_entry { qa_actor_id actor; qa_client_id client; double slot; bool admitted; uint64_t identity; } qa_compat_q2_native_client_entry;
typedef struct qa_compat_q2_native_mod_clients_binding {
  qa_compat_q2_native_mod_client_operations operations;
  qa_compat_q2_native_client_entry *entries; size_t entry_count, entry_capacity;
  qa_actor_id *denied; size_t denied_count, denied_capacity;
  uint64_t next_identity;
  bool subscribed, input_subscribed;
  qa_mod_client_unsubscribe unsubscribe, unsubscribe_input;
} qa_compat_q2_native_mod_clients_binding;
void qa_compat_q2_native_mod_clients_init(qa_compat_q2_native_mod_clients_binding *s, qa_compat_q2_native_mod_client_operations operations);
void qa_compat_q2_native_mod_clients_destroy(qa_compat_q2_native_mod_clients_binding *s);
/* False is TS null. */
bool qa_compat_q2_native_mod_clients_slot(qa_compat_q2_native_mod_clients_binding *s, qa_actor_id actor, double *out);
bool qa_compat_q2_native_mod_clients_has(const qa_compat_q2_native_mod_clients_binding *s, qa_actor_id actor);
bool qa_compat_q2_native_mod_clients_admitted(qa_compat_q2_native_mod_clients_binding *s, qa_actor_id actor);
bool qa_compat_q2_native_mod_clients_rejects(const qa_compat_q2_native_mod_clients_binding *s, qa_actor_id actor);
void qa_compat_q2_native_mod_clients_start(qa_compat_q2_native_mod_clients_binding *s);
bool qa_compat_q2_native_mod_clients_frame(qa_compat_q2_native_mod_clients_binding *s, double slot);
void qa_compat_q2_native_mod_clients_end_frame(qa_compat_q2_native_mod_clients_binding *s);
const char *qa_compat_q2_native_mod_clients_userinfo(qa_compat_q2_native_mod_clients_binding *s, qa_actor_id actor);
void qa_compat_q2_native_mod_clients_set_userinfo(qa_compat_q2_native_mod_clients_binding *s, qa_actor_id actor, const char *value);
bool qa_compat_q2_native_mod_clients_invoke_command(qa_compat_q2_native_mod_clients_binding *s, qa_command_invocation *command);
/* Malloc'd rows; caller frees. */
qa_compat_q2_native_mod_client_slot *qa_compat_q2_native_mod_clients_checkpoint(qa_compat_q2_native_mod_clients_binding *s, size_t *out_count);
void qa_compat_q2_native_mod_clients_validate_restore(qa_compat_q2_native_mod_clients_binding *s, const qa_compat_q2_native_mod_client_slot *entries, size_t count);
void qa_compat_q2_native_mod_clients_restore(qa_compat_q2_native_mod_clients_binding *s, const qa_compat_q2_native_mod_client_slot *entries, size_t count);
void qa_compat_q2_native_mod_clients_forget(qa_compat_q2_native_mod_clients_binding *s, qa_actor_id actor);
void qa_compat_q2_native_mod_clients_close(qa_compat_q2_native_mod_clients_binding *s);
#endif
