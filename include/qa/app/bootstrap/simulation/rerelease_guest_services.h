/* Port of src/app/bootstrap/simulation/rerelease-guest-services.ts.
 * Guest-memory-backed operations (public edict views, invoke,
 * publish) run through the compat host bundle; configstrings,
 * resource indices, messages, frames and validation live here.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_RERELEASE_GUEST_SERVICES_H
#define QA_APP_BOOTSTRAP_SIMULATION_RERELEASE_GUEST_SERVICES_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/rerelease_guest_services_contract.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/math.h"
typedef struct qa_app_sim_rerelease_guest_services qa_app_sim_rerelease_guest_services;
typedef struct qa_app_sim_rerelease_services_host {
  void *ctx;
  double max_clients;
  const char *map_path;
  size_t model_count;
  void (*set_cvar)(void *ctx, const char *name, const char *value, bool force);
  void (*emit_lightstyle)(void *ctx, double style, const char *pattern);
  void (*emit_music)(void *ctx, const char *track);
  /* guest-memory-backed compat operations */
  qa_q2_rerelease_entity_state (*entity_state)(void *ctx, double slot);
  qa_q2_rerelease_player_state (*player_state)(void *ctx, double slot);
  qa_app_sim_rerelease_model_appearance (*model_appearance)(void *ctx, double slot);
  qa_app_sim_rerelease_entity_info (*entity_info)(void *ctx, double slot);
  void (*publish_entities)(void *ctx);
  double (*player_ping)(void *ctx, double slot);
  void (*set_player_ping)(void *ctx, double slot, double ping);
} qa_app_sim_rerelease_services_host;
qa_app_sim_rerelease_guest_services *qa_app_sim_rerelease_guest_services_create(
    qa_app_sim_rerelease_guest_services_options options, qa_app_sim_rerelease_services_host host);
void qa_app_sim_rerelease_guest_services_destroy(qa_app_sim_rerelease_guest_services *services);
qa_app_sim_rerelease_guest_services_options qa_app_sim_rerelease_guest_services_options_of(qa_app_sim_rerelease_guest_services *services);
const void *qa_app_sim_rerelease_guest_services_memory(qa_app_sim_rerelease_guest_services *services);
const void *qa_app_sim_rerelease_guest_services_host_of(qa_app_sim_rerelease_guest_services *services);
double qa_app_sim_rerelease_guest_services_server_frame(qa_app_sim_rerelease_guest_services *services);
/* Returns the RereleaseCoreServices port pointer (compat opaque). */
const void *qa_app_sim_rerelease_guest_services_bind_memory(qa_app_sim_rerelease_guest_services *services, const void *memory);
void qa_app_sim_rerelease_guest_services_bind_host(qa_app_sim_rerelease_guest_services *services, const void *host, const void *host_memory);
void qa_app_sim_rerelease_guest_services_complete_spawn(qa_app_sim_rerelease_guest_services *services);
void qa_app_sim_rerelease_guest_services_validate_map(qa_app_sim_rerelease_guest_services *services, size_t model_count);
void qa_app_sim_rerelease_guest_services_publish_entities(qa_app_sim_rerelease_guest_services *services);
double qa_app_sim_rerelease_guest_services_player_ping(qa_app_sim_rerelease_guest_services *services, double slot);
void qa_app_sim_rerelease_guest_services_set_player_ping(qa_app_sim_rerelease_guest_services *services, double slot, double ping);
void qa_app_sim_rerelease_guest_services_begin_frame(qa_app_sim_rerelease_guest_services *services, double frame);
void qa_app_sim_rerelease_guest_services_rebind_world(qa_app_sim_rerelease_guest_services *services,
    qa_app_sim_rerelease_guest_map_services binding, size_t model_count);
qa_q2_rerelease_entity_state qa_app_sim_rerelease_guest_services_entity_state(qa_app_sim_rerelease_guest_services *services, double slot);
qa_q2_rerelease_player_state qa_app_sim_rerelease_guest_services_player_state(qa_app_sim_rerelease_guest_services *services, double slot);
qa_app_sim_rerelease_model_appearance qa_app_sim_rerelease_guest_services_model_appearance(qa_app_sim_rerelease_guest_services *services, double slot);
qa_app_sim_rerelease_entity_info qa_app_sim_rerelease_guest_services_entity_info(qa_app_sim_rerelease_guest_services *services, double slot);
/* Malloc'd array of queued message views; caller frees. */
const qa_app_sim_rerelease_guest_message *qa_app_sim_rerelease_guest_services_drain_messages(qa_app_sim_rerelease_guest_services *services, size_t *out_count);
const qa_app_sim_rerelease_configstring *qa_app_sim_rerelease_guest_services_configstrings(qa_app_sim_rerelease_guest_services *services, size_t *out_count);
void qa_app_sim_rerelease_guest_services_set_configstring(qa_app_sim_rerelease_guest_services *services, double index, const char *value);
void qa_app_sim_rerelease_guest_services_restore_configstrings(qa_app_sim_rerelease_guest_services *services,
    const qa_app_sim_rerelease_configstring *values, size_t count);
/* kind: "model"|"sound"|"image". */
const char *qa_app_sim_rerelease_guest_services_resource(qa_app_sim_rerelease_guest_services *services, const char *kind, double index);
double qa_app_sim_rerelease_guest_services_resource_index(qa_app_sim_rerelease_guest_services *services, const char *kind, const char *name);
#endif
