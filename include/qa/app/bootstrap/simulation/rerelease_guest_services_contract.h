/* Port of src/app/bootstrap/simulation/rerelease-guest-services-contract.ts.
 * Types only: the TS RereleaseGuestServicesPort interface is implemented
 * by RereleaseGuestServices alone, so its methods are the
 * qa_app_sim_rerelease_guest_services_* functions (rerelease_guest_services.h).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_RERELEASE_GUEST_SERVICES_CONTRACT_H
#define QA_APP_BOOTSTRAP_SIMULATION_RERELEASE_GUEST_SERVICES_CONTRACT_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/classic_guest_services.h"
#include "qa/compat/q2/rerelease/debug_shapes.h"
#include "qa/compat/q2/rerelease/foreign_actors.h"
#include "qa/compat/q2/rerelease/host.h"
#include "qa/compat/q2/rerelease/navigation.h"
#include "qa/compat/q2/rerelease/world_text.h"

/* TS clipboard: { kind: "dedicated" } | { kind: "client", write(text) }. */
typedef struct qa_app_sim_rerelease_clipboard { bool client; void *ctx; void (*write)(void *ctx, const char *text); } qa_app_sim_rerelease_clipboard;
/* TS localize(key, arguments): malloc'd result, freed by the caller. */
typedef struct qa_app_sim_rerelease_localize { void *ctx; char *(*localize)(void *ctx, const char *key, const char *const *arguments, size_t count); } qa_app_sim_rerelease_localize;
typedef struct qa_app_sim_rerelease_debug_shapes { void *ctx; void (*debug_shapes)(void *ctx, qa_compat_q2_rerelease_debug_shapes_event event); } qa_app_sim_rerelease_debug_shapes;
typedef struct qa_app_sim_rerelease_world_text { void *ctx; void (*world_text)(void *ctx, qa_compat_q2_rerelease_world_text_event event); } qa_app_sim_rerelease_world_text;

/* TS RereleaseGuestServicesOptions extends ClassicGuestServicesOptions. engine adds Q2FoundationHost.worldActor. */
typedef struct qa_app_sim_rerelease_guest_services_options {
  qa_app_sim_classic_guest_services_options base;
  double frame_milliseconds;
  qa_app_sim_rerelease_localize localize;
  qa_app_sim_rerelease_clipboard clipboard;
  qa_app_sim_rerelease_debug_shapes debug_shapes;
  qa_app_sim_rerelease_world_text world_text;
  qa_compat_q2_rerelease_navigation_services navigation;
  const qa_compat_q2_rerelease_semantic_bindings *semantic_bindings;      /* optional */
  const qa_compat_q2_rerelease_foreign_damage_services *foreign_damage;   /* optional */
} qa_app_sim_rerelease_guest_services_options;
/* TS RereleaseGuestMapServices = Omit<ClassicGuestMapServices,"engine"> & Pick<options, engine|localize|debugShapes|worldText|navigation|foreignDamage>. */
typedef struct qa_app_sim_rerelease_guest_map_services {
  qa_app_sim_classic_guest_map_services base;
  qa_app_sim_rerelease_localize localize;
  qa_app_sim_rerelease_debug_shapes debug_shapes;
  qa_app_sim_rerelease_world_text world_text;
  qa_compat_q2_rerelease_navigation_services navigation;
  const qa_compat_q2_rerelease_foreign_damage_services *foreign_damage;
} qa_app_sim_rerelease_guest_map_services;
/* The Pick<> of the services options (TS hands the options object itself; rerelease_guest_services.c). */
void qa_app_sim_rerelease_guest_map_services_of(const qa_app_sim_rerelease_guest_services_options *options, qa_app_sim_rerelease_guest_map_services *out);
/* TS RereleaseGuestMessage: ClassicGuestMessage with sourceDialect "q2-multicast-float" and dupeKey (both carried by the shared message type). */
typedef qa_app_sim_classic_guest_message qa_app_sim_rerelease_guest_message;
/* TS entityInfo result (port shape). clusters_null true is TS null. */
typedef struct qa_app_sim_rerelease_entity_info {
  bool has_actor; qa_actor_id actor; bool active; double server_flags; double areas[2];
  bool clusters_null; const double *clusters; size_t cluster_count; double first_cluster, headnode;
  bool has_owner_slot; double owner_slot;
} qa_app_sim_rerelease_entity_info;
#endif
