/* Port of src/content/q2/missionpacks/monsters/state.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_STATE_H
#define QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_STATE_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q2/foundation/monsters/index.h"
#include "qa/content/q2/missionpacks/monsters/hints.h"
#include "qa/contracts/session.h"

/* TS RogueMonsterState. Nullable ActorId fields use has_* flags;
 * chosen_reinforcements is an owned dynamic array. */
typedef struct qa_q2mpm_rogue_state {
  bool blocked;
  double turret_orientation;
  bool has_healer; qa_actor_id healer;
  bool has_bad_medic1; qa_actor_id bad_medic1;
  bool has_bad_medic2; qa_actor_id bad_medic2;
  double medic_tries;
  double *chosen_reinforcements; size_t reinforcement_count, reinforcement_cap;
  double react_to_damage_time;
  double summon_strength;
  bool has_last_player_enemy; qa_actor_id last_player_enemy;
  bool has_bad_area; qa_actor_id bad_area;
  bool good_guy;
  double widow_quad_until, widow_double_until, widow_invulnerable_until;
} qa_q2mpm_rogue_state;

typedef enum qa_q2mpm_flyer_next_move { QA_Q2MPM_FLYER_NONE, QA_Q2MPM_FLYER_RUN } qa_q2mpm_flyer_next_move;

/* TS Q2MissionPackMonstersCheckpoint (hints owned when present). */
typedef struct qa_q2mpm_saved_state {
  bool blocked;
  double turret_orientation, medic_tries, react_to_damage_time, summon_strength;
  double *chosen_reinforcements; size_t reinforcement_count;
  bool has_healer; qa_saved_actor_id healer;
  bool has_bad_medic1; qa_saved_actor_id bad_medic1;
  bool has_bad_medic2; qa_saved_actor_id bad_medic2;
  bool has_last_player_enemy; qa_saved_actor_id last_player_enemy;
  bool has_bad_area; qa_saved_actor_id bad_area;
  bool good_guy;
  double widow_quad_until, widow_double_until, widow_invulnerable_until;
} qa_q2mpm_saved_state;
typedef struct qa_q2mpm_checkpoint_actor {
  qa_saved_actor_id actor;
  qa_q2mpm_saved_state state;
} qa_q2mpm_checkpoint_actor;
typedef struct qa_q2mpm_monsters_checkpoint {
  double version; /* 1 */
  qa_q2mpm_flyer_next_move flyer_next_move;
  bool has_hints; qa_q2mpm_hints_checkpoint hints;
  double widow_shots_fired;
  double widow_damage_multiplier; /* 1 | 2 | 4 */
  qa_q2mpm_checkpoint_actor *actors; size_t actor_count;
} qa_q2mpm_monsters_checkpoint;
void qa_q2mpm_monsters_checkpoint_free(qa_q2mpm_monsters_checkpoint *checkpoint);

typedef struct qa_q2mpm_actor_state {
  qa_actor_id actor;
  qa_q2mpm_rogue_state state;
} qa_q2mpm_actor_state;

/* TS Q2MissionPackMonsterState. */
typedef struct qa_q2mpm_monster_state {
  qa_q2mpm_actor_state *actors; size_t actor_count, actor_cap;
  qa_q2mpm_flyer_next_move flyer_next_move;
  double widow_shots_fired;
  double widow_damage_multiplier; /* 1 | 2 | 4 */
  void **bound_registries; size_t bound_count, bound_cap; /* TS released WeakSet */
  int64_t *bound_subscriptions; /* on_release ids, parallel; undone by destroy */
} qa_q2mpm_monster_state;

void qa_q2mpm_state_init(qa_q2mpm_monster_state *state);
void qa_q2mpm_state_destroy(qa_q2mpm_monster_state *state);
/* TS get(entity): creates the default entry on first use. The
 * returned pointer is invalidated by the next get for a new actor. */
qa_q2mpm_rogue_state *qa_q2mpm_state_get(qa_q2mpm_monster_state *state, qa_q2_entity *entity);
/* capture fills a malloc-backed checkpoint; false only on OOM. */
/* TS bind(game): drop an actor's rogue state when the actor registry releases
 * it; registered once per registry. The state must not move afterwards. */
void qa_q2mpm_state_bind(qa_q2mpm_monster_state *state, qa_q2_game_services *game);
bool qa_q2mpm_state_capture(qa_q2mpm_monster_state *state, qa_q2_game_services *game, qa_q2mpm_monsters_checkpoint *out);
void qa_q2mpm_state_restore(qa_q2mpm_monster_state *state, qa_q2_game_services *game, const qa_q2mpm_monsters_checkpoint *checkpoint);

#endif
