/* Port of src/compat/q2/native-primary-player.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_PRIMARY_PLAYER_H
#define QA_COMPAT_Q2_NATIVE_PRIMARY_PLAYER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/native_primary_weapons.h"
#include "qa/contracts/content.h"
#include "qa/contracts/source_match.h"

typedef struct qa_compat_q2_native_primary_player_profile {
  qa_content_digest digest; bool has_match; qa_source_primary_match match; double spawn;
  const char *objectives_kind; double objectives_entry; double command_angles, velocity; bool has_forward; double forward;
} qa_compat_q2_native_primary_player_profile;
typedef struct qa_compat_q2_native_primary_player { qa_compat_q2_native_primary_weapon_host host; const qa_compat_q2_native_primary_weapon_profile *weapon; const qa_compat_q2_native_primary_player_profile *profile; } qa_compat_q2_native_primary_player;
typedef struct qa_compat_q2_native_spawn_point { qa_vec3 origin, angles; } qa_compat_q2_native_spawn_point;
void qa_compat_q2_native_primary_player_init(qa_compat_q2_native_primary_player *s, qa_compat_q2_native_primary_weapon_host host, const qa_compat_q2_native_primary_weapon_profile *weapon, const qa_compat_q2_native_primary_player_profile *profile);
double qa_compat_q2_native_primary_player_score(qa_compat_q2_native_primary_player *s, qa_actor_id actor);
void qa_compat_q2_native_primary_player_set_score(qa_compat_q2_native_primary_player *s, qa_actor_id actor, double score);
double qa_compat_q2_native_primary_player_max_health(qa_compat_q2_native_primary_player *s, qa_actor_id actor);
void qa_compat_q2_native_primary_player_set_max_health(qa_compat_q2_native_primary_player *s, qa_actor_id actor, double value);
void qa_compat_q2_native_primary_player_drop_objectives(qa_compat_q2_native_primary_player *s, qa_actor_id actor);
qa_compat_q2_native_spawn_point qa_compat_q2_native_primary_player_spawn_point(qa_compat_q2_native_primary_player *s, qa_actor_id actor);
void qa_compat_q2_native_primary_player_teleport(qa_compat_q2_native_primary_player *s, qa_actor_id actor, qa_vec3 origin, qa_vec3 velocity, qa_vec3 angles, double hold_milliseconds);
#endif
