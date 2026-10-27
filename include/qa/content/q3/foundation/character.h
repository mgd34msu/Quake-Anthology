/* Port of src/content/q3/foundation/character.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_FOUNDATION_CHARACTER_H
#define QA_CONTENT_Q3_FOUNDATION_CHARACTER_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/movement.h"
#include "qa/contracts/world.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/callbacks.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"

extern const qa_bounds qa_q3_character_bounds;
enum { QA_Q3_CHARACTER_VIEW_HEIGHT = 26 };

typedef struct qa_q3_character_event {
  qa_owned_actor actor; double sequence, time_milliseconds, event, parameter;
} qa_q3_character_event;
typedef struct qa_q3_character_death_context {
  bool blood, no_drop, suicide; double killer_source_slot;
} qa_q3_character_death_context;
/* TS Q3CharacterServices: placement "source-game" suppresses killBox/link/spawnTargets. */
typedef struct qa_q3_character_services {
  qa_shared_body_table *bodies; qa_actor_callback_table *callbacks;
  qa_gameplay_authority *combat; qa_shared_inventory_table *inventory;
  void *ctx;
  double (*time_milliseconds)(void *ctx);
  void (*emit)(void *ctx, qa_q3_character_event event);
  qa_q3_character_death_context (*death_context)(void *ctx, qa_owned_actor actor);
  const char *placement; /* NULL/"character" | "source-game" */
  void (*spawn_targets)(void *ctx, qa_owned_actor actor);
  void (*kill_box)(void *ctx, qa_owned_actor actor);
} qa_q3_character_services;
typedef struct qa_q3_character_spawn {
  qa_body_state body; qa_combat_state combat;
  const qa_inventory_entry *inventory; size_t inventory_count;
} qa_q3_character_spawn;
typedef struct qa_q3_character_checkpoint {
  double version; const char *product; qa_animation_state animation;
  double flags, event_sequence, respawn_time, spawn_count; bool dead, gibbed, initialized;
} qa_q3_character_checkpoint;
typedef struct qa_q3_death_animation_checkpoint { double version; double index; } qa_q3_death_animation_checkpoint;
typedef struct qa_q3_death_animation_sequence { double index; } qa_q3_death_animation_sequence;
typedef struct qa_q3_death_animation_next { double animation, event; } qa_q3_death_animation_next;
void qa_q3_death_animation_sequence_init(qa_q3_death_animation_sequence *sequence);
qa_q3_death_animation_checkpoint qa_q3_death_animation_sequence_capture(const qa_q3_death_animation_sequence *sequence);
void qa_q3_death_animation_sequence_restore(qa_q3_death_animation_sequence *sequence, qa_q3_death_animation_checkpoint checkpoint);
qa_q3_death_animation_next qa_q3_death_animation_sequence_next(qa_q3_death_animation_sequence *sequence);

double qa_q3_maximum_health(const char *handicap);
qa_combat_state qa_q3_initial_combat(const char *handicap, const char *team /* nullable */);

typedef struct qa_q3_character_actor {
  qa_owned_actor actor; qa_provider_id provider; const char *product;
  qa_q3_character_services services; qa_q3_death_animation_sequence *death_animations;
  qa_animation_state current_animation; double flags, sequence, respawn_time, spawn_count;
  bool dead, gibbed, initialized;
} qa_q3_character_actor;
void qa_q3_character_actor_init(qa_q3_character_actor *actor, qa_owned_actor owned, qa_provider_id provider,
    const char *product, qa_q3_character_services services, qa_q3_death_animation_sequence *death_animations);
qa_actor_animation_state qa_q3_character_actor_get_animation(const qa_q3_character_actor *actor);
double qa_q3_character_actor_get_source_flags(const qa_q3_character_actor *actor);
double qa_q3_character_actor_get_event_sequence(const qa_q3_character_actor *actor);
double qa_q3_character_actor_get_respawn_eligible_after(const qa_q3_character_actor *actor);
double qa_q3_character_actor_get_spawns(const qa_q3_character_actor *actor);
void qa_q3_character_actor_jump(qa_q3_character_actor *actor);
qa_q3_character_checkpoint qa_q3_character_actor_capture(const qa_q3_character_actor *actor);
void qa_q3_character_actor_restore(qa_q3_character_actor *actor, qa_q3_character_checkpoint checkpoint);
void qa_q3_character_actor_spawn(qa_q3_character_actor *actor, qa_q3_character_spawn input);
void qa_q3_character_actor_die(qa_q3_character_actor *actor);
bool qa_q3_character_actor_wants_respawn(const qa_q3_character_actor *actor, double now_milliseconds,
    bool attack, bool use_holdable, double force_respawn_seconds);
void qa_q3_character_actor_commit_animation(qa_q3_character_actor *actor, qa_actor_animation_state animation);

/* Result effects are malloc'd (free effects pointer). */
qa_animation_step_result qa_q3_step_character_animation(qa_animation_step_input input,
    const char *product, bool dead, double event_sequence);
#endif
