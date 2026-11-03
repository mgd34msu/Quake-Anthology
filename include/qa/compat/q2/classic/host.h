/* Port of src/compat/q2/classic/host.ts. GPL-2.0-or-later.
 * Async *Loading variants are synchronous with a next-frame
 * callback (PORTING). NativePrimaryPickups is held by value. */
#ifndef QA_COMPAT_Q2_CLASSIC_HOST_H
#define QA_COMPAT_Q2_CLASSIC_HOST_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/classic/cvars.h"
#include "qa/compat/q2/classic/layout.h"
#include "qa/compat/q2/classic/records.h"
#include "qa/compat/q2/native_pickups.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/contracts/original_pickups.h"
#include "qa/movement/body_shape.h"

typedef struct qa_compat_q2_classic_world_link {
  const double *clusters; size_t cluster_count; bool clusters_null; double headnode; double areas[2];
} qa_compat_q2_classic_world_link;
/** TS damageProvenance result: Omit<AttackProvenance,"attacker"|"inflictor"|"cause">. */
typedef struct qa_compat_q2_classic_damage_provenance {
  void *ctx;
  qa_attack_provenance (*provenance)(void *ctx, qa_actor_id attacker, qa_actor_id inflictor, qa_actor_id target);
} qa_compat_q2_classic_damage_provenance;

typedef struct qa_compat_q2_classic_guest_host qa_compat_q2_classic_guest_host;
typedef struct qa_compat_q2_classic_engine_services {
  void *ctx;
  void (*dispose)(void *ctx); /* optional */
  const qa_original_pickup_admission *pickups; /* optional */
  const qa_compat_q2_classic_damage_provenance *damage_provenance; /* optional */
  const qa_compat_q2_classic_actor_projection *projection; /* optional */
  /* TS standInActor: the shared actor behind a stand-in edict (a selected monster), so guest T_Damage on it reaches
   * shared combat; NULL is TS undefined, a false return TS null. */
  bool (*stand_in_actor)(void *ctx, qa_raw_entity_view record, qa_actor_id *out);
  void *stand_in_ctx;
  qa_q2_foundation_host *engine;
  qa_cvar_registry *cvars;
  void *bind_ctx; void (*bind_entity)(void *ctx, qa_raw_entity_view record, qa_owned_actor actor);
  void *link_ctx; void (*link_body)(void *ctx, qa_raw_entity_view record, qa_owned_actor actor); /* optional */
  /* destination: "broadcast"|"debug"|"client"|"center"; entity false = TS null. */
  void (*print)(void *ctx, const char *destination, bool has_entity, qa_guest_address entity, double level, const char *text);
  void (*configstring)(void *ctx, double index, const char *value);
  double (*resource_index)(void *ctx, const char *kind, const char *name); /* kind model|sound|image */
  void (*sound)(void *ctx, const qa_vec3 *origin, bool has_entity, qa_guest_address entity, double channel, double index, double volume, double attenuation, double time_offset);
  bool (*areas_connected)(void *ctx, double first, double second);
  qa_compat_q2_classic_world_link (*world_link)(void *ctx, qa_bounds bounds);
  const qa_actor_id *(*box_edicts)(void *ctx, qa_bounds bounds, const char *kind, size_t *out_count);
  void (*message)(void *ctx, const char *operation, const qa_guest_call_value *values, size_t count, const qa_guest_call_context *context);
  /* command(): arguments + args tail. */
  const char *const *(*command)(void *ctx, size_t *out_count, const char **out_args);
  void (*add_command)(void *ctx, const char *text);
  void (*debug_graph)(void *ctx, double value, double color);
  void *pmove_ctx; /* set below to avoid a circular type */
  void (*pmove)(void *ctx, qa_guest_address address, qa_compat_q2_classic_guest_host *host);
} qa_compat_q2_classic_engine_services;

typedef struct qa_compat_q2_classic_guest_host_options {
  void *import_boundary_ctx;
  qa_guest_call_result (*import_boundary)(void *ctx, const char *name, const qa_guest_call_value *arguments, size_t count,
                                          qa_guest_call_result (*invoke)(void *), void *invoke_ctx); /* optional */
  qa_compat_q2_runner *runner;
  qa_provider_id provider;
  qa_compat_q2_classic_engine_services services;
  double instruction_budget;
} qa_compat_q2_classic_guest_host_options;

