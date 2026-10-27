/* Port of src/compat/qc/mod-messages.ts. GPL-2.0-or-later.
 * QuakeCLocalMessages (TS owner src/app/bootstrap/simulation/
 * quakec-local-messages.ts) is mirrored as operations fn+ctx.
 * Engine presentation host assembly is an operations call. */
#ifndef QA_COMPAT_QC_MOD_MESSAGES_H
#define QA_COMPAT_QC_MOD_MESSAGES_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/qc/quakeworld_presentation.h"
#include "qa/world/actors/registry.h"
#include "qa/world/session/mod_clients.h"
#include "qa/world/gameplay/q1_client_visibility.h"
typedef struct qa_compat_qc_local_client_messages { qa_actor_id actor; qa_netquake_message *messages; size_t message_count; } qa_compat_qc_local_client_messages;
typedef struct qa_compat_qc_local_capture { qa_netquake_message *baseline; size_t baseline_count; qa_compat_qc_local_client_messages *clients; size_t client_count; } qa_compat_qc_local_capture;
typedef struct qa_compat_qc_local_view { qa_actor_id actor, target; } qa_compat_qc_local_view;
typedef struct qa_compat_qc_local_views { bool has_baseline; qa_actor_id baseline; qa_compat_qc_local_view *clients; size_t client_count; } qa_compat_qc_local_views;
/* Mirror of TS QuakeCLocalMessages. */
typedef struct qa_compat_qc_local_message_ops {
  void *ctx; qa_compat_qc_local_messages messages;
  void (*admit)(void *ctx, qa_actor_id actor);
  void (*retire)(void *ctx, qa_actor_id actor);
  qa_compat_qc_local_capture (*capture)(void *ctx);
  void (*capture_free)(void *ctx, qa_compat_qc_local_capture capture);
  qa_compat_qc_local_views (*capture_views)(void *ctx);
  void (*views_free)(void *ctx, qa_compat_qc_local_views views);
  void (*restore)(void *ctx, const qa_netquake_message *baseline, size_t baseline_count, const qa_compat_qc_local_client_messages *clients, size_t client_count);
  void (*restore_views)(void *ctx, const qa_actor_id *baseline, const qa_compat_qc_local_view *clients, size_t client_count);
} qa_compat_qc_local_message_ops;
typedef struct qa_compat_qc_mod_messages_operations {
  void *ctx;
  bool (*loading)(void *ctx); bool (*phs)(void *ctx);
  bool (*client_is_player)(void *ctx, qa_actor_id actor); /* engine presentation players fallback */
  const qa_actor_id *(*players)(void *ctx, size_t *out_count);
  qa_actor_id (*world_actor)(void *ctx, int64_t slot);
  const char *(*resource_name)(void *ctx, const char *kind, double index);
  bool (*body_origin)(void *ctx, qa_actor_id actor, qa_vec3 *out);
  bool (*camera_angles)(void *ctx, qa_actor_id actor, qa_vec3 *out);
  void *host_ctx;
  qa_compat_qc_local_message_host (*host)(void *ctx, const qa_actor_id *target, double seconds, const qa_actor_id *recipients, size_t recipient_count, qa_actor_id source_actor);
  void (*emit)(void *ctx, qa_q1_event event, const qa_actor_id *recipient);
  bool (*clients_for_actor)(void *ctx, qa_actor_id actor); /* services.clients pick */
  bool has_clients;
  qa_mod_client_unsubscribe (*subscribe_clients)(void *ctx, void (*on_event)(void *, qa_mod_client_event), void *event_ctx); /* nullable */
  bool (*visibility)(void *ctx, qa_q1_client_visibility_scene *out);
  double (*time_seconds)(void *ctx);
} qa_compat_qc_mod_messages_operations;
typedef struct qa_compat_qc_mod_messages {
  qa_compat_qc_broadcast_messages messages;
  qa_compat_qc_quakeworld_message_services qw_routes; qa_compat_qc_netquake_message_services nq_routes;
  bool is_quakeworld;
  qa_compat_qc_presentation_world *world; qa_content_id content;
  qa_compat_qc_mod_messages_operations operations;
  qa_compat_qc_local_message_ops local;
  qa_compat_qc_routed_message *signon; size_t signon_count, signon_capacity;
  struct { qa_actor_id actor; int64_t cursor; } *admitted; size_t admitted_count, admitted_capacity;
  int64_t release_id; qa_mod_client_unsubscribe unsubscribe; bool has_unsubscribe;
  qa_session_actor_registry *actors;
  struct qa_compat_qc_mod_messages *self_ref;
} qa_compat_qc_mod_messages;
bool qa_compat_qc_mod_messages_init(qa_compat_qc_mod_messages *m, qa_compat_qc_presentation_world *world, qa_session_actor_registry *actors, qa_content_id content, qa_compat_qc_mod_messages_operations operations, qa_compat_qc_local_message_ops local);
void qa_compat_qc_mod_messages_destroy(qa_compat_qc_mod_messages *m);
const qa_compat_qc_local_messages *qa_compat_qc_mod_messages_client_state(qa_compat_qc_mod_messages *m);
void qa_compat_qc_mod_messages_start(qa_compat_qc_mod_messages *m);
qa_save_value *qa_compat_qc_mod_messages_capture(qa_compat_qc_mod_messages *m);
void qa_compat_qc_mod_messages_restore(qa_compat_qc_mod_messages *m, const qa_save_reader *reader, qa_compat_qc_actor_resolver resolve);
void qa_compat_qc_mod_messages_close(qa_compat_qc_mod_messages *m);
#endif
