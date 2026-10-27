/* Port of src/content/q2/missionpacks/projectiles/common.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_PROJECTILES_COMMON_H
#define QA_CONTENT_Q2_MISSIONPACKS_PROJECTILES_COMMON_H
#include <stdbool.h>
#include "qa/content/q2/foundation/weapons/ballistics.h"

#define QA_Q2MP_SHOT_MASK ((double)0x6000003)
double qa_q2mp_projectile_mask(qa_q2f_game *game);
/* TS freeProjectile think value. */
qa_q2f_think qa_q2mp_free_projectile_think(void);
void qa_q2mp_free_projectile_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game);

/* TS projectile(): self is Pick<Q2Entity, "actor">. */
qa_q2f_entity *qa_q2mp_projectile(qa_owned_actor self, qa_q2f_game *game, const char *classname, qa_vec3 start,
                                  qa_vec3 direction, double speed, const char *model, qa_q2f_motion_kind motion,
                                  double effects);
/* TS publishProjectile behavior argument; has_behavior false = absent.
 * sound "" = TS default/silent. */
typedef struct qa_q2mp_projectile_behavior { qa_item_id weapon; qa_projectile_role role; } qa_q2mp_projectile_behavior;
void qa_q2mp_publish_projectile(qa_q2f_entity *entity, qa_q2f_game *game, const char *sound,
                                bool has_behavior, qa_q2mp_projectile_behavior behavior);
void qa_q2mp_effect(qa_q2f_entity *entity, qa_q2f_game *game, const char *name, qa_vec3 direction,
                    double count, double color);
void qa_q2mp_explode(qa_q2f_entity *entity, qa_q2f_game *game, const char *name);
void qa_q2mp_explode_default(qa_q2f_entity *entity, qa_q2f_game *game);
bool qa_q2mp_sight(qa_q2f_game *game, qa_q2f_entity *from, qa_actor_id target);
void qa_q2mp_velocity(qa_q2f_game *game, qa_actor_id actor, qa_vec3 value, bool lift_ground);
#endif
