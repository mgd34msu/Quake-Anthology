/*
 * Port of src/persistence/q2-foundation.ts. Attack provenance and per-entity foundation checkpoints.
 * Readers borrow strings/bytes from the checkpoint tree; list
 * fields are malloc'd arrays the caller frees (free(3) each array).
 * Decoded top-level checkpoints own their parsed tree in `root`
 * (release with qa_save_value_free). Encoders return malloc'd
 * bytes (NULL only on OOM). Checkpoint structs mirror the
 * content/q2 TS interfaces named in the TS imports, which have
 * no C module yet; string-union fields keep their TS spelling.
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q2_FOUNDATION_H
#define QA_PERSISTENCE_Q2_FOUNDATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/math.h"
#include "qa/contracts/session.h"
#include "qa/persistence/shared.h"
#include "qa/persistence/value.h"

typedef struct qa_q2_native_cause_checkpoint {
  const char *edition; /* "classic" | "rerelease" */
  const char *game; /* classic: "base"|"xatrix"|"rogue"|"ctf" */
  double value; /* classic */
  double id; bool friendly_fire; bool no_point_loss; /* rerelease */
} qa_q2_native_cause_checkpoint;
typedef enum qa_q2_attack_cause_kind {
  QA_Q2_ATTACK_CAUSE_Q1, QA_Q2_ATTACK_CAUSE_Q2, QA_Q2_ATTACK_CAUSE_Q3,
  QA_Q2_ATTACK_CAUSE_ENVIRONMENT
} qa_q2_attack_cause_kind;
/* TS AttackProvenance["cause"] union; only the fields of `kind` apply. */
typedef struct qa_q2_attack_cause {
  qa_q2_attack_cause_kind kind;
  const char *death_type; /* q1 */
  const char *armor_effect; /* q1, NULL when absent: "bypass"|"half-effectiveness" */
  double means_of_death; double damage_flags; /* q2/q3 */
  bool has_native; qa_q2_native_cause_checkpoint native; /* q2 */
  const char *hazard; /* environment */
} qa_q2_attack_cause;

typedef struct qa_q2_attack_checkpoint {
  double sequence;
  qa_source_time time;
  bool has_attacker;
  qa_saved_actor_id attacker;
  bool has_inflictor;
  qa_saved_actor_id inflictor;
  bool has_originating_projectile;
  qa_saved_actor_id originating_projectile;
  const char *damage_powerup_owner;
  const char *weapon;
  const char *weapon_provider;
  const char *combat_provider;
  const char *inventory_provider;
  const char *movement_provider;
  qa_q2_attack_cause cause;
} qa_q2_attack_checkpoint;

typedef struct qa_q2_spawn_value {
  const char *key;
  const char *value;
} qa_q2_spawn_value;

typedef struct qa_q2_entity_spawn {
  const char *classname;
  double ordinal;
  qa_q2_spawn_value *values; size_t values_count;
} qa_q2_entity_spawn;

typedef struct qa_q2_entity_values {
  const char *classname;
  const char *target;
  const char *targetname;
  const char *killtarget;
  const char *combat_target;
  const char *death_target;
  const char *health_target;
  const char *item_target;
  const char *message;
  const char *model;
  const char *model2;
  const char *model3;
  const char *model4;
  double spawnflags;
  double delay;
  double wait;
  double speed;
  double accel;
  double decel;
  double damage;
  double damage_radius;
  double radius_damage;
  double count;
  double max_health;
  double view_height;
  double frame;
  double old_frame;
  double scale;
  double alpha;
  double skin;
  double effects;
  double render_flags;
  double flags;
  double server_flags;
  double light_level;
  double power_cubes;
  double timestamp;
  const char *noise;
  const char *sound;
  double volume;
  double attenuation;
  double random;
  const char *map;
  double style;
  bool transition_started;
  double clip_mask;
  bool projectile;
  bool dodgeable;
  bool laser_immune;
  bool damageable_target;
  bool visible;
  const char *solid;
  const char *motion;
  double gravity;
  qa_vec3 gravity_vector;
  qa_vec3 angular_velocity;
  qa_vec3 movedir;
  qa_vec3 pos1;
  qa_vec3 pos2;
  bool has_next_think;
  double next_think;
} qa_q2_entity_values;

typedef struct qa_q2_entity_links {
  bool has_activator;
  qa_saved_actor_id activator;
  bool has_enemy;
  qa_saved_actor_id enemy;
  bool has_owner;
  qa_saved_actor_id owner;
  bool has_goal;
  qa_saved_actor_id goal;
  bool has_team_master;
  qa_saved_actor_id team_master;
  bool has_team_chain;
  qa_saved_actor_id team_chain;
  bool has_chain;
  qa_saved_actor_id chain;
  bool has_beam;
  qa_saved_actor_id beam;
  bool has_beam2;
  qa_saved_actor_id beam2;
  bool has_proboscus;
  qa_saved_actor_id proboscus;
} qa_q2_entity_links;

typedef struct qa_q2_entity_callbacks {
  const char *think;
  const char *prethink;
  const char *postthink;
  const char *use;
  const char *touch;
  const char *pain;
  const char *die;
  const char *blocked;
} qa_q2_entity_callbacks;

typedef struct qa_q2_entity_checkpoint {
  qa_saved_actor_id actor;
  bool has_source_slot;
  double source_slot;
  qa_q2_entity_spawn spawn;
  qa_q2_entity_values values;
  qa_q2_entity_links links;
  bool has_last_attack;
  qa_q2_attack_checkpoint last_attack;
  qa_q2_entity_callbacks callbacks;
} qa_q2_entity_checkpoint;

typedef struct qa_q2_freed_slot {
  double slot;
  double time;
} qa_q2_freed_slot;

typedef struct qa_q2_foundation_counters {
  double total_secrets;
  double found_secrets;
  double total_goals;
  double found_goals;
  double total_monsters;
  double killed_monsters;
  double server_flags;
} qa_q2_foundation_counters;

typedef struct qa_q2_foundation_checkpoint {
  double version;
  double next_source_slot;
  double sequence;
  qa_q2_freed_slot *freed_slots; size_t freed_slots_count;
  qa_q2_foundation_counters counters;
  qa_q2_entity_checkpoint *entities; size_t entities_count;
  qa_save_value *root; /* owned when decoded; NULL from qa_read_* */
} qa_q2_foundation_checkpoint;

qa_q2_attack_checkpoint qa_read_q2_attack_checkpoint(const qa_save_reader *reader);
/* Builds the checkpoint tree (C encoder helper); NULL on OOM. */
qa_save_value *qa_q2_attack_checkpoint_value(const qa_q2_attack_checkpoint *checkpoint);
qa_q2_foundation_checkpoint qa_read_q2_foundation_checkpoint(const qa_save_reader *reader);
uint8_t *qa_encode_q2_foundation_checkpoint(const qa_q2_foundation_checkpoint *checkpoint, size_t *out_length);
bool qa_decode_q2_foundation_checkpoint(const uint8_t *bytes, size_t length, qa_q2_foundation_checkpoint *out);

#endif
