/* Port of src/content/q3/base/game/item-lifecycle.ts. GPL-2.0-or-later.
 * ItemLifecycleContext's world/pool/random/log/warn members become fn+ctx
 * (single shared ctx, as in sibling game contexts); the item table is
 * shared/items.ts itemList(registry.product). Optional TS members: callbacks (has_callbacks),
 * admitPickup (NULL fn), originalPickups (has_original_pickups). */
#ifndef QA_CONTENT_Q3_BASE_GAME_ITEM_LIFECYCLE_H
#define QA_CONTENT_Q3_BASE_GAME_ITEM_LIFECYCLE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/item_pickup.h"
#include "qa/content/q3/base/game/spawn.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/world.h"
#include "qa/contracts/original_pickups.h"
#include "qa/contracts/pickups.h"
#include "qa/persistence/value.h"
/* TS q3ItemInventory: inventory.ctx borrows client->ps. */
qa_q3_base_player_inventory qa_q3_item_inventory(qa_q3_game_client *client);
/* TS ItemRegistry: items/count are the shared itemList(product) table. */
typedef struct qa_q3_item_registry {
  const qa_q3_base_item_definition *items; size_t count; qa_q3_base_product product;
  uint8_t *registered; /* owned, count bytes of 0/1 (TS #registered) */
} qa_q3_item_registry;
/* TS new ItemRegistry(product): items/count view itemList(product). */
void qa_q3_item_registry_init(qa_q3_item_registry *registry, qa_q3_base_product product);
void qa_q3_item_registry_destroy(qa_q3_item_registry *registry);
void qa_q3_item_registry_register(qa_q3_item_registry *registry, const qa_q3_base_item_definition *item);
bool qa_q3_item_registry_is_registered(qa_q3_item_registry *registry, const qa_q3_base_item_definition *item);
void qa_q3_item_registry_clear(qa_q3_item_registry *registry, double game_type);
/* TS save(): returns the registered count; index is always 27 (CS_ITEMS). */
double qa_q3_item_registry_save(qa_q3_item_registry *registry, void *ctx,
    void (*set_configstring)(void *ctx, double index, const char *value),
    void (*log)(void *ctx, const char *message));
/* Bytes save value (NULL only on OOM); caller frees with qa_save_value_free. */
qa_save_value *qa_q3_item_registry_capture_save_state(qa_q3_item_registry *registry);
void qa_q3_item_registry_restore_save_state(qa_q3_item_registry *registry, const qa_save_value *value);
/* TS SourcePickupDescriptor. */
typedef struct qa_q3_source_pickup_descriptor {
  qa_actor_id item_actor, player_actor;
  const qa_q3_base_item_definition *item;
  double count, generic1; bool dropped;
  double game_type, weapon_respawn_seconds, team_weapon_respawn_seconds;
} qa_q3_source_pickup_descriptor;
/* TS SourcePickupAdmission: respawn_seconds only for the "picked" kind. */
typedef enum qa_q3_source_pickup_admission_kind {
  QA_Q3_SOURCE_PICKUP_NATIVE, QA_Q3_SOURCE_PICKUP_REJECTED, QA_Q3_SOURCE_PICKUP_PICKED
} qa_q3_source_pickup_admission_kind;
typedef struct qa_q3_source_pickup_admission {
  qa_q3_source_pickup_admission_kind kind; double respawn_seconds;
} qa_q3_source_pickup_admission;
/* TS SourcePickupPreview: offer/preview only for the "selected" kind. */
typedef enum qa_q3_source_pickup_preview_kind {
  QA_Q3_SOURCE_PREVIEW_NATIVE, QA_Q3_SOURCE_PREVIEW_REJECTED, QA_Q3_SOURCE_PREVIEW_SELECTED
} qa_q3_source_pickup_preview_kind;
typedef struct qa_q3_source_pickup_preview {
  qa_q3_source_pickup_preview_kind kind;
  qa_pickup_supply_offer offer; qa_pickup_supply_preview preview;
} qa_q3_source_pickup_preview;
/* TS observeQ3Supply non-null result. */
typedef struct qa_q3_supply_observation {
  qa_pickup_supply_observation observation; qa_pickup_supply_preview preview;
} qa_q3_supply_observation;
/* TS ItemLifecycleContext. All host fns share ctx. */
typedef struct qa_q3_item_lifecycle_context {
  void *ctx; qa_q3_item_registry registry;
  qa_q3_entity_pool *entities;
  qa_q3_base_product product;
  double game_type, weapon_respawn_seconds, team_weapon_respawn_seconds;
  bool has_callbacks;
  qa_q3_entity_callback touch_callback, respawn_callback;
  bool has_original_pickups;
  qa_original_pickup_admission original_pickups;
  qa_q3_source_pickup_admission (*admit_pickup)(void *ctx, qa_q3_source_pickup_descriptor descriptor);
  /* TS previewPickup (optional; NULL = undefined). */
  qa_q3_source_pickup_preview (*preview_pickup)(void *ctx, qa_q3_source_pickup_descriptor descriptor);
  qa_q3_base_server_trace_result (*trace)(void *ctx, qa_q3_base_server_trace_query query);
  qa_q3_base_actor_trace_result (*trace_actor)(void *ctx, qa_q3_base_actor_trace_query query);
  const char *(*handicap_for_client)(void *ctx, double client_num);
  double (*team_pickup)(void *ctx, qa_q3_game_entity *item, qa_q3_game_entity *player);
  void (*use_targets)(void *ctx, qa_q3_game_entity *item, qa_q3_game_entity *activator);
  double (*sound_index)(void *ctx, const char *path);
  double (*rand_value)(void *ctx);
  double (*random)(void *ctx);
  void (*log)(void *ctx, const char *message);
  void (*warn)(void *ctx, const char *message);
} qa_q3_item_lifecycle_context;
void qa_q3_respawn_item(qa_q3_game_entity *entity, qa_q3_item_lifecycle_context context);
void qa_q3_touch_item(qa_q3_game_entity *entity, qa_q3_damage_participant other, qa_touch_contact contact, qa_q3_item_lifecycle_context context);
void qa_q3_finish_spawning_item(qa_q3_game_entity *entity, qa_q3_item_lifecycle_context context);
/* Returns false for TS null; *out is written only on true. */
bool qa_q3_observe_q3_supply(qa_q3_game_entity *entity, qa_q3_game_entity *recipient,
    qa_q3_item_lifecycle_context context, qa_q3_supply_observation *out);
/* TS spawnItem; is_disabled is the TS () => boolean closure (fn+ctx). */
void qa_q3_spawn_item(qa_q3_game_entity *entity, const qa_q3_base_item_definition *item,
    const qa_q3_spawn_variables *variables, bool (*is_disabled)(void *ctx), void *is_disabled_ctx,
    qa_q3_item_lifecycle_context context);
void qa_q3_bind_item_save_callbacks(qa_q3_item_lifecycle_context context);
#endif
