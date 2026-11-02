/* Port of src/content/q3/base/game/item-lifecycle.ts. GPL-2.0-or-later.
 * ItemLifecycleContext's world/pool/random/log/warn members become fn+ctx
 * (single shared ctx, as in sibling game contexts); the item table is the
 * registry's mirror list, index-aligned with shared/items.ts itemList for
 * the context product. Optional TS members: callbacks (has_callbacks),
 * admitPickup (NULL fn), originalPickups (has_original_pickups). */
#ifndef QA_CONTENT_Q3_BASE_GAME_ITEM_LIFECYCLE_H
#define QA_CONTENT_Q3_BASE_GAME_ITEM_LIFECYCLE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/item_pickup.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/world.h"
#include "qa/contracts/original_pickups.h"
/* Mirror of shared/items.ts PlayerInventory (TS owner unported). */
typedef struct qa_q3_player_inventory_mirror {
  qa_q3_game_player_slots_mirror stats, ammo, powerups; double health;
} qa_q3_player_inventory_mirror;
qa_q3_player_inventory_mirror qa_q3_item_inventory(qa_q3_game_client *client);
/* Mirror of ItemRegistry's product item table; product must be set by the
 * caller (init preserves it). */
typedef struct qa_q3_item_registry {
  const qa_q3_game_item_definition_mirror *items; size_t count; qa_q3_game_product product;
} qa_q3_item_registry;
void qa_q3_item_registry_init(qa_q3_item_registry *registry, const qa_q3_game_item_definition_mirror *items, size_t count);
/* TS SourcePickupDescriptor. */
typedef struct qa_q3_source_pickup_descriptor {
  qa_actor_id item_actor, player_actor;
  const qa_q3_game_item_definition_mirror *item;
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
/* TS ItemLifecycleContext. All host fns share ctx. */
typedef struct qa_q3_item_lifecycle_context {
  void *ctx; qa_q3_item_registry registry;
  qa_q3_entity_pool *entities;
  qa_q3_game_product product;
  double game_type, weapon_respawn_seconds, team_weapon_respawn_seconds;
  bool has_callbacks;
  qa_q3_entity_callback touch_callback, respawn_callback;
  bool has_original_pickups;
  qa_original_pickup_admission original_pickups;
  qa_q3_source_pickup_admission (*admit_pickup)(void *ctx, qa_q3_source_pickup_descriptor descriptor);
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
void qa_q3_bind_item_save_callbacks(qa_q3_item_lifecycle_context context);
#endif
