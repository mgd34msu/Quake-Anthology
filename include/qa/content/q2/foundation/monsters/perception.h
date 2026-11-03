/* Port of src/content/q2/foundation/monsters/perception.ts.
 * MonsterMission is the landed qa_monster_mission; the mission lookup
 * and context map are fn+ctx. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_MONSTERS_PERCEPTION_H
#define QA_CONTENT_Q2_FOUNDATION_MONSTERS_PERCEPTION_H
#include "qa/content/monsters/authored.h"
#include "qa/content/q2/foundation/monsters/checkpoint.h"

bool qa_q2m_facing_ideal(qa_q2m_context *context);
typedef struct qa_q2m_attack_chance_profile {
  double stand_ground, melee, near, mid, far, strafe_scalar;
} qa_q2m_attack_chance_profile;
bool qa_q2m_default_check_attack(qa_q2m_context *context, const qa_q2m_attack_chance_profile *profile); /* NULL = normal */

typedef struct qa_q2m_sighting { qa_actor_id actor; double time; } qa_q2m_sighting;
typedef struct qa_q2m_noise_sighting { qa_actor_id actor, owner; qa_vec3 origin; double time; } qa_q2m_noise_sighting;
typedef struct qa_q2m_sighting_entry { qa_actor_id actor; qa_q2m_sighting sighting; } qa_q2m_sighting_entry;
typedef struct qa_q2m_noise_pair {
  qa_actor_id actor, primary, secondary;
  bool has_primary_record, has_secondary_record; /* false = TS null */
  qa_q2m_noise_sighting primary_record, secondary_record;
} qa_q2m_noise_pair;
typedef struct qa_q2m_trail { qa_actor_id actor; qa_q2m_trail_point *points; size_t count, cap; } qa_q2m_trail;
typedef struct qa_q2m_actor_vec { qa_actor_id actor; qa_vec3 origin; } qa_q2m_actor_vec;
typedef struct qa_q2m_actor_time { qa_actor_id actor; double time; } qa_q2m_actor_time;

typedef struct qa_q2m_context_lookup {
  void *ctx;
  qa_q2m_context *(*get)(void *ctx, qa_actor_id actor); /* NULL = absent */
  /* Iteration over alerted map values etc. stays internal; mission: */
  const qa_monster_mission *(*mission)(void *ctx, qa_actor_id actor); /* NULL = none */
} qa_q2m_context_lookup;

typedef struct qa_q2m_perception {
  qa_q2m_context_lookup contexts;
  qa_q2_game_services *services; /* nullable */
  bool has_sight_client; qa_actor_id sight_client;
  bool has_sight; qa_q2m_sighting sight;
  qa_q2m_sighting_entry *alerted; size_t alerted_count, alerted_cap;
  bool has_primary, has_secondary;
  qa_q2m_noise_sighting primary, secondary;
  qa_q2m_noise_pair *noises; size_t noise_count, noise_cap;
  qa_q2m_trail *trails; size_t trail_count, trail_cap;
  qa_q2m_actor_vec *player_origins; size_t origin_count, origin_cap;
  qa_q2m_actor_time *hostile; size_t hostile_count, hostile_cap;
  double last_frame; /* -inf initially */
  qa_q2m_combat_rules source_combat_rules;
  const qa_q2m_source_combat_hooks *source_combat_hooks; /* nullable */
  const qa_q2m_hint_hooks *hint_hooks; /* nullable */
} qa_q2m_perception;

bool qa_q2m_perception_init(qa_q2m_perception *p, qa_q2m_context_lookup contexts);
void qa_q2m_perception_destroy(qa_q2m_perception *p);
void qa_q2m_perception_set_source_combat_rules(qa_q2m_perception *p, qa_q2m_combat_rules rules,
                                               const qa_q2m_source_combat_hooks *hooks);
void qa_q2m_perception_set_hint_paths(qa_q2m_perception *p, const qa_q2m_hint_hooks *hooks);
bool qa_q2m_perception_current_sight_client(qa_q2m_perception *p, qa_actor_id *out);
void qa_q2m_perception_bind(qa_q2m_perception *p, qa_q2_game_services *game);
/* capture: arrays malloc'd (trail points too); free with _free. */
bool qa_q2m_perception_capture(qa_q2m_perception *p, qa_q2m_perception_checkpoint *out);
void qa_q2m_perception_checkpoint_free(qa_q2m_perception_checkpoint *checkpoint);
void qa_q2m_perception_restore(qa_q2m_perception *p, qa_q2_game_services *game, const qa_q2m_perception_checkpoint *checkpoint);
void qa_q2m_perception_release(qa_q2m_perception *p, qa_actor_id actor);
void qa_q2m_perception_begin_frame(qa_q2m_perception *p, qa_q2_game_services *game);
void qa_q2m_perception_report_noise(qa_q2m_perception *p, qa_actor_id actor, qa_vec3 origin, bool secondary);
bool qa_q2m_perception_find_target(qa_q2m_perception *p, qa_q2m_context *context);
void qa_q2m_perception_hunt_target(qa_q2m_perception *p, qa_q2m_context *context);
void qa_q2m_perception_found_target(qa_q2m_perception *p, qa_q2m_context *context);
void qa_q2m_perception_react_to_damage(qa_q2m_perception *p, qa_q2m_context *context, const qa_actor_id *attacker);
typedef struct qa_q2m_check_attack_fn {
  void *ctx;
  bool (*fn)(void *ctx, qa_q2m_context *context);
} qa_q2m_check_attack_fn;
bool qa_q2m_perception_check_attack(qa_q2m_perception *p, qa_q2m_context *context, qa_q2m_check_attack_fn check);
bool qa_q2m_perception_move_to_goal(qa_q2m_perception *p, qa_q2m_context *context, double distance);
#endif
