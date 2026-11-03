/* Port of src/app/bootstrap/simulation/classic-guest-services.ts.
 * API 3 imports over the shared actor, body, collision and presentation
 * tables. Every leaf callback carries its own ctx so a caller that
 * rebinds one leaf (the worlds wrap `command`) keeps the others' ctx.
 * Ownership: drained messages and appearance strings are services-owned
 * and stay valid until the next drain / the next appearance query.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_CLASSIC_GUEST_SERVICES_H
#define QA_APP_BOOTSTRAP_SIMULATION_CLASSIC_GUEST_SERVICES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/simulation/classic_guest_player.h" /* qa_app_sim_configstring */
#include "qa/compat/q2/classic/host.h"
#include "qa/compat/q2/classic/world_profile.h"
#include "qa/compat/q2/native_input.h"
#include "qa/compat/q2/native_pickups.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/contracts/movement.h"
#include "qa/contracts/numeric.h"
#include "qa/contracts/protocol.h"
#include "qa/core/cvars/index.h"
#include "qa/world/collision/index.h"
#include "qa/world/gameplay/inventory.h"

/* TS ClassicGuestAudience. scope: "all"|"phs"|"pvs" (multicast). */
typedef struct qa_app_sim_classic_guest_audience { bool unicast; double slot; qa_vec3 origin; const char *scope; } qa_app_sim_classic_guest_audience;
/* TS ClassicGuestMessage (+ the rerelease dialect fields). */
typedef struct qa_app_sim_classic_guest_message {
  qa_app_sim_classic_guest_audience audience; bool reliable; uint8_t *bytes; size_t length;
  const char *source_dialect; /* NULL classic, "q2-multicast-float" rerelease */
  double dupe_key;
} qa_app_sim_classic_guest_message;

/* Leaf callbacks of ClassicGuestServicesOptions. */
typedef struct qa_app_sim_guest_admit { void *ctx; void (*admit)(void *ctx, qa_raw_entity_view record, qa_owned_actor actor); } qa_app_sim_guest_admit;
typedef struct qa_app_sim_guest_collision { void *ctx; void (*collision)(void *ctx, qa_owned_actor actor, qa_actor_collision collision); } qa_app_sim_guest_collision;
typedef struct qa_app_sim_guest_print { void *ctx; void (*print)(void *ctx, const char *text); } qa_app_sim_guest_print;
/* TS command(): { arguments, args }. */
typedef struct qa_app_sim_guest_command { void *ctx; const char *const *(*command)(void *ctx, size_t *out_count, const char **out_args); } qa_app_sim_guest_command;
typedef struct qa_app_sim_guest_add_command { void *ctx; void (*add_command)(void *ctx, const char *text); } qa_app_sim_guest_add_command;
typedef struct qa_app_sim_guest_debug_graph { void *ctx; void (*debug_graph)(void *ctx, double value, double color); } qa_app_sim_guest_debug_graph;
typedef struct qa_app_sim_guest_accepts_client { void *ctx; bool (*accepts_client)(void *ctx, double slot); } qa_app_sim_guest_accepts_client;

typedef struct qa_app_sim_classic_guest_services_options {
  /* primaryWorld?: profile | null — has_primary_world false is undefined. */
  bool has_primary_world; const qa_compat_q2_classic_world_profile *primary_world;
  const qa_compat_q2_native_pickup_profile *pickup_profile; /* optional */
  const qa_original_pickup_admission *pickups;               /* optional */
  const qa_compat_q2_classic_damage_provenance *damage_provenance; /* optional */
  qa_q2_foundation_host *engine;
  qa_shared_scene_queries *scene;
  qa_cvar_registry *cvars;
  qa_numeric_operations numeric;
  const char *map_path;
  double max_clients;
  qa_app_sim_guest_accepts_client accepts_client; /* optional (NULL fn) */
  qa_app_sim_guest_admit admit;
  qa_app_sim_guest_collision collision;
  qa_app_sim_guest_print print;
  qa_app_sim_guest_command command;
  qa_app_sim_guest_add_command add_command;
  qa_app_sim_guest_debug_graph debug_graph;
} qa_app_sim_classic_guest_services_options;
/* TS ClassicGuestMapServices = Pick<options, engine|scene|mapPath|admit|collision|print|command|addCommand|debugGraph|damageProvenance|pickups>. */
typedef struct qa_app_sim_classic_guest_map_services {
  qa_q2_foundation_host *engine; qa_shared_scene_queries *scene; const char *map_path;
  qa_app_sim_guest_admit admit; qa_app_sim_guest_collision collision; qa_app_sim_guest_print print;
  qa_app_sim_guest_command command; qa_app_sim_guest_add_command add_command; qa_app_sim_guest_debug_graph debug_graph;
  const qa_compat_q2_classic_damage_provenance *damage_provenance; const qa_original_pickup_admission *pickups;
} qa_app_sim_classic_guest_map_services;
/* The Pick<> of the services options (TS hands the options object itself). */
void qa_app_sim_classic_guest_map_services_of(const qa_app_sim_classic_guest_services_options *options, qa_app_sim_classic_guest_map_services *out);
typedef struct qa_app_sim_model_appearance { const char *path; double skin; const char *skin_path; const char *attached_models[3]; size_t attached_model_count; } qa_app_sim_model_appearance;
/* TS (actor, velocity) writer/reader for API3 player velocity writes. */
typedef struct qa_app_sim_player_velocity {
  void *ctx;
  void (*write)(void *ctx, qa_owned_actor actor, qa_vec3 velocity);
  bool (*read)(void *ctx, qa_actor_id actor, qa_vec3 *out);
} qa_app_sim_player_velocity;

