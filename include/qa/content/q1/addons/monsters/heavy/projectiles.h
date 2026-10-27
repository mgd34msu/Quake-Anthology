/* Port of src/content/q1/addons/monsters/heavy/projectiles.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_HEAVY_PROJECTILES_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_HEAVY_PROJECTILES_H

#include "qa/content/q1/addons/monsters/ai/controller.h"

qa_q1am_actor *qa_q1am_heavy_spike(qa_q1am_game *game, qa_actor_id owner, qa_vec3 origin, qa_vec3 velocity);
void qa_q1am_register_heavy_projectiles(qa_q1am_game *game);
void qa_q1am_heavy_lightning_damage(qa_q1am_game *game, qa_actor_id actor, qa_vec3 start, qa_vec3 end, double damage);

#endif
