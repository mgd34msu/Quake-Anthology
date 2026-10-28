/* Port of src/app/bootstrap/simulation/network.ts. Frame/entity
 * projection runs through the simulation view bundle; cvar
 * registration and resource config seeding are here.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NETWORK_H
#define QA_APP_BOOTSTRAP_SIMULATION_NETWORK_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/network/types.h"
#include "qa/core/cvars/index.h"
typedef struct qa_app_sim_q2_network_simulation {
  void *ctx;
  bool has_q2_source;
  qa_cvar_registry *cvars; /* NULL when absent */
  const char *map_name; double max_clients; const char *edition;
  double air_accelerate; bool n64_physics; bool has_movement_config;
  const char *world_message, *world_sky, *world_sky_axis, *world_sky_rotate;
  const char *geometry_path;
  const char *const *item_names; size_t item_count;
  int64_t map_checksum;
  /* landed host implemented by the provider (frame projection). */
  qa_app_q2_server_host host;
} qa_app_sim_q2_network_simulation;
typedef struct qa_app_sim_q2_server_binding_options {
  const void *session;
  bool (*rejects)(void *ctx, const void *address); void *rejects_ctx;
  qa_app_sim_q2_network_simulation simulation;
  const void *content; /* LoadedApplicationContent (opaque) */
  qa_q2_protocol_identity protocol;
  const void *administration, *masters;
  void (*player_identity)(void *ctx, qa_client_id client, bool has_identity, double seat, const char *social_id);
  void *player_identity_ctx;
  void (*print)(void *ctx, const char *text); void *print_ctx;
} qa_app_sim_q2_server_binding_options;
/* Synchronous form of the TS async create. Returned host ctx is
 * heap storage owned by the caller (free with destroy). */
qa_app_q2_server_host qa_app_sim_create_q2_application_server_host(qa_app_sim_q2_server_binding_options options);
void qa_app_sim_q2_application_server_host_destroy(qa_app_q2_server_host host);
#endif
