/* Port of src/content/q2/base/player/view.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/player/view.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qa/content/q2/foundation/fields.h"
#include "qa/core/math.h"

static double clampd(double value, double minimum, double maximum) { return fmax(minimum, fmin(maximum, value)); }

qa_vec4 qa_q2_add_blend(qa_vec4 blend, qa_vec3 color, double alpha) {
  if (alpha <= 0) return blend;
  double total = blend.w + (1 - blend.w) * alpha, old = blend.w / total;
  return (qa_vec4){blend.x * old + color.x * (1 - old), blend.y * old + color.y * (1 - old),
                   blend.z * old + color.z * (1 - old), total};
}

qa_q2_base_frame_range qa_q2_pain_animation_frames(bool ducked, double index) {
  return ducked ? (qa_q2_base_frame_range){168, 172} : (qa_q2_base_frame_range){53 + index * 4, 57 + index * 4};
}
qa_q2_base_frame_range qa_q2_death_animation_frames(bool ducked, double index) {
  if (ducked) return (qa_q2_base_frame_range){172, 177};
  if (index == 0) return (qa_q2_base_frame_range){177, 183};
  if (index == 1) return (qa_q2_base_frame_range){183, 189};
  return (qa_q2_base_frame_range){189, 197};
}

static double health_of(qa_q2_character_context *context) {
  qa_combat_state_result combat = qa_gameplay_authority_read(context->game.combat, context->entity->actor.id);
  return combat.has_value ? combat.value.health : 0;
}

qa_q2_damage_feedback_result qa_q2_damage_feedback(qa_q2_character_context *context, double pain_index) {
  qa_q2_base_player_state *state = context->state;
  qa_q2_entity *entity = context->entity;
  qa_q2_character_services *game = &context->game;
  double now = game->now(game->host_ctx);
  qa_q2_player_powerups powers = context->powerups(context->cb_ctx);
  double flashes = (double)((state->damage_blood != 0 ? 1 : 0) |
      (state->damage_armor != 0 && !state->god && powers.invulnerability_until <= now ? 2 : 0));
  double total = state->damage_blood + state->damage_armor + state->damage_power_armor;
  if (total == 0) return (qa_q2_damage_feedback_result){flashes, pain_index};
  double next_pain = pain_index;
  if (context->movement.animate_q2 && state->animation_priority < 3) {
    state->animation_priority = 3;
    if (!context->movement.ducked) next_pain = fmod(pain_index + 1, 3);
    qa_q2_base_frame_range frames = qa_q2_pain_animation_frames(context->movement.ducked, next_pain);
    entity->frame = frames.first;
    state->animation_end = frames.last;
  }
  double count = fmax(total, 10), health = health_of(context);
  if (now > state->pain_debounce && !state->god && powers.invulnerability_until <= now) {
    double severity = health < 25 ? 25 : health < 50 ? 50 : health < 75 ? 75 : 100;
    char path[64];
    (void)snprintf(path, sizeof path, "*pain%d_%d.wav", (int)severity, (int)floor(game->random(game->host_ctx) * 2) + 1);
    game->sound(game->ctx, entity, path, 2, 1, 1);
    state->pain_debounce = now + 0.7;
  }
  state->damage_alpha = clampd(fmax(state->damage_alpha, 0) + count * 0.01, 0.2, 0.6);
  state->damage_blend = (qa_vec3){(state->damage_armor + state->damage_blood) / total,
                                  (state->damage_power_armor + state->damage_armor) / total,
                                  state->damage_armor / total};
  if (state->damage_knockback != 0 && health > 0) {
    double kick = clampd(fabs(state->damage_knockback) * 100 / health, count * 0.5, 50);
    qa_body_state body = game->body(game->ctx, entity->actor);
    qa_vec3 direction = qa_q2_normalize(qa_q2_subtract(state->damage_from, body.origin));
    qa_angle_vectors vectors = qa_math_angle_vectors_rule(context->movement.view_angles, QA_ANGLE_RULE_NATIVE);
    state->damage_roll = kick * qa_q2_dot(direction, vectors.right) * 0.3;
    state->damage_pitch = kick * -qa_q2_dot(direction, vectors.forward) * 0.3;
    state->damage_time = now + 0.5;
  }
  state->damage_blood = 0; state->damage_armor = 0; state->damage_power_armor = 0; state->damage_knockback = 0;
  return (qa_q2_damage_feedback_result){flashes, next_pain};
}

static double angle_difference(double old, double current) {
  double delta = old - current;
  if (delta > 180) delta -= 360;
  if (delta < -180) delta += 360;
  return clampd(delta, -45, 45);
}

/** Each seat gets a complete source view; no module-global current player/vectors. */
qa_q2_base_player_view qa_q2_build_view(qa_q2_character_context *context, double flashes, bool intermission) {
  qa_q2_base_player_state *state = context->state;
  qa_q2_entity *entity = context->entity;
  qa_q2_character_services *game = &context->game;
  qa_q2_player_movement *movement = &context->movement;
  qa_q2_base_player_rules *rules = context->rules;
  qa_body_state body = game->body(game->ctx, entity->actor);
  double now = game->now(game->host_ctx), health = health_of(context);
  qa_q2_character_weapon weapon_value = {0};
  bool has_weapon = context->weapon_state(context->cb_ctx, &weapon_value);
  const qa_q2_character_weapon *weapon = has_weapon ? &weapon_value : NULL;
  qa_q2_player_powerups powers = context->powerups(context->cb_ctx);
  qa_angle_vectors vectors = qa_math_angle_vectors_rule(movement->view_angles, QA_ANGLE_RULE_NATIVE);
  double speed = hypot(body.velocity.x, body.velocity.y);
  double bob_time = movement->ducked ? state->bob_time * 4 : state->bob_time;
  double bob_cycle = trunc(bob_time), bob = fabs(sin(bob_time * M_PI));
  qa_vec3 angles = movement->view_angles;
  qa_vec3 kicks = weapon ? weapon->kick_angles : QA_Q2_ZERO;
  if (state->dead) { angles = (qa_vec3){-15, state->killer_yaw, 40}; kicks = QA_Q2_ZERO; }
  else {
    double damage_ratio = fmax(0, (state->damage_time - now) / 0.5);
    double fall_ratio = fmax(0, (state->fall_time - now) / 0.3);
    if (damage_ratio == 0) { state->damage_pitch = 0; state->damage_roll = 0; }
    double duck = movement->ducked ? 6 : 1;
    kicks = (qa_vec3){
        kicks.x + damage_ratio * state->damage_pitch + fall_ratio * state->fall_value + qa_q2_dot(body.velocity, vectors.forward) * rules->run_pitch + bob * rules->bob_pitch * speed * duck,
        kicks.y,
        kicks.z + damage_ratio * state->damage_roll + qa_q2_dot(body.velocity, vectors.right) * rules->run_roll + bob * rules->bob_roll * speed * duck * (((int64_t)bob_cycle & 1) != 0 ? -1 : 1)};
  }
  qa_vec3 recoil = weapon ? weapon->kick_origin : QA_Q2_ZERO;
  qa_vec3 offset = {clampd(recoil.x, -14, 14), clampd(recoil.y, -14, 14),
      clampd(entity->view_height - fmax(0, (state->fall_time - now) / 0.3) * state->fall_value * 0.4 + fmin(bob * speed * rules->bob_up, 6) + recoil.z, -22, 30)};
  double yaw_delta = angle_difference(state->old_view_angles.y, angles.y);
  qa_vec3 gun_angles = {speed * bob * 0.005 + angle_difference(state->old_view_angles.x, angles.x) * 0.2,
      speed * bob * 0.01 * (((int64_t)bob_cycle & 1) != 0 ? -1 : 1) + yaw_delta * 0.2,
      speed * bob * 0.005 * (((int64_t)bob_cycle & 1) != 0 ? -1 : 1) + angle_difference(state->old_view_angles.z, angles.z) * 0.2 + yaw_delta * 0.1};
  qa_vec3 gun_offset = qa_q2_add(qa_q2_add(qa_q2_scale(vectors.forward, rules->gun_offset.y),
                                           qa_q2_scale(vectors.right, rules->gun_offset.x)),
                                 qa_q2_scale(vectors.up, -rules->gun_offset.z));
  double contents = game->point_contents(game->host_ctx, qa_q2_add(body.origin, offset));
  qa_vec4 blend = {0, 0, 0, 0};
  if (((int64_t)contents & 9) != 0) blend = qa_q2_add_blend(blend, (qa_vec3){1, 0.3, 0}, 0.6);
  else if (((int64_t)contents & 16) != 0) blend = qa_q2_add_blend(blend, (qa_vec3){0, 0.1, 0.05}, 0.6);
  else if (((int64_t)contents & 32) != 0) blend = qa_q2_add_blend(blend, (qa_vec3){0.5, 0.3, 0.2}, 0.4);
  const char *power_item = NULL; double power_until = 0; const char *power_sound = NULL; qa_vec3 power_color = QA_Q2_ZERO; double power_alpha = 0;
  if (powers.quad_until > now) { power_item = "q2:item_quad"; power_until = powers.quad_until; power_sound = "items/damage2.wav"; power_color = (qa_vec3){0, 0, 1}; power_alpha = 0.08; }
  else if (powers.invulnerability_until > now) { power_item = "q2:item_invulnerability"; power_until = powers.invulnerability_until; power_sound = "items/protect2.wav"; power_color = (qa_vec3){1, 1, 0}; power_alpha = 0.08; }
  else if (powers.enviro_until > now) { power_item = "q2:item_enviro"; power_until = powers.enviro_until; power_sound = "items/airout.wav"; power_color = (qa_vec3){0, 1, 0}; power_alpha = 0.08; }
  else if (powers.breather_until > now) { power_item = "q2:item_breather"; power_until = powers.breather_until; power_sound = "items/airout.wav"; power_color = (qa_vec3){0.4, 1, 0.4}; power_alpha = 0.04; }
  if (power_item) {
    int64_t remaining = llround((power_until - now) * 10);
    if (remaining == 30) game->sound(game->ctx, entity, power_sound, 3, 1, 1);
    if (remaining > 30 || (remaining & 4) != 0) blend = qa_q2_add_blend(blend, power_color, power_alpha);
  }
  blend = qa_q2_add_blend(blend, state->damage_blend, state->damage_alpha);
  blend = qa_q2_add_blend(blend, (qa_vec3){0.85, 0.7, 0.3}, state->bonus_alpha);
  state->damage_alpha = fmax(0, state->damage_alpha - 0.06);
  state->bonus_alpha = fmax(0, state->bonus_alpha - 0.1);
  qa_combat_state_result combat = qa_gameplay_authority_read(game->combat, entity->actor.id);
  double armor_points = !combat.has_value || combat.value.armor.regular.kind == QA_REGULAR_ARMOR_NONE ? 0 : combat.value.armor.regular.points;
  const char *active_weapon = weapon ? weapon->q2_name : NULL;
  qa_item_id ammo = active_weapon == NULL ? NULL : (weapon ? weapon->ammo : NULL);
  bool has_timer = false; qa_item_id timer_item = NULL; double timer_seconds = 0;
  if (powers.quad_until > now) { has_timer = true; timer_item = "q2:item_quad"; timer_seconds = trunc(powers.quad_until - now); }
  else if (powers.invulnerability_until > now) { has_timer = true; timer_item = "q2:item_invulnerability"; timer_seconds = trunc(powers.invulnerability_until - now); }
  else if (powers.enviro_until > now) { has_timer = true; timer_item = "q2:item_enviro"; timer_seconds = trunc(powers.enviro_until - now); }
  else if (powers.breather_until > now) { has_timer = true; timer_item = "q2:item_breather"; timer_seconds = trunc(powers.breather_until - now); }
  qa_q2_base_player_view view = {0};
  view.angles = angles;
  view.offset = intermission ? QA_Q2_ZERO : offset;
  view.kick_angles = intermission ? QA_Q2_ZERO : kicks;
  view.gun_angles = gun_angles;
  view.gun_offset = gun_offset;
  view.blend = intermission ? (qa_vec4){0, 0, 0, 0} : blend;
  view.fov = intermission ? 90 : state->fov;
  view.underwater = !intermission && ((int64_t)contents & 56) != 0;
  view.flashes = flashes;
  view.health = health;
  view.armor = combat.has_value && strcmp(combat.value.armor.powered.kind, "none") != 0 &&
          (armor_points == 0 || (llround(now * 10) & 8) != 0) ? combat.value.armor.powered.cells : armor_points;
  view.ammo = ammo == NULL ? 0 : qa_shared_inventory_table_count(game->inventory, entity->actor.id, ammo);
  view.score = state->score;
  view.selected_item = state->selected_item;
  view.has_timer = has_timer; view.timer_item = timer_item; view.timer_seconds = timer_seconds;
  view.spectator = state->spectator;
  view.layouts = (double)((state->show_scores || state->show_help || health <= 0 || intermission ? 1 : 0) |
                           (state->show_inventory && health > 0 ? 2 : 0));
  return view;
}

