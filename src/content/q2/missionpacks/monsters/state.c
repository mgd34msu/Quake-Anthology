/* Port of src/content/q2/missionpacks/monsters/state.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/state.h"

#include <stdlib.h>
#include <string.h>

void qa_q2mpm_state_init(qa_q2mpm_monster_state *state) {
  memset(state, 0, sizeof *state);
  state->flyer_next_move = QA_Q2MPM_FLYER_NONE;
  state->widow_damage_multiplier = 1;
}
void qa_q2mpm_state_destroy(qa_q2mpm_monster_state *state) {
  for (size_t i = 0; i < state->actor_count; i++) free(state->actors[i].state.chosen_reinforcements);
  free(state->actors);
  memset(state, 0, sizeof *state);
}
qa_q2mpm_rogue_state *qa_q2mpm_state_get(qa_q2mpm_monster_state *state, qa_q2f_entity *entity) {
  for (size_t i = 0; i < state->actor_count; i++)
    if (qa_same_actor(state->actors[i].actor, entity->actor.id)) return &state->actors[i].state;
  if (state->actor_count == state->actor_cap) {
    size_t cap = state->actor_cap ? state->actor_cap * 2 : 8;
    qa_q2mpm_actor_state *grown = realloc(state->actors, cap * sizeof *grown);
    if (!grown) qa_q2f_error("Out of memory growing mission-pack monster state");
    state->actors = grown;
    state->actor_cap = cap;
  }
  qa_q2mpm_actor_state *entry = &state->actors[state->actor_count++];
  entry->actor = entity->actor.id;
  memset(&entry->state, 0, sizeof entry->state);
  return &entry->state;
}
void qa_q2mpm_monsters_checkpoint_free(qa_q2mpm_monsters_checkpoint *checkpoint) {
  for (size_t i = 0; i < checkpoint->actor_count; i++) free(checkpoint->actors[i].state.chosen_reinforcements);
  free(checkpoint->actors);
  if (checkpoint->has_hints) qa_q2mpm_hints_checkpoint_free(&checkpoint->hints);
  memset(checkpoint, 0, sizeof *checkpoint);
}
static bool save_optional(bool has, qa_actor_id actor, bool *out_has, qa_saved_actor_id *out) {
  const qa_actor_id *value = has ? &actor : NULL;
  *out_has = qa_q2f_save_actor(value, out);
  return true;
}
bool qa_q2mpm_state_capture(qa_q2mpm_monster_state *state, qa_q2f_game *game, qa_q2mpm_monsters_checkpoint *out) {
  memset(out, 0, sizeof *out);
  out->version = 1;
  out->flyer_next_move = state->flyer_next_move;
  out->widow_shots_fired = state->widow_shots_fired;
  out->widow_damage_multiplier = state->widow_damage_multiplier;
  size_t live = 0;
  for (size_t i = 0; i < state->actor_count; i++)
    if (game->host->actors.is_live(game->host->actors.ctx, state->actors[i].actor)) live++;
  if (live) {
    out->actors = calloc(live, sizeof *out->actors);
    if (!out->actors) return false;
  }
  for (size_t i = 0; i < state->actor_count; i++) {
    qa_actor_id actor = state->actors[i].actor;
    if (!game->host->actors.is_live(game->host->actors.ctx, actor)) continue;
    const qa_q2mpm_rogue_state *saved = &state->actors[i].state;
    qa_q2mpm_checkpoint_actor *entry = &out->actors[out->actor_count++];
    entry->actor.slot = (double)actor.slot;
    entry->actor.generation = (double)actor.generation;
    qa_q2mpm_saved_state *copy = &entry->state;
    copy->blocked = saved->blocked;
    copy->turret_orientation = saved->turret_orientation;
    copy->medic_tries = saved->medic_tries;
    copy->react_to_damage_time = saved->react_to_damage_time;
    copy->summon_strength = saved->summon_strength;
    copy->good_guy = saved->good_guy;
    copy->widow_quad_until = saved->widow_quad_until;
    copy->widow_double_until = saved->widow_double_until;
    copy->widow_invulnerable_until = saved->widow_invulnerable_until;
    copy->reinforcement_count = saved->reinforcement_count;
    if (saved->reinforcement_count) {
      copy->chosen_reinforcements = malloc(saved->reinforcement_count * sizeof *copy->chosen_reinforcements);
      if (!copy->chosen_reinforcements) { qa_q2mpm_monsters_checkpoint_free(out); return false; }
      memcpy(copy->chosen_reinforcements, saved->chosen_reinforcements,
             saved->reinforcement_count * sizeof *copy->chosen_reinforcements);
    }
    save_optional(saved->has_healer, saved->healer, &copy->has_healer, &copy->healer);
    save_optional(saved->has_bad_medic1, saved->bad_medic1, &copy->has_bad_medic1, &copy->bad_medic1);
    save_optional(saved->has_bad_medic2, saved->bad_medic2, &copy->has_bad_medic2, &copy->bad_medic2);
    save_optional(saved->has_last_player_enemy, saved->last_player_enemy, &copy->has_last_player_enemy, &copy->last_player_enemy);
    save_optional(saved->has_bad_area, saved->bad_area, &copy->has_bad_area, &copy->bad_area);
  }
  return true;
}
static void restore_optional(qa_q2f_game *game, bool has, qa_saved_actor_id saved, bool *out_has, qa_actor_id *out) {
  *out_has = has;
  if (has) *out = game->host->actors.reference_saved(game->host->actors.ctx, saved);
}
void qa_q2mpm_state_restore(qa_q2mpm_monster_state *state, qa_q2f_game *game, const qa_q2mpm_monsters_checkpoint *checkpoint) {
  for (size_t i = 0; i < state->actor_count; i++) free(state->actors[i].state.chosen_reinforcements);
  state->actor_count = 0;
  state->flyer_next_move = checkpoint->flyer_next_move;
  state->widow_shots_fired = checkpoint->widow_shots_fired;
  state->widow_damage_multiplier = checkpoint->widow_damage_multiplier;
  for (size_t i = 0; i < checkpoint->actor_count; i++) {
    const qa_q2mpm_checkpoint_actor *saved = &checkpoint->actors[i];
    qa_owned_actor actor = qa_q2f_restore_actor(game, saved->actor);
    qa_q2f_entity probe;
    memset(&probe, 0, sizeof probe);
    probe.actor = actor;
    qa_q2mpm_rogue_state *restored = qa_q2mpm_state_get(state, &probe);
    const qa_q2mpm_saved_state *value = &saved->state;
    restored->blocked = value->blocked;
    restored->turret_orientation = value->turret_orientation;
    restored->medic_tries = value->medic_tries;
    restored->react_to_damage_time = value->react_to_damage_time;
    restored->summon_strength = value->summon_strength;
    restored->good_guy = value->good_guy;
    restored->widow_quad_until = value->widow_quad_until;
    restored->widow_double_until = value->widow_double_until;
    restored->widow_invulnerable_until = value->widow_invulnerable_until;
    restored->reinforcement_count = value->reinforcement_count;
    restored->reinforcement_cap = value->reinforcement_count;
    if (value->reinforcement_count) {
      restored->chosen_reinforcements = malloc(value->reinforcement_count * sizeof *restored->chosen_reinforcements);
      if (!restored->chosen_reinforcements) qa_q2f_error("Out of memory restoring mission-pack monster state");
      memcpy(restored->chosen_reinforcements, value->chosen_reinforcements,
             value->reinforcement_count * sizeof *restored->chosen_reinforcements);
    }
    restore_optional(game, value->has_healer, value->healer, &restored->has_healer, &restored->healer);
    restore_optional(game, value->has_bad_medic1, value->bad_medic1, &restored->has_bad_medic1, &restored->bad_medic1);
    restore_optional(game, value->has_bad_medic2, value->bad_medic2, &restored->has_bad_medic2, &restored->bad_medic2);
    restore_optional(game, value->has_last_player_enemy, value->last_player_enemy, &restored->has_last_player_enemy, &restored->last_player_enemy);
    restore_optional(game, value->has_bad_area, value->bad_area, &restored->has_bad_area, &restored->bad_area);
  }
}
