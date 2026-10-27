/* Port of src/content/q3/foundation/weapon-behavior.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_FOUNDATION_WEAPON_BEHAVIOR_H
#define QA_CONTENT_Q3_FOUNDATION_WEAPON_BEHAVIOR_H
#include "qa/contracts/gameplay.h"
#include "qa/contracts/weapon_behavior.h"
typedef struct qa_q3_projectile_behavior_result { qa_item_id weapon; qa_projectile_role role; } qa_q3_projectile_behavior_result;
qa_q3_projectile_behavior_result qa_q3_projectile_behavior(double weapon);
#endif
