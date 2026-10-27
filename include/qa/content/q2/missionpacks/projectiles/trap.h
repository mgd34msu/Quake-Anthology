/* Port of src/content/q2/missionpacks/projectiles/trap.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_PROJECTILES_TRAP_H
#define QA_CONTENT_Q2_MISSIONPACKS_PROJECTILES_TRAP_H
#include "qa/content/q2/missionpacks/projectiles/nuke.h"

/* TS Q2MissionPackProjectiles extends Q2MissionPackNuke. */
typedef struct qa_q2mp_projectiles {
  qa_q2mp_nuke base;
  qa_q2f_named_think cb_think[18];
  qa_q2f_named_die cb_die[4];
  qa_q2f_think trap_think, trap_rerelease, trap_gib;
  qa_q2f_die trap_die;
} qa_q2mp_projectiles;

void qa_q2mp_projectiles_init(qa_q2mp_projectiles *projectiles, qa_q2mp_projectile_hooks hooks);
qa_q2f_callback_definitions qa_q2mp_projectiles_callbacks(qa_q2mp_projectiles *projectiles);
qa_q2f_entity *qa_q2mp_projectiles_fire_trap(qa_q2mp_projectiles *p, qa_owned_actor self, qa_q2f_game *game,
                                             qa_vec3 start, qa_vec3 direction, double damage, double speed,
                                             double timer, double radius, bool held);
#endif
