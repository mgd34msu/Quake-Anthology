/* Port of src/content/q2/rerelease/view.ts. GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/view.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/core/math.h"
#include "rr_internal.h"
#include <math.h>
#include <string.h>

qa_q2rr_base_view_ops qa_q2rr_base_view;
static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
static double kick_ratio(double until, double now, double duration, double slack) {
  double remaining = until - now;
  if (remaining <= 0) return 0;
  return (slack != 0 && remaining > duration) ? (duration + slack - remaining) / slack : remaining / duration;
}

qa_q2rr_feedback_result qa_q2_rerelease_damage_feedback(qa_q2rr_player_context *c, qa_q2rr_player_extra_state *extra, double pain_index) {
  qa_q2rr_player_state *st = c->state; qa_q2_game_services *g = c->game; qa_q2_entity *e = c->entity;
  double now = rr_now(g);
  double blood = st->damage_blood, armor = st->damage_armor, power = st->damage_power_armor, kick = st->damage_knockback;
  double total = blood + armor + power, old_alpha = st->damage_alpha, old_pain = st->pain_debounce;
  if (!qa_q2rr_base_view.damage_feedback) qa_q2rr_error("q2DamageFeedback base op missing");
  qa_q2rr_feedback_result result = qa_q2rr_base_view.damage_feedback(qa_q2rr_base_view.ctx, c, pain_index);
  if (result.flashes != 0) { extra->flashes = result.flashes; extra->flash_time = now + 0.1; }
  else if (extra->flash_time < now) extra->flashes = 0;
  if (total != 0) {
    extra->animation_time = 0;
    double count = blood != 0 ? (total > 10 ? total : 10) : (total < 2 ? total : 2);
    st->damage_alpha = old_alpha > 0 ? old_alpha : 0;
    if (blood != 0 || st->damage_alpha + count * 0.06 < 0.15)
      st->damage_alpha = clampd(st->damage_alpha + count * 0.06, 0.06, 0.4);
    st->damage_blend = qa_q2_normalize(rr_v3(armor / total + (blood != 0 ? (15 > blood / total ? 15 : blood / total) : 0),
                                             (power + armor) / total, armor / total));
    double health = rr_health(g, e->actor.id);
    if (kick != 0 && health > 0) {
      double amount = clampd(fabs(kick) * 100 / health, count * 0.5, 50);
      qa_vec3 direction = qa_q2_normalize(qa_q2_subtract(st->damage_from, rr_body_of(g, e).origin));
      qa_angle_vectors v = qa_math_angle_vectors_rule(c->movement.view_angles, QA_ANGLE_RULE_NATIVE);
      st->damage_roll = amount * qa_q2_dot(direction, v.right) * 0.3;
      st->damage_pitch = -amount * qa_q2_dot(direction, v.forward) * 0.3;
      st->damage_time = now + 0.5 + (0.1 - rr_frame(g));
    }
    if (st->pain_debounce > old_pain) c->hooks->noise(c->hooks->ctx, e->actor.id, rr_body_of(g, e).origin);
  }
  qa_q2rr_feedback_result out = {extra->flashes, result.pain_index};
  return out;
}

typedef struct { qa_q2rr_player_context *inner; qa_q2_player_powerups powers; double now; } shared_call;
static void shared_sound(void *ctx, qa_q2_entity *e, const char *path, double ch, double vol, double att) {
  shared_call *s = ctx;
  bool has_until = true; double until = 0;
  if (strcmp(path, "items/damage2.wav") == 0) until = s->powers.quad_until;
  else if (strcmp(path, "items/protect2.wav") == 0) until = s->powers.invulnerability_until;
  else if (strcmp(path, "items/airout.wav") == 0) until = s->powers.enviro_until > s->now ? s->powers.enviro_until : s->powers.breather_until;
  else has_until = false;
  if (has_until && llround((until - s->now) * 1000) != 3000) return;
  rr_sound_ent(s->inner->game, e, path, ch, vol, att);
}

qa_q2rr_player_view qa_q2_rerelease_build_view(qa_q2rr_player_context *c, qa_q2rr_player_extra_state *extra, double flashes, bool intermission) {
  qa_q2rr_player_state *st = c->state; qa_q2_game_services *g = c->game; qa_q2_entity *e = c->entity;
  double now = rr_now(g), dt = rr_frame(g);
  qa_q2_player_powerups powers = c->powerups(c->call_ctx);
  qa_q2rr_player_context shared = *c;
  shared_call sc = {c, powers, now};
  shared.call_ctx = &sc; shared.sound = shared_sound;
  double alpha = st->damage_alpha, bonus = st->bonus_alpha;
  if (!qa_q2rr_base_view.build_view) qa_q2rr_error("q2BuildView base op missing");
  qa_q2rr_player_view base = qa_q2rr_base_view.build_view(qa_q2rr_base_view.ctx, &shared, flashes, intermission);
  st->damage_alpha = alpha - dt * 0.6 > 0 ? alpha - dt * 0.6 : 0;
  st->bonus_alpha = bonus - dt > 0 ? bonus - dt : 0;
  if (intermission) { base.offset = QA_Q2_ZERO; return base; }
  qa_body_state body = rr_body_of(g, e);
  qa_q2rr_weapon_result weapon = c->weapon_state(c->call_ctx);
  qa_angle_vectors vectors = qa_math_angle_vectors_rule(c->movement.view_angles, QA_ANGLE_RULE_NATIVE);
  double speed = hypot(body.velocity.x, body.velocity.y);
  bool duck = c->movement.ducked && c->movement.grounded;
  double bob_time = st->bob_time * (duck ? 4 : 1);
  int64_t cycle = (int64_t)trunc(bob_time);
  double bob = fabs(sin(bob_time * 3.141592653589793));
  double sign = (cycle & 1) != 0 ? -1 : 1;
  double fall = kick_ratio(st->fall_time, now, 0.3, 0.1 - dt);
  const qa_q2rr_player_rules *rules = c->rules;
  qa_vec3 kick = QA_Q2_ZERO, offset = QA_Q2_ZERO;
  if (!st->dead && !extra->bob_skip) {
    qa_vec3 recoil = weapon.has ? weapon.v.kick_angles : QA_Q2_ZERO;
    double damage = kick_ratio(st->damage_time, now, 0.5, 0.1 - dt);
    double bx = bob * rules->bob_pitch * speed * (duck ? 6 : 1); if (bx > 1.2) bx = 1.2;
    double bz = bob * rules->bob_roll * speed * (duck ? 6 : 1); if (bz > 1.2) bz = 1.2;
    kick.x = recoil.x + damage * st->damage_pitch + fall * st->fall_value + qa_q2_dot(body.velocity, vectors.forward) * rules->run_pitch + bx;
    kick.y = recoil.y;
    kick.z = recoil.z + damage * st->damage_roll + qa_q2_dot(body.velocity, vectors.right) * rules->run_roll + bz * sign;
    if (extra->quake_time > now) {
      double factor = extra->quake_time / now * 0.25; if (factor > 1) factor = 1;
      kick.x += (g->host->random(g->host->ctx) * 2 - 1) * factor;
      kick.y += (g->host->random(g->host->ctx) * 2 - 1) * factor;
      kick.z += (g->host->random(g->host->ctx) * 2 - 1) * factor;
    }
  }
  if (!extra->bob_skip) {
    qa_vec3 recoil = weapon.has ? weapon.v.kick_origin : QA_Q2_ZERO;
    double bz = bob * speed * rules->bob_up; if (bz > 6) bz = 6;
    offset.x = clampd(recoil.x, -14, 14);
    offset.y = clampd(recoil.y, -14, 14);
    offset.z = clampd(-fall * st->fall_value * 0.4 + bz + recoil.z, -22, 30);
  }
  qa_vec3 gun = QA_Q2_ZERO;
  qa_q2rr_weapon_state_entry *src_w = NULL;
  if (c->weapons && c->weapons->get_state) src_w = c->weapons->get_state(c->weapons->ctx, e->actor.id);
  bool suppress = weapon.has && weapon.v.q2_name &&
    (strcmp(weapon.v.q2_name, "heatbeam") == 0 || strcmp(weapon.v.q2_name, "grapple") == 0) &&
    src_w && src_w->phase && strcmp(src_w->phase, "firing") == 0;
  if (weapon.has && !suppress) {
    qa_vec3 delta = qa_q2_subtract(st->old_view_angles, base.angles);
    qa_vec3 slow = extra->slow_view_angles;
    double in[3] = {slow.x, slow.y, slow.z}, ch[3] = {delta.x, delta.y, delta.z}, kk[3], vv[3];
    for (int i = 0; i < 3; i++) {
      double value = in[i] + ch[i];
      if (value > 180) value -= 360;
      if (value < -180) value += 360;
      value = clampd(value, -45, 45);
      double amount = dt * 1000 * (ch[i] != 0 ? 0.05 : 0.15);
      kk[i] = value;
      vv[i] = value > 0 ? (value - amount > 0 ? value - amount : 0) : (value + amount < 0 ? value + amount : 0);
    }
    extra->slow_view_angles = rr_v3(vv[0], vv[1], vv[2]);
    gun.x = speed * bob * 0.005 + kk[0] * 0.1;
    gun.y = speed * bob * 0.01 * sign + kk[1] * 0.1;
    gun.z = -(speed * bob * 0.005 * sign + kk[2] * 0.05);
  }
  base.offset = offset;
  base.kick_angles = rr_v3(clampd(kick.x, -31, 31), clampd(kick.y, -31, 31), clampd(kick.z, -31, 31));
  base.gun_angles = gun;
  return base;
}

void qa_q2_rerelease_client_animation(qa_q2rr_player_context *c, qa_q2rr_player_extra_state *extra) {
  qa_q2rr_player_state *st = c->state; qa_q2_entity *e = c->entity; qa_q2_game_services *g = c->game;
  if (!c->movement.animate_q2 || st->gibbed) return;
  qa_body_state b = rr_body_of(g, e);
  bool run = hypot(b.velocity.x, b.velocity.y) != 0, duck = c->movement.ducked;
  double priority = st->animation_priority == 6 ? 256 : st->animation_priority;
  bool reversed = rr_band(priority, 256) != 0;
  bool changed = (duck != st->animation_duck && priority < 5) || (run != st->animation_run && priority == 0) ||
                 (!c->movement.grounded && priority <= 1);
  if (!changed) {
    if (extra->animation_time > rr_now(g)) return;
    if ((reversed && e->frame > st->animation_end) || (!reversed && e->frame < st->animation_end)) {
      e->frame += reversed ? -1 : 1; extra->animation_time = rr_now(g) + 0.1; return;
    }
    if (priority == 5) return;
    if (priority == 2) {
      if (!c->movement.grounded) return;
      st->animation_priority = duck ? 257 : 1; e->frame = duck ? 71 : 68; st->animation_end = duck ? 69 : 71;
      extra->animation_time = rr_now(g) + 0.1; return;
    }
  }
  st->animation_priority = 0; st->animation_duck = duck; st->animation_run = run;
  extra->animation_time = rr_now(g) + 0.1;
  if (!c->movement.grounded && !extra->grapple_attached) {
    st->animation_priority = 2;
    if (duck) { if (e->frame != 155) e->frame = 154; st->animation_end = 155; }
    else { if (e->frame != 67) e->frame = 66; st->animation_end = 67; }
  } else if (c->movement.grounded && run) { e->frame = duck ? 154 : 40; st->animation_end = duck ? 159 : 45; }
  else { e->frame = duck ? 135 : 0; st->animation_end = duck ? 153 : 39; }
}
