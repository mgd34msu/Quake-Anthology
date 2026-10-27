/* Port of src/content/q1/foundation/weapons.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_FOUNDATION_WEAPONS_H
#define QA_CONTENT_Q1_FOUNDATION_WEAPONS_H

#include "qa/content/q1/foundation/entity_services.h"

const char *qa_q1_weapon_model(qa_q1_weapon weapon);
qa_q1_weapon qa_q1_best_weapon(qa_q1_services *game, qa_owned_actor actor);
qa_vec3 qa_q1_aim(qa_q1_services *game, qa_owned_actor actor, qa_vec3 forward);
void qa_q1_fire_bullets(qa_q1_services *game, qa_owned_actor shooter, qa_vec3 direction, qa_vec3 view_angles,
                        double count, double spread_x, double spread_y, qa_q1_weapon weapon);
void qa_q1_projectile_touch(qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                            qa_vec3 normal, const qa_touch_surface *surface);
bool qa_q1_fire_weapon(qa_q1_services *game, qa_q1_player_state *player);
bool qa_q1_fire_base_weapon(qa_q1_services *game, qa_q1_player_state *player);
void qa_q1_register_weapon_callbacks(qa_q1_services *game);

#endif
