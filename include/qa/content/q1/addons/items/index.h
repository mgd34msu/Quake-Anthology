/* Port of src/content/q1/addons/items/index.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_ITEMS_INDEX_H
#define QA_CONTENT_Q1_ADDONS_ITEMS_INDEX_H
#include "qa/content/q1/addons/context.h"
#include "qa/content/q1/addons/items/weapons.h"
/* The rule, extension and weapon tables registerMg3Items gives the game
 * (TS object literals; the game keeps borrowed pointers). */
typedef struct qa_q1ad_mg3_items {
  qa_q1ad_context *context;
  qa_q1_pickup_rules pickup_rules;
  qa_q1_player_extension player_extension;
  qa_q1ad_mg3_weapon_tables weapons;
} qa_q1ad_mg3_items;
/** Register after the shared base arsenal and before player admission/map spawning.
 * Returns the registered tables (NULL only on OOM); destroy after the last use of the game. */
qa_q1ad_mg3_items *qa_q1ad_register_mg3_items(qa_q1ad_context *context);
void qa_q1ad_mg3_items_destroy(qa_q1ad_mg3_items *items);
#endif
