/* Port of src/content/q2/missionpacks/weapons/player.ts. GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/weapons/player.h"
#include "qa/foundation/js.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../missionpacks_internal.h"
#include "qa/content/q2/foundation/monsters/types.h"
#include "qa/content/q2/missionpacks/projectiles/common.h"
#include "qa/content/q2/missionpacks/weapons/definitions.h"
#include "qa/foundation/support.h"

static qa_q2mp_bolts *bolts_of(qa_q2mp_weapons *w) { return &w->projectiles->base.base.base; }
static qa_q2mp_mines *mines_of(qa_q2mp_weapons *w) { return &w->projectiles->base.base; }
static bool name_is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static void noise(qa_q2w_weapons *weapons, qa_q2w_context *c, qa_vec3 origin) {
  qa_q2w_ballistics_player_noise(&weapons->base, c->self.actor, c->game, origin, "weapon");
}
static void throw_weapon(qa_q2mp_weapons *w, qa_q2w_context *context, qa_q2w_weapons *weapons, bool held);
typedef struct throw_env { qa_q2mp_weapons *w; qa_q2w_weapons *weapons; } throw_env;
static void throw_fire(void *ctx, qa_q2w_context *context, bool held) {
  throw_env *e = ctx;
  throw_weapon(e->w, context, e->weapons, held);
}
static void held_fn(void *ctx, qa_q2w_context *context, qa_q2w_weapons *weapons, bool held) {
  qa_q2mp_weapons_ext_ctx *e = ctx;
  throw_weapon(e->owner, context, weapons, held);
}

static void think_fn(void *ctx, qa_q2w_context *context, qa_q2w_weapons *weapons) {
  qa_q2mp_weapons_ext_ctx *e = ctx;
  qa_q2w_state *state = context->state;
  const qa_q2w_input *input = &context->input;
  qa_q2_game_services *game = context->game;
  const qa_q2w_definition *definition = context->definition;
  if (name_is(definition->name, "trap") || name_is(definition->name, "tesla")) {
    bool trap = name_is(definition->name, "trap");
    if (!trap && !context->rerelease)
      state->view_model = state->frame > 1 && state->frame < 9 ? "models/weapons/v_tesla2/tris.md2" : NULL;
    throw_env env = {e->owner, weapons};
    qa_q2w_throw_definition throwing;
    memset(&throwing, 0, sizeof throwing);
    throwing.sound_frame = trap ? 5 : 99; throwing.hold_frame = trap ? 11 : 1; throwing.fire_frame = trap ? 12 : 2;
    throwing.cock_sound = "weapons/trapcock.wav"; throwing.hold_sound = trap ? "weapons/traploop.wav" : "";
    throwing.explode = trap && !context->rerelease;
    throwing.wrap_before_pause = !trap; throwing.release_held = !trap;
    throwing.ctx = &env; throwing.fire = throw_fire;
    if (context->rerelease) qa_q2w_weapons_throw_rerelease(weapons, context, &throwing);
    else qa_q2w_weapons_throw_classic(weapons, context, &throwing);
    return;
  }
  if (context->rerelease) {
    qa_q2w_weapons_generic_rerelease(weapons, context);
    if (state->primary_handoff == QA_Q2W_HANDOFF_HOLSTERED) return;
    if (name_is(definition->name, "chainfist")) {
      if ((state->frame == 42 || state->frame == 51) && floor(q2mp_rnd(game) * 8) != 0 && input->hand != QA_Q2W_HAND_CENTER &&
          q2mp_rnd(game) < 0.4) {
        qa_q2w_projection projection = qa_q2w_weapons_project_default(weapons, context, q2mp_v3(8, 8, -4));
        qa_q2_presentation_event ev;
        memset(&ev, 0, sizeof ev);
        ev.kind = "effect"; ev.effect = "q2:chainfist_smoke"; ev.origin = projection.start;
        ev.direction = q2mp_zero(); ev.count = 0; ev.color = 0;
        game->host->emit(game->host->ctx, ev);
      }
      qa_q2w_weapons_set_loop(weapons, context->self, game, state,
                              state->phase == QA_Q2W_FIRING ? "weapons/sawhit.wav"
                              : state->phase == QA_Q2W_DROPPING ? "" : "weapons/sawidle.wav");
    }
    return;
  }
  double last_sequence = 0;
  if (name_is(definition->name, "chainfist")) {
    if (state->frame == 13 || state->frame == 23) state->frame = 32;
    else if ((state->frame == 42 || state->frame == 51) && floor(q2mp_rnd(game) * 8) != 0 &&
             input->hand != QA_Q2W_HAND_CENTER && q2mp_rnd(game) < 0.4) {
      qa_q2w_projection projection = qa_q2w_weapons_project_default(weapons, context, q2mp_v3(8, 8, -4));
      qa_q2_presentation_event ev;
      memset(&ev, 0, sizeof ev);
      ev.kind = "effect"; ev.effect = "q2:chainfist_smoke"; ev.origin = projection.start;
      ev.direction = q2mp_zero(); ev.count = 0; ev.color = 0;
      game->host->emit(game->host->ctx, ev);
    }
    qa_q2w_weapons_set_loop(weapons, context->self, game, state,
                            state->phase == QA_Q2W_FIRING ? "weapons/sawhit.wav"
                            : state->phase == QA_Q2W_DROPPING ? "" : "weapons/sawidle.wav");
  } else if (name_is(definition->name, "etf_rifle") && state->phase == QA_Q2W_FIRING && qa_q2w_weapons_ammo(weapons, context) <= 0) {
    state->frame = 8;
  } else if (name_is(definition->name, "heatbeam")) {
    if (state->phase == QA_Q2W_FIRING) {
      qa_q2w_weapons_set_loop(weapons, context->self, game, state, "weapons/bfg__l1a.wav");
      if (qa_q2w_weapons_ammo(weapons, context) >= 2 && qa_q2w_weapons_continues_attack(weapons, context)) {
        if (state->frame >= 13) state->frame = 9;
        state->view_model = "models/weapons/v_beamer2/tris.md2";
      } else { state->frame = 13; state->view_model = NULL; }
    } else { state->view_model = NULL; qa_q2w_weapons_set_loop(weapons, context->self, game, state, ""); }
  }
  qa_q2w_weapons_generic_classic(weapons, context);
  if (state->primary_handoff == QA_Q2W_HANDOFF_HOLSTERED) return;
  if (name_is(definition->name, "etf_rifle") && state->frame == 8 && qa_q2w_weapons_continues_attack(weapons, context))
    state->frame = 6;
  if (name_is(definition->name, "chainfist")) {
    if (qa_q2w_weapons_continues_attack(weapons, context) &&
        (state->frame == 13 || state->frame == 23 || state->frame == 32)) {
      last_sequence = state->frame; state->frame = 6;
    }
    if (state->frame == 6) {
      double chance = q2mp_rnd(game);
      if (last_sequence == 13) chance -= 0.34;
      else if (last_sequence == 23) chance += 0.33;
      else if (last_sequence == 32 && chance >= 0.33) chance += 0.34;
      if (chance < 0.33) state->frame = 14;
      else if (chance < 0.66) state->frame = 24;
    }
  }
}

static void throw_weapon(qa_q2mp_weapons *w, qa_q2w_context *context, qa_q2w_weapons *weapons, bool held) {
  qa_q2w_owner self = context->self;
  qa_q2_game_services *game = context->game;
  const qa_q2w_input *input = &context->input;
  qa_q2w_state *state = context->state;
  bool trap = name_is(context->definition->name, "trap");
  if (context->rerelease) {
    qa_q2mp_mines * m2;
    qa_owned_actor self3;
    qa_vec3 start3;
    qa_vec3 direction3;
    double multiplier2;
    double timer = state->grenade_time - context->now;
    double duration = trap ? 5 : 3, minimum = trap ? 300 : 400, maximum = trap ? 700 : 800;
    double charged = minimum + (duration - timer) * (maximum - minimum) / duration;
    double speed = trunc(q2mp_health(game, self.actor.id) <= 0 ? minimum : charged < maximum ? charged : maximum);
    qa_vec3 angles = q2mp_v3(input->angles.x > -62.5 ? input->angles.x : -62.5, input->angles.y, input->angles.z);
    qa_q2w_projection projection = qa_q2w_weapons_project(weapons, context, trap ? q2mp_v3(8, 0, -8) : q2mp_v3(0, 0, -22), angles);
    state->grenade_time = 0;
    if (trap)
      (void)qa_q2mp_projectiles_fire_trap(w->projectiles, self.actor, game, projection.start, projection.direction,
                                          125 * qa_q2w_weapons_multiplier(weapons, context), speed, 1, 165, held);
    else
      (void)(m2 = mines_of(w), self3 = self.actor, start3 = projection.start, direction3 = projection.direction, multiplier2 = qa_q2w_weapons_multiplier(weapons, context), qa_q2mp_mines_fire_tesla(m2, self3, game, start3, direction3,
                                     multiplier2, speed));
    qa_q2w_weapons_consume(weapons, context, 1, false);
    return;
  }
  double timer = state->grenade_time - context->now;
  double charged = 400 + (3 - timer) * 400 / 3;
  double speed = trunc(trap ? charged : charged < 800 ? charged : 800);
  qa_angle_vectors axes = q2mp_axes(input->angles);
  qa_q2w_projection projection = qa_q2w_weapons_project_default(weapons, context, q2mp_v3(8, 8, -8));
  if (trap) {
    (void)qa_q2mp_projectiles_fire_trap(w->projectiles, self.actor, game, projection.start, projection.direction,
                                        125 * qa_q2w_weapons_multiplier(weapons, context), speed, timer, 165, held);
  } else {
    double side = input->hand == QA_Q2W_HAND_LEFT ? 4 : input->hand == QA_Q2W_HAND_CENTER ? 0 : -4;
    qa_vec3 start = q2mp_add(q2mp_add(game->body(game->ctx, self.actor).origin, q2mp_scale(axes.right, side)),
                             q2mp_scale(axes.up, self.view_height - 22));
    qa_q2mp_mines * m = mines_of(w);
    qa_owned_actor self2 = self.actor;
    qa_vec3 direction2 = axes.forward;
    double multiplier = qa_q2w_weapons_multiplier(weapons, context);
    (void)qa_q2mp_mines_fire_tesla(m, self2, game, start, direction2,
                                   multiplier, speed);
  }
  qa_q2w_weapons_consume(weapons, context, 1, !trap);
  state->grenade_time = context->now + qa_q2w_weapons_firing_interval(weapons, self.actor.id, 1);
  if (trap && qa_q2w_weapons_ammo(weapons, context) == 0 && !held) qa_q2w_weapons_no_ammo(weapons, context, false);
  if (!trap)
    qa_q2w_weapons_animation(weapons, context, input->ducked ? "attack" : "reverse", input->ducked ? 159 : 119,
                             input->ducked ? 162 : 112);
}

static void fire_prox(qa_q2mp_weapons *w, qa_q2w_context *context, qa_q2w_weapons *weapons) {
  const qa_q2w_input *input = &context->input;
  qa_vec3 angles = context->rerelease ? q2mp_v3(input->angles.x > -62.5 ? input->angles.x : -62.5, input->angles.y, input->angles.z)
                                       : input->angles;
  qa_q2w_projection projection = qa_q2w_weapons_project(weapons, context, q2mp_v3(8, context->rerelease ? 0 : 8, -8), angles);
  qa_q2w_weapons_kick_default(weapons, context, q2mp_scale(q2mp_axes(input->angles).forward, -2), q2mp_v3(-1, 0, 0));
  qa_q2mp_mines * m = mines_of(w);
  qa_owned_actor self2 = context->self.actor;
  qa_q2_game_services * game2 = context->game;
  qa_vec3 start2 = projection.start;
  qa_vec3 direction2 = projection.direction;
  double multiplier = qa_q2w_weapons_multiplier(weapons, context);
  (void)qa_q2mp_mines_fire_prox(m, self2, game2, start2, direction2,
                                multiplier, 600);
  qa_q2w_weapons_flash(weapons, context, context->rerelease ? 31 : 6);
  if (!context->rerelease) context->state->frame++;
  noise(weapons, context, projection.start);
  qa_q2w_weapons_consume_default(weapons, context);
}
static void fire_ion(qa_q2mp_weapons *w, qa_q2w_context *context, qa_q2w_weapons *weapons) {
  qa_q2_game_services *game = context->game;
  const qa_q2w_input *input = &context->input;
  qa_vec3 aim = q2mp_v3(input->angles.x, input->angles.y + q2mp_rnd(game) * 2 - 1, input->angles.z);
  qa_q2w_projection projection = qa_q2w_weapons_project(weapons, context, q2mp_v3(16, 7, -8), aim);
  qa_q2w_weapons_kick_default(weapons, context,
                              q2mp_scale(q2mp_axes(context->rerelease ? input->angles : aim).forward, -3), q2mp_v3(-3, 0, 0));
  qa_q2mp_bolts * b = bolts_of(w);
  qa_owned_actor self2 = context->self.actor;
  qa_vec3 start2 = projection.start;
  qa_vec3 direction2 = projection.direction;
  double left = (q2mp_dm(game) ? 30 : 50);
  double right = qa_q2w_weapons_multiplier(weapons, context);
  double damage = left * right;
  (void)qa_q2mp_bolts_fire_ion_ripper(b, self2, game, start2, direction2,
                                      damage, 500, 0x100000);
  qa_q2w_weapons_flash(weapons, context, 16);
  if (!context->rerelease) context->state->frame++;
  noise(weapons, context, projection.start);
  qa_q2w_weapons_consume_default(weapons, context);
}
static void fire_phalanx(qa_q2mp_weapons *w, qa_q2w_context *context, qa_q2w_weapons *weapons) {
  qa_q2_game_services *game = context->game;
  const qa_q2w_input *input = &context->input;
  qa_q2w_state *state = context->state;
  bool second = state->frame == 8;
  double multiplier = qa_q2w_weapons_multiplier(weapons, context);
  double damage = (70 + floor(q2mp_rnd(game) * 10)) * multiplier;
  qa_vec3 angles = q2mp_v3(input->angles.x, input->angles.y + (second ? -1.5 : 1.5), input->angles.z);
  qa_q2w_projection projection = qa_q2w_weapons_project(weapons, context, q2mp_v3(0, 8, -8),
                                                        context->rerelease ? angles : input->angles);
  qa_vec3 direction = context->rerelease ? projection.direction : q2mp_axes(angles).forward;
  qa_q2w_weapons_kick_default(weapons, context, q2mp_scale(q2mp_axes(input->angles).forward, -2), q2mp_v3(-2, 0, 0));
  (void)qa_q2mp_bolts_fire_plasma(bolts_of(w), context->self.actor, game, projection.start, direction, damage, 725, 120,
                                  second ? 30 : 120 * multiplier);
  if (second) {
    qa_q2w_weapons_consume_default(weapons, context);
    if (context->rerelease) qa_q2w_weapons_flash(weapons, context, 20);
  } else {
    qa_q2w_weapons_flash(weapons, context, 18);
    noise(weapons, context, projection.start);
  }
  if (!context->rerelease) state->frame++;
}
static void fire_etf(qa_q2mp_weapons *w, qa_q2w_context *context, qa_q2w_weapons *weapons) {
  qa_q2_game_services *game = context->game;
  const qa_q2w_input *input = &context->input;
  qa_q2w_state *state = context->state;
  qa_q2w_owner self = context->self;
  if (context->rerelease) {
    if (!qa_q2w_weapons_continues_attack(weapons, context)) { state->frame = 8; return; }
    state->frame = state->frame == 6 ? 7 : 6;
  }
  if (qa_q2w_weapons_ammo(weapons, context) < context->definition->quantity) {
    qa_q2w_weapons_kick_default(weapons, context, q2mp_zero(), q2mp_zero());
    state->frame = 8;
    qa_q2w_weapons_no_ammo(weapons, context, true);
    return;
  }
  qa_vec3 kick_origin = q2mp_v3((q2mp_rnd(game) * 2 - 1) * 0.85, 0, 0);
  qa_vec3 kick_angles = q2mp_v3((q2mp_rnd(game) * 2 - 1) * 0.85, 0, 0);
  kick_origin.y = (q2mp_rnd(game) * 2 - 1) * 0.85;
  kick_angles.y = (q2mp_rnd(game) * 2 - 1) * 0.85;
  kick_origin.z = (q2mp_rnd(game) * 2 - 1) * 0.85;
  kick_angles.z = (q2mp_rnd(game) * 2 - 1) * 0.85;
  qa_q2w_weapons_kick_default(weapons, context, kick_origin, kick_angles);
  qa_q2w_projection projection;
  if (context->rerelease) {
    projection = qa_q2w_weapons_project(weapons, context, q2mp_v3(15, state->frame == 6 ? 8 : 6, -8),
                                        q2mp_add(input->angles, kick_angles));
  } else {
    qa_angle_vectors axes = q2mp_axes(input->angles);
    double side = (state->frame == 6 ? 8 : 6) * (input->hand == QA_Q2W_HAND_LEFT ? -1 : input->hand == QA_Q2W_HAND_CENTER ? 0 : 1);
    qa_vec3 start = q2mp_add(game->body(game->ctx, self.actor).origin, q2mp_v3(0, 0, self.view_height));
    start = q2mp_add(start, q2mp_scale(axes.forward, 15));
    start = q2mp_add(start, q2mp_scale(axes.right, side));
    start = q2mp_add(start, q2mp_scale(axes.up, -8));
    projection = (qa_q2w_projection){start, axes.forward};
  }
  qa_q2mp_bolts * b = bolts_of(w);
  qa_owned_actor self2 = self.actor;
  qa_vec3 start2 = projection.start;
  qa_vec3 direction2 = projection.direction;
  double damage = 10 * qa_q2w_weapons_multiplier(weapons, context);
  double speed = context->rerelease ? 1150 : 750;
  double kick = 3 * qa_q2w_weapons_multiplier(weapons, context);
  (void)qa_q2mp_bolts_fire_flechette(b, self2, game, start2, direction2,
                                     damage, speed, kick);
  if (context->rerelease) qa_q2w_weapons_powerup_sound(weapons, context);
  qa_q2w_weapons_flash(weapons, context, context->rerelease && state->frame == 7 ? 32 : 30);
  noise(weapons, context, projection.start);
  if (!context->rerelease) state->frame++;
  qa_q2w_weapons_consume(weapons, context, context->definition->quantity, context->rerelease);
  qa_q2w_weapons_attack_animation(weapons, context, 1);
}

static void fire_tracker(qa_q2mp_weapons *w, qa_q2w_context *context, qa_q2w_weapons *weapons) {
  qa_q2_game_services *game = context->game;
  qa_q2w_owner self = context->self;
  qa_q2w_projection projection = qa_q2w_weapons_project_default(weapons, context, q2mp_v3(24, 8, -8));
  qa_vec3 end = q2mp_add(projection.start, q2mp_scale(projection.direction, 8192));
  double mask = context->rerelease ? (double)(int64_t)0x46004003 : (double)0x6000003;
  if (context->rerelease && !context->input.players_collide) mask = (double)(qa_js_to_int32(q2mp_u32(mask) & ~0x40000000u));
  void *token = NULL;
  bool lag = context->rerelease && weapons->base.hooks.lag_is_history;
  if (lag) token = weapons->base.hooks.lag_begin(weapons->base.hooks.ctx, self.actor.id, projection.start, projection.direction);
  qa_trace_result trace = q2mp_trace(game, projection.start, end, NULL, &self.actor.id, mask);
  if (lag) weapons->base.hooks.lag_end(weapons->base.hooks.ctx, token);
  qa_actor_id world = game->host->world_actor(game->host->ctx);
  if (trace.hit.kind != QA_TRACE_HIT_ACTOR || qa_same_actor(trace.hit.actor, world)) {
    qa_bounds bounds = {q2mp_v3(-16, -16, -16), q2mp_v3(16, 16, 16)};
    trace = q2mp_trace(game, projection.start, end, &bounds, &self.actor.id, mask);
  }
  bool has_actor = trace.hit.kind == QA_TRACE_HIT_ACTOR;
  qa_actor_id actor = has_actor ? trace.hit.actor : self.actor.id;
  bool has_enemy = false;
  if (has_actor) {
    qa_q2_entity *target = q2mp_ent(game, true, actor);
    has_enemy = (game->host->is_monster(game->host->ctx, actor) || game->host->is_player(game->host->ctx, actor) ||
                 (target != NULL && target->damageable_target)) && q2mp_health(game, actor) > 0;
  }
  qa_q2w_weapons_kick_default(weapons, context, q2mp_scale(q2mp_axes(context->input.angles).forward, -2), q2mp_v3(-1, 0, 0));
  double left = (context->rerelease ? (q2mp_dm(game) ? 45 : 135) : (q2mp_dm(game) ? 30 : 45));
  double right = qa_q2w_weapons_multiplier(weapons, context);
  double damage = left *
                  right;
  (void)qa_q2mp_bolts_fire_tracker(bolts_of(w), self.actor, game, projection.start, projection.direction, damage, 1000,
                                   has_enemy, actor);
  qa_q2w_event ev;
  memset(&ev, 0, sizeof ev);
  ev.kind = QA_Q2W_EVENT_MUZZLEFLASH; ev.actor = &self.actor.id; ev.flash = 35;
  ev.silenced = context->rerelease && context->silenced;
  weapons->base.hooks.emit(weapons->base.hooks.ctx, ev);
  noise(weapons, context, projection.start);
  if (!context->rerelease) context->state->frame++;
  qa_q2w_weapons_consume(weapons, context, context->definition->quantity, context->rerelease);
}
static void fire_heat(qa_q2mp_weapons *w, qa_q2w_context *context, qa_q2w_weapons *weapons) {
  qa_q2_game_services *game = context->game;
  qa_q2w_owner self = context->self;
  qa_q2w_state *state = context->state;
  qa_q2w_projection projection = qa_q2w_weapons_project_default(weapons, context, q2mp_v3(7, 2, -3));
  if (context->rerelease) {
    if (!qa_q2w_weapons_continues_attack(weapons, context) || qa_q2w_weapons_ammo(weapons, context) < 2) {
      state->frame = 13; state->view_skin = 0;
      qa_q2w_weapons_set_loop(weapons, self, game, state, "");
      if (qa_q2w_weapons_continues_attack(weapons, context)) qa_q2w_weapons_no_ammo(weapons, context, true);
      return;
    }
    state->frame = state->frame > 12 || state->frame == 11 ? 8 : state->frame + 1;
    state->view_skin = 1;
    qa_q2w_weapons_set_loop(weapons, self, game, state, "weapons/bfg__l1a.wav");
    qa_q2w_weapons_powerup_sound(weapons, context);
  } else {
    state->frame++;
    state->view_model = "models/weapons/v_beamer2/tris.md2";
  }
  qa_q2w_weapons_kick_default(weapons, context, q2mp_zero(), q2mp_zero());
  void *token = NULL;
  bool lag = context->rerelease && weapons->base.hooks.lag_is_history;
  if (lag) token = weapons->base.hooks.lag_begin(weapons->base.hooks.ctx, self.actor.id, projection.start, projection.direction);
  qa_q2mp_bolts * b = bolts_of(w);
  qa_owned_actor self2 = self.actor;
  qa_vec3 start2 = projection.start;
  qa_vec3 direction2 = projection.direction;
  qa_vec3 offset = q2mp_v3(2, 7, -3);
  double damage = 15 * qa_q2w_weapons_multiplier(weapons, context);
  double left = (q2mp_dm(game) ? 75 : 30);
  double right = qa_q2w_weapons_multiplier(weapons, context);
  double kick = left * right;
  qa_q2mp_bolts_fire_heat_beam(b, self2, game, start2, direction2, offset, damage, kick);
  if (lag) weapons->base.hooks.lag_end(weapons->base.hooks.ctx, token);
  qa_q2w_weapons_flash(weapons, context, 33);
  noise(weapons, context, projection.start);
  qa_q2w_weapons_consume_default(weapons, context);
  qa_q2w_weapons_attack_animation(weapons, context, 1);
}

static qa_vec3 closest_point(qa_vec3 point, qa_vec3 min, qa_vec3 max) {
  return q2mp_v3(point.x < min.x ? min.x : point.x > max.x ? max.x : point.x,
                 point.y < min.y ? min.y : point.y > max.y ? max.y : point.y,
                 point.z < min.z ? min.z : point.z > max.z ? max.z : point.z);
}
static void fire_chainfist_rr(qa_q2mp_weapons *w, qa_q2w_context *context, qa_q2w_weapons *weapons) {
  qa_q2_game_services *game = context->game;
  qa_q2w_owner self = context->self;
  qa_q2w_state *state = context->state;
  if (!qa_q2w_weapons_continues_attack(weapons, context) &&
      (state->frame == 13 || state->frame == 23 || state->frame >= 32)) {
    state->frame = 33;
    return;
  }
  qa_q2w_projection projection = qa_q2w_weapons_project_default(weapons, context, q2mp_v3(0, 0, -4));
  qa_body_state own = game->body(game->ctx, self.actor);
  qa_vec3 own_min = q2mp_add(own.origin, own.bounds.min), own_max = q2mp_add(own.origin, own.bounds.max);
  double count = 0;
  bool hit = false;
  size_t observed = 0;
  qa_actor_id *actors = q2mp_observations(game, &observed);
  for (size_t i = 0; i < observed; i++) {
    double left;
    double right;
    qa_actor_id actor = actors[i];
    if (qa_same_actor(actor, self.actor.id) || !q2mp_can_hurt(game, actor)) continue;
    qa_body_state_result body = q2mp_bread(game, actor);
    qa_q2_entity *target = q2mp_ent(game, true, actor);
    if (!body.has_value || (target != NULL && (strcmp(target->solid, "none") == 0 || strcmp(target->solid, "trigger") == 0))) continue;
    qa_vec3 min = q2mp_add(body.value.origin, body.value.bounds.min), max = q2mp_add(body.value.origin, body.value.bounds.max);
    if (min.x > own_max.x + 23 || max.x < own_min.x - 23 || min.y > own_max.y + 23 || max.y < own_min.y - 23 ||
        min.z > own_max.z + 23 || max.z < own_min.z - 23) continue;
    qa_vec3 point = closest_point(projection.start, min, max);
    qa_vec3 near = closest_point(point, own_min, own_max);
    if (q2mp_len(q2mp_sub(point, near)) > 24) continue;
    bool intersect = min.x + 2 <= own_max.x - 2 && max.x - 2 >= own_min.x + 2 && min.y + 2 <= own_max.y - 2 &&
                     max.y - 2 >= own_min.y + 2 && min.z + 2 <= own_max.z - 2 && max.z - 2 >= own_min.z + 2;
    if (!intersect &&
        q2mp_dot(q2mp_norm(q2mp_sub(q2mp_scale(q2mp_add(min, max), 0.5), projection.start)), projection.direction) < 0.7) continue;
    if (++count > 4) break;
    bool visible = target == NULL ? game->can_damage(game->ctx, actor, self.actor)
                                  : game->can_damage(game->ctx, self.actor.id, target->actor);
    if (!visible) continue;
    qa_q2m_context *monster = bolts_of(w)->hooks.monster(bolts_of(w)->hooks.ctx, actor);
    if (monster != NULL) monster->state->pain_time -= 0.005 + q2mp_rnd(game) * 0.07;
    q2mp_damage(game, actor, NULL, self.actor.id, &self.actor.id,
                (left = (q2mp_dm(game) ? 15 : 7), right = qa_q2w_weapons_multiplier(weapons, context), left * right), 50, projection.direction, point,
                q2mp_scale(projection.direction, -1), QA_Q2MP_MOD_CHAINFIST, true, 64 | 8, "q2:weapon_chainfist");
    hit = true;
  }
  free(actors);
  if (hit && state->empty_sound_time < context->now) {
    state->empty_sound_time = context->now + 0.5;
    game->sound(game->ctx, self.actor, "weapons/sawslice.wav", 1, 1, 1);
  }
  noise(weapons, context, projection.start);
  state->frame++;
  if (qa_q2w_weapons_continues_attack(weapons, context)) {
    if (state->frame == 12) state->frame = 14;
    else if (state->frame == 22) state->frame = 24;
    else if (state->frame >= 32) state->frame = 7;
  }
  qa_q2w_weapons_attack_animation(weapons, context, 1);
}
static void fire_chainfist(qa_q2mp_weapons *w, qa_q2w_context *context, qa_q2w_weapons *weapons) {
  if (context->rerelease) { fire_chainfist_rr(w, context, weapons); return; }
  qa_q2_game_services *game = context->game;
  qa_q2w_owner self = context->self;
  qa_q2w_projection projection = qa_q2w_weapons_project_default(weapons, context, q2mp_v3(0, 8, -4));
  qa_angle_vectors axes = q2mp_axes(context->input.angles);
  qa_q2w_weapons_kick_default(weapons, context, q2mp_scale(axes.forward, -2), q2mp_v3(-1, 0, 0));
  qa_trace_result trace = q2mp_trace(game, projection.start, q2mp_add(projection.start, q2mp_scale(projection.direction, 64)),
                                     NULL, &self.actor.id, 0x6000003);
  if (trace.fraction < 1) {
    bool has_target = trace.hit.kind == QA_TRACE_HIT_ACTOR;
    qa_actor_id target = has_target ? trace.hit.actor : self.actor.id;
    if (has_target && q2mp_can_hurt(game, target)) {
      double left;
      double right;
      qa_q2mp_velocity(game, self.actor.id,
                       q2mp_add(q2mp_add(game->body(game->ctx, self.actor).velocity, q2mp_scale(axes.forward, 75)),
                                q2mp_scale(axes.up, 75)), false);
      qa_body_state_result target_body = q2mp_bread(game, target);
      q2mp_damage(game, target, NULL, self.actor.id, &self.actor.id,
                  (left = (q2mp_dm(game) ? 30 : 15), right = qa_q2w_weapons_multiplier(weapons, context), left * right), 50, q2mp_zero(),
                  target_body.has_value ? target_body.value.origin : trace.end, q2mp_zero(), QA_Q2MP_MOD_CHAINFIST, true,
                  64 | 8, "q2:weapon_chainfist");
    } else {
      qa_q2_presentation_event e;
      memset(&e, 0, sizeof e);
      e.kind = "effect"; e.effect = "q2:gunshot"; e.origin = trace.end;
      e.direction = q2mp_normal(trace); e.count = 0; e.color = 0;
      game->host->emit(game->host->ctx, e);
    }
  }
  noise(weapons, context, projection.start);
  context->state->frame++;
}

static void fire_fn(void *ctx, qa_q2w_context *context, qa_q2w_weapons *weapons) {
  qa_q2mp_weapons_ext_ctx *e = ctx;
  const char *name = context->definition->name;
  if (name_is(name, "ionripper")) fire_ion(e->owner, context, weapons);
  else if (name_is(name, "phalanx")) fire_phalanx(e->owner, context, weapons);
  else if (name_is(name, "etf_rifle")) fire_etf(e->owner, context, weapons);
  else if (name_is(name, "disintegrator")) fire_tracker(e->owner, context, weapons);
  else if (name_is(name, "heatbeam")) fire_heat(e->owner, context, weapons);
  else if (name_is(name, "chainfist")) fire_chainfist(e->owner, context, weapons);
  else if (name_is(name, "proxlauncher")) fire_prox(e->owner, context, weapons);
  else qa_raise("Error", 0, 0, "No mission-pack fire callback for %s", name);
}
static bool selection_choose(void *ctx, qa_q2w_owner self, qa_q2_game_services *game, qa_q2w_state *state) {
  qa_q2mp_weapons_ext_ctx *e = ctx;
  const char *requested = name_is(e->definition->name, "ionripper") ? "hyperblaster" : "railgun";
  if (qa_shared_inventory_table_count(game->host->inventory, self.actor.id, e->definition->item) == 0) return false;
  if (strcmp(requested, "hyperblaster") == 0) return name_is(state->weapon, requested);
  if (qa_shared_inventory_table_count(game->host->inventory, self.actor.id, "q2:ammo_slugs") == 0)
    return qa_shared_inventory_table_count(game->host->inventory, self.actor.id, "q2:ammo_magslug") > 0;
  return name_is(state->weapon, requested);
}

void qa_q2mp_weapons_init(qa_q2mp_weapons *weapons, qa_q2mp_projectiles *projectiles) {
  memset(weapons, 0, sizeof *weapons);
  weapons->projectiles = projectiles;
}
static const double ion_fires_rr[] = {6};
void qa_q2mp_weapons_register(qa_q2mp_weapons *weapons, qa_q2w_weapons *target, qa_q2mp_pack pack, qa_q2_edition edition) {
  const qa_q2w_definition *originals = pack == QA_Q2MP_PACK_XATRIX ? qa_q2mp_xatrix_weapon_definitions
                                                                    : qa_q2mp_rogue_weapon_definitions;
  size_t count = pack == QA_Q2MP_PACK_XATRIX ? qa_q2mp_xatrix_weapon_definition_count
                                             : qa_q2mp_rogue_weapon_definition_count;
  for (size_t i = 0; i < count; i++) {
    if (weapons->def_count >= 12) qa_raise("Error", 0, 0, "Mission-pack weapon registration overflow");
    qa_q2w_definition definition = originals[i];
    if (edition == QA_Q2_EDITION_RERELEASE) {
      if (name_is(definition.name, "ionripper")) {
        definition.activate_last = 5; definition.fire_last = 7; definition.fires = ion_fires_rr; definition.fire_count = 1;
      } else if (name_is(definition.name, "heatbeam")) {
        definition.repeating = true; definition.idle_last = 42; definition.deactivate_last = 47;
      } else if (name_is(definition.name, "chainfist") || name_is(definition.name, "etf_rifle")) definition.repeating = true;
    }
    size_t slot = weapons->def_count++;
    weapons->defs[slot] = definition;
    weapons->ext_ctxs[slot] = (qa_q2mp_weapons_ext_ctx){weapons, &weapons->defs[slot]};
    qa_q2w_extension extension;
    memset(&extension, 0, sizeof extension);
    extension.definition = &weapons->defs[slot];
    extension.ctx = &weapons->ext_ctxs[slot];
    extension.fire = fire_fn;
    extension.think = think_fn;
    if (name_is(definition.name, "trap") || name_is(definition.name, "tesla")) extension.held = held_fn;
    if (edition == QA_Q2_EDITION_CLASSIC && pack == QA_Q2MP_PACK_XATRIX &&
        (name_is(definition.name, "ionripper") || name_is(definition.name, "phalanx"))) {
      extension.selection_requested = name_is(definition.name, "ionripper") ? "hyperblaster" : "railgun";
      extension.selection_choose = selection_choose;
    }
    qa_q2w_weapons_register(target, extension);
  }
  if (pack == QA_Q2MP_PACK_ROGUE) {
    static const qa_q2w_name order[] = {"railgun", "heatbeam", "etf_rifle", "chaingun",
                                        "machinegun", "supershotgun", "shotgun", "blaster"};
    qa_q2w_weapons_set_fallback_order(target, order, 8);
  }
}

