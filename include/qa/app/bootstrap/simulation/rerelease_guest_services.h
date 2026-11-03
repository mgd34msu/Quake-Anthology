/* Port of src/app/bootstrap/simulation/rerelease-guest-services.ts.
 * Public API2023 imports borrow the shared engine while source RunFrame
 * owns gameplay. The functions below are TS RereleaseGuestServicesPort
 * plus the class's own members. Ownership as classic_guest_services.h.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_RERELEASE_GUEST_SERVICES_H
#define QA_APP_BOOTSTRAP_SIMULATION_RERELEASE_GUEST_SERVICES_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/rerelease_guest_services_contract.h"
#include "qa/compat/q2/rerelease/imports.h"

typedef struct qa_app_sim_rerelease_guest_services qa_app_sim_rerelease_guest_services;
/* NULL only on OOM; invalid options abort as the TS RangeErrors. */
qa_app_sim_rerelease_guest_services *qa_app_sim_rerelease_guest_services_create(const qa_app_sim_rerelease_guest_services_options *options);
void qa_app_sim_rerelease_guest_services_destroy(qa_app_sim_rerelease_guest_services *services);
const qa_app_sim_rerelease_guest_services_options *qa_app_sim_rerelease_guest_services_options_of(const qa_app_sim_rerelease_guest_services *services);
void qa_app_sim_rerelease_guest_services_set_command(qa_app_sim_rerelease_guest_services *services, qa_app_sim_guest_command command);
/* TS hostOptions: fills the host option members the services own (engine,
 * spatial, semantics, messages, debug shapes, world text, sound, frame
 * duration, foreign damage, pickups); the caller fills the rest. */
void qa_app_sim_rerelease_guest_services_host_options(qa_app_sim_rerelease_guest_services *services, qa_compat_q2_rerelease_q2_host_options *out);
qa_compat_q2_rerelease_core_services qa_app_sim_rerelease_guest_services_bind_memory(qa_app_sim_rerelease_guest_services *services, qa_compat_q2_memory *memory);
qa_compat_q2_memory *qa_app_sim_rerelease_guest_services_memory(qa_app_sim_rerelease_guest_services *services);
void qa_app_sim_rerelease_guest_services_bind_host(qa_app_sim_rerelease_guest_services *services, qa_compat_q2_rerelease_q2_guest_host *host);
qa_compat_q2_rerelease_q2_guest_host *qa_app_sim_rerelease_guest_services_host(qa_app_sim_rerelease_guest_services *services);
double qa_app_sim_rerelease_guest_services_server_frame(const qa_app_sim_rerelease_guest_services *services);
/* -1 null, 0 false, 1 true. */
int qa_app_sim_rerelease_guest_services_notarget(qa_app_sim_rerelease_guest_services *services, double slot);
bool qa_app_sim_rerelease_guest_services_player_grounded(qa_app_sim_rerelease_guest_services *services, double slot, qa_actor_id actor);
qa_compat_q2_native_input_motion_view qa_app_sim_rerelease_guest_services_player_view(qa_app_sim_rerelease_guest_services *services, double slot, qa_actor_id actor);
void qa_app_sim_rerelease_guest_services_set_player_view_roll(qa_app_sim_rerelease_guest_services *services, double slot, qa_actor_id actor, double roll);
bool qa_app_sim_rerelease_guest_services_has_source_inventory(qa_app_sim_rerelease_guest_services *services);
bool qa_app_sim_rerelease_guest_services_source_inventory(qa_app_sim_rerelease_guest_services *services, double slot, qa_inventory_state_binding *out);
void qa_app_sim_rerelease_guest_services_set_player_velocity_writer(qa_app_sim_rerelease_guest_services *services, qa_app_sim_player_velocity velocity);
void qa_app_sim_rerelease_guest_services_with_input_movement(qa_app_sim_rerelease_guest_services *services, qa_actor_id actor, const qa_compat_q2_native_input_motion *projection, void (*run)(void *), void *ctx);
void qa_app_sim_rerelease_guest_services_validate_map(qa_app_sim_rerelease_guest_services *services, const qa_shared_scene_queries *scene);
void qa_app_sim_rerelease_guest_services_rebind_world(qa_app_sim_rerelease_guest_services *services, const qa_app_sim_rerelease_guest_map_services *binding);
void qa_app_sim_rerelease_guest_services_complete_spawn(qa_app_sim_rerelease_guest_services *services);
void qa_app_sim_rerelease_guest_services_begin_frame(qa_app_sim_rerelease_guest_services *services, double frame);
const qa_app_sim_configstring *qa_app_sim_rerelease_guest_services_configstrings(qa_app_sim_rerelease_guest_services *services, size_t *out_count);
const char *qa_app_sim_rerelease_guest_services_configstring(qa_app_sim_rerelease_guest_services *services, double index);
void qa_app_sim_rerelease_guest_services_restore_configstrings(qa_app_sim_rerelease_guest_services *services, const qa_app_sim_configstring *values, size_t count);
void qa_app_sim_rerelease_guest_services_set_configstring(qa_app_sim_rerelease_guest_services *services, double index, const char *value);
const char *qa_app_sim_rerelease_guest_services_resource(qa_app_sim_rerelease_guest_services *services, const char *kind, double index);
double qa_app_sim_rerelease_guest_services_resource_index(qa_app_sim_rerelease_guest_services *services, const char *kind, const char *name);
qa_q2_rerelease_entity_state qa_app_sim_rerelease_guest_services_entity_state(qa_app_sim_rerelease_guest_services *services, double slot);
qa_q2_rerelease_player_state qa_app_sim_rerelease_guest_services_player_state(qa_app_sim_rerelease_guest_services *services, double slot);
double qa_app_sim_rerelease_guest_services_player_ping(qa_app_sim_rerelease_guest_services *services, double slot);
void qa_app_sim_rerelease_guest_services_set_player_ping(qa_app_sim_rerelease_guest_services *services, double slot, double ping);
/* clusters borrow services storage valid until the next link update. */
qa_app_sim_rerelease_entity_info qa_app_sim_rerelease_guest_services_entity_info(qa_app_sim_rerelease_guest_services *services, double slot);
qa_app_sim_model_appearance qa_app_sim_rerelease_guest_services_model_appearance(qa_app_sim_rerelease_guest_services *services, double slot);
void qa_app_sim_rerelease_guest_services_publish_entities(qa_app_sim_rerelease_guest_services *services);
const qa_app_sim_rerelease_guest_message *qa_app_sim_rerelease_guest_services_drain_messages(qa_app_sim_rerelease_guest_services *services, size_t *out_count);
#endif
