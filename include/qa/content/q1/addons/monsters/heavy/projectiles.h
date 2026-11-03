/* Port of src/content/q1/addons/monsters/heavy/projectiles.ts.
 * MG3 launch_spike and LightningDamage source operations. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_HEAVY_PROJECTILES_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_HEAVY_PROJECTILES_H

#include "qa/content/q1/foundation/entity_services.h"

qa_q1_actor *qa_q1am_heavy_spike(qa_q1_services *game, qa_actor_id owner, qa_vec3 origin, qa_vec3 velocity);
void qa_q1am_register_heavy_projectiles(qa_q1_services *game);
void qa_q1am_heavy_lightning_damage(qa_q1_services *game, qa_actor_id actor, qa_vec3 start, qa_vec3 end, double damage);

#endif
