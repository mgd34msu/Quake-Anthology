/* Port of src/content/q2/rerelease/environment.ts. GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/environment.h"
#include "qa/content/q2/foundation/items.h"
#include "rr_internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

void qa_q2_rerelease_world_effects(qa_q2rr_player_context *c, qa_q2rr_player_extra_state *extra) {
  qa_q2_entity *e = c->entity; qa_q2_game_services *g = c->game;
  qa_q2rr_player_state *st = c->state; qa_q2rr_player_movement *mv = &c->movement;
  double now = rr_now(g);
  if (st->noclip || st->spectator) { st->air_finished = now + 12; return; }
  double water = mv->water_level, old = st->old_water_level;
  qa_q2_player_powerups powers = c->powerups(c->call_ctx);
  bool breather = powers.breather_until > now, suit = powers.enviro_until > now;
  st->old_water_level = water;
  if (old == 0 && water != 0) {
    c->hooks->noise(c->hooks->ctx, e->actor.id, rr_body_of(g, e).origin);
    rr_sound_ent(g, e, rr_band(mv->water_type, 8) != 0 ? "player/lava_in.wav" : "player/watr_in.wav", 4, 1, 1);
    e->flags = rr_bor(e->flags, 8);
  }
  if (old != 0 && water == 0) {
    c->hooks->noise(c->hooks->ctx, e->actor.id, rr_body_of(g, e).origin);
    rr_sound_ent(g, e, "player/watr_out.wav", 4, 1, 1);
    e->flags = rr_bnot_and(e->flags, 8);
  }
  if (old != 3 && water == 3) rr_sound_ent(g, e, "player/watr_un.wav", 4, 1, 1);
  if (old == 3 && water != 3 && rr_health(g, e->actor.id) > 0) {
    if (st->air_finished < now) {
      rr_sound_ent(g, e, "player/gasp1.wav", 2, 1, 1);
      c->hooks->noise(c->hooks->ctx, e->actor.id, rr_body_of(g, e).origin);
    } else if (st->air_finished < now + 11) rr_sound_ent(g, e, "player/gasp2.wav", 2, 1, 1);
  }
  if (water == 3) {
    if (breather || suit) {
      st->air_finished = now + 10;
      if ((int64_t)llround((powers.breather_until - now) * 1000) % 2500 == 0) {
        rr_sound_ent(g, e, st->breather_sound == 0 ? "player/u_breath1.wav" : "player/u_breath2.wav", 0, 1, 1);
        st->breather_sound = rr_bxor(st->breather_sound, 1);
        c->hooks->noise(c->hooks->ctx, e->actor.id, rr_body_of(g, e).origin);
      }
    }
    double health = rr_health(g, e->actor.id);
    if (st->air_finished < now && st->next_drown_time < now && health > 0) {
      st->next_drown_time = now + 1;
      st->drown_damage = st->drown_damage + 2 < 15 ? st->drown_damage + 2 : 15;
      rr_sound_ent(g, e, health <= st->drown_damage ? "*drown1.wav" : (g->host->random(g->host->ctx) < 0.5 ? "*gurp2.wav" : "*gurp1.wav"), 2, 1, 1);
      st->pain_debounce = now;
      c->environment_damage(c->call_ctx, st->drown_damage, 17, 2);
    } else if (st->air_finished <= now + 3 && st->next_drown_time < now) {
      char path[32];
      snprintf(path, sizeof path, "player/wade%lld.wav", (long long)(1 + (int64_t)trunc(now) % 3));
      rr_sound_ent(g, e, path, 2, 1, 1);
      st->next_drown_time = now + 1;
    }
  } else { st->air_finished = now + 12; st->drown_damage = 2; }
  if (water != 0 && rr_band(mv->water_type, 24) != 0 && extra->slime_debounce <= now) {
    if (rr_band(mv->water_type, 8) != 0) {
      if (rr_health(g, e->actor.id) > 0 && st->pain_debounce <= now && powers.invulnerability_until < now) {
        rr_sound_ent(g, e, g->host->random(g->host->ctx) < 0.5 ? "player/burn2.wav" : "player/burn1.wav", 2, 1, 1);
        st->pain_debounce = now + 1;
      }
      c->environment_damage(c->call_ctx, (suit ? 1 : 3) * water, 19, 0);
    }
    if (rr_band(mv->water_type, 16) != 0 && !suit) c->environment_damage(c->call_ctx, water, 18, 0);
    extra->slime_debounce = now + 0.1;
  }
}

void qa_q2_rerelease_falling_damage(qa_q2rr_player_context *c, qa_q2rr_player_extra_state *extra,
                                    qa_q2_rerelease_options options, double frame_seconds) {
  qa_q2_entity *e = c->entity; qa_q2_game_services *g = c->game;
  qa_q2rr_player_state *st = c->state; qa_q2rr_player_movement *mv = &c->movement;
  double now = rr_now(g), impact = extra->impact_delta;
  extra->impact_delta = 0;
  if (st->dead || st->noclip || st->spectator || rr_health(g, e->actor.id) <= 0 || mv->water_level == 3 ||
      extra->grapple_released_until >= now || extra->grapple_attached) return;
  double delta = impact * impact * 0.0001;
  if (mv->water_level == 2) delta *= 0.25;
  if (mv->water_level == 1) delta *= 0.5;
  if (delta < 1) return;
  st->bob_time = 0;
  if (st->landmark_free_fall) { delta = delta < 30 ? delta : 30; st->landmark_free_fall = false; st->landmark_noise_time = now + 0.1; }
  if (delta < 15) {
    if (!extra->on_ladder) snprintf(st->event, sizeof st->event, "q2:footstep");
    return;
  }
  st->fall_value = delta * 0.5 < 40 ? delta * 0.5 : 40;
  st->fall_time = now + 0.3 + (0.1 - frame_seconds);
  if (delta > 30) {
    snprintf(st->event, sizeof st->event, "%s", delta >= 55 ? "q2:fall-far" : "q2:fall");
    st->pain_debounce = now + frame_seconds;
    if (g->options.mode != QA_Q2_MODE_DEATHMATCH || !options.deathmatch_no_fall_damage) {
      double amt = trunc((delta - 30) / 2);
      c->environment_damage(c->call_ctx, amt > 1 ? amt : 1, 22, 0);
    }
  } else snprintf(st->event, sizeof st->event, "q2:fall-short");
  if (rr_health(g, e->actor.id) != 0) c->hooks->noise(c->hooks->ctx, e->actor.id, rr_body_of(g, e).origin);
}