/* TS classicPointer/classicRequiredPointer/classicNumber. */
bool qa_compat_q2_classic_pointer(const qa_guest_call_value *arguments, size_t count, size_t index, qa_guest_address *out);
qa_guest_address qa_compat_q2_classic_required_pointer(const qa_guest_call_value *arguments, size_t count, size_t index);
double qa_compat_q2_classic_number(const qa_guest_call_value *arguments, size_t count, size_t index);

/* NULL only on OOM. */
qa_compat_q2_classic_guest_host *qa_compat_q2_classic_guest_host_create(const qa_compat_q2_classic_guest_host_options *options);
void qa_compat_q2_classic_guest_host_destroy(qa_compat_q2_classic_guest_host *h);
const qa_compat_q2_classic_guest_host_options *qa_compat_q2_classic_guest_host_get_options(const qa_compat_q2_classic_guest_host *h);
qa_compat_q2_memory *qa_compat_q2_classic_guest_host_get_memory(const qa_compat_q2_classic_guest_host *h);
qa_compat_q2_classic_cvars *qa_compat_q2_classic_guest_host_get_cvars(const qa_compat_q2_classic_guest_host *h);
qa_guest_address qa_compat_q2_classic_guest_host_get_imports(const qa_compat_q2_classic_guest_host *h);
int64_t qa_compat_q2_classic_guest_host_spawn_instructions(const qa_compat_q2_classic_guest_host *h);
qa_compat_q2_classic_edicts *qa_compat_q2_classic_guest_host_get_edicts(qa_compat_q2_classic_guest_host *h);
/* TS #exports (edicts.exports): aborts before GetGameAPI returned. */
qa_guest_address qa_compat_q2_classic_guest_host_get_exports(const qa_compat_q2_classic_guest_host *h);
void qa_compat_q2_classic_guest_host_bind_pickups(qa_compat_q2_classic_guest_host *h, qa_guest_address image,
                                                  const qa_compat_q2_native_pickup_profile *declared);
void qa_compat_q2_classic_guest_host_bind_pickup_supply(qa_compat_q2_classic_guest_host *h, qa_compat_q2_native_pickup_supply owner);
/* TS bindPickupSupply's returned remover. */
void qa_compat_q2_classic_guest_host_unbind_pickup_supply(qa_compat_q2_classic_guest_host *h, qa_compat_q2_native_pickup_supply owner);
/* TS pickupSupply: valid while the held source grant settles. */
qa_compat_q2_native_pickup_supply_evaluation qa_compat_q2_classic_guest_host_pickup_supply(qa_compat_q2_classic_guest_host *h, const qa_original_pickup_offer *offer);
bool qa_compat_q2_classic_guest_host_pickup_supply_owns(qa_compat_q2_classic_guest_host *h, qa_actor_id actor, qa_item_id item, qa_actor_id pickup);
/* bindInputMovement: returns an unbind handle (0 = none). */
typedef void (*qa_compat_q2_classic_input_boundary_fn)(void *ctx, qa_guest_address address, void *run_ctx, void (*run)(void *, const qa_movement_body_shape *));
uint64_t qa_compat_q2_classic_guest_host_bind_input_movement(qa_compat_q2_classic_guest_host *h, void *ctx, qa_compat_q2_classic_input_boundary_fn boundary);
void qa_compat_q2_classic_guest_host_unbind_input_movement(qa_compat_q2_classic_guest_host *h, uint64_t handle);
void qa_compat_q2_classic_guest_host_apply_input_movement(qa_compat_q2_classic_guest_host *h, qa_guest_address address,
                                                          void *run_ctx, void (*run)(void *, const qa_movement_body_shape *));
size_t qa_compat_q2_classic_guest_host_variadic_layouts(qa_compat_q2_classic_guest_host *h, qa_callback_id id,
                                                        const qa_guest_call_value *fixed, size_t fixed_count,
                                                        qa_guest_value_layout *out, size_t capacity);
qa_guest_call_result qa_compat_q2_classic_guest_host_invoke(qa_compat_q2_classic_guest_host *h, qa_guest_address target,
                                                            qa_compat_q2_guest_call_signature signature, const qa_guest_call_value *arguments,
                                                            size_t count, const qa_raw_entity_view *self, double instruction_budget);
qa_guest_address qa_compat_q2_classic_guest_host_get_game_api(qa_compat_q2_classic_guest_host *h, qa_guest_address target);
qa_guest_call_result qa_compat_q2_classic_guest_host_call(qa_compat_q2_classic_guest_host *h, const char *name,
                                                          const qa_guest_call_value *arguments, size_t count, const qa_raw_entity_view *self, double instruction_budget);
