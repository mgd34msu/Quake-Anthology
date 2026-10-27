/* Port of src/content/q2/base/player/environment.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/player/environment.h"

#include <math.h>
#include <stdint.h>

void qa_q2_environment_damage(qa_q2_character_context *context, double amount, double means, double flags) {
  context->environment_damage(context->cb_ctx, amount, means, flags);
}

static double health_of(qa_q2_character_context *context) {
  qa_combat_state_result combat = qa_gameplay_authority_read(context->game.combat, context->entity->actor.id);
  return combat.has_value ? combat.value.health : 0;
}
static void play(qa_q2_character_context *context, const char *path, double channel) {
  context->game.sound(context->game.ctx, context->entity, path, channel, 1, 1);
}
static void noise(qa_q2_character_context *context) {
  qa_body_state body = context->game.body(context->game.ctx, context->entity->actor);
  context->noise(context->noise_ctx, context->entity->actor.id, body.origin);
}

/** p_view.c P_WorldEffects runs once on the selected Q2 source frame. */
void qa_q2_world_effects(qa_q2_character_context *context) {
  qa_q2_entity *entity = context->entity;
  qa_q2_character_services *game = &context->game;
  qa_q2_base_player_state *state = context->state;
  qa_q2_player_movement *movement = &context->movement;
  double now = game->now(game->host_ctx);
  if (state->noclip || state->spectator) { state->air_finished = now + 12; return; }
  double level = movement->water_level, old = state->old_water_level;
  qa_q2_player_powerups powers = context->powerups(context->cb_ctx);
  bool breather = powers.breather_until > now, suit = powers.enviro_until > now;
  state->old_water_level = level;
  if (old == 0 && level != 0) {
    noise(context);
    play(context, ((int64_t)movement->water_type & 8) != 0 ? "player/lava_in.wav" : "player/watr_in.wav", 4);
    entity->flags = (double)((int64_t)entity->flags | 8LL);
  }
  if (old != 0 && level == 0) {
    noise(context);
    play(context, "player/watr_out.wav", 4);
    entity->flags = (double)((int64_t)entity->flags & ~8LL);
  }
  if (old != 3 && level == 3) play(context, "player/watr_un.wav", 4);
  if (old == 3 && level != 3) {
    if (state->air_finished < now) { play(context, "player/gasp1.wav", 2); noise(context); }
    else if (state->air_finished < now + 11) play(context, "player/gasp2.wav", 2);
  }
  if (level == 3) {
    if (breather || suit) {
      state->air_finished = now + 10;
      int64_t remaining = llround((powers.breather_until - now) * 10);
      if (remaining % 25 == 0) {
        play(context, state->breather_sound == 0 ? "player/u_breath1.wav" : "player/u_breath2.wav", 0);
        state->breather_sound = (double)((int64_t)state->breather_sound ^ 1LL);
        noise(context);
      }
    }
    double health = health_of(context);
    if (state->air_finished < now && state->next_drown_time < now && health > 0) {
      state->next_drown_time = now + 1;
      state->drown_damage = fmin(state->drown_damage + 2, 15);
      play(context, health <= state->drown_damage ? "player/drown1.wav" : game->random(game->host_ctx) < 0.5 ? "*gurp2.wav" : "*gurp1.wav", 2);
      state->pain_debounce = now;
      qa_q2_environment_damage(context, state->drown_damage, 17, 2);
    }
  } else { state->air_finished = now + 12; state->drown_damage = 2; }
  if (level != 0 && ((int64_t)movement->water_type & (8 | 16)) != 0) {
    if (((int64_t)movement->water_type & 8) != 0) {
      if (health_of(context) > 0 && state->pain_debounce <= now && powers.invulnerability_until < now) {
        play(context, game->random(game->host_ctx) < 0.5 ? "player/burn2.wav" : "player/burn1.wav", 2);
        state->pain_debounce = now + 1;
      }
      qa_q2_environment_damage(context, (suit ? 1 : 3) * level, 19, 0);
    }
    if (((int64_t)movement->water_type & 16) != 0 && !suit) qa_q2_environment_damage(context, level, 18, 0);
  }
}

void qa_q2_falling_damage(qa_q2_character_context *context) {
  qa_q2_entity *entity = context->entity;
  qa_q2_character_services *game = &context->game;
  qa_q2_player_movement *movement = &context->movement;
  qa_q2_base_player_state *state = context->state;
  if (!movement->animate_q2 || state->noclip || state->spectator) return;
  qa_vec3 velocity = game->body(game->ctx, entity->actor).velocity;
  double delta;
  if (state->old_velocity.z < 0 && velocity.z > state->old_velocity.z && !movement->grounded) delta = state->old_velocity.z;
  else { if (!movement->grounded) return; delta = velocity.z - state->old_velocity.z; }
  delta = delta * delta * 0.0001;
  if (movement->water_level == 3) return;
  if (movement->water_level == 2) delta *= 0.25;
  if (movement->water_level == 1) delta *= 0.5;
  if (delta < 1) return;
  if (state->landmark_free_fall) {
    delta = fmin(30, delta);
    state->landmark_free_fall = false;
    state->landmark_noise_time = game->now(game->host_ctx) + 0.1;
  }
  if (delta < 15) { state->event = "q2:footstep"; return; }
  state->fall_value = fmin(delta * 0.5, 40);
  state->fall_time = game->now(game->host_ctx) + 0.3;
  if (delta > 30) {
    if (health_of(context) > 0) state->event = delta >= 55 ? "q2:fall-far" : "q2:fall";
    state->pain_debounce = game->now(game->host_ctx);
    if (game->mode != QA_Q2_MODE_DEATHMATCH || ((int64_t)game->deathmatch_flags & 8) == 0)
      qa_q2_environment_damage(context, fmax(1, trunc((delta - 30) / 2)), 22, 0);
  } else state->event = "q2:fall-short";
}
