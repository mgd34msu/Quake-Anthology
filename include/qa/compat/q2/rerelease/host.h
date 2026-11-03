/* Port of src/compat/q2/rerelease/host.ts. GPL-2.0-or-later.
 * Async *Loading variants are synchronous with a next-frame
 * callback. */
#ifndef QA_COMPAT_Q2_RERELEASE_HOST_H
#define QA_COMPAT_Q2_RERELEASE_HOST_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/native_pickups.h"
#include "qa/compat/q2/rerelease/debug_shapes.h"
#include "qa/compat/q2/rerelease/foreign_actors.h"
#include "qa/compat/q2/rerelease/sounds.h"
#include "qa/compat/q2/rerelease/world_text.h"
#include "qa/compat/q2/rerelease/imports.h"
#include "qa/compat/q2/rerelease/messages.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/contracts/original_pickups.h"

typedef struct qa_compat_q2_rerelease_actor_bindings {
  qa_body_state_binding body;
  bool has_combat; qa_combat_state_binding combat;
  bool has_power_armor_cells; qa_power_armor_cell_binding power_armor_cells;
  bool has_inventory; qa_inventory_state_binding inventory;
  qa_actor_callbacks callbacks;
} qa_compat_q2_rerelease_actor_bindings;

/* TS RereleaseSourceSave. native is malloc'd in write results;
 * deferred_damage is freed by qa_compat_q2_rerelease_deferred_damage_free_saves;
 * projections borrows the foreign-actors scratch in write results. */
typedef struct qa_compat_q2_rerelease_source_save {
  uint8_t *native; size_t native_length;
  const qa_compat_q2_rerelease_deferred_damage_save *deferred_damage; size_t deferred_damage_count;
  const qa_compat_q2_rerelease_projection_save *projections; size_t projection_count;
} qa_compat_q2_rerelease_source_save;

typedef struct qa_compat_q2_rerelease_semantic_bindings {
  void *ctx;
  /* project NULL fn is the TS undefined. Its false return is TS null. */
  bool (*project)(void *ctx, qa_raw_entity_view view, qa_compat_q2_rerelease_guest_module *module, qa_owned_actor *out);
  bool has_generation; double (*generation)(void *ctx, qa_raw_entity_view view);
  void (*bound)(void *ctx, qa_raw_entity_view view, qa_owned_actor actor); /* optional */
  qa_compat_q2_rerelease_actor_bindings (*bind)(void *ctx, qa_raw_entity_view view, qa_owned_actor actor,
                                                qa_compat_q2_rerelease_guest_module *module);
  qa_guest_address (*foreign_address)(void *ctx, qa_actor_id actor);
} qa_compat_q2_rerelease_semantic_bindings;

typedef struct qa_compat_q2_rerelease_link_metadata { double area, area2, network_solid; } qa_compat_q2_rerelease_link_metadata;
typedef struct qa_compat_q2_rerelease_spatial_services {
  void *ctx;
  bool (*areas_connected)(void *ctx, double first, double second);
  bool (*visibility)(void *ctx, const char *kind, qa_vec3 first, qa_vec3 second, bool portals); /* kind "pvs"|"phs" */
  double (*surface_id)(void *ctx, qa_q2_surface_info surface);
  const qa_actor_id *(*box_edicts)(void *ctx, qa_vec3 min, qa_vec3 max, double area, size_t *out_count);
  double (*inline_model)(void *ctx, double source_model_index);
  void (*prepare_link)(void *ctx, qa_owned_actor actor, qa_raw_entity_view view); /* optional */
  qa_compat_q2_rerelease_link_metadata (*link_metadata)(void *ctx, qa_owned_actor actor, qa_raw_entity_view view);
} qa_compat_q2_rerelease_spatial_services;