void qa_compat_q2_classic_guest_host_init(qa_compat_q2_classic_guest_host *h);
void qa_compat_q2_classic_guest_host_init_loading(qa_compat_q2_classic_guest_host *h, void (*next_frame)(void *), void *next_ctx);
void qa_compat_q2_classic_guest_host_shutdown(qa_compat_q2_classic_guest_host *h);
void qa_compat_q2_classic_guest_host_spawn_entities(qa_compat_q2_classic_guest_host *h, const char *map, const char *entities, const char *spawn_point);
qa_guest_call_result qa_compat_q2_classic_guest_host_call_loading(qa_compat_q2_classic_guest_host *h, const char *name,
                                                                  const qa_guest_call_value *arguments, size_t count,
                                                                  void (*next_frame)(void *), void *next_ctx, double instruction_budget);
void qa_compat_q2_classic_guest_host_save_loading(qa_compat_q2_classic_guest_host *h, const char *name, const char *filename,
                                                  void (*next_frame)(void *), void *next_ctx);
void qa_compat_q2_classic_guest_host_spawn_entities_loading(qa_compat_q2_classic_guest_host *h, const char *map, const char *entities,
                                                            const char *spawn_point, void (*next_frame)(void *), void *next_ctx);
void qa_compat_q2_classic_guest_host_set_model_name(qa_compat_q2_classic_guest_host *h, double index, const char *name);
void qa_compat_q2_classic_guest_host_rebind_world(qa_compat_q2_classic_guest_host *h);
void qa_compat_q2_classic_guest_host_write_travel_level(qa_compat_q2_classic_guest_host *h, const char *filename, double max_clients);
void qa_compat_q2_classic_guest_host_write_travel_level_loading(qa_compat_q2_classic_guest_host *h, const char *filename, double max_clients,
                                                                void (*next_frame)(void *), void *next_ctx);
void qa_compat_q2_classic_guest_host_run_frame(qa_compat_q2_classic_guest_host *h);
typedef struct qa_compat_q2_classic_client_connect { bool allowed; char *userinfo; } qa_compat_q2_classic_client_connect;
qa_compat_q2_classic_client_connect qa_compat_q2_classic_guest_host_client_connect(qa_compat_q2_classic_guest_host *h, double slot, const char *userinfo);
void qa_compat_q2_classic_guest_host_client_event(qa_compat_q2_classic_guest_host *h, const char *name, double slot);
/* Malloc'd; caller frees. */
char *qa_compat_q2_classic_guest_host_client_userinfo_changed(qa_compat_q2_classic_guest_host *h, double slot, const char *userinfo);
void qa_compat_q2_classic_guest_host_client_think(qa_compat_q2_classic_guest_host *h, double slot, const uint8_t *command, size_t length);
void qa_compat_q2_classic_guest_host_save(qa_compat_q2_classic_guest_host *h, const char *name, const char *filename, bool autosave);
qa_guest_call_result qa_compat_q2_classic_guest_host_import_call(qa_compat_q2_classic_guest_host *h, const char *name,
                                                                 const qa_guest_call_context *context, const qa_guest_call_value *arguments, size_t count);
/* Writes 56 trace bytes into out. */
void qa_compat_q2_classic_guest_host_trace_bytes(qa_compat_q2_classic_guest_host *h, qa_trace_result trace, uint8_t out[56]);
void qa_compat_q2_classic_guest_host_link_entity(qa_compat_q2_classic_guest_host *h, qa_guest_address address);
/* TS observeReload: called when the guest's edict table is replaced (level spawn, saved-game load, world rebind);
 * stand-in edicts it held are gone. Returns a handle for qa_compat_q2_classic_guest_host_unobserve_reload. */
size_t qa_compat_q2_classic_guest_host_observe_reload(qa_compat_q2_classic_guest_host *h, void (*observer)(void *ctx), void *ctx);
void qa_compat_q2_classic_guest_host_unobserve_reload(qa_compat_q2_classic_guest_host *h, size_t handle);
/* TS fitClassicUserinfo: SV_ConnectionlessPacket strncpy, peers supply userinfo, so oversized text is cut to the guest
 * buffer (capacity bytes with the terminator) rather than refused. Malloc'd. */
char *qa_compat_q2_fit_classic_userinfo(const char *text, size_t capacity);

#endif
