/* Port of src/compat/qc/client-host.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_QC_CLIENT_HOST_H
#define QA_COMPAT_QC_CLIENT_HOST_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/qc/world_host.h"
#include "qa/world/gameplay/q1_client_visibility.h"
typedef struct qa_compat_qc_client_host_services {
  qa_q1_client_visibility_scene scene; int64_t max_clients; void *ctx; double (*server_time)(void *ctx);
} qa_compat_qc_client_host_services;
typedef struct qa_compat_qc_client_host {
  qa_compat_qc_host_binding host_entries[1]; size_t host_count;
  qa_q1_client_visibility *visibility;
  qa_compat_qc_world_host *world; qa_compat_qc_client_host_services services;
  int64_t origin, view_ofs, health, flags;
} qa_compat_qc_client_host;
bool qa_compat_qc_client_host_init(qa_compat_qc_client_host *h, qa_compat_qc_world_host *world, qa_compat_qc_client_host_services services);
void qa_compat_qc_client_host_destroy(qa_compat_qc_client_host *h);
typedef struct qa_compat_qc_aim_target_ref { qa_actor_id actor; int64_t reference; } qa_compat_qc_aim_target_ref;
typedef struct qa_compat_qc_aim_state {
  qa_compat_qc_world_host *world; void *ctx;
  double (*aim_threshold)(void *ctx); double (*teamplay)(void *ctx);
  const qa_compat_qc_aim_target_ref *(*targets)(void *ctx, size_t *out_count);
  bool (*no_aim)(void *ctx, qa_actor_id actor); /* nullable */
} qa_compat_qc_aim_state;
qa_compat_qc_builtin qa_compat_qc_create_aim_binding(qa_compat_qc_world_host *world, qa_compat_qc_aim_state *storage);
#endif