void qa_q2_client_animation(qa_q2_character_context *context) {
  if (!context->movement.animate_q2 || context->state->gibbed) return;
  qa_body_state body = context->game.body(context->game.ctx, context->entity->actor);
  qa_q2_advance_player_animation(context->state, context->entity, context->movement.grounded,
      context->movement.ducked, hypot(body.velocity.x, body.velocity.y) != 0);
}

void qa_q2_advance_player_animation(qa_q2_base_player_state *state, qa_q2_entity *entity, bool grounded, bool duck, bool run) {
  bool changed = (duck != state->animation_duck && state->animation_priority < 5) ||
                 (run != state->animation_run && state->animation_priority == 0) ||
                 (!grounded && state->animation_priority <= 1);
  if (!changed) {
    if (state->animation_priority == 6) { if (entity->frame > state->animation_end) { entity->frame--; return; } }
    else if (entity->frame < state->animation_end) { entity->frame++; return; }
    if (state->animation_priority == 5) return;
    if (state->animation_priority == 2) {
      if (!grounded) return;
      state->animation_priority = 1; entity->frame = 68; state->animation_end = 71;
      return;
    }
  }
  state->animation_priority = 0; state->animation_duck = duck; state->animation_run = run;
  if (!grounded) { state->animation_priority = 2; if (entity->frame != 67) entity->frame = 66; state->animation_end = 67; }
  else if (run) { entity->frame = duck ? 154 : 40; state->animation_end = duck ? 159 : 45; }
  else { entity->frame = duck ? 135 : 0; state->animation_end = duck ? 153 : 39; }
}

