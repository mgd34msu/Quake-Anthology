/* Port of src/content/q1/addons/monsters/bosses/oldnew-projectiles.ts.
 * mg3_oldone_new.qc projectiles and sphere managers. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_BOSSES_OLDNEW_PROJECTILES_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_BOSSES_OLDNEW_PROJECTILES_H

#include "qa/content/q1/addons/context.h"

#define QA_Q1AM_OLDNEW_PREFIX "mg3:bosses:" /* TS oldnewPrefix */

void qa_q1am_boss_later(qa_q1ad_context *context, qa_q1_actor *entity, const char *name, double delay);
/* TS bossEnemy: entity.monster?.enemy ?? references.get("enemy") ?? null; false = null. */
bool qa_q1am_boss_enemy(qa_q1ad_context *context, qa_q1_actor *entity, qa_actor_id *out);
qa_actor_id qa_q1am_boss_world(qa_q1ad_context *context);
qa_vec3 qa_q1am_boss_target(qa_q1ad_context *context, qa_q1_actor *entity);
qa_q1_actor *qa_q1am_spawn_sphere_manager(qa_q1ad_context *context, qa_q1_actor *source, double maximum);
qa_q1_actor *qa_q1am_spawn_sphere_chunk_manager(qa_q1ad_context *context, qa_q1_actor *source, double maximum);
void qa_q1am_auto_gun(qa_q1ad_context *context, qa_q1_actor *source, qa_vec3 origin, double offset);
qa_q1_actor *qa_q1am_spawn_boss_teledeath(qa_q1ad_context *context, qa_vec3 origin, qa_q1_actor *owner);
void qa_q1am_register_oldnew_projectiles(qa_q1ad_context *context);

#endif
