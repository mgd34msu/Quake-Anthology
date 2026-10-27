/* Port of src/content/q1/addons/items/weapons.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_ITEMS_WEAPONS_H
#define QA_CONTENT_Q1_ADDONS_ITEMS_WEAPONS_H
#include "qa/content/q1/addons/context.h"
bool qa_q1ad_mg3_hammer_body_frame(qa_q1ad_context *context, qa_q1ad_player *player, double *out);
void qa_q1ad_mg3_weapon_frame(qa_q1ad_context *context, qa_q1ad_player *player);
void qa_q1ad_register_mg3_weapons(qa_q1ad_context *context);
/* Registered weapon/rule closures. */
bool qa_q1ad_mg3_fire_laser(qa_q1ad_context *context, qa_q1ad_player *player);
bool qa_q1ad_mg3_fire_hammer(qa_q1ad_context *context, qa_q1ad_player *player);
bool qa_q1ad_mg3_fire_shotgun(qa_q1ad_context *context, qa_q1ad_player *player);
bool qa_q1ad_mg3_consume_ammo(qa_q1ad_context *context, qa_q1ad_player *player, qa_item_id item, double amount);
const char *qa_q1ad_mg3_hammer_model(qa_q1ad_context *context, qa_q1ad_player *player);
const char *qa_q1ad_mg3_shotgun_model(qa_q1ad_context *context, qa_q1ad_player *player, const char *weapon);
bool qa_q1ad_mg3_shotgun_best_available(qa_q1ad_context *context, qa_q1ad_player *player, const char *weapon);
void qa_q1ad_mg3_hammer_animate(qa_q1ad_context *context, qa_q1ad_player *player, double seconds);
#endif
