/* Port of src/content/q3/base/game/item-lifecycle.ts. GPL-2.0-or-later.
 * q3ItemInventory ported; lifecycle spawn/touch need world+items hosts (unresolved). */
#ifndef QA_CONTENT_Q3_BASE_GAME_ITEM_LIFECYCLE_H
#define QA_CONTENT_Q3_BASE_GAME_ITEM_LIFECYCLE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/base/game/item_pickup.h"
#include "qa/content/q3/base/game/state.h"
/* Mirror of shared/items.ts PlayerInventory (TS owner unported). */
typedef struct qa_q3_player_inventory_mirror {
  qa_q3_game_player_slots_mirror stats, ammo, powerups; double health;
} qa_q3_player_inventory_mirror;
qa_q3_player_inventory_mirror qa_q3_item_inventory(qa_q3_game_client *client);
typedef struct qa_q3_item_registry { const qa_q3_game_item_definition_mirror *items; size_t count; } qa_q3_item_registry;
void qa_q3_item_registry_init(qa_q3_item_registry *registry, const qa_q3_game_item_definition_mirror *items, size_t count);
typedef struct qa_q3_item_lifecycle_context { void *ctx; qa_q3_item_registry registry; } qa_q3_item_lifecycle_context;
void qa_q3_respawn_item(qa_q3_game_entity *entity, qa_q3_item_lifecycle_context context);
void qa_q3_touch_item(qa_q3_game_entity *entity, qa_q3_damage_participant other, qa_touch_contact contact, qa_q3_item_lifecycle_context context);
void qa_q3_finish_spawning_item(qa_q3_game_entity *entity, qa_q3_item_lifecycle_context context);
void qa_q3_bind_item_save_callbacks(qa_q3_item_lifecycle_context context);
#endif
