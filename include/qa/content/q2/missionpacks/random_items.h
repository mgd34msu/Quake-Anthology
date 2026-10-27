/* Port of src/content/q2/missionpacks/random-items.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_RANDOM_ITEMS_H
#define QA_CONTENT_Q2_MISSIONPACKS_RANDOM_ITEMS_H
#include <stdbool.h>
#include "qa/content/q2/foundation/weapons/types.h"

typedef struct qa_q2mp_item_definition qa_q2mp_item_definition;

/* TS Q2RandomItemSettings. */
typedef struct qa_q2mp_random_item_settings {
  bool enabled, no_mines, no_nukes, no_spheres;
} qa_q2mp_random_item_settings;

/* NULL return is TS null. */
const char *qa_q2mp_random_item(qa_q2f_entity *entity, const qa_q2mp_item_definition *item, qa_q2f_game *game,
                                qa_q2mp_random_item_settings settings);
#endif
