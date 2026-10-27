/* Port of src/content/q3/base/game/item-pickup.ts. GPL-2.0-or-later.
 * shared/items.ts itemList/itemAt unported: caller registers mirror lists via
 * qa_q3_item_pickup_set_item_list_mirror (TS-owner comment; see unresolved). */
#ifndef QA_CONTENT_Q3_BASE_GAME_ITEM_PICKUP_H
#define QA_CONTENT_Q3_BASE_GAME_ITEM_PICKUP_H
#include <stddef.h>
#include "qa/content/q3/base/game/state.h"
enum { QA_Q3_RESPAWN_AMMO = 40 };
typedef struct qa_q3_weapon_pickup_context { double game_type, weapon_respawn_seconds, team_weapon_respawn_seconds; } qa_q3_weapon_pickup_context;
typedef struct qa_q3_powerup_sight_trace { double fraction; } qa_q3_powerup_sight_trace;
typedef struct qa_q3_powerup_pickup_context {
  double time, game_type; qa_q3_game_client *const *clients; size_t client_count;
  void *trace_ctx; qa_q3_powerup_sight_trace (*trace_solid_line)(void *ctx, qa_vec3 start, qa_vec3 end);
} qa_q3_powerup_pickup_context;
typedef struct qa_q3_item_pickup_context {
  qa_q3_weapon_pickup_context weapon; qa_q3_powerup_pickup_context powerup;
  void *handicap_ctx; const char *(*handicap_for_client)(void *ctx, double client_num);
} qa_q3_item_pickup_context;
void qa_q3_item_pickup_set_item_list_mirror(qa_q3_game_product product, const qa_q3_game_item_definition_mirror *items, size_t count);
void qa_q3_add_ammo(qa_q3_game_entity *other, double weapon, double count);
double qa_q3_pickup_ammo(qa_q3_game_entity *item_entity, qa_q3_game_entity *other);
typedef struct qa_q3_weapon_pickup_quantity_input { double count, quantity; bool dropped; double game_type, current_ammo; } qa_q3_weapon_pickup_quantity_input;
double qa_q3_weapon_pickup_quantity(qa_q3_weapon_pickup_quantity_input input);
double qa_q3_weapon_respawn_seconds(qa_q3_weapon_pickup_context context);
double qa_q3_item_respawn_seconds(const qa_q3_game_item_definition_mirror *item, qa_q3_weapon_pickup_context context);
double qa_q3_pickup_weapon(qa_q3_game_entity *item_entity, qa_q3_game_entity *other, qa_q3_weapon_pickup_context context);
double qa_q3_pickup_health(qa_q3_game_entity *item_entity, qa_q3_game_entity *other);
double qa_q3_pickup_armor(qa_q3_game_entity *item_entity, qa_q3_game_entity *other);
double qa_q3_pickup_holdable(qa_q3_game_entity *item_entity, qa_q3_game_entity *other);
double qa_q3_pickup_persistent_powerup(qa_q3_game_entity *item_entity, qa_q3_game_entity *other, const char *raw_handicap);
double qa_q3_pickup_powerup(qa_q3_game_entity *item_entity, qa_q3_game_entity *other, qa_q3_powerup_pickup_context context);
double qa_q3_pickup_item(qa_q3_game_entity *item_entity, qa_q3_game_entity *other, qa_q3_item_pickup_context context);
#endif
