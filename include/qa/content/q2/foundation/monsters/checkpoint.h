/* Port of src/content/q2/foundation/monsters/checkpoint.ts (types only).
 * Pending attacks use TS Q2AttackCheckpoint (qa_q2_attack_checkpoint,
 * content/q2/foundation/checkpoint.h). GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_MONSTERS_CHECKPOINT_H
#define QA_CONTENT_Q2_FOUNDATION_MONSTERS_CHECKPOINT_H
#include "qa/content/q2/foundation/checkpoint.h"
#include "qa/content/q2/foundation/monsters/types.h"
#include "qa/contracts/session.h"

/* Q2MonsterStateCheckpoint: qa_q2m_state with move/soundTarget/oldEnemy/
 * moveTarget/commander saved by name/id. The embedded `rest` is the
 * state with move/next_move NULL and the has_* link flags cleared. */
typedef struct qa_q2m_saved_sound_target {
  qa_saved_actor_id actor, owner;
  qa_vec3 origin;
  double time;
} qa_q2m_saved_sound_target;
typedef struct qa_q2m_state_checkpoint {
  qa_q2m_state rest;
  const char *move;
  const char *next_move; /* nullable */
  bool has_sound_target; qa_q2m_saved_sound_target sound_target;
  bool has_old_enemy; qa_saved_actor_id old_enemy;
  bool has_move_target; qa_saved_actor_id move_target;
  bool has_commander; qa_saved_actor_id commander;
} qa_q2m_state_checkpoint;

/* Q2MonsterDamageCheckpoint reaction: DeathReaction minus self/attack
 * with attacker/inflictor saved. */
typedef struct qa_q2m_damage_checkpoint {
  double kick, damage;
  qa_vec3 point;
  bool has_attacker; qa_saved_actor_id attacker;
  bool has_inflictor; qa_saved_actor_id inflictor;
  bool has_attack; qa_q2_attack_checkpoint attack;
} qa_q2m_damage_checkpoint;
typedef struct qa_q2m_damage_checkpoint_result { bool has_value; qa_q2m_damage_checkpoint value; } qa_q2m_damage_checkpoint_result;

typedef struct qa_q2m_saved_sighting { qa_saved_actor_id actor; double time; } qa_q2m_saved_sighting;
typedef struct qa_q2m_saved_sighting_result { bool has_value; qa_q2m_saved_sighting value; } qa_q2m_saved_sighting_result;
typedef struct qa_q2m_saved_noise { qa_saved_actor_id actor, owner; qa_vec3 origin; double time; } qa_q2m_saved_noise;
typedef struct qa_q2m_saved_noise_result { bool has_value; qa_q2m_saved_noise value; } qa_q2m_saved_noise_result;
typedef struct qa_q2m_saved_actor_result { bool has_value; qa_saved_actor_id value; } qa_q2m_saved_actor_result;
typedef struct qa_q2m_alerted_entry { qa_saved_actor_id actor; qa_q2m_saved_sighting sighting; } qa_q2m_alerted_entry;
/* noises entry: the player's two noise entities and its most recent record of each kind. */
typedef struct qa_q2m_noise_pair_entry {
  qa_saved_actor_id actor, primary, secondary;
  qa_q2m_saved_noise_result primary_record, secondary_record;
} qa_q2m_noise_pair_entry;
typedef struct qa_q2m_trail_point { qa_vec3 origin; double time, yaw; } qa_q2m_trail_point;
typedef struct qa_q2m_trail_entry { qa_saved_actor_id actor; const qa_q2m_trail_point *points; size_t point_count; } qa_q2m_trail_entry;
typedef struct qa_q2m_origin_entry { qa_saved_actor_id actor; qa_vec3 origin; } qa_q2m_origin_entry;
typedef struct qa_q2m_hostile_entry { qa_saved_actor_id actor; double time; } qa_q2m_hostile_entry;

typedef struct qa_q2m_perception_checkpoint {
  qa_q2m_saved_actor_result sight_client;
  qa_q2m_saved_sighting_result sight;
  const qa_q2m_alerted_entry *alerted; size_t alerted_count;
  qa_q2m_saved_noise_result primary, secondary;
  const qa_q2m_noise_pair_entry *noises; size_t noise_count;
  const qa_q2m_trail_entry *trails; size_t trail_count;
  const qa_q2m_origin_entry *player_origins; size_t player_origin_count;
  const qa_q2m_hostile_entry *hostile; size_t hostile_count;
  bool has_last_frame; double last_frame;
} qa_q2m_perception_checkpoint;

typedef struct qa_q2m_actor_checkpoint {
  qa_saved_actor_id actor;
  const char *definition;
  qa_q2m_state_checkpoint state;
  qa_q2m_damage_checkpoint_result pending_damage;
} qa_q2m_actor_checkpoint;
typedef struct qa_q2m_monsters_checkpoint {
  double version; /* literal 1 */
  const qa_q2m_actor_checkpoint *actors; size_t actor_count;
  qa_q2m_perception_checkpoint perception;
} qa_q2m_monsters_checkpoint;
#endif
