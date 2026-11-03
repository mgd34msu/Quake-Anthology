/* Port of src/content/q1/addons/items/pickups.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_ITEMS_PICKUPS_H
#define QA_CONTENT_Q1_ADDONS_ITEMS_PICKUPS_H
#include "qa/content/q1/addons/context.h"
#define QA_Q1AD_MG3_BLOODY_SHOTGUN 1
#define QA_Q1AD_MG3_BLOODY_SUPER_SHOTGUN 2
double qa_q1ad_mg3_weapon_rank(qa_q1_weapon weapon);
void qa_q1ad_register_mg3_pickups(qa_q1ad_context *context);
#endif
