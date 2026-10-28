/* Port of src/app/bootstrap/simulation/rerelease-guest-services-contract.ts.
 * ClassicGuestServicesOptions/MapServices/Message are the sibling
 * slice's opaque options (classic_guest_services.h); the rerelease
 * additions are carried explicitly. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_RERELEASE_GUEST_SERVICES_CONTRACT_H
#define QA_APP_BOOTSTRAP_SIMULATION_RERELEASE_GUEST_SERVICES_CONTRACT_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/classic_guest_services.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/original_pickups.h"
#include "qa/contracts/protocol.h"
typedef struct qa_app_sim_rerelease_clipboard {
  const char *kind; /* "dedicated"|"client" */
  void *ctx; void (*write)(void *ctx, const char *text);
} qa_app_sim_rerelease_clipboard;
typedef struct qa_app_sim_rerelease_guest_services_options {
  qa_app_sim_classic_guest_services_options classic;
  double frame_milliseconds;
  const void *engine; /* classic engine + Q2FoundationHost.worldActor */
  void *localize_ctx; char *(*localize)(void *ctx, const char *key, const char *const *args, size_t arg_count);
  qa_app_sim_rerelease_clipboard clipboard;
  void *shapes_ctx; void (*debug_shapes)(void *ctx, const void *event);
  void *text_ctx; void (*world_text)(void *ctx, const void *event);
  const void *navigation;          /* RereleaseNavigationServices */
  const void *semantic_bindings;   /* optional */
  const void *foreign_damage;      /* optional */
} qa_app_sim_rerelease_guest_services_options;
typedef struct qa_app_sim_rerelease_guest_map_services {
  qa_app_sim_classic_guest_services_options classic_map; /* minus engine */
  const void *engine;
  void *localize_ctx; char *(*localize)(void *ctx, const char *key, const char *const *args, size_t arg_count);
  void *shapes_ctx; void (*debug_shapes)(void *ctx, const void *event);
  void *text_ctx; void (*world_text)(void *ctx, const void *event);
  const void *navigation;
  const void *foreign_damage;
} qa_app_sim_rerelease_guest_map_services;
typedef struct qa_app_sim_rerelease_guest_message {
  const void *classic; /* ClassicGuestMessage (sibling opaque) */
  const char *source_dialect; /* "q2-multicast-float" */
  double dupe_key;
} qa_app_sim_rerelease_guest_message;
typedef struct qa_app_sim_rerelease_model_appearance {
  const char *path; double skin; const char *skin_path;
  const char *const *attached_models; size_t attached_model_count;
} qa_app_sim_rerelease_model_appearance;
typedef struct qa_app_sim_rerelease_entity_info {
  bool has_actor; qa_actor_id actor; bool active; double server_flags;
  double areas[2]; const double *clusters; size_t cluster_count; bool has_clusters;
  double first_cluster, headnode; bool has_owner_slot; double owner_slot;
} qa_app_sim_rerelease_entity_info;
typedef struct qa_app_sim_rerelease_configstring { double index; const char *value; } qa_app_sim_rerelease_configstring;
typedef struct qa_app_sim_rerelease_guest_services_port {
  void *ctx;
  qa_app_sim_rerelease_guest_services_options options;
  const void *host_options; /* Pick<RereleaseQ2HostOptions,...> + pickups */
  const qa_original_pickup_admission *pickups; /* optional */
  const void *(*bind_memory)(void *ctx, const void *memory);
  void (*bind_host)(void *ctx, const void *host);
  void (*complete_spawn)(void *ctx);
  void (*validate_map)(void *ctx, qa_app_sim_rerelease_guest_map_services binding);
  void (*publish_entities)(void *ctx);
  double (*player_ping)(void *ctx, double slot);
  void (*set_player_ping)(void *ctx, double slot, double ping);
  void (*begin_frame)(void *ctx, double frame);
  void (*rebind_world)(void *ctx, qa_app_sim_rerelease_guest_map_services binding);
  qa_q2_rerelease_entity_state (*entity_state)(void *ctx, double slot);
  qa_q2_rerelease_player_state (*player_state)(void *ctx, double slot);
  qa_app_sim_rerelease_model_appearance (*model_appearance)(void *ctx, double slot);
  qa_app_sim_rerelease_entity_info (*entity_info)(void *ctx, double slot);
  const qa_app_sim_rerelease_guest_message *(*drain_messages)(void *ctx, size_t *out_count);
  const qa_app_sim_rerelease_configstring *(*configstrings)(void *ctx, size_t *out_count);
  void (*set_configstring)(void *ctx, double index, const char *value);
  void (*restore_configstrings)(void *ctx, const qa_app_sim_rerelease_configstring *values, size_t count);
} qa_app_sim_rerelease_guest_services_port;
#endif
