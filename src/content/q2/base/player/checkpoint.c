/* Port of src/content/q2/base/player/checkpoint.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/player/checkpoint.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static qa_inventory_entry *copy_entries(const qa_inventory_entry *entries, size_t count) {
  if (count == 0) return NULL;
  qa_inventory_entry *copy = malloc(count * sizeof *copy);
  if (!copy) qa_range_error("Out of memory");
  memcpy(copy, entries, count * sizeof *copy);
  return copy;
}
static double *copy_doubles(const double *values, size_t count) {
  if (count == 0) return NULL;
  double *copy = malloc(count * sizeof *copy);
  if (!copy) qa_range_error("Out of memory");
  memcpy(copy, values, count * sizeof *copy);
  return copy;
}

void qa_q2_base_player_state_checkpoint_capture(const qa_q2_base_player_state *state, qa_q2_base_player_state_checkpoint *out) {
  *out = (qa_q2_base_player_state_checkpoint){0};
  out->slot = state->slot; out->entered_at = state->entered_at;
  out->use_q2_weapons = state->use_q2_weapons; out->use_q2_inventory = state->use_q2_inventory;
  out->spawn_inventory = copy_entries(state->spawn_inventory, state->spawn_inventory_count);
  out->spawn_inventory_count = state->spawn_inventory_count;
  (void)snprintf(out->userinfo, sizeof out->userinfo, "%s", state->userinfo);
  (void)snprintf(out->name, sizeof out->name, "%s", state->name);
  (void)snprintf(out->skin, sizeof out->skin, "%s", state->skin);
  out->gender = state->gender;
  out->fov = state->fov; out->hand = state->hand;
  out->spectator = state->spectator; out->requested_spectator = state->requested_spectator;
  out->connected = state->connected; out->dead = state->dead; out->gibbed = state->gibbed;
  out->noclip = state->noclip; out->god = state->god; out->notarget = state->notarget;
  out->score = state->score; out->ping = state->ping; out->respawn_time = state->respawn_time;
  out->air_finished = state->air_finished; out->next_drown_time = state->next_drown_time;
  out->drown_damage = state->drown_damage; out->old_water_level = state->old_water_level;
  out->breather_sound = state->breather_sound; out->pain_debounce = state->pain_debounce;
  out->damage_blood = state->damage_blood; out->damage_armor = state->damage_armor;
  out->damage_power_armor = state->damage_power_armor; out->damage_knockback = state->damage_knockback;
  out->damage_from = state->damage_from; out->damage_blend = state->damage_blend;
  out->damage_alpha = state->damage_alpha; out->bonus_alpha = state->bonus_alpha;
  out->damage_pitch = state->damage_pitch; out->damage_roll = state->damage_roll; out->damage_time = state->damage_time;
  out->power_armor_time = state->power_armor_time; out->fall_time = state->fall_time; out->fall_value = state->fall_value;
  out->landmark_free_fall = state->landmark_free_fall; out->landmark_noise_time = state->landmark_noise_time;
  out->old_velocity = state->old_velocity; out->old_view_angles = state->old_view_angles;
  out->killer_yaw = state->killer_yaw; out->buttons = state->buttons; out->latched_buttons = state->latched_buttons;
  out->weapon_thunk = state->weapon_thunk; out->bob_time = state->bob_time; out->bob_move = state->bob_move;
  out->event = state->event; out->animation_priority = state->animation_priority; out->animation_end = state->animation_end;
  out->animation_duck = state->animation_duck; out->animation_run = state->animation_run;
  out->loop_sound = state->loop_sound; out->selected_item = state->selected_item;
  out->show_scores = state->show_scores; out->show_inventory = state->show_inventory; out->show_help = state->show_help;
  out->has_chase_target = qa_q2_save_actor(state->has_chase_target ? &state->chase_target : NULL, &out->chase_target);
  out->has_coop_respawn = state->has_coop_respawn;
  if (state->has_coop_respawn) {
    out->coop_respawn = state->coop_respawn;
    out->coop_respawn.inventory = copy_entries(state->coop_respawn.inventory, state->coop_respawn.inventory_count);
  }
  out->flood_times = copy_doubles(state->flood_times, state->flood_time_count);
  out->flood_time_count = state->flood_time_count;
  out->flood_lock_until = state->flood_lock_until;
}

void qa_q2_base_player_state_checkpoint_clear(qa_q2_base_player_state_checkpoint *checkpoint) {
  free(checkpoint->spawn_inventory);
  checkpoint->spawn_inventory = NULL; checkpoint->spawn_inventory_count = 0;
  qa_q2_base_player_carry_clear(&checkpoint->coop_respawn);
  free(checkpoint->flood_times);
  checkpoint->flood_times = NULL; checkpoint->flood_time_count = 0;
}

void qa_q2_base_player_state_checkpoint_restore(qa_q2_base_player_state *state,
    const qa_q2_base_player_state_checkpoint *checkpoint,
    qa_actor_id (*resolve)(void *ctx, qa_saved_actor_id saved), void *resolve_ctx) {
  /* TS Object.assign over a fresh/cloned state: release owned arrays
   * first, then take deep copies of the checkpoint's. */
  qa_q2_base_player_state_destroy(state);
  *state = (qa_q2_base_player_state){0};
  state->slot = checkpoint->slot; state->entered_at = checkpoint->entered_at;
  state->use_q2_weapons = checkpoint->use_q2_weapons; state->use_q2_inventory = checkpoint->use_q2_inventory;
  state->spawn_inventory = copy_entries(checkpoint->spawn_inventory, checkpoint->spawn_inventory_count);
  state->spawn_inventory_count = checkpoint->spawn_inventory_count;
  (void)snprintf(state->userinfo, sizeof state->userinfo, "%s", checkpoint->userinfo);
  (void)snprintf(state->name, sizeof state->name, "%s", checkpoint->name);
  (void)snprintf(state->skin, sizeof state->skin, "%s", checkpoint->skin);
  state->gender = checkpoint->gender; state->fov = checkpoint->fov; state->hand = checkpoint->hand;
  state->spectator = checkpoint->spectator; state->requested_spectator = checkpoint->requested_spectator;
  state->connected = checkpoint->connected; state->dead = checkpoint->dead; state->gibbed = checkpoint->gibbed;
  state->noclip = checkpoint->noclip; state->god = checkpoint->god; state->notarget = checkpoint->notarget;
  state->score = checkpoint->score; state->ping = checkpoint->ping; state->respawn_time = checkpoint->respawn_time;
  state->air_finished = checkpoint->air_finished; state->next_drown_time = checkpoint->next_drown_time;
  state->drown_damage = checkpoint->drown_damage; state->old_water_level = checkpoint->old_water_level;
  state->breather_sound = checkpoint->breather_sound; state->pain_debounce = checkpoint->pain_debounce;
  state->damage_blood = checkpoint->damage_blood; state->damage_armor = checkpoint->damage_armor;
  state->damage_power_armor = checkpoint->damage_power_armor; state->damage_knockback = checkpoint->damage_knockback;
  state->damage_from = checkpoint->damage_from; state->damage_blend = checkpoint->damage_blend;
  state->damage_alpha = checkpoint->damage_alpha; state->bonus_alpha = checkpoint->bonus_alpha;
  state->damage_pitch = checkpoint->damage_pitch; state->damage_roll = checkpoint->damage_roll; state->damage_time = checkpoint->damage_time;
  state->power_armor_time = checkpoint->power_armor_time; state->fall_time = checkpoint->fall_time; state->fall_value = checkpoint->fall_value;
  state->landmark_free_fall = checkpoint->landmark_free_fall; state->landmark_noise_time = checkpoint->landmark_noise_time;
  state->old_velocity = checkpoint->old_velocity; state->old_view_angles = checkpoint->old_view_angles;
  state->killer_yaw = checkpoint->killer_yaw; state->buttons = checkpoint->buttons; state->latched_buttons = checkpoint->latched_buttons;
  state->weapon_thunk = checkpoint->weapon_thunk; state->bob_time = checkpoint->bob_time; state->bob_move = checkpoint->bob_move;
  state->event = checkpoint->event; state->animation_priority = checkpoint->animation_priority; state->animation_end = checkpoint->animation_end;
  state->animation_duck = checkpoint->animation_duck; state->animation_run = checkpoint->animation_run;
  state->loop_sound = checkpoint->loop_sound; state->selected_item = checkpoint->selected_item;
  state->show_scores = checkpoint->show_scores; state->show_inventory = checkpoint->show_inventory; state->show_help = checkpoint->show_help;
  state->has_chase_target = checkpoint->has_chase_target;
  if (checkpoint->has_chase_target) state->chase_target = resolve(resolve_ctx, checkpoint->chase_target);
  state->has_coop_respawn = checkpoint->has_coop_respawn;
  if (checkpoint->has_coop_respawn) qa_q2_base_player_carry_set(&state->coop_respawn, checkpoint->coop_respawn);
  state->flood_times = copy_doubles(checkpoint->flood_times, checkpoint->flood_time_count);
  state->flood_time_count = checkpoint->flood_time_count;
  state->flood_time_cap = checkpoint->flood_time_count;
  state->flood_lock_until = checkpoint->flood_lock_until;
}
