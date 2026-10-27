/* Port of src/content/q1/missionpacks/world/rogue-plats.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/rogue_plats.h"
#include "../missionpacks_internal.h"
#include "qa/content/q1/missionpacks/world/common.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>

typedef struct rp_ctx { qa_q1mp_game *game; } rp_ctx;
typedef struct up_ctx { rp_ctx base; bool up; } up_ctx;
static up_ctx up_ctxs[2];
static void move_plat(rp_ctx *c, qa_q1b_game *g, qa_q1b_actor *e, bool up) {
  (void)c; g->sound(g, e, qa_q1mp_text(e, "noise"), QA_Q1MP_CH("voice"), 1, 1);
  e->state = up ? "up" : "down";
  if (g->calc_move) g->calc_move(g, e, up ? e->pos1 : e->pos2, e->speed, g->named.action(g->named.ctx, e, up ? "rogue:plat_top" : "rogue:plat_bottom"));
}
static void elevator_go(rp_ctx *c, qa_q1b_game *g, qa_q1b_actor *e) {
  (void)c; g->sound(g, e, qa_q1mp_text(e, "noise"), QA_Q1MP_CH("voice"), 1, 1);
  e->state = "up"; qa_q1mp_world_number(e, "elevatorLastUse", g->time);
  qa_vec3 dest = e->pos2; dest.z = e->pos2.z + qa_q1mp_number(e,"height") * qa_q1mp_number(e,"elevatorToFloor");
  if (g->calc_move) g->calc_move(g, e, dest, e->speed, g->named.action(g->named.ctx, e, "rogue:elevator_stop"));
}
static void button_fire(rp_ctx *c, qa_q1b_game *g, qa_q1b_actor *e) {
  (void)c;
  if (qa_q1mp_streq(e->state,"up")||qa_q1mp_streq(e->state,"top")) return;
  g->sound(g, e, qa_q1mp_text(e, "noise"), QA_Q1MP_CH("voice"), 1, 1); e->state = "up";
  if (g->calc_move) g->calc_move(g, e, e->pos2, e->speed, g->named.action(g->named.ctx, e, "rogue:elvbutton_wait"));
}
static void plat_move_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) { move_plat(&((up_ctx *)vctx)->base, g, e, ((up_ctx *)vctx)->up); }
static void plat_end_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  bool up = ((up_ctx *)vctx)->up;
  g->sound(g, e, qa_q1mp_text(e, "noise1"), QA_Q1MP_CH("voice"), 1, 1);
  e->state = up ? "top" : "bottom";
  if ((e->spawnflags & 1) != 0 && !up) {
    if (g->schedule_at) g->schedule_at(g, e, qa_q1mp_fround(qa_q1mp_number(e,"ltime") + g->health(g, e->actor.id)), g->named.action(g->named.ctx, e, "rogue:plat_up"));
    return;
  }
  if ((e->spawnflags & 16) == 0) return;
  qa_q1mp_world_number(e, "plat2LastMove", g->time);
  if (qa_q1mp_number(e, "plat2Called") == 1) {
    qa_q1mp_world_number(e, "plat2Called", 0); qa_q1mp_world_number(e, "plat2LastMove", 0);
    if (g->schedule_at) g->schedule_at(g, e, qa_q1mp_fround(qa_q1mp_number(e,"ltime") + 1.5), g->named.action(g->named.ctx, e, up ? "rogue:plat_down" : "rogue:plat_up"));
    return;
  }
  if (up != ((e->spawnflags & 8) != 0)) {
    qa_q1mp_world_number(e, "plat2Called", 0);
    if (g->schedule_at) g->schedule_at(g, e, qa_q1mp_fround(qa_q1mp_number(e,"ltime") + e->delay), g->named.action(g->named.ctx, e, up ? "rogue:plat_down" : "rogue:plat_up"));
  }
}
static void plat_blocked_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_actor_id other) {
  rp_ctx *c = vctx; qa_q1b_game_damage(g, other, e->actor.id, QA_Q1MP_SOME(e->actor.id), 1);
  if (!qa_q1mp_streq(e->state,"up") && !qa_q1mp_streq(e->state,"down")) qa_range_error("plat_new_crush: bad self.state");
  move_plat(c, g, e, qa_q1mp_streq(e->state, "down"));
}
static void plat_use_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) {
  (void)o; (void)a; rp_ctx *c = vctx;
  if ((e->spawnflags & 1) != 0) { if (qa_q1mp_streq(e->state,"top")) move_plat(c, g, e, false); return; }
  if (qa_q1mp_streq(e->state,"top")) move_plat(c, g, e, false);
  else if (qa_q1mp_streq(e->state,"bottom")) move_plat(c, g, e, true);
}
static void elevator_stop_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  (void)vctx; qa_q1mp_world_number(e, "elevatorOnFloor", qa_q1mp_number(e,"elevatorToFloor"));
  g->sound(g, e, qa_q1mp_text(e, "noise1"), QA_Q1MP_CH("voice"), 1, 1); e->state = "bottom";
  qa_q1mp_world_number(e, "elevatorLastUse", g->time);
}
static void elevator_blocked_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_actor_id other) {
  (void)other; rp_ctx *c = vctx;
  qa_q1mp_world_number(e, "elevatorToFloor", qa_q1mp_number(e,"elevatorOnFloor"));
  elevator_go(c, g, e);
}
static void elevator_use_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor other, qa_q1mp_opt_actor a) {
  (void)a; rp_ctx *c = vctx;
  if (qa_q1mp_number(e, "elevatorLastUse") + 2 > g->time) return;
  qa_q1mp_world_number(e, "elevatorLastUse", g->time);
  double direction = g->world ? qa_q1mp_number(g->world, "rogue:elvButnDir") : 0;
  if (direction == 0) return;
  qa_q1mp_actor *button = other.has ? g->entity(g, other) : NULL;
  if (!button) button = g->world;
  if (!button) return;
  qa_q1b_body body = g->body(g, e), other_body = g->body(g, button);
  double position = body.origin.z + (body.bounds.min.z + body.bounds.max.z) / 2;
  double button_position = other_body.origin.z + (other_body.bounds.min.z + other_body.bounds.max.z) / 2;
  double height = qa_q1mp_number(e,"height"), floor_n = qa_q1mp_number(e,"elevatorOnFloor");
  if (position > button_position) qa_q1mp_world_number(e, "elevatorToFloor", floor_n - ceil((position - button_position) / height));
  else if (button_position - position > height) qa_q1mp_world_number(e, "elevatorToFloor", floor_n + floor((button_position - position) / height));
  else if (direction == -1 && floor_n > 0) qa_q1mp_world_number(e, "elevatorToFloor", floor_n - 1);
  else if (direction == 1 && floor_n < qa_q1mp_number(e,"cnt") - 1) qa_q1mp_world_number(e, "elevatorToFloor", floor_n + 1);
  else return;
  elevator_go(c, g, e);
}
static void plat2_enable_use(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) {
  (void)vctx; (void)g; (void)o; (void)a; qa_q1mp_world_number(e, "plat2Disabled", 0); e->use.has = false;
}
static void plat2_center_touch(void *vctx, qa_q1b_game *g, qa_q1b_actor *trigger, qa_actor_id other, bool hn, qa_vec3 n, const void *s) {
  (void)hn; (void)n; (void)s; rp_ctx *c = vctx;
  if (!g->is_player(g, QA_Q1MP_SOME(other)) || g->health(g, other) <= 0) return;
  qa_q1mp_actor *e = g->entity(g, trigger->owner); qa_q1b_body body;
  bool has = qa_q1mp_body_read(c->game, other, &body);
  if (!e || !has || qa_q1mp_number(e,"plat2LastMove") + 2 > g->time || qa_q1mp_number(e,"plat2Disabled") != 0) return;
  double pending = qa_q1mp_number(e, "plat2GoTo");
  if (pending > 0) {
    if (qa_q1mp_number(e,"plat2GoTime") < g->time) { move_plat(c, g, e, pending == 1); qa_q1mp_world_number(e, "plat2GoTo", 0); }
    return;
  }
  if (qa_q1mp_streq(e->state,"up")||qa_q1mp_streq(e->state,"down")) return;
  qa_q1b_body platform = g->body(g, e);
  double center = platform.origin.z + (platform.bounds.min.z + platform.bounds.max.z) / 2;
  bool same_level = qa_q1mp_streq(e->state,"top") ? center <= body.origin.z : body.origin.z - center <= qa_q1mp_number(e,"height");
  qa_q1mp_world_number(e, "plat2Called", same_level ? 0 : 1);
  qa_q1mp_world_number(e, "plat2GoTime", g->time + (same_level ? 0.5 : 0.1));
  qa_q1mp_world_number(e, "plat2GoTo", qa_q1mp_streq(e->state,"bottom") ? 1 : 2);
}
static void new_plat_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  rp_ctx *c = vctx; qa_q1mp_world_brush(c->game, e);
  if (e->speed == 0) e->speed = 150;
  if (e->sounds == 0) e->sounds = 2;
  if (e->sounds == 1 || e->sounds == 2) {
    qa_q1b_fields_set(e, "noise", e->sounds == 1 ? "plats/plat1.wav" : "plats/medplat1.wav");
    qa_q1b_fields_set(e, "noise1", e->sounds == 1 ? "plats/plat2.wav" : "plats/medplat2.wav");
  }
  qa_q1b_body body = g->body(g, e);
  double height = qa_q1mp_number(e, "height"); bool negative = height < 0; height = fabs(height);
  if (height == 0) { negative = true; height = body.bounds.max.z - body.bounds.min.z - 8; }
  qa_q1mp_world_number(e, "height", height);
  e->pos1 = body.origin; e->pos2 = body.origin; e->pos2.z = body.origin.z - height;
  if ((e->spawnflags & 3) != 0) {
    e->use = g->named.use(g->named.ctx, e, "rogue:plat_use");
    e->blocked = g->named.blocked(g->named.ctx, e, "rogue:plat_blocked");
    e->state = negative ? "bottom" : "top";
    if (negative) g->set_origin(g, e, e->pos2);
    if ((e->spawnflags & 1) != 0 && g->health(g, e->actor.id) == 0) g->host->combat_set_health(g->host->ctx, e->actor, 5);
  } else if ((e->spawnflags & 4) != 0) {
    double floor_n = (e->spawnflags & 8) != 0 ? qa_q1mp_number(e,"cnt") - 1 : 0;
    qa_q1mp_world_number(e, "elevatorOnFloor", floor_n); qa_q1mp_world_number(e, "elevatorToFloor", 0); qa_q1mp_world_number(e, "elevatorLastUse", 0);
    if ((e->spawnflags & 8) != 0) { e->pos2 = body.origin; e->pos2.z = body.origin.z - height * (qa_q1mp_number(e,"cnt") - 1); }
    else { e->pos1 = body.origin; e->pos1.z = body.origin.z + height * (qa_q1mp_number(e,"cnt") - 1); e->pos2 = body.origin; }
    e->use = g->named.use(g->named.ctx, e, "rogue:elevator_use");
    e->blocked = g->named.blocked(g->named.ctx, e, "rogue:elevator_blocked");
  } else if ((e->spawnflags & 16) != 0) {
    qa_q1mp_actor *t = g->create(g, "rogue_plat2_trigger");
    t->owner = QA_Q1MP_SOME(e->actor.id); t->solid = "trigger"; t->movement = "none";
    t->touch = g->named.touch(g->named.ctx, t, "rogue:plat2_center");
    qa_vec3 mn = qa_q1mp_vadd(body.bounds.min, QA_Q1MP_V3(25,25,0)), mx = qa_q1mp_vsub(body.bounds.max, QA_Q1MP_V3(25,25,-8));
    mn.z = mx.z - height - 8;
    if ((e->spawnflags & 1) != 0) mx.z = mn.z + 8;
    if (body.bounds.max.x - body.bounds.min.x <= 50) { mn.x = (body.bounds.min.x + body.bounds.max.x) / 2; mx.x = mn.x + 1; }
    if (body.bounds.max.y - body.bounds.min.y <= 50) { mn.y = (body.bounds.min.y + body.bounds.max.y) / 2; mx.y = mn.y + 1; }
    g->set_bounds(g, t, (qa_bounds){mn, mx});
    static const char *const keys[] = {"plat2Called","plat2LastMove","plat2GoTo","plat2GoTime"};
    for (size_t i = 0; i < 4; i++) qa_q1mp_world_number(e, keys[i], 0);
    e->blocked = g->named.blocked(g->named.ctx, e, "rogue:plat_blocked");
    if (e->delay == 0) e->delay = 3;
    if (negative) { e->state = "bottom"; e->spawnflags = 16; g->set_origin(g, e, e->pos2); }
    else { e->spawnflags |= 8; e->state = "top"; }
    if (e->targetname[0]) { qa_q1mp_world_number(e, "plat2Disabled", 1); e->use = g->named.use(g->named.ctx, e, "rogue:plat2_enable"); }
  }
}
static void elvbutton_wait_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  (void)vctx;
  if (g->world) qa_q1mp_world_number(g->world, "rogue:elvButnDir", (e->spawnflags & 1) != 0 ? -1 : 1);
  e->state = "top";
  if (g->schedule_at) g->schedule_at(g, e, qa_q1mp_fround(qa_q1mp_number(e,"ltime") + e->wait), g->named.action(g->named.ctx, e, "rogue:elvbutton_return"));
  g->use_targets(g, e, e->activator); e->frame = 1;
}
static void elvbutton_done_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) { (void)vctx; (void)g; e->state = "bottom"; }
static void elvbutton_return_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  (void)vctx; e->state = "down"; e->frame = 0;
  if (g->health(g, e->actor.id) != 0) qa_q1b_actor_set_damageable(g, e, true);
  if (g->calc_move) g->calc_move(g, e, e->pos1, e->speed, g->named.action(g->named.ctx, e, "rogue:elvbutton_done"));
}
static void elvbutton_fire_use(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) { (void)o; e->activator = a; button_fire(vctx, g, e); }
static void elvbutton_fire_touch(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_actor_id other, bool hn, qa_vec3 n, const void *s) {
  (void)hn; (void)n; (void)s;
  if (!g->is_player(g, QA_Q1MP_SOME(other))) return;
  e->activator = QA_Q1MP_SOME(other); button_fire(vctx, g, e);
}
static void elvbutton_fire_die(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor a) {
  e->activator = a; g->host->combat_set_health(g->host->ctx, e->actor, e->max_health);
  qa_q1b_actor_set_damageable(g, e, false); button_fire(vctx, g, e);
}
static void elvbutton_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  (void)vctx;
  static const char *const sounds[] = {"buttons/airbut1.wav","buttons/switch21.wav","buttons/switch02.wav","buttons/switch04.wav"};
  int idx = (int)e->sounds;
  if (idx >= 0 && idx < 4) qa_q1b_fields_set(e, "noise", sounds[idx]);
  e->movedir = qa_q1b_move_direction(g, g->body(g, e).angles);
  qa_q1b_body_patch p = {0}; p.has_angles = true; p.angles = QA_Q1MP_ZERO; g->set_body(g, e, &p);
  e->movement = "push"; e->solid = "bsp"; e->use = g->named.use(g->named.ctx, e, "rogue:elvbutton_fire");
  if (g->health(g, e->actor.id) != 0) {
    e->max_health = g->health(g, e->actor.id);
    e->die = g->named.die(g->named.ctx, e, "rogue:elvbutton_fire");
    qa_q1b_actor_set_damageable(g, e, true);
  } else e->touch = g->named.touch(g->named.ctx, e, "rogue:elvbutton_fire");
  if (e->speed == 0) e->speed = 40;
  if (e->wait == 0) e->wait = 1;
  qa_q1b_body body = g->body(g, e); e->state = "bottom"; e->pos1 = body.origin;
  double lip = qa_q1mp_number(e, "lip"); if (lip == 0) lip = 4;
  e->pos2 = qa_q1mp_vadd(e->pos1, qa_q1mp_vscale(e->movedir, fabs(qa_q1mp_dot(e->movedir, qa_q1mp_vsub(body.bounds.max, body.bounds.min))) - lip));
}
void qa_q1mp_register_rogue_plats(qa_q1mp_game *game) {
  rp_ctx *c = malloc(sizeof *c); c->game = game; qa_q1b_game *g = game->base;
  for (int i = 0; i < 2; i++) {
    up_ctxs[i].base.game = game; up_ctxs[i].up = i == 0;
    qa_q1b_handlers hm = {0}; hm.ctx = &up_ctxs[i]; hm.action = plat_move_cb;
    g->named.register_named(g->named.ctx, i == 0 ? "rogue:plat_up" : "rogue:plat_down", &hm);
    qa_q1b_handlers he = {0}; he.ctx = &up_ctxs[i]; he.action = plat_end_cb;
    g->named.register_named(g->named.ctx, i == 0 ? "rogue:plat_top" : "rogue:plat_bottom", &he);
  }
  qa_q1b_handlers hb = {0}; hb.ctx = c; hb.blocked = plat_blocked_cb;
  g->named.register_named(g->named.ctx, "rogue:plat_blocked", &hb);
  qa_q1b_handlers hu = {0}; hu.ctx = c; hu.use = plat_use_cb;
  g->named.register_named(g->named.ctx, "rogue:plat_use", &hu);
  qa_q1b_handlers hes = {0}; hes.ctx = c; hes.action = elevator_stop_cb;
  g->named.register_named(g->named.ctx, "rogue:elevator_stop", &hes);
  qa_q1b_handlers heb = {0}; heb.ctx = c; heb.blocked = elevator_blocked_cb;
  g->named.register_named(g->named.ctx, "rogue:elevator_blocked", &heb);
  qa_q1b_handlers heu = {0}; heu.ctx = c; heu.use = elevator_use_cb;
  g->named.register_named(g->named.ctx, "rogue:elevator_use", &heu);
  qa_q1b_handlers hpe = {0}; hpe.ctx = c; hpe.use = plat2_enable_use;
  g->named.register_named(g->named.ctx, "rogue:plat2_enable", &hpe);
  qa_q1b_handlers hpc = {0}; hpc.ctx = c; hpc.touch = plat2_center_touch;
  g->named.register_named(g->named.ctx, "rogue:plat2_center", &hpc);
  g->register_spawn(g, "func_new_plat", new_plat_spawn, c);
  qa_q1b_handlers hbw = {0}; hbw.ctx = c; hbw.action = elvbutton_wait_cb;
  g->named.register_named(g->named.ctx, "rogue:elvbutton_wait", &hbw);
  qa_q1b_handlers hbd = {0}; hbd.ctx = c; hbd.action = elvbutton_done_cb;
  g->named.register_named(g->named.ctx, "rogue:elvbutton_done", &hbd);
  qa_q1b_handlers hbr = {0}; hbr.ctx = c; hbr.action = elvbutton_return_cb;
  g->named.register_named(g->named.ctx, "rogue:elvbutton_return", &hbr);
  qa_q1b_handlers hbf = {0}; hbf.ctx = c; hbf.use = elvbutton_fire_use; hbf.touch = elvbutton_fire_touch; hbf.die = elvbutton_fire_die;
  g->named.register_named(g->named.ctx, "rogue:elvbutton_fire", &hbf);
  g->register_spawn(g, "func_elvtr_button", elvbutton_spawn, c);
}
