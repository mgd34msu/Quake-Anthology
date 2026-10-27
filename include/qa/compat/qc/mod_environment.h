/* Port of src/compat/qc/mod-environment.ts. GPL-2.0-or-later.
 * Engine clients pick is supplied via fn+ctx (landed engine
 * services expose it as opaque). Owns cvar registry + visibility. */
#ifndef QA_COMPAT_QC_MOD_ENVIRONMENT_H
#define QA_COMPAT_QC_MOD_ENVIRONMENT_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/qc/builtins.h"
#include "qa/core/cvars/index.h"
#include "qa/world/gameplay/q1_client_visibility.h"
#include "qa/world/session/mods.h"
typedef struct qa_compat_qc_mod_environment_clients {
  void *ctx; double maximum; qa_q1_client_visibility_scene visibility;
  qa_q1_visibility_client (*at)(void *ctx, int64_t slot);
} qa_compat_qc_mod_environment_clients;
typedef struct qa_compat_qc_mod_environment {
  qa_cvar_registry *cvars; qa_q1_client_visibility *visibility;
  qa_compat_qc_host_binding host_entries[4]; size_t host_count;
  qa_mod_host_services *services; bool has_reference;
  void *reference_ctx; int64_t (*reference)(void *ctx, const qa_actor_id *actor);
  const qa_compat_qc_mod_environment_clients *clients;
  struct qa_compat_qc_mod_environment *self_ref;
} qa_compat_qc_mod_environment;
bool qa_compat_qc_mod_environment_init(qa_compat_qc_mod_environment *e, qa_mod_host_services *services, qa_compat_qc_program *program, qa_mod_callback_declaration declaration, void *reference_ctx, int64_t (*reference)(void *ctx, const qa_actor_id *actor), const qa_compat_qc_mod_environment_clients *clients);
void qa_compat_qc_mod_environment_destroy(qa_compat_qc_mod_environment *e);
void qa_compat_qc_mod_environment_initialize_globals(qa_compat_qc_mod_environment *e, qa_compat_qc_machine *machine);
bool qa_compat_qc_mod_environment_client(qa_compat_qc_mod_environment *e, qa_actor_id actor, qa_q1_visibility_client *out);
#endif
