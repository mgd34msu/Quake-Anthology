/*
 * Port of src/content/q1/missionpacks/types.ts. The mission packs run on
 * the one Q1 game (qa_q1_services, TS Q1EntityServices) and its foundation
 * types; TS string unions (pack, weapon, powerup) stay C strings.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_TYPES_H
#define QA_CONTENT_Q1_MISSIONPACKS_TYPES_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q1/foundation/entity_services.h"

typedef const char *qa_q1mp_pack; /* TS Q1MissionPack: "hipnotic" | "rogue" */

/* TS MissionWeaponDefinition. pickup/ammo NULL = TS null. */
typedef struct qa_q1mp_mission_weapon_definition {
  const char *id;
  const char *model;
  const char *pickup; /* nullable */
  const char *world_model;
  const char *ammo; /* nullable, TS ItemId */
  double pickup_ammo;
  double rank;
} qa_q1mp_mission_weapon_definition;
#define QA_Q1MP_MISSION_WEAPONS_COUNT 8
extern const qa_q1mp_mission_weapon_definition qa_q1mp_mission_weapons[QA_Q1MP_MISSION_WEAPONS_COUNT];

/* actor NULL = TS null. */
void qa_q1mp_set_mission_reference(qa_q1_actor *entity, const char *key, const qa_actor_id *actor);
/* False = TS null (absent, null or no longer live). */
bool qa_q1mp_mission_reference(qa_q1_services *game, qa_q1_actor *entity, const char *key, qa_actor_id *out);
void qa_q1mp_set_mission_number(qa_q1_actor *entity, const char *key, double value);
qa_vec3 qa_q1mp_velocity_angles(qa_vec3 velocity);
qa_vec3 qa_q1mp_grenade_velocity(qa_q1_services *game, qa_vec3 angles, qa_vec3 aimed);
void qa_q1mp_move_missile(qa_q1_services *game, qa_q1_actor *entity, qa_vec3 velocity);

#endif
