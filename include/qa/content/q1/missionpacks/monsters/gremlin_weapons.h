/* Port of src/content/q1/missionpacks/monsters/gremlin_weapons.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_MONSTERS_GREMLIN_WEAPONS_H
#define QA_CONTENT_Q1_MISSIONPACKS_MONSTERS_GREMLIN_WEAPONS_H

#include "qa/content/q1/missionpacks/monsters/runtime.h"

/* TS gremlinWeapon: NULL = TS null. */
const char *qa_q1mpm_gremlin_weapon(qa_q1mpm_mission_monster *monster);
bool qa_q1mpm_gremlin_has_ammo(qa_q1mpm_mission_monster *monster);
bool qa_q1mpm_gremlin_steal(qa_q1mpm_mission_monster *monster);
void qa_q1mpm_gremlin_fire_nail(qa_q1mpm_mission_monster *monster);
void qa_q1mpm_gremlin_fire_laser(qa_q1mpm_mission_monster *monster);
void qa_q1mpm_gremlin_fire_lightning(qa_q1mpm_mission_monster *monster);
bool qa_q1mpm_gremlin_weapon_attack(qa_q1mpm_mission_monster *monster);
void qa_q1mpm_gremlin_drop_backpack(qa_q1mpm_mission_monster *monster);

#endif