typedef struct qa_app_sim_classic_guest_services qa_app_sim_classic_guest_services;
/* TS constructor(memory, options). NULL only on OOM; invalid options abort. */
qa_app_sim_classic_guest_services *qa_app_sim_classic_guest_services_create(qa_compat_q2_memory *memory, const qa_app_sim_classic_guest_services_options *options);
void qa_app_sim_classic_guest_services_destroy(qa_app_sim_classic_guest_services *services);
/* TS `services`: the ClassicQ2EngineServices table the host imports call. */
qa_compat_q2_classic_engine_services qa_app_sim_classic_guest_services_engine(qa_app_sim_classic_guest_services *services);
const qa_app_sim_classic_guest_services_options *qa_app_sim_classic_guest_services_options_of(const qa_app_sim_classic_guest_services *services);
/* Replaces the command leaf (the worlds' command context wrapper). */
void qa_app_sim_classic_guest_services_set_command(qa_app_sim_classic_guest_services *services, qa_app_sim_guest_command command);
void qa_app_sim_classic_guest_services_validate_map(qa_app_sim_classic_guest_services *services, const qa_app_sim_classic_guest_map_services *binding);
void qa_app_sim_classic_guest_services_rebind_world(qa_app_sim_classic_guest_services *services, const qa_app_sim_classic_guest_map_services *binding);
/* image_base NULL is TS undefined. */
void qa_app_sim_classic_guest_services_bind_host(qa_app_sim_classic_guest_services *services, qa_compat_q2_classic_guest_host *host, const qa_guest_address *image_base);
qa_compat_q2_classic_guest_host *qa_app_sim_classic_guest_services_host(qa_app_sim_classic_guest_services *services);
/* TS notarget: -1 null, 0 false, 1 true. */
int qa_app_sim_classic_guest_services_notarget(qa_app_sim_classic_guest_services *services, double slot);
bool qa_app_sim_classic_guest_services_player_grounded(qa_app_sim_classic_guest_services *services, double slot, qa_actor_id actor);
qa_compat_q2_native_input_motion_view qa_app_sim_classic_guest_services_player_view(qa_app_sim_classic_guest_services *services, double slot, qa_actor_id actor);
void qa_app_sim_classic_guest_services_set_player_view_roll(qa_app_sim_classic_guest_services *services, double slot, qa_actor_id actor, double roll);
bool qa_app_sim_classic_guest_services_has_source_inventory(qa_app_sim_classic_guest_services *services);
/* False is the TS null. */
bool qa_app_sim_classic_guest_services_source_inventory(qa_app_sim_classic_guest_services *services, double slot, qa_inventory_state_binding *out);
void qa_app_sim_classic_guest_services_complete_spawn(qa_app_sim_classic_guest_services *services);
void qa_app_sim_classic_guest_services_set_player_velocity_writer(qa_app_sim_classic_guest_services *services, qa_app_sim_player_velocity velocity);
/* TS withPlayerMovement / withInputMovement (try/finally restore). */
void qa_app_sim_classic_guest_services_with_player_movement(qa_app_sim_classic_guest_services *services, const qa_equipment_movement *movement, void (*run)(void *), void *ctx);
void qa_app_sim_classic_guest_services_with_input_movement(qa_app_sim_classic_guest_services *services, qa_actor_id actor, const qa_compat_q2_native_input_motion *projection, void (*run)(void *), void *ctx);
/* The live table in insertion order; valid until the next mutation. */
const qa_app_sim_configstring *qa_app_sim_classic_guest_services_configstrings(qa_app_sim_classic_guest_services *services, size_t *out_count);
/* NULL when absent (TS undefined). */
const char *qa_app_sim_classic_guest_services_configstring(qa_app_sim_classic_guest_services *services, double index);
const qa_app_sim_classic_guest_message *qa_app_sim_classic_guest_services_drain_messages(qa_app_sim_classic_guest_services *services, size_t *out_count);
const char *qa_app_sim_classic_guest_services_resource(qa_app_sim_classic_guest_services *services, const char *kind, double index);
double qa_app_sim_classic_guest_services_resource_index(qa_app_sim_classic_guest_services *services, const char *kind, const char *name);
void qa_app_sim_classic_guest_services_set_configstring(qa_app_sim_classic_guest_services *services, double index, const char *value);
qa_compat_q2_classic_world_link qa_app_sim_classic_guest_services_world_link(qa_app_sim_classic_guest_services *services, qa_bounds bounds);
qa_q2_entity_state qa_app_sim_classic_guest_services_entity_state(qa_app_sim_classic_guest_services *services, double slot);
/* stats view is services-owned scratch, valid until the next call. */
qa_q2_player_state qa_app_sim_classic_guest_services_player_state(qa_app_sim_classic_guest_services *services, double slot);
qa_app_sim_model_appearance qa_app_sim_classic_guest_services_model_appearance(qa_app_sim_classic_guest_services *services, double slot);
/* -1 is the TS null. */
double qa_app_sim_classic_guest_services_allocate_monster_proxy(qa_app_sim_classic_guest_services *services);
void qa_app_sim_classic_guest_services_bind_monster_proxy(qa_app_sim_classic_guest_services *services, double slot, qa_owned_actor actor);
void qa_app_sim_classic_guest_services_sync_monster_proxies(qa_app_sim_classic_guest_services *services);
void qa_app_sim_classic_guest_services_forward_monster_proxy_damage(qa_app_sim_classic_guest_services *services);
void qa_app_sim_classic_guest_services_publish_entities(qa_app_sim_classic_guest_services *services);
/* The engine imports by name, for direct calls (TS adapter.services.sound(...)). */
void qa_app_sim_classic_guest_services_sound(qa_app_sim_classic_guest_services *services, const qa_vec3 *origin, bool has_entity, qa_guest_address entity,
                                             double channel, double index, double volume, double attenuation, double offset);
#endif