typedef struct qa_compat_q2_rerelease_q2_host_options {
  /* TS RereleaseModuleOptions minus invokeImport/actorAtSlot, which
   * the host overwrites. */
  qa_compat_q2_rerelease_module_options module_options;
  bool has_pickups;
  const qa_original_pickup_admission *pickup_admission;
  qa_guest_address pickup_image_base;
  const qa_compat_q2_native_pickup_profile *pickup_profile; /* optional */
  void *import_boundary_ctx; /* optional */
  qa_guest_call_result (*import_boundary)(void *ctx, const char *name, const qa_guest_call_value *arguments, size_t count,
                                          qa_guest_call_result (*invoke)(void *), void *invoke_ctx);
  qa_q2_foundation_host *engine;
  qa_compat_q2_rerelease_core_services services;
  qa_compat_q2_rerelease_spatial_services spatial;
  qa_compat_q2_rerelease_semantic_bindings semantics;
  const qa_compat_q2_rerelease_message_services *messages; /* optional */
  bool debug_drawing_headless;
  void *debug_shapes_ctx; void (*debug_shapes)(void *ctx, qa_compat_q2_rerelease_debug_shapes_event event); /* optional */
  void *world_text_ctx; void (*world_text)(void *ctx, qa_compat_q2_rerelease_world_text_event event); /* optional */
  void *sound_ctx; void (*sound)(void *ctx, qa_compat_q2_rerelease_sound_event event); /* optional */
  bool has_native_entries; qa_compat_q2_rerelease_native_entries native_entries;
  const qa_compat_q2_rerelease_foreign_damage_services *foreign_damage; /* optional */
  void *intercept_ctx; /* optional; false return is TS undefined */
  bool (*intercept_import)(void *ctx, qa_compat_q2_rerelease_import_call call, qa_compat_q2_rerelease_q2_guest_host *host,
                           qa_guest_call_result *out);
} qa_compat_q2_rerelease_q2_host_options;

/* NULL only on OOM. */
qa_compat_q2_rerelease_q2_guest_host *qa_compat_q2_rerelease_q2_guest_host_create(const qa_compat_q2_rerelease_q2_host_options *options);
void qa_compat_q2_rerelease_q2_guest_host_destroy(qa_compat_q2_rerelease_q2_guest_host *h);
const qa_compat_q2_rerelease_q2_host_options *qa_compat_q2_rerelease_q2_guest_host_get_options(const qa_compat_q2_rerelease_q2_guest_host *h);
qa_compat_q2_rerelease_guest_module *qa_compat_q2_rerelease_q2_guest_host_get_module(qa_compat_q2_rerelease_q2_guest_host *h);
qa_compat_q2_rerelease_core_imports *qa_compat_q2_rerelease_q2_guest_host_get_core(qa_compat_q2_rerelease_q2_guest_host *h);
/* NULL is the TS null. */
qa_compat_q2_rerelease_foreign_actors *qa_compat_q2_rerelease_q2_guest_host_get_foreign_actors(qa_compat_q2_rerelease_q2_guest_host *h);
void qa_compat_q2_rerelease_q2_guest_host_retire_input_client(qa_compat_q2_rerelease_q2_guest_host *h, double slot);
void qa_compat_q2_rerelease_q2_guest_host_finish_input_retirement(qa_compat_q2_rerelease_q2_guest_host *h, double slot);
void qa_compat_q2_rerelease_q2_guest_host_bind_pickup_supply(qa_compat_q2_rerelease_q2_guest_host *h, qa_compat_q2_native_pickup_supply owner);
/* TS bindPickupSupply's returned remover. */
void qa_compat_q2_rerelease_q2_guest_host_unbind_pickup_supply(qa_compat_q2_rerelease_q2_guest_host *h, qa_compat_q2_native_pickup_supply owner);
/* TS pickupSupply: valid while the held source grant settles. */
qa_compat_q2_native_pickup_supply_evaluation qa_compat_q2_rerelease_q2_guest_host_pickup_supply(qa_compat_q2_rerelease_q2_guest_host *h, const qa_original_pickup_offer *offer);
bool qa_compat_q2_rerelease_q2_guest_host_pickup_supply_owns(qa_compat_q2_rerelease_q2_guest_host *h, qa_actor_id actor, qa_item_id item, qa_actor_id pickup);
/* False return is the TS null. */
bool qa_compat_q2_rerelease_q2_guest_host_actor(qa_compat_q2_rerelease_q2_guest_host *h, qa_raw_entity_view view, qa_owned_actor *out);
qa_owned_actor qa_compat_q2_rerelease_q2_guest_host_reserve_client(qa_compat_q2_rerelease_q2_guest_host *h, double slot);
bool qa_compat_q2_rerelease_q2_guest_host_is_client_reserved(qa_compat_q2_rerelease_q2_guest_host *h, double slot);
void qa_compat_q2_rerelease_q2_guest_host_release_client_reservation(qa_compat_q2_rerelease_q2_guest_host *h, double slot);
void qa_compat_q2_rerelease_q2_guest_host_rebind_world(qa_compat_q2_rerelease_q2_guest_host *h, void *ctx, void (*commit)(void *ctx));
void qa_compat_q2_rerelease_q2_guest_host_reconcile(qa_compat_q2_rerelease_q2_guest_host *h);
/* Returned list is scratch owned by h, replaced by the next call. */
const qa_raw_entity_view *qa_compat_q2_rerelease_q2_guest_host_bot_entities(qa_compat_q2_rerelease_q2_guest_host *h, size_t *out_count);
qa_guest_address qa_compat_q2_rerelease_q2_guest_host_address_for_actor(qa_compat_q2_rerelease_q2_guest_host *h, qa_actor_id actor);
void qa_compat_q2_rerelease_q2_guest_host_pre_init(qa_compat_q2_rerelease_q2_guest_host *h);
void qa_compat_q2_rerelease_q2_guest_host_init(qa_compat_q2_rerelease_q2_guest_host *h);
qa_guest_call_result qa_compat_q2_rerelease_q2_guest_host_call_loading(qa_compat_q2_rerelease_q2_guest_host *h, const char *name,
                                                                       const qa_guest_call_value *arguments, size_t count,
                                                                       void (*next_frame)(void *), void *next_ctx);
