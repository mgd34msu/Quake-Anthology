/* Port of src/content/q2/missionpacks/projectiles/nuke.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_PROJECTILES_NUKE_H
#define QA_CONTENT_Q2_MISSIONPACKS_PROJECTILES_NUKE_H
#include "qa/content/q2/missionpacks/projectiles/mines.h"

/* TS Q2MissionPackNuke extends Q2MissionPackMines. */
typedef struct qa_q2mp_nuke {
  qa_q2mp_mines base;
  qa_q2_named_think cb_think[15];
  qa_q2_named_touch cb_touch[10];
  qa_q2_named_die cb_die[3];
  qa_q2_think nuke_think, nuke_quake;
  qa_q2_touch nuke_bounce;
  qa_q2_die nuke_die;
} qa_q2mp_nuke;

void qa_q2mp_nuke_init(qa_q2mp_nuke *nuke, qa_q2mp_projectile_hooks hooks);
qa_q2_callback_definitions qa_q2mp_nuke_callbacks(qa_q2mp_nuke *nuke);
/* TS fireNuke self is a full Q2Entity; only its actor is used. */
qa_q2_entity *qa_q2mp_nuke_fire_nuke(qa_q2mp_nuke *n, qa_owned_actor self, qa_q2_game_services *game, qa_vec3 start,
                                      qa_vec3 direction, double speed, double multiplier);
#endif