void qa_q2_client_effects(qa_q2_character_context *context) {
  qa_q2_entity *entity = context->entity;
  qa_q2_base_player_state *state = context->state;
  qa_q2_character_services *game = &context->game;
  double now = game->now(game->host_ctx);
  qa_q2_player_powerups powers = context->powerups(context->cb_ctx);
  qa_combat_state_result combat = qa_gameplay_authority_read(game->combat, entity->actor.id);
  entity->effects = 0;
  entity->render_flags = game->edition == QA_Q2_EDITION_RERELEASE ? 32768 : 0;
  if ((combat.has_value ? combat.value.health : 0) > 0) {
    if (state->power_armor_time > now && combat.has_value && strcmp(combat.value.armor.powered.kind, "none") != 0) {
      if (strcmp(combat.value.armor.powered.kind, "screen") == 0) entity->effects = (double)((int64_t)entity->effects | 0x200LL);
      else if (strcmp(combat.value.armor.powered.kind, "shield") == 0) {
        entity->effects = (double)((int64_t)entity->effects | 0x100LL);
        entity->render_flags = (double)((int64_t)entity->render_flags | 0x800LL);
      }
    }
    if (powers.quad_until > now && (powers.quad_until - now > 3 || (llround((powers.quad_until - now) * 10) & 4) != 0))
      entity->effects = (double)((int64_t)entity->effects | 0x8000LL);
    if (powers.invulnerability_until > now && (powers.invulnerability_until - now > 3 || (llround((powers.invulnerability_until - now) * 10) & 4) != 0))
      entity->effects = (double)((int64_t)entity->effects | 0x10000LL);
    if (powers.invis_until > now) entity->render_flags = (double)((int64_t)entity->render_flags | 32LL);
    if (state->god) {
      entity->effects = (double)((int64_t)entity->effects | 0x100LL);
      entity->render_flags = (double)((int64_t)entity->render_flags | 0x1c00LL);
    }
  }
  qa_q2_character_weapon weapon_value = {0};
  bool has_weapon = context->weapon_state(context->cb_ctx, &weapon_value);
  const qa_q2_character_weapon *weapon = has_weapon ? &weapon_value : NULL;
  const char *loop = context->movement.water_level != 0 && ((int64_t)context->movement.water_type & 24) != 0 ? "player/fry.wav"
      : weapon && weapon->q2_name && strcmp(weapon->q2_name, "railgun") == 0 ? "weapons/rg_hum.wav"
      : weapon && weapon->q2_name && strcmp(weapon->q2_name, "bfg") == 0 ? "weapons/bfg_hum.wav"
      : weapon && weapon->loop_sound ? weapon->loop_sound : "";
  if (strcmp(loop, state->loop_sound) != 0) {
    if (state->loop_sound[0] != '\0') {
      qa_q2_presentation_event ev = {0};
      ev.kind = "sound"; ev.has_actor = true; ev.actor = entity->actor.id;
      ev.origin = game->body(game->ctx, entity->actor).origin; ev.path = state->loop_sound;
      ev.channel = 0; ev.volume = 1; ev.attenuation = 1; ev.reliable = false; ev.loop = "stop";
      game->emit(game->host_ctx, ev);
    }
    state->loop_sound = loop;
    if (loop[0] != '\0') {
      qa_q2_presentation_event ev = {0};
      ev.kind = "sound"; ev.has_actor = true; ev.actor = entity->actor.id;
      ev.origin = game->body(game->ctx, entity->actor).origin; ev.path = loop;
      ev.channel = 0; ev.volume = 1; ev.attenuation = 1; ev.reliable = false; ev.loop = "start";
      game->emit(game->host_ctx, ev);
    }
  }
}
