/*
 * Port of src/persistence/q2-monsters.ts. Monster state and perception checkpoints.
 * Readers borrow strings/bytes from the checkpoint tree; list
 * fields are malloc'd arrays the caller frees (free(3) each array).
 * Decoded top-level checkpoints own their parsed tree in `root`
 * (release with qa_save_value_free). Encoders return malloc'd
 * bytes (NULL only on OOM). Checkpoint structs mirror the
 * content/q2 TS interfaces named in the TS imports, which have
 * no C module yet; string-union fields keep their TS spelling.
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q2_MONSTERS_H
#define QA_PERSISTENCE_Q2_MONSTERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/math.h"
#include "qa/contracts/session.h"
#include "qa/persistence/q2_foundation.h"
#include "qa/persistence/shared.h"
#include "qa/persistence/value.h"

typedef struct qa_q2_fly_pathing {
  qa_vec3 first_move_point;
  qa_vec3 second_move_point;
  bool traversal_pending;
} qa_q2_fly_pathing;

typedef struct qa_q2_alternate_fly_state {
  bool alternate_fly;
  double fly_min_distance;
  double fly_max_distance;
  double fly_acceleration;
  double fly_speed;
  qa_vec3 fly_ideal_position;
  double fly_position_time;
  bool fly_buzzard;
  bool fly_above;
  bool fly_pinned;
  bool fly_thrusters;
  double fly_recovery_time;
  qa_vec3 fly_recovery_direction;
  bool hint_path;
  bool has_pathing;
  qa_q2_fly_pathing pathing;
} qa_q2_alternate_fly_state;

typedef struct qa_q2_monster_sound_target {
  qa_saved_actor_id actor;
  qa_saved_actor_id owner;
  qa_vec3 origin;
  double time;
} qa_q2_monster_sound_target;

typedef struct qa_q2_monster_state_checkpoint {
  const char *initial_power_armor_type;
  double max_power_armor_power;
  double base_health;
  double health_scaling;
  qa_q2_alternate_fly_state fly;
  const char *kind;
  const char *weapon;
  const char *locomotion;
  bool has_melee;
  bool has_ranged_attack;
  bool has_idle;
  bool has_search;
  bool blind_fire;
  bool good_guy;
  bool target_anger;
  bool ignore_shots;
  bool do_not_count;
  const char *spawned_by;
  bool has_commander;
  qa_saved_actor_id commander;
  double monster_slots;
  double monster_used;
  bool brutal;
  bool medic;
  bool resurrecting;
  const char *move;
  const char *next_move;
  double next_frame;
  double next_move_time;
  double scale;
  double gib_health;
  bool can_take_damage;
  bool dead;
  bool corpse;
  bool gibbed;
  bool stand_ground;
  bool temporary_stand_ground;
  bool hold_frame;
  bool ducked;
  bool dodging;
  bool charging;
  bool manual_steering;
  bool combat_point;
  const char *attack_state;
  bool lefty;
  double ideal_yaw;
  double yaw_speed;
  double pause_time;
  double idle_time;
  double pain_time;
  double fire_wait;
  double duck_wait;
  double next_duck_time;
  double dodge_time;
  double attack_finished;
  double check_attack_time;
  double strafe_time;
  bool had_visibility;
  bool close_sight_tripped;
  double melee_time;
  double search_time;
  double trail_time;
  double show_hostile;
  qa_vec3 last_sighting;
  bool has_saved_goal;
  qa_vec3 saved_goal;
  bool lost_sight;
  bool pursue_next;
  bool pursue_temporary;
  bool pursuit_last_seen;
  qa_vec3 blind_fire_target;
  double blind_fire_delay;
  bool has_sound_target;
  qa_q2_monster_sound_target sound_target;
  bool has_old_enemy;
  qa_saved_actor_id old_enemy;
  bool has_move_target;
  qa_saved_actor_id move_target;
  const char *combat_target;
  bool cocked;
  bool force_refire;
  double normal_height;
  double air_finished;
  double environmental_damage_time;
  double water_level;
  double water_type;
  double last_link_count;
  double jump_time;
  bool has_flies_time;
  double flies_time;
} qa_q2_monster_state_checkpoint;

typedef struct qa_q2_saved_sighting {
  qa_saved_actor_id actor;
  double time;
} qa_q2_saved_sighting;

typedef struct qa_q2_saved_noise {
  qa_saved_actor_id actor;
  double time;
  qa_saved_actor_id owner;
  qa_vec3 origin;
} qa_q2_saved_noise;

typedef struct qa_q2_alerted_entry {
  qa_saved_actor_id actor;
  qa_q2_saved_sighting sighting;
} qa_q2_alerted_entry;

typedef struct qa_q2_noise_entry {
  qa_saved_actor_id actor;
  qa_saved_actor_id primary;
  qa_saved_actor_id secondary;
} qa_q2_noise_entry;

typedef struct qa_q2_trail_point {
  qa_vec3 origin;
  double time;
  double yaw;
} qa_q2_trail_point;

typedef struct qa_q2_trail_entry {
  qa_saved_actor_id actor;
  qa_q2_trail_point *points; size_t points_count;
} qa_q2_trail_entry;

typedef struct qa_q2_player_origin_entry {
  qa_saved_actor_id actor;
  qa_vec3 origin;
} qa_q2_player_origin_entry;

typedef struct qa_q2_monster_perception_checkpoint {
  bool has_sight_client;
  qa_saved_actor_id sight_client;
  bool has_sight;
  qa_q2_saved_sighting sight;
  qa_q2_alerted_entry *alerted; size_t alerted_count;
  bool has_primary;
  qa_q2_saved_noise primary;
  bool has_secondary;
  qa_q2_saved_noise secondary;
  qa_q2_noise_entry *noises; size_t noises_count;
  qa_q2_trail_entry *trails; size_t trails_count;
  qa_q2_player_origin_entry *player_origins; size_t player_origins_count;
  qa_q2_saved_sighting *hostile; size_t hostile_count;
  bool has_last_frame;
  double last_frame;
} qa_q2_monster_perception_checkpoint;

typedef struct qa_q2_monster_reaction {
  double damage;
  double kick;
  qa_vec3 point;
  bool has_attacker;
  qa_saved_actor_id attacker;
  bool has_inflictor;
  qa_saved_actor_id inflictor;
} qa_q2_monster_reaction;

typedef struct qa_q2_monster_damage_checkpoint {
  qa_q2_monster_reaction reaction;
  bool has_attack;
  qa_persist_q2_attack_checkpoint attack;
} qa_q2_monster_damage_checkpoint;

typedef struct qa_q2_monster_actor {
  qa_saved_actor_id actor;
  const char *definition;
  qa_q2_monster_state_checkpoint state;
  bool has_pending_damage;
  qa_q2_monster_damage_checkpoint pending_damage;
} qa_q2_monster_actor;

typedef struct qa_q2_monsters_checkpoint {
  double version;
  qa_q2_monster_actor *actors; size_t actors_count;
  qa_q2_monster_perception_checkpoint perception;
  qa_save_value *root; /* owned when decoded; NULL from qa_read_* */
} qa_q2_monsters_checkpoint;

qa_q2_monsters_checkpoint qa_read_q2_monsters_checkpoint(const qa_save_reader *reader);
uint8_t *qa_encode_q2_monsters_checkpoint(const qa_q2_monsters_checkpoint *checkpoint, size_t *out_length);
bool qa_decode_q2_monsters_checkpoint(const uint8_t *bytes, size_t length, qa_q2_monsters_checkpoint *out);

#endif
