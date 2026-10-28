/*
 * Port of src/persistence/q1-foundation.ts: the Q1 foundation
 * checkpoint reader/encoder.
 * The checkpoint types are TS-owned by src/content/q1/foundation/
 * checkpoint.ts (Q1FoundationCheckpoint, Q1SavedEntity,
 * Q1SavedPlayer, Q1EntitySourceState), types.ts (Q1Weapon,
 * Q1Powerup, Q1PrecacheTables, Q1PlayerState, Q1Basis) and
 * entity.ts (Q1Monster); content/ has no C port yet, so they are
 * mirrored here and reconciled when it lands. String-union fields
 * keep their TS spelling (q2_missionpacks precedent). Readers
 * borrow strings/bytes from the checkpoint tree; list fields are
 * malloc'd arrays released by qa_q1_foundation_checkpoint_free
 * (which also frees the decoded `root`). Reader allocation
 * failure aborts via the reader (shared.h precedent).
 * Encode key order follows the TS capture objects
 * (captureFoundation / saveEntity / captureEntitySourceState in
 * checkpoint.ts); player state and intermission use their TS
 * interface declaration order, the capture spread order not being
 * recoverable from the types alone.
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q1_FOUNDATION_H
#define QA_PERSISTENCE_Q1_FOUNDATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/session.h"
#include "qa/persistence/value.h"

/* Mirrored TS Q1EntitySourceState (checkpoint.ts). */
typedef struct qa_persist_q1_entity_source_state {
  const char *model;
  double frame, skin, effects;
  const char *solid;    /* none|trigger|bbox|slidebox|bsp|corpse */
  const char *movement; /* none|push|step|toss|bounce|fly|flymissile|noclip|gib */
  const char *target;
  const char *targetname;
  const char *killtarget;
  const char *message;
  double delay, spawnflags, sounds, wait, speed, damage, max_health;
  bool aimed_damage;
  double next_think;
  const char *original_model;
  qa_vec3 pos1, pos2, dest1, dest2, movedir, mangle;
  const char *state; /* bottom|up|top|down */
  bool has_trigger_bounds;
  qa_bounds trigger_bounds;
  double attack_finished, count;
  bool activated;
  const char *projectile;        /* nullable: rocket|grenade|spike|superspike */
  const char *projectile_weapon; /* nullable Q1Weapon */
  qa_vec3 angular_velocity;
  double water_level;
  double water_type; /* 0 | -1 | -2 | -3 | -4 | -5 | -6 */
  double movement_flags, ideal_yaw, yaw_speed;
  const char *attack_state; /* straight|melee|missile|dodging */
} qa_persist_q1_entity_source_state;

/* Mirrored TS Q1SavedCallbacks (checkpoint.ts); each nullable. */
typedef struct qa_persist_q1_saved_callbacks {
  const char *think;
  const char *use;
  const char *touch;
  const char *pain;
  const char *die;
  const char *blocked;
  const char *path_end;
} qa_persist_q1_saved_callbacks;

typedef struct qa_persist_q1_saved_field {
  const char *key;
  const char *value;
} qa_persist_q1_saved_field;
typedef struct qa_persist_q1_saved_reference {
  const char *key;
  bool has_actor;
  qa_saved_actor_id actor;
} qa_persist_q1_saved_reference;

/* Mirrored TS Q1Monster with saved-actor enemy fields (entity.ts +
 * checkpoint.ts). */
typedef struct qa_persist_q1_saved_monster {
  const char *species; /* Q1_MONSTER_SPECIES spelling */
  const char *mode;    /* stand|walk|run|attack|leap|pain|death */
  double frame_index;
  double *sequence; /* owned array */
  size_t sequence_count;
  double first_frame;
  bool has_enemy;
  qa_saved_actor_id enemy;
  bool has_old_enemy;
  qa_saved_actor_id old_enemy;
  const char *path;
  double pause_until, attack_finished, pain_finished, search_until;
  bool death_drop, refired;
} qa_persist_q1_saved_monster;

typedef struct qa_q1_saved_move {
  qa_vec3 destination;
  const char *done;
} qa_q1_saved_move;

/* Mirrored TS Q1SavedEntity (checkpoint.ts). */
typedef struct qa_persist_q1_saved_entity {
  qa_saved_actor_id actor;
  qa_provider_id actor_provider;
  bool has_source_slot;
  double source_slot;
  const char *classname;
  bool has_source_ordinal;
  double source_ordinal;
  qa_persist_q1_entity_source_state state;
  qa_persist_q1_saved_field *fields; /* owned array */
  size_t field_count;
  qa_persist_q1_saved_reference *references; /* owned array */
  size_t reference_count;
  bool has_owner;
  qa_saved_actor_id owner;
  bool has_activator;
  qa_saved_actor_id activator;
  qa_saved_actor_id *door_group; /* owned array */
  size_t door_group_count;
  bool has_monster;
  qa_persist_q1_saved_monster monster;
  bool has_move;
  qa_q1_saved_move move;
  qa_persist_q1_saved_callbacks callbacks;
} qa_persist_q1_saved_entity;

