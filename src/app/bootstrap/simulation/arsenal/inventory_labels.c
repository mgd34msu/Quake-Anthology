/* Port of src/app/bootstrap/simulation/arsenal/inventory-labels.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/arsenal/inventory_labels.h"
#include <string.h>
#include "qa/content/q2/foundation/items.h"
#include "qa/content/q2/missionpacks/items.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/items.h"
#include "qa/content/q3/foundation/arsenal.h"
#include "qa/foundation/support.h"
static const struct { const char *item, *label; } Q1_AMMO[] = {
  {"q1:ammo/shells", "Shells"}, {"q1:ammo/nails", "Nails"}, {"q1:ammo/rockets", "Rockets"}, {"q1:ammo/cells", "Cells"},
  {"rogue:ammo/lava-nails", "Lava Nails"}, {"rogue:ammo/multi-rockets", "Multi Rockets"}, {"rogue:ammo/plasma", "Plasma"},
};
const char *qa_app_selected_ammo_label(const char *family, qa_item_id item) {
  const char *label = NULL;
  if (family && strcmp(family, "q1") == 0) {
    for (size_t i = 0; i < sizeof Q1_AMMO / sizeof *Q1_AMMO; i++)
      if (item && strcmp(item, Q1_AMMO[i].item) == 0) label = Q1_AMMO[i].label;
  } else if (family && strcmp(family, "q2") == 0) {
    label = qa_q2_item_pickup_name(item && strlen(item) >= 3 ? item + 3 : "");
    if (!label) label = qa_q2mp_mission_weapon_display_name(item && strlen(item) >= strlen("q2:ammo_") ? item + strlen("q2:ammo_") : "");
  } else {
    const qa_q3_weapon_item *weapon = NULL;
    for (size_t i = 0; i < QA_Q3_WEAPON_ITEM_COUNT; i++)
      if (qa_q3_weapon_items[i].ammo && item && strcmp(qa_q3_weapon_items[i].ammo, item) == 0) weapon = &qa_q3_weapon_items[i];
    size_t count = 0;
    const qa_q3_base_item_definition *items = qa_q3_base_item_list(QA_Q3_BASE_PRODUCT_MISSIONPACK, &count);
    for (size_t i = 0; i < count; i++)
      if (items[i].type == QA_Q3_BASE_IT_AMMO && weapon && items[i].tag == weapon->weapon) label = items[i].pickup_name;
  }
  if (!label) qa_range_error("Selected ammunition lacks a source catalog label: %s", item ? item : "");
  return label;
}
