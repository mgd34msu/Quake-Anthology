/* Port of src/content/q1/addons/items/weapons.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_ITEMS_WEAPONS_H
#define QA_CONTENT_Q1_ADDONS_ITEMS_WEAPONS_H
#include "qa/content/q1/addons/context.h"
/* Storage for the tables registerMg3Weapons registers (the game keeps
 * borrowed pointers; TS object literals). Every ctx is the context. */
typedef struct qa_q1ad_mg3_weapon_tables {
  qa_q1_weapon_rules ammunition;
  qa_q1_weapon_definition laser, mjolnir, shotgun, supershotgun;
} qa_q1ad_mg3_weapon_tables;
/* TS mg3HammerBodyFrame: false = null. */
bool qa_q1ad_mg3_hammer_body_frame(qa_q1ad_context *context, qa_q1_player_state *player, double *out);
void qa_q1ad_mg3_weapon_frame(qa_q1ad_context *context, qa_q1_player_state *player);
/* TS registerMg3Weapons(context); tables must outlive the game. */
void qa_q1ad_register_mg3_weapons(qa_q1ad_context *context, qa_q1ad_mg3_weapon_tables *tables);
#endif
