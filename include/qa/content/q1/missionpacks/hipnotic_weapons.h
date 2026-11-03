/* Port of src/content/q1/missionpacks/hipnotic-weapons.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_HIPNOTIC_WEAPONS_H
#define QA_CONTENT_Q1_MISSIONPACKS_HIPNOTIC_WEAPONS_H
#include "qa/content/q1/missionpacks/types.h"

/* TS HipnoticLaserProfile. */
typedef struct qa_q1mp_hipnotic_laser_profile { qa_q1_weapon weapon; double damage; double light_damage; } qa_q1mp_hipnotic_laser_profile;
extern const qa_q1mp_hipnotic_laser_profile qa_q1mp_hipnotic_laser_default;
/* profile NULL = TS default profile; light false = TS default. */
qa_q1_actor *qa_q1mp_launch_hipnotic_laser(qa_q1_services *game, qa_actor_id shooter, qa_vec3 origin, qa_vec3 direction,
                                           bool light, const qa_q1mp_hipnotic_laser_profile *profile);
bool qa_q1mp_fire_hipnotic_laser(qa_q1_services *game, qa_q1_player_state *player);
qa_q1_actor *qa_q1mp_launch_hipnotic_proximity(qa_q1_services *game, qa_actor_id owner, qa_vec3 origin, qa_vec3 velocity);
bool qa_q1mp_fire_hipnotic_proximity(qa_q1_services *game, qa_q1_player_state *player);
bool qa_q1mp_fire_hipnotic_mjolnir(qa_q1_services *game, qa_q1_player_state *player);
/* weapon NULL = TS default "hipnotic:mjolnir". */
void qa_q1mp_spawn_hipnotic_hammer_base(qa_q1_services *game, qa_q1_player_state *player, qa_vec3 origin, qa_q1_weapon weapon);
void qa_q1mp_register_hipnotic_laser_callbacks(qa_q1_services *game);
void qa_q1mp_register_hipnotic_weapon_callbacks(qa_q1_services *game);
void qa_q1mp_register_hipnotic_hammer_callbacks(qa_q1_services *game);

#endif