/* Mirrored TS Q1SavedPlayer.state (checkpoint.ts): Q1PlayerState
 * without actor/powerups; alpha/scale default to 0 when absent. */
typedef struct qa_persist_q1_saved_player_state {
  double alpha, scale;
  const char *weapon; /* Q1Weapon spelling */
  bool primary_holstered, attack_held, jump_held;
  double teleport_until, attack_finished, weapon_frame;
  double weapon_animation_at, weapon_animation_base;
  bool continuous_firing;
  double next_weapon_frame, lightning_sound_at;
  qa_vec3 punch_angles;
  double nail_side, max_health, mega_rot_at, hostile_until;
  qa_vec3 view_angles;
  double water_level, air_finished, drown_damage, drown_at, hazard_at;
  const char *auto_switch; /* always|new|never */
} qa_persist_q1_saved_player_state;

typedef struct qa_q1_saved_powerup {
  const char *kind; /* Q1_POWERUP_IDS spelling */
  double expires;
} qa_q1_saved_powerup;

/* Mirrored TS Q1SavedPlayer (checkpoint.ts). */
typedef struct qa_persist_q1_saved_player {
  qa_saved_actor_id actor;
  qa_provider_id actor_provider;
  qa_persist_q1_saved_player_state state;
  qa_q1_saved_powerup *powerups; /* owned array */
  size_t powerup_count;
} qa_persist_q1_saved_player;

/* Mirrored TS Q1PrecacheTables (types.ts). */
typedef struct qa_persist_q1_precache_tables {
  const char *phase; /* loading|frozen */
  const char **models; /* owned array of borrowed strings */
  size_t model_count;
  const char **sounds; /* owned array of borrowed strings */
  size_t sound_count;
} qa_persist_q1_precache_tables;

/* Mirrored TS Q1Basis (types.ts). */
typedef struct qa_persist_q1_basis {
  qa_vec3 forward, right, up;
} qa_persist_q1_basis;

typedef struct qa_q1_intermission {
  const char *map;
  bool has_cause;
  qa_saved_actor_id cause;
  double exit_after;
} qa_q1_intermission;

typedef struct qa_q1_checkpoint_extension {
  const char *id;
  qa_byte_view bytes; /* borrowed from the tree when read */
} qa_q1_checkpoint_extension;

/* Mirrored TS Q1FoundationCheckpoint (checkpoint.ts). */
typedef struct qa_persist_q1_foundation_checkpoint {
  qa_provider_id provider;
  const char *format; /* literal "q1-foundation" */
  double version;     /* literal 5 */
  qa_persist_q1_precache_tables precaches;
  const char *edition; /* classic|rerelease */
  double time, frame_seconds, force_retouch;
  double sequence, next_dynamic_slot;
  double total_secrets, found_secrets, total_monsters, killed_monsters;
  double world_type;
  const char *map_name;
  qa_persist_q1_basis basis;
  bool has_world;
  qa_saved_actor_id world;
  bool has_sight_entity;
  qa_saved_actor_id sight_entity;
  double sight_time;
  bool has_intermission;
  qa_q1_intermission intermission;
  qa_persist_q1_saved_entity *entities; /* owned array */
  size_t entity_count;
  qa_persist_q1_saved_player *players; /* owned array */
  size_t player_count;
  qa_q1_checkpoint_extension *extensions; /* owned array */
  size_t extension_count;
  qa_save_value *root; /* owned when decoded; NULL from qa_read_* */
} qa_persist_q1_foundation_checkpoint;

qa_persist_q1_foundation_checkpoint qa_read_q1_foundation_checkpoint(
    const qa_save_reader *reader);
/* Malloc'd envelope bytes; *out_length set. NULL only on OOM. */
uint8_t *qa_encode_q1_foundation_checkpoint(
    const qa_persist_q1_foundation_checkpoint *checkpoint, size_t *out_length);
/* False only on OOM; malformed input aborts as the TS throws. */
bool qa_decode_q1_foundation_checkpoint(const uint8_t *bytes, size_t length,
                                        qa_persist_q1_foundation_checkpoint *out);
void qa_q1_foundation_checkpoint_free(qa_persist_q1_foundation_checkpoint *checkpoint);

#endif
