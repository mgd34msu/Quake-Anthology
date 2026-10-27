/* Port of src/content/q1/foundation/checkpoint.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_FOUNDATION_CHECKPOINT_H
#define QA_CONTENT_Q1_FOUNDATION_CHECKPOINT_H

#include "qa/content/q1/foundation/entity_services.h"
#include "qa/contracts/session.h"

typedef struct qa_q1_saved_callbacks {
  const char *think, *use, *touch, *pain, *die, *blocked, *path_end;
} qa_q1_saved_callbacks;

typedef struct qa_q1_entity_source_state {
  const char *model; double frame, skin, effects;
  qa_q1_solid solid; qa_q1_move_type movement;
  const char *target, *targetname, *killtarget, *message;
  double delay, spawnflags, sounds, wait, speed, damage, max_health;
  bool aimed_damage; double next_think;
  const char *original_model;
  qa_vec3 pos1, pos2, dest1, dest2, movedir, mangle;
  const char *state;
  bool has_trigger_bounds; qa_bounds trigger_bounds;
  double attack_finished, count; bool activated;
  const char *projectile; qa_q1_weapon projectile_weapon;
  qa_vec3 angular_velocity;
  double water_level, water_type, movement_flags, ideal_yaw, yaw_speed;
  const char *attack_state;
} qa_q1_entity_source_state;

typedef struct qa_q1_saved_field { const char *key, *value; } qa_q1_saved_field;
typedef struct qa_q1_saved_reference { const char *key; bool has_actor; qa_saved_actor_id actor; } qa_q1_saved_reference;
typedef struct qa_q1_saved_monster {
  qa_q1_monster_species species; const char *mode;
  double frame_index, first_frame;
  const double *sequence; size_t sequence_count;
  bool has_enemy; qa_saved_actor_id enemy;
  bool has_old_enemy; qa_saved_actor_id old_enemy;
  const char *path;
  double pause_until, attack_finished, pain_finished, search_until;
  bool death_drop, refired;
} qa_q1_saved_monster;

typedef struct qa_q1_saved_entity {
  qa_saved_actor_id actor;
  bool has_source_slot; double source_slot;
  qa_provider_id actor_provider;
  const char *classname;
  bool has_source_ordinal; double source_ordinal;
  qa_q1_entity_source_state state;
  const qa_q1_saved_field *fields; size_t field_count;
  const qa_q1_saved_reference *references; size_t reference_count;
  bool has_owner; qa_saved_actor_id owner;
  bool has_activator; qa_saved_actor_id activator;
  const qa_saved_actor_id *door_group; size_t door_group_count;
  const qa_q1_saved_monster *monster;
  bool has_move; qa_vec3 move_destination; const char *move_done;
  qa_q1_saved_callbacks callbacks;
} qa_q1_saved_entity;

typedef struct qa_q1_saved_player_state {
  qa_q1_weapon weapon; bool primary_holstered;
  double attack_finished; bool attack_held, jump_held;
  double teleport_until, weapon_frame, weapon_animation_at, weapon_animation_base;
  bool continuous_firing; double next_weapon_frame, lightning_sound_at;
  qa_vec3 punch_angles, view_angles;
  double nail_side, max_health, mega_rot_at, hostile_until, water_level;
  double air_finished, drown_damage, drown_at, hazard_at;
  const char *auto_switch;
  bool has_alpha; double alpha; bool has_scale; double scale;
} qa_q1_saved_player_state;
typedef struct qa_q1_saved_player {
  qa_provider_id actor_provider;
  qa_saved_actor_id actor;
  qa_q1_saved_player_state state;
  const qa_q1_powerup_entry *powerups; size_t powerup_count;
} qa_q1_saved_player;

typedef struct qa_q1_foundation_checkpoint {
  const char *format; /* "q1-foundation" */
  qa_provider_id provider;
  double version; /* 5 */
  qa_q1_precache_tables precaches;
  const char *edition;
  double time, frame_seconds, force_retouch;
  qa_q1_basis basis;
  double sequence, next_dynamic_slot;
  double total_secrets, found_secrets, total_monsters, killed_monsters, world_type;
  const char *map_name;
  bool has_world; qa_saved_actor_id world;
  bool has_sight_entity; qa_saved_actor_id sight_entity;
  double sight_time;
  bool has_intermission; const char *intermission_map;
  bool has_intermission_cause; qa_saved_actor_id intermission_cause;
  double intermission_exit_after;
  /* Owned capture storage (valid until qa_q1_checkpoint_free). */
  void *storage;
  const qa_q1_saved_entity *entities; size_t entity_count;
  const qa_q1_saved_player *players; size_t player_count;
  struct { const char *id; const uint8_t *bytes; size_t len; } *extensions; size_t extension_count;
} qa_q1_foundation_checkpoint;

qa_q1_entity_source_state qa_q1_capture_entity_source_state(const qa_q1_actor *entity);
bool qa_q1_save_actor(const qa_actor_id *actor, qa_saved_actor_id *out); /* false = TS null */
/* Capture: false only on OOM; unnamed callback aborts (TS throw). */
bool qa_q1_capture_foundation(qa_q1_services *game, double sequence, double next_dynamic_slot, qa_q1_foundation_checkpoint *out);
void qa_q1_checkpoint_free(qa_q1_foundation_checkpoint *checkpoint);
void qa_q1_restore_foundation(qa_q1_services *game, const qa_q1_foundation_checkpoint *checkpoint);
/* TS Q1EntityServices.capture/restore conveniences. */
bool qa_q1_services_capture(qa_q1_services *game, qa_q1_foundation_checkpoint *out);
void qa_q1_services_restore(qa_q1_services *game, const qa_q1_foundation_checkpoint *checkpoint, bool schedule_thinks);

#endif
