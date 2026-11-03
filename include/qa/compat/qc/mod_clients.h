/* Port of src/compat/qc/mod-clients.ts. GPL-2.0-or-later.
 * Operations live fn is fn+ctx (TS closure). userinfo returns
 * malloc'd text (caller frees). Entries owned by struct. */
#ifndef QA_COMPAT_QC_MOD_CLIENTS_H
#define QA_COMPAT_QC_MOD_CLIENTS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/mod_callbacks.h"
#include "qa/world/session/mod_clients.h"
typedef struct qa_compat_qc_mod_client_slot { qa_actor_id actor; int64_t slot; bool admitted; } qa_compat_qc_mod_client_slot;
typedef struct qa_compat_qc_mod_client_input_ops {
  void *ctx;
  void (*open)(void *ctx, qa_mod_client_application *application, void (**cleanup)(void *), void **cleanup_ctx);
  void (*invoke)(void *ctx, qa_mod_source_call call, qa_mod_client_application *application);
  void (*output)(void *ctx, const qa_mod_qc_input_output *outputs, size_t output_count, qa_mod_client_application *application, void (*run)(void *), void *run_ctx, qa_mod_client_input_output **out_outputs, size_t *out_count);
} qa_compat_qc_mod_client_input_ops;
typedef struct qa_compat_qc_mod_client_operations {
  void *ctx;
  qa_mod_client_services *services;
  const qa_mod_qc_clients *declaration;
  void (*project)(void *ctx, qa_actor_id actor);
  const char *(*release)(void *ctx, qa_actor_id actor); /* "released"|"deferred" */
  void (*invoke)(void *ctx, qa_mod_source_call call, qa_actor_id actor, const qa_frame_context *frame);
  void (*think)(void *ctx, qa_actor_id actor, qa_frame_context frame, void *live_ctx, bool (*live)(void *live_ctx)); /* nullable */
  void (*reserve)(void *ctx, qa_actor_id actor);  /* nullable */
  void (*admitted)(void *ctx, qa_actor_id actor); /* nullable */
  const qa_compat_qc_mod_client_input_ops *input; /* nullable */
} qa_compat_qc_mod_client_operations;
typedef struct qa_compat_qc_mod_client_entry { qa_actor_id actor; bool has_client; qa_client_id client; int64_t slot; bool admitted; uint64_t generation; } qa_compat_qc_mod_client_entry;
typedef struct qa_compat_qc_mod_client_bindings {
  qa_compat_qc_mod_client_operations operations;
  qa_compat_qc_mod_client_entry *entries; size_t entry_count, entry_capacity;
  qa_mod_client_unsubscribe unsubscribe, unsubscribe_input; bool has_unsubscribe, has_unsubscribe_input;
  uint64_t next_generation;
} qa_compat_qc_mod_client_bindings;
void qa_compat_qc_mod_client_bindings_init(qa_compat_qc_mod_client_bindings *b, qa_compat_qc_mod_client_operations operations);
void qa_compat_qc_mod_client_bindings_destroy(qa_compat_qc_mod_client_bindings *b);
bool qa_compat_qc_mod_client_bindings_admitted(qa_compat_qc_mod_client_bindings *b, qa_actor_id actor);
bool qa_compat_qc_mod_client_bindings_slot(qa_compat_qc_mod_client_bindings *b, qa_actor_id actor, int64_t *out);
void qa_compat_qc_mod_client_bindings_start(qa_compat_qc_mod_client_bindings *b);
char *qa_compat_qc_mod_client_bindings_userinfo(qa_compat_qc_mod_client_bindings *b, qa_actor_id actor, const char *key);
bool qa_compat_qc_mod_client_bindings_frame(qa_compat_qc_mod_client_bindings *b, int64_t slot, qa_frame_context frame);
void qa_compat_qc_mod_client_bindings_set_userinfo(qa_compat_qc_mod_client_bindings *b, qa_actor_id actor, const char *key, const char *value);
size_t qa_compat_qc_mod_client_bindings_checkpoint(qa_compat_qc_mod_client_bindings *b, qa_compat_qc_mod_client_slot *out, size_t capacity);
void qa_compat_qc_mod_client_bindings_restore(qa_compat_qc_mod_client_bindings *b, const qa_compat_qc_mod_client_slot *entries, size_t count);
void qa_compat_qc_mod_client_bindings_forget(qa_compat_qc_mod_client_bindings *b, qa_actor_id actor);
void qa_compat_qc_mod_client_bindings_close(qa_compat_qc_mod_client_bindings *b);
#endif
