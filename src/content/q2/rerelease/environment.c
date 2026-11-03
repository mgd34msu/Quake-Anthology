/* Port of src/content/q2/rerelease/environment.ts. GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/environment.h"
#include "qa/content/q2/foundation/items.h"
#include "rr_internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "qa/foundation/js.h"

/* Q2CharacterContext services (TS context.game.host / body / sound). */
static double env_now(qa_q2_character_context *c) { return c->game.now(c->game.host_ctx); }
static double env_health(qa_q2_character_context *c) {
  qa_combat_state_result r = qa_gameplay_authority_read(c->game.combat, c->entity->actor.id);
  return r.has_value ? r.value.health : 0;
}
static void env_sound(qa_q2_character_context *c, const char *path, double channel) {
  c->game.sound(c->game.ctx, c->entity, path, channel, 1, 1);
}
static void env_noise(qa_q2_character_context *c) {
  c->noise(c->noise_ctx, c->entity->actor.id, c->game.body(c->game.ctx, c->entity->actor).origin);
}

void qa_q2_rerelease_world_effects(qa_q2_character_context *c, qa_q2rr_player_extra_state *extra) {
  qa_q2_entity *e = c->entity;
  qa_q2_base_player_state *st = c->state; qa_q2_player_movement *mv = &c->movement;
  double now = env_now(c);
  if (st->noclip || st->spectator) { st->air_finished = now + 12; return; }
  double water = mv->water_level, old = st->old_water_level;
  qa_q2_player_powerups powers = c->powerups(c->cb_ctx);
  bool breather = powers.breather_until > now, suit = powers.enviro_until > now;
  st->old_water_level = water;
  if (old == 0 && water != 0) {
    env_noise(c);
    env_sound(c, rr_band(mv->water_type, 8) != 0 ? "player/lava_in.wav" : "player/watr_in.wav", 4);
    e->flags = rr_bor(e->flags, 8);
  }
  if (old != 0 && water == 0) {
    env_noise(c);
    env_sound(c, "player/watr_out.wav", 4);
    e->flags = rr_bnot_and(e->flags, 8);
  }
  if (old != 3 && water == 3) env_sound(c, "player/watr_un.wav", 4);
  if (old == 3 && water != 3 && env_health(c) > 0) {
    if (st->air_finished < now) {
      env_sound(c, "player/gasp1.wav", 2);
      env_noise(c);
    } else if (st->air_finished < now + 11) env_sound(c, "player/gasp2.wav", 2);
  }
  if (water == 3) {
    if (breather || suit) {
      st->air_finished = now + 10;
      if (fmod(qa_js_round((powers.breather_until - now) * 1000), 2500) == 0) {
        env_sound(c, st->breather_sound == 0 ? "player/u_breath1.wav" : "player/u_breath2.wav", 0);
        st->breather_sound = rr_bxor(st->breather_sound, 1);
        env_noise(c);
      }
    }
    double health = env_health(c);
    if (st->air_finished < now && st->next_drown_time < now && health > 0) {
      st->next_drown_time = now + 1;
      st->drown_damage = st->drown_damage + 2 < 15 ? st->drown_damage + 2 : 15;
      env_sound(c, health <= st->drown_damage ? "*drown1.wav" : (c->game.random(c->game.host_ctx) < 0.5 ? "*gurp2.wav" : "*gurp1.wav"), 2);
      st->pain_debounce = now;
      c->environment_damage(c->cb_ctx, st->drown_damage, 17, 2);
    } else if (st->air_finished <= now + 3 && st->next_drown_time < now) {
      char path[32];
      snprintf(path, sizeof path, "player/wade%lld.wav", (long long)(1 + (int64_t)trunc(now) % 3));
      env_sound(c, path, 2);
      st->next_drown_time = now + 1;
    }
  } else { st->air_finished = now + 12; st->drown_damage = 2; }
  if (water != 0 && rr_band(mv->water_type, 24) != 0 && extra->slime_debounce <= now) {
    if (rr_band(mv->water_type, 8) != 0) {
      if (env_health(c) > 0 && st->pain_debounce <= now && powers.invulnerability_until < now) {
        env_sound(c, c->game.random(c->game.host_ctx) < 0.5 ? "player/burn2.wav" : "player/burn1.wav", 2);
        st->pain_debounce = now + 1;
      }
      c->environment_damage(c->cb_ctx, (suit ? 1 : 3) * water, 19, 0);
    }
    if (rr_band(mv->water_type, 16) != 0 && !suit) c->environment_damage(c->cb_ctx, water, 18, 0);
    extra->slime_debounce = now + 0.1;
  }
}

void qa_q2_rerelease_falling_damage(qa_q2_character_context *c, qa_q2rr_player_extra_state *extra,
                                    qa_q2_rerelease_options options, double frame_seconds) {
  qa_q2_base_player_state *st = c->state; qa_q2_player_movement *mv = &c->movement;
  double now = env_now(c), impact = extra->impact_delta;
  extra->impact_delta = 0;
  if (st->dead || st->noclip || st->spectator || env_health(c) <= 0 || mv->water_level == 3 ||
      extra->grapple_released_until >= now || extra->grapple_attached) return;
  double delta = impact * impact * 0.0001;
  if (mv->water_level == 2) delta *= 0.25;
  if (mv->water_level == 1) delta *= 0.5;
  if (delta < 1) return;
  st->bob_time = 0;
  if (st->landmark_free_fall) { delta = delta < 30 ? delta : 30; st->landmark_free_fall = false; st->landmark_noise_time = now + 0.1; }
  if (delta < 15) {
    if (!extra->on_ladder) st->event = "q2:footstep";
    return;
  }
  st->fall_value = delta * 0.5 < 40 ? delta * 0.5 : 40;
  st->fall_time = now + 0.3 + (0.1 - frame_seconds);
  if (delta > 30) {
    st->event = delta >= 55 ? "q2:fall-far" : "q2:fall";
    st->pain_debounce = now + frame_seconds;
    if (c->game.mode != QA_Q2_MODE_DEATHMATCH || !options.deathmatch_no_fall_damage) {
      double amt = trunc((delta - 30) / 2);
      c->environment_damage(c->cb_ctx, amt > 1 ? amt : 1, 22, 0);
    }
  } else st->event = "q2:fall-short";
  if (env_health(c) != 0) env_noise(c);
}
