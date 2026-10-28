/* Port of src/content/q3/base/shared/items.ts. GPL-2.0-or-later.
 * Table transcribed by bun JSON dump of itemList("missionpack") (52 rows);
 * base list is the first 36. String fields NULL are TS null. */
#ifndef QA_CONTENT_Q3_BASE_SHARED_ITEMS_H
#define QA_CONTENT_Q3_BASE_SHARED_ITEMS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/trajectory.h"
#include "qa/contracts/math.h"

typedef struct qa_q3_base_item_definition {
  const char *class_name, *pickup_sound;
  const char *world_models[4];
  const char *icon, *pickup_name;
  double quantity;
  double type; /* qa_q3_base_item_type value */
  double tag;  /* Weapon/Powerup/Holdable value per type, else 0 */
  const char *precaches, *sounds;
} qa_q3_base_item_definition;

const qa_q3_base_item_definition *qa_q3_base_item_list(qa_q3_base_product product, size_t *out_count);
const qa_q3_base_item_definition *qa_q3_base_item_at(qa_q3_base_product product, double index);
/* TS find* returning null become NULL. */
const qa_q3_base_item_definition *qa_q3_base_find_item(qa_q3_base_product product, const char *pickup_name);
const qa_q3_base_item_definition *qa_q3_base_find_item_for_powerup(qa_q3_base_product product, qa_q3_powerup powerup);
const qa_q3_base_item_definition *qa_q3_base_find_item_for_holdable(qa_q3_base_product product, qa_q3_holdable holdable);
const qa_q3_base_item_definition *qa_q3_base_find_item_for_weapon(qa_q3_base_product product, qa_q3_weapon weapon);

typedef struct qa_q3_base_pickup_entity { double model_index, model_index2, generic1; } qa_q3_base_pickup_entity;
/* TS PlayerInventory: has_persistent_powerup_index only for missionpack. */
typedef struct qa_q3_base_player_inventory {
  void *ctx;
  qa_q3_base_product product;
  double health, armor, max_health, holdable_item, team;
  bool has_persistent_powerup_index; double persistent_powerup_index;
  double (*ammo)(void *ctx, qa_q3_weapon weapon);
  double (*powerup)(void *ctx, qa_q3_powerup powerup);
} qa_q3_base_player_inventory;
bool qa_q3_base_can_q3_armor_be_grabbed(const qa_q3_base_player_inventory *ps);
bool qa_q3_base_can_item_be_grabbed(double gametype, qa_q3_base_pickup_entity ent, const qa_q3_base_player_inventory *ps);
bool qa_q3_base_player_touches_item(qa_vec3 player_origin, qa_q3_base_trajectory item_position, double at_time);
#endif
