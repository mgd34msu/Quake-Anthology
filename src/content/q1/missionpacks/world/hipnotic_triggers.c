/* Port of src/content/q1/missionpacks/world/hipnotic-triggers.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/hipnotic_triggers.h"
#include "../missionpacks_internal.h"
#include "qa/content/q1/missionpacks/world/common.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct tr_ctx { qa_q1mp_game *game; } tr_ctx;
static void counter_off(tr_ctx *c, qa_q1b_game *g, qa_q1b_actor *e) {
  (void)c;
  if (qa_q1mp_number(e, "cnt") != 0 && (e->spawnflags & 32) != 0) { qa_q1mp_world_number(e, "aflag", 1); return; }
  e->use = g->named.use(g->named.ctx, e, "hip:counter_start");
  qa_q1mp_world_number(e, "aflag", 0); g->cancel(g, e);
}
static void counter_tick_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  tr_ctx *c = vctx;
  double count = qa_q1mp_number(e, "cnt") + 1; qa_q1mp_world_number(e, "cnt", count);
  qa_q1mp_world_number(e, "counter_state", (e->spawnflags & 16) != 0 ? floor(qa_q1mp_random(c->game) * e->count) + 1 : count);
  g->use_targets(g, e, e->activator);
  qa_q1mp_world_later(c->game, e, e->wait, "hip:counter_tick");
  if ((e->spawnflags & 4) != 0) counter_off(c, g, e);
  if (count >= e->count) {
    qa_q1mp_world_number(e, "cnt", 0);
    if (qa_q1mp_number(e, "aflag") != 0 || (e->spawnflags & 2) == 0) {
      if ((e->spawnflags & 1) != 0) counter_off(c, g, e);
      else g->remove(g, e);
    }
  }
}
static void counter_start(tr_ctx *c, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor activator) {
  e->activator = activator; qa_q1mp_world_number(e, "aflag", 0);
  if ((e->spawnflags & 1) != 0) e->use = g->named.use(g->named.ctx, e, "hip:counter_stop");
  else e->use.has = false;
  if ((e->spawnflags & 8) != 0) { qa_q1mp_world_number(e, "cnt", 0); qa_q1mp_world_number(e, "counter_state", 0); }
  if (e->delay != 0) qa_q1mp_world_later(c->game, e, e->delay, "hip:counter_tick");
  else counter_tick_cb(c, g, e);
}
static void counter_start_use(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) { (void)o; counter_start(vctx, g, e, a); }
static void counter_start_action(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) { counter_start(vctx, g, e, QA_Q1MP_NONE); }
static void counter_stop_use(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) { (void)o; (void)a; counter_off(vctx, g, e); }
static void oncount_use(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor other, qa_q1mp_opt_actor a) {
  (void)vctx; (void)a;
  qa_q1mp_actor *counter = other.has ? g->entity(g, other) : NULL;
  double count = (counter && qa_q1mp_streq(counter->classname, "func_counter")) ? qa_q1mp_number(counter, "counter_state") : 0;
  if (count == e->count) g->use_targets(g, e, other);
}
static void use_key(tr_ctx *c, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor activator) {
  if (!activator.has || !g->is_player(g, activator) || e->attack_finished > g->time) return;
  e->attack_finished = g->time + 2;
  bool gold = (e->spawnflags & 1) != 0;
  const char *key = gold ? "q1:key/gold" : "q1:key/silver";
  qa_owned_actor owner; bool has_owner = g->host->actors_resolve_owned(g->host->ctx, activator.id, &owner);
  double wt = g->world_type;
  const char *sound = wt == 2 ? "base" : wt == 1 ? "rune" : "med";
  if (!has_owner || !qa_q1mp_consume(c->game, owner, key, 1)) {
    char need[96]; (void)snprintf(need, sizeof need, "$qc_need_%s_%s", gold ? "gold" : "silver", wt == 2 ? "keycard" : wt == 1 ? "runekey" : "key");
    g->message(g, activator, e->message[0] ? e->message : need, false);
    char path[64]; (void)snprintf(path, sizeof path, "doors/%stry.wav", sound);
    qa_q1mp_sound(c->game, e, path); return;
  }
  e->touch.has = false; e->use.has = false; e->message[0] = '\0';
  qa_q1mp_world_later(c->game, e, 0.1, "SUB_Remove");
  char path[64]; (void)snprintf(path, sizeof path, "doors/%suse.wav", sound);
  qa_q1mp_sound(c->game, e, path);
  g->use_targets(g, e, activator);
}
static void key_use(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) { (void)o; use_key(vctx, g, e, a); }
static void key_touch(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_actor_id other, bool hn, qa_vec3 n, const void *s) { (void)hn; (void)n; (void)s; use_key(vctx, g, e, QA_Q1MP_SOME(other)); }
static void counter_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  tr_ctx *c = vctx;
  if (e->wait == 0) e->wait = 1;
  e->count = floor(e->count); if (e->count <= 0) e->count = 10;
  qa_q1mp_world_number(e, "cnt", 0); qa_q1mp_world_number(e, "counter_state", 0);
  e->use = g->named.use(g->named.ctx, e, "hip:counter_start");
  if ((e->spawnflags & 64) != 0) qa_q1mp_world_later(c->game, e, 0.1, "hip:counter_start");
}
static void oncount_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  (void)vctx; e->count = floor(e->count); if (e->count <= 0) e->count = 1;
  e->use = g->named.use(g->named.ctx, e, "hip:oncount");
}
static void usekey_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  tr_ctx *c = vctx; qa_q1mp_world_trigger(c->game, e);
  e->use = g->named.use(g->named.ctx, e, "hip:key"); e->touch = g->named.touch(g->named.ctx, e, "hip:key");
}
static void remove_touch(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_actor_id other, bool hn, qa_vec3 n, const void *s) {
  (void)vctx; (void)hn; (void)n; (void)s;
  const char *cn = g->host->classname(g->host->ctx, other);
  if ((g->is_player(g, QA_Q1MP_SOME(other)) && (e->spawnflags & 2) == 0) || (qa_q1mp_starts(cn, "monster_") && (e->spawnflags & 1) == 0)) return;
  qa_q1mp_actor *victim = g->entity(g, QA_Q1MP_SOME(other));
  if (victim) { victim->touch.has = false; victim->model[0] = '\0'; }
  g->remove(g, e);
}
static void remove_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  tr_ctx *c = vctx; qa_q1mp_world_trigger(c->game, e);
  e->touch = g->named.touch(g->named.ctx, e, "hip:remove_touch");
}
static void set_gravity_touch(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_actor_id other, bool hn, qa_vec3 n, const void *s) {
  (void)hn; (void)n; (void)s; tr_ctx *c = vctx;
  if (!g->is_player(g, QA_Q1MP_SOME(other))) return;
  double grav = qa_q1mp_number(e, "gravity") == -1 ? 1 : qa_q1mp_number(e, "gravity");
  if (c->game->set_gravity) c->game->set_gravity(c->game->ctx, other, grav);
  else if (g->host->set_gravity) g->host->set_gravity(g->host->ctx, other, grav);
}
static void setgravity_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  tr_ctx *c = vctx; qa_q1mp_world_trigger(c->game, e);
  double grav = qa_q1mp_number(e, "gravity");
  qa_q1mp_world_number(e, "gravity", grav == 0 ? -1 : (grav - 1) / 100);
  e->touch = g->named.touch(g->named.ctx, e, "hip:set_gravity");
}
static void command_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) { (void)vctx; e->use = g->named.use(g->named.ctx, e, "hip:oncount"); }
static void decoy_touch(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_actor_id other, bool hn, qa_vec3 n, const void *s) {
  (void)hn; (void)n; (void)s; tr_ctx *c = vctx;
  if (!qa_q1mp_streq(g->host->classname(g->host->ctx, other), "monster_decoy")) return;
  e->touch.has = false; qa_q1mp_world_later(c->game, e, 0.1, "SUB_Remove");
  g->use_targets(g, e, QA_Q1MP_SOME(other));
}
static void decoy_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  tr_ctx *c = vctx;
  if (g->options.deathmatch != 0) { g->remove(g, e); return; }
  qa_q1mp_world_trigger(c->game, e); e->touch = g->named.touch(g->named.ctx, e, "hip:decoy_trigger");
}
static void waterfall_touch(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_actor_id other, bool hn, qa_vec3 n, const void *s) {
  (void)hn; (void)n; (void)s; tr_ctx *c = vctx;
  if (!g->is_player(g, QA_Q1MP_SOME(other))) return;
  qa_owned_actor owner; qa_q1b_body body;
  if (!g->host->actors_resolve_owned(g->host->ctx, other, &owner) || !qa_q1mp_body_read(c->game, other, &body)) return;
  qa_vec3 velocity = qa_q1mp_vadd(body.velocity, e->movedir);
  velocity.x = qa_q1mp_fround(velocity.x + e->count * (qa_q1mp_random(c->game) - 0.5));
  velocity.y = qa_q1mp_fround(velocity.y + e->count * (qa_q1mp_random(c->game) - 0.5));
  body.velocity = velocity; g->host->bodies_write(g->host->ctx, owner, &body);
}
static void waterfall_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  tr_ctx *c = vctx; qa_q1mp_world_trigger(c->game, e);
  if (e->count == 0) e->count = 100;
  double speed = e->speed != 0 ? e->speed : 50;
  e->movedir = QA_Q1MP_V3(qa_q1mp_fround(e->movedir.x * speed), qa_q1mp_fround(e->movedir.y * speed), qa_q1mp_fround(e->movedir.z * speed));
  e->touch = g->named.touch(g->named.ctx, e, "hip:waterfall");
}
static void threshold_pain(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor a, double d) { (void)vctx; (void)a; (void)d; g->host->combat_set_health(g->host->ctx, e->actor, e->max_health); }
static void threshold_die(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor attacker) {
  (void)vctx; g->host->combat_set_health(g->host->ctx, e->actor, e->max_health);
  qa_q1b_actor_set_damageable(g, e, false); g->use_targets(g, e, attacker); qa_q1b_actor_set_damageable(g, e, true);
  if ((e->spawnflags & 1) == 0) g->remove(g, e);
}
static void threshold_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  tr_ctx *c = vctx; qa_q1mp_world_brush(c->game, e);
  if ((e->spawnflags & 2) != 0) e->model[0] = '\0';
  if (e->max_health == 0) e->max_health = 60;
  g->host->combat_set_health(g->host->ctx, e->actor, e->max_health);
  qa_q1b_actor_set_damageable(g, e, true);
  e->pain = g->named.pain(g->named.ctx, e, "hip:threshold_pain");
  e->die = g->named.die(g->named.ctx, e, "hip:threshold_die");
}
static void breakaway_use(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) { (void)vctx; (void)o; (void)a; g->remove(g, e); }
static void breakaway_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  tr_ctx *c = vctx; qa_q1mp_world_brush(c->game, e);
  e->use = g->named.use(g->named.ctx, e, "hip:breakaway");
  qa_q1b_body_patch p = {0}; p.has_angles = true; p.angles = QA_Q1MP_ZERO; g->set_body(g, e, &p);
}
void qa_q1mp_register_hipnotic_triggers(qa_q1mp_game *game) {
  tr_ctx *c = malloc(sizeof *c); c->game = game; qa_q1b_game *g = game->base;
  qa_q1b_handlers h = {0}; h.ctx = c; h.action = counter_tick_cb;
  g->named.register_named(g->named.ctx, "hip:counter_tick", &h);
  qa_q1b_handlers hs = {0}; hs.ctx = c; hs.use = counter_start_use; hs.action = counter_start_action;
  g->named.register_named(g->named.ctx, "hip:counter_start", &hs);
  qa_q1b_handlers hsp = {0}; hsp.ctx = c; hsp.use = counter_stop_use;
  g->named.register_named(g->named.ctx, "hip:counter_stop", &hsp);
  qa_q1b_handlers ho = {0}; ho.ctx = c; ho.use = oncount_use;
  g->named.register_named(g->named.ctx, "hip:oncount", &ho);
  g->register_spawn(g, "func_counter", counter_spawn, c);
  g->register_spawn(g, "func_oncount", oncount_spawn, c);
  qa_q1b_handlers hk = {0}; hk.ctx = c; hk.use = key_use; hk.touch = key_touch;
  g->named.register_named(g->named.ctx, "hip:key", &hk);
  g->register_spawn(g, "trigger_usekey", usekey_spawn, c);
  qa_q1b_handlers hr = {0}; hr.ctx = c; hr.touch = remove_touch;
  g->named.register_named(g->named.ctx, "hip:remove_touch", &hr);
  g->register_spawn(g, "trigger_remove", remove_spawn, c);
  qa_q1b_handlers hg = {0}; hg.ctx = c; hg.touch = set_gravity_touch;
  g->named.register_named(g->named.ctx, "hip:set_gravity", &hg);
  g->register_spawn(g, "trigger_setgravity", setgravity_spawn, c);
  g->register_spawn(g, "trigger_command", command_spawn, c);
  qa_q1b_handlers hd = {0}; hd.ctx = c; hd.touch = decoy_touch;
  g->named.register_named(g->named.ctx, "hip:decoy_trigger", &hd);
  g->register_spawn(g, "trigger_decoy_use", decoy_spawn, c);
  qa_q1b_handlers hwf = {0}; hwf.ctx = c; hwf.touch = waterfall_touch;
  g->named.register_named(g->named.ctx, "hip:waterfall", &hwf);
  g->register_spawn(g, "trigger_waterfall", waterfall_spawn, c);
  qa_q1b_handlers htp = {0}; htp.ctx = c; htp.pain = threshold_pain;
  g->named.register_named(g->named.ctx, "hip:threshold_pain", &htp);
  qa_q1b_handlers htd = {0}; htd.ctx = c; htd.die = threshold_die;
  g->named.register_named(g->named.ctx, "hip:threshold_die", &htd);
  g->register_spawn(g, "trigger_damagethreshold", threshold_spawn, c);
  qa_q1b_handlers hb = {0}; hb.ctx = c; hb.use = breakaway_use;
  g->named.register_named(g->named.ctx, "hip:breakaway", &hb);
  g->register_spawn(g, "func_breakawaywall", breakaway_spawn, c);
}
