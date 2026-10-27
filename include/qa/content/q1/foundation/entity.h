/*
 * Port of src/content/q1/foundation/entity.ts. GPL-2.0-or-later.
 * Callbacks are named bound slots (name + handler); qa_q1_callback_name
 * aborts on a handler without a name (TS callbackName throw). Body/health
 * are actor-local mirrors of the shared tables (see types.h note).
 */
#ifndef QA_CONTENT_Q1_FOUNDATION_ENTITY_H
#define QA_CONTENT_Q1_FOUNDATION_ENTITY_H

#include "qa/content/q1/foundation/types.h"
#include "qa/formats/q1_map/types.h"
#include "qa/formats/q1_map/entities.h"

typedef void (*qa_q1_action_fn)(void *ctx, qa_q1_services *game, qa_q1_actor *entity);
typedef void (*qa_q1_use_fn)(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator);
typedef void (*qa_q1_touch_fn)(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                               const qa_vec3 *normal, const qa_touch_surface *surface);
typedef void (*qa_q1_pain_fn)(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *attacker, double damage);
typedef void (*qa_q1_die_fn)(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *attacker);
typedef void (*qa_q1_blocked_fn)(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other);
typedef void (*qa_q1_trajectory_fn)(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_weapon_trajectory_update update);

typedef struct qa_q1_bound_action { const char *name; void *ctx; qa_q1_action_fn fn; } qa_q1_bound_action;
typedef struct qa_q1_bound_use { const char *name; void *ctx; qa_q1_use_fn fn; } qa_q1_bound_use;
typedef struct qa_q1_bound_touch { const char *name; void *ctx; qa_q1_touch_fn fn; qa_q1_trajectory_fn trajectory; } qa_q1_bound_touch;
typedef struct qa_q1_bound_pain { const char *name; void *ctx; qa_q1_pain_fn fn; } qa_q1_bound_pain;
typedef struct qa_q1_bound_die { const char *name; void *ctx; qa_q1_die_fn fn; } qa_q1_bound_die;
typedef struct qa_q1_bound_blocked { const char *name; void *ctx; qa_q1_blocked_fn fn; } qa_q1_bound_blocked;

typedef struct qa_q1_move { qa_vec3 destination; qa_q1_bound_action done; } qa_q1_move;

typedef const char *qa_q1_monster_species;
extern const qa_q1_monster_species QA_Q1_MONSTER_SPECIES[28];

typedef struct qa_q1_monster {
  qa_q1_monster_species species;
  const char *mode; /* stand|walk|run|attack|leap|pain|death */
  double frame_index, first_frame;
  double sequence[32]; size_t sequence_count;
  bool has_enemy; qa_actor_id enemy;
  bool has_old_enemy; qa_actor_id old_enemy;
  char *path;
  double pause_until, attack_finished, pain_finished, search_until;
  bool death_drop, refired;
} qa_q1_monster;

typedef struct qa_q1_field { char *key, *value; } qa_q1_field;
typedef struct qa_q1_reference { char *key; bool has_actor; qa_actor_id actor; } qa_q1_reference;

typedef struct qa_q1_body {
  qa_vec3 origin, angles, velocity;
  qa_bounds bounds;
  bool has_ground; qa_actor_id ground;
} qa_q1_body;

struct qa_q1_actor {
  qa_owned_actor actor;
  char *classname;
  bool has_source_ordinal; double source_ordinal;
  char *model;
  double frame, skin, effects;
  qa_q1_solid solid; qa_q1_move_type movement;
  qa_q1_field *fields; size_t field_count, field_capacity;
  qa_q1_reference *references; size_t reference_count, reference_capacity;
  char *target, *targetname, *killtarget, *message;
  double delay, spawnflags, sounds, wait, speed, damage, max_health;
  bool damageable, aimed_damage, activated;
  double next_think;
  qa_q1_bound_action think, path_end;
  qa_q1_bound_use use;
  qa_q1_bound_touch touch;
  qa_q1_bound_pain pain;
  qa_q1_bound_die die;
  qa_q1_bound_blocked blocked;
  bool has_move; qa_q1_move move;
  bool has_owner; qa_actor_id owner;
  bool has_activator; qa_actor_id activator;
  char *original_model;
  qa_vec3 pos1, pos2, dest1, dest2, movedir, mangle, angular_velocity;
  const char *state; /* bottom|up|top|down */
  qa_q1_actor **door_group; size_t door_group_count;
  bool has_trigger_bounds; qa_bounds trigger_bounds;
  double attack_finished, count;
  qa_q1_monster *monster; /* owned, NULL = TS null */
  const char *projectile; /* rocket|grenade|spike|superspike, NULL */
  qa_q1_weapon projectile_weapon; /* NULL */
  double water_level, water_type, movement_flags, ideal_yaw, yaw_speed;
  const char *attack_state; /* straight|melee|missile|dodging */
  /* Actor-local mirrors of shared body/combat state (types.h note). */
  qa_q1_body body;
  double health;
  bool live;
};

/* False only on OOM. source may be NULL (TS undefined). */
bool qa_q1_actor_init(qa_q1_actor *entity, qa_owned_actor actor, const char *classname,
                      const double *source_ordinal, const qa_q1_entity *source);
void qa_q1_actor_free(qa_q1_actor *entity);
const char *qa_q1_actor_text(const qa_q1_actor *entity, const char *key);
double qa_q1_actor_number(const qa_q1_actor *entity, const char *key);
double qa_q1_actor_number_or(const qa_q1_actor *entity, const char *key, double fallback);
qa_vec3 qa_q1_actor_vector(const qa_q1_actor *entity, const char *key);
bool qa_q1_actor_field_set(qa_q1_actor *entity, const char *key, const char *value);
const char *qa_q1_actor_field_get(const qa_q1_actor *entity, const char *key); /* NULL absent */
bool qa_q1_actor_reference_set(qa_q1_actor *entity, const char *key, const qa_actor_id *actor);
bool qa_q1_actor_reference_get(const qa_q1_actor *entity, const char *key, qa_actor_id *out); /* false = absent/null */

qa_vec3 qa_q1_parse_vector(const char *text);
qa_vec3 qa_q1_source_angles(const qa_q1_entity *source);
qa_vec3 qa_q1_move_direction(qa_vec3 angles, qa_q1_services *game); /* game may be NULL */

#endif