void qa_compat_q2_rerelease_q2_guest_host_init_loading(qa_compat_q2_rerelease_q2_guest_host *h, void (*next_frame)(void *), void *next_ctx);
void qa_compat_q2_rerelease_q2_guest_host_spawn_entities_loading(qa_compat_q2_rerelease_q2_guest_host *h, const char *map, const char *entities,
                                                                 const char *spawnpoint, void (*next_frame)(void *), void *next_ctx);
void qa_compat_q2_rerelease_q2_guest_host_run_frame_loading(qa_compat_q2_rerelease_q2_guest_host *h, bool main_loop,
                                                            void (*next_frame)(void *), void *next_ctx);
/* domain: "checkpoint" | "current". */
void qa_compat_q2_rerelease_q2_guest_host_read_save_loading(qa_compat_q2_rerelease_q2_guest_host *h, const char *kind,
                                                            qa_compat_q2_rerelease_source_save saved, void (*next_frame)(void *), void *next_ctx,
                                                            const char *domain);
void qa_compat_q2_rerelease_q2_guest_host_shutdown(qa_compat_q2_rerelease_q2_guest_host *h);
void qa_compat_q2_rerelease_q2_guest_host_spawn_entities(qa_compat_q2_rerelease_q2_guest_host *h, const char *map, const char *entities, const char *spawnpoint);
void qa_compat_q2_rerelease_q2_guest_host_prep_frame(qa_compat_q2_rerelease_q2_guest_host *h);
void qa_compat_q2_rerelease_q2_guest_host_run_frame(qa_compat_q2_rerelease_q2_guest_host *h, bool main_loop);
qa_compat_q2_rerelease_client_connect qa_compat_q2_rerelease_q2_guest_host_client_connect(
    qa_compat_q2_rerelease_q2_guest_host *h, double slot, const char *userinfo, const char *social_id, bool is_bot);
void qa_compat_q2_rerelease_q2_guest_host_client_begin(qa_compat_q2_rerelease_q2_guest_host *h, double slot);
void qa_compat_q2_rerelease_q2_guest_host_client_think(qa_compat_q2_rerelease_q2_guest_host *h, double slot,
                                                       qa_q2_rerelease_user_command command, const qa_equipment_movement *equipment);
void qa_compat_q2_rerelease_q2_guest_host_client_disconnect(qa_compat_q2_rerelease_q2_guest_host *h, double slot);
/* kind: "game" | "level". */
qa_compat_q2_rerelease_source_save qa_compat_q2_rerelease_q2_guest_host_write_save(qa_compat_q2_rerelease_q2_guest_host *h, const char *kind,
                                                                                   bool automatic_or_transition);
qa_compat_q2_rerelease_source_save qa_compat_q2_rerelease_q2_guest_host_write_save_loading(
    qa_compat_q2_rerelease_q2_guest_host *h, const char *kind, bool automatic_or_transition, void (*next_frame)(void *), void *next_ctx);
void qa_compat_q2_rerelease_q2_guest_host_read_save(qa_compat_q2_rerelease_q2_guest_host *h, const char *kind,
                                                    qa_compat_q2_rerelease_source_save saved, const char *domain);
/* TS #dispatchImport(call): the import itself, outside importBoundary (the
 * boundary's invoke closure; a native mod host replays restore links with it). */
qa_guest_call_result qa_compat_q2_rerelease_q2_guest_host_dispatch_import(qa_compat_q2_rerelease_q2_guest_host *h, qa_compat_q2_rerelease_import_call call);
/* clipped_entity NULL is the TS undefined. */
qa_guest_call_result qa_compat_q2_rerelease_q2_guest_host_encode_trace(qa_compat_q2_rerelease_q2_guest_host *h, qa_trace_result trace,
                                                                       const qa_guest_address *clipped_entity);
#endif
