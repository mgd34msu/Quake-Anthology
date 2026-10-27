/* Port of src/content/q1/missionpacks/world/hipnotic-misc.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/hipnotic_misc.h"
#include "../missionpacks_internal.h"
#include "qa/content/q1/missionpacks/world/common.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct hm_ctx { qa_q1mp_game *game; } hm_ctx;
static void snd(hm_ctx *c, qa_q1mp_actor *e, const char *path, qa_q1mp_channel channel) {
  if (c->game->emit_sound_event) c->game->emit_sound_event(c->game->ctx, e->actor.id, path, channel, qa_q1b_actor_number_or(e, "volume", 1), e->speed);
}
static void play_sound(hm_ctx *c, qa_q1mp_actor *e) {
  const char *path = qa_q1mp_text(e, "noise");
  if ((e->spawnflags & 1) != 0) {
    bool active = qa_q1mp_number(e, "sound_state") == 0;
    qa_q1mp_world_number(e, "sound_state", active ? 1 : 0);
    if (!active) path = "misc/null.wav";
  }
  double channel = qa_q1mp_number(e, "impulse");
  qa_q1mp_channel ch = channel == 0 ? QA_Q1MP_CH("auto") : channel == 1 ? QA_Q1MP_CH("weapon") : channel == 2 ? QA_Q1MP_CH("voice") : channel == 3 ? QA_Q1MP_CH("item") : channel == 4 ? QA_Q1MP_CH("body") : (qa_q1mp_channel){NULL, channel == 5 ? 5 : channel == 6 ? 6 : 7};
  snd(c, e, path, ch);
}
static void play_use(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) { (void)g; (void)o; (void)a; play_sound(vctx, e); }
static void play_action(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  hm_ctx *c = vctx; (void)g;
  qa_q1mp_world_later(c->game, e, fmax(e->delay, e->wait * qa_q1mp_random(c->game)), "hip:play_sound");
  play_sound(c, e);
}
static void sound_spawn(hm_ctx *c, qa_q1b_game *g, qa_q1mp_actor *e, bool periodic) {
  if (qa_q1mp_number(e, "volume") == 0) qa_q1mp_world_number(e, "volume", 1);
  e->speed = e->speed == 0 ? 1 : e->speed == -1 ? 0 : e->speed;
  if ((e->spawnflags & 1) != 0 && qa_q1mp_number(e, "impulse") == 0) qa_q1mp_world_number(e, "impulse", 7);
  e->use = g->named.use(g->named.ctx, e, "hip:play_sound");
  if (periodic) {
    if (e->wait == 0) e->wait = 20;
    if (e->delay == 0) e->delay = 2;
    qa_q1mp_world_later(c->game, e, fmax(e->delay, e->wait * qa_q1mp_random(c->game)), "hip:play_sound");
  }
}
static void sound_class_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  hm_ctx *c = vctx; bool thunder = qa_q1mp_starts(e->classname, "random_thunder");
  if (thunder) qa_q1b_fields_set(e, "noise", "ambience/thunder1.wav");
  sound_spawn(c, g, e, !qa_q1mp_streq(e->classname,"play_sound_triggered") && !qa_q1mp_streq(e->classname,"random_thunder_triggered"));
  if (thunder) qa_q1mp_world_number(e, "impulse", 6);
}
static void become_explosion(hm_ctx *c, qa_q1mp_actor *e) {
  e->solid = "none"; e->movement = "none"; e->touch.has = false; qa_q1mp_set_model(e, "progs/s_explod.spr"); e->frame = 0;
  qa_q1mp_set_velocity(c->game, e, QA_Q1MP_ZERO); c->game->base->link(c->game->base, e);
  qa_q1mp_world_later(c->game, e, 0.1, "base:explosion_frame");
}
static void explode_impl(hm_ctx *c, qa_q1mp_actor *e) {
  qa_q1b_game *g = c->game->base;
  g->use_targets(g, e, e->activator);
  snd(c, e, e->damage < 120 ? "misc/shortexp.wav" : "misc/longexpl.wav", QA_Q1MP_CH("auto"));
  g->radius_damage(g, e->actor.id, e->owner, e->damage, QA_Q1MP_SOME(e->actor.id), NULL, NULL);
  if ((e->spawnflags & 1) != 0) qa_q1mp_effect_a(c->game, "explosion", g->body(g, e).origin, QA_Q1MP_NONE, 0);
  become_explosion(c, e);
}
static void multi_explode_impl(hm_ctx *c, qa_q1mp_actor *e) {
  qa_q1mp_game *game = c->game; qa_q1b_game *g = game->base;
  qa_q1mp_world_later(game, e, e->wait, "hip:multi_explode");
  if (qa_q1mp_number(e, "explosion_state") == 0) {
    qa_q1mp_world_number(e, "explosion_state", 1);
    qa_q1mp_world_number(e, "duration", g->time + qa_q1mp_number(e, "duration"));
    g->use_targets(g, e, e->activator);
  }
  if (g->time > qa_q1mp_number(e, "duration")) { g->remove(g, e); return; }
  qa_q1b_body body = g->body(g, e);
  qa_vec3 mn = qa_q1mp_vadd(body.origin, body.bounds.min), mx = qa_q1mp_vadd(body.origin, body.bounds.max);
  qa_q1mp_actor *explosion = g->create(g, "hip_explosion");
  explosion->owner = e->owner; explosion->damage = e->damage;
  g->set_origin(g, explosion, QA_Q1MP_V3(mn.x + qa_q1mp_random(game)*(mx.x-mn.x), mn.y + qa_q1mp_random(game)*(mx.y-mn.y), mn.z + qa_q1mp_random(game)*(mx.z-mn.z)));
  if (game->emit_sound_event) game->emit_sound_event(game->ctx, explosion->actor.id, "misc/shortexp.wav", QA_Q1MP_CH("voice"), qa_q1mp_number(e, "volume"), e->speed);
  g->radius_damage(g, explosion->actor.id, e->owner, e->damage, QA_Q1MP_SOME(e->actor.id), NULL, NULL);
  if ((e->spawnflags & 1) != 0) qa_q1mp_effect_a(game, "explosion", g->body(g, explosion).origin, QA_Q1MP_NONE, 0);
  become_explosion(c, explosion);
}
qa_q1mp_actor *qa_q1mp_multi_explosion(qa_q1mp_game *game, qa_q1mp_actor *source, qa_vec3 origin, double radius, double damage, double duration, double pause, double volume) {
  (void)source; qa_q1b_game *g = game->base;
  qa_q1mp_actor *e = g->create(g, "hip_multi_explosion");
  e->damage = damage; e->wait = pause; e->owner = g->world ? QA_Q1MP_SOME(g->world->actor.id) : QA_Q1MP_NONE;
  qa_q1mp_world_number(e, "duration", duration); qa_q1mp_world_number(e, "volume", volume);
  qa_q1b_body_patch p = {0}; p.has_origin = true; p.origin = origin;
  p.has_bounds = true; p.bounds = (qa_bounds){{-radius,-radius,-radius},{radius,radius,radius}}; qa_q1mp_patch(game, e, &p);
  hm_ctx ctx = {game}; multi_explode_impl(&ctx, e); return e;
}
void qa_q1mp_earthquake_after_physics(qa_q1mp_game *game, qa_actor_id actor) {
  qa_q1mp_actor *world = game->base->world;
  if (!world) return;
  qa_owned_actor owner; qa_q1b_body body;
  if (!game->base->host->actors_resolve_owned(game->base->host->ctx, actor, &owner) || !qa_q1mp_body_read(game, actor, &body)) return;
  if (qa_q1mp_number(world, "hip:earthquake") > game->base->time) {
    if (qa_q1mp_number(world, "hip:quakeactive") == 0) { game->base->sound_owned(game->base, owner, "misc/quake.wav", QA_Q1MP_CH("voice"), 0, 1); qa_q1mp_world_number(world, "hip:quakeactive", 1); }
    if (body.ground.has) { body.velocity = qa_q1mp_vadd(body.velocity, QA_Q1MP_V3(0,0,qa_q1mp_random(game)*150)); game->base->host->bodies_write(game->base->host->ctx, owner, &body); }
  } else if (qa_q1mp_number(world, "hip:quakeactive") == 1) { game->base->sound_owned(game->base, owner, "misc/quakeend.wav", QA_Q1MP_CH("voice"), 0, 1); qa_q1mp_world_number(world, "hip:quakeactive", 0); }
}
static void explode_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) { (void)g; explode_impl(vctx, e); }
static void explode_use(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) {
  (void)o; hm_ctx *c = vctx; e->activator = a;
  if (e->delay == 0) { explode_impl(c, e); return; }
  double delay = e->delay; e->delay = 0; qa_q1mp_world_later(c->game, e, delay, "hip:explode"); (void)g;
}
static void multi_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) { (void)g; multi_explode_impl(vctx, e); }
static void multi_use(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) {
  (void)o; hm_ctx *c = vctx; e->activator = a;
  if (e->delay == 0) { multi_explode_impl(c, e); return; }
  double delay = e->delay; e->delay = 0; qa_q1mp_world_later(c->game, e, delay, "hip:multi_explode"); (void)g;
}
static void exploder_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  (void)vctx; bool multi = qa_q1mp_streq(e->classname, "func_multi_exploder");
  e->damage = e->damage == 0 ? 120 : fmax(0, e->damage);
  if (e->speed == 0) e->speed = 1;
  if (qa_q1mp_number(e, "volume") == 0) qa_q1mp_world_number(e, "volume", multi ? 0.5 : 1);
  if (multi) {
    e->model[0] = '\0'; e->movement = "none";
    if (e->wait == 0) e->wait = 0.25;
    if (qa_q1mp_number(e, "duration") == 0) qa_q1mp_world_number(e, "duration", 1);
    qa_q1mp_world_number(e, "explosion_state", 0);
  }
  e->use = g->named.use(g->named.ctx, e, multi ? "hip:multi_explode" : "hip:explode");
}
static void rubble_touch(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_actor_id other, bool hn, qa_vec3 n, const void *s) {
  (void)vctx; (void)hn; (void)n; (void)s;
  if (qa_q1mp_number(e,"ltime") < qa_q1mp_number(e,"pausetime") || !qa_q1mp_can_damage_actor(((hm_ctx *)vctx)->game, other)) return;
  qa_q1b_game_damage(g, other, e->actor.id, e->owner, 10);
  qa_q1mp_sound_ch(((hm_ctx *)vctx)->game, e, "zombie/z_hit.wav", "weapon");
  qa_q1mp_world_number(e, "pausetime", qa_q1mp_number(e,"ltime") + 0.1);
}
static void rubble_use(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) {
  (void)o; (void)a; hm_ctx *c = vctx; qa_q1mp_game *game = c->game;
  int count = (int)fmax(1, e->count);
  for (int i = 0; i < count; i++) {
    double which = qa_q1mp_number(e,"cnt") != 0 ? qa_q1mp_number(e,"cnt") : floor(1 + 3 * qa_q1mp_random(game));
    qa_q1mp_actor *piece = g->create(g, "hip_rubble"); char model[48];
    (void)snprintf(model, sizeof model, "progs/rubble%d.mdl", which == 1 ? 1 : which == 2 ? 3 : 2);
    qa_q1mp_set_model(piece, model); piece->movement = "bounce"; piece->solid = "bbox";
    qa_q1b_body_patch p = {0}; p.has_origin = true; p.origin = g->body(g, e).origin;
    p.has_bounds = true; p.bounds = QA_Q1MP_POINT;
    p.has_velocity = true; p.velocity = QA_Q1MP_V3(70*(qa_q1mp_random(game)*2-1), 70*(qa_q1mp_random(game)*2-1), 140 + 70*qa_q1mp_random(game));
    qa_q1mp_patch(game, piece, &p);
    piece->angular_velocity = QA_Q1MP_V3(qa_q1mp_random(game)*600, qa_q1mp_random(game)*600, qa_q1mp_random(game)*600);
    piece->touch = g->named.touch(g->named.ctx, piece, "hip:rubble_touch");
    qa_q1mp_world_number(piece, "ltime", g->time); qa_q1mp_world_number(e, "pausetime", g->time);
    qa_q1mp_world_later(game, piece, 13 + qa_q1mp_random(game) * 10, "SUB_Remove"); g->link(g, piece);
  }
}
typedef struct rubble_spawn_ctx { hm_ctx base; double index; } rubble_spawn_ctx;
static void rubble_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  static const char *const names[] = {"func_rubble","func_rubble1","func_rubble2","func_rubble3"};
  double index = 0; for (int i = 0; i < 4; i++) if (qa_q1mp_streq(e->classname, names[i])) index = i;
  (void)vctx; qa_q1mp_world_number(e, "cnt", index);
  e->use = g->named.use(g->named.ctx, e, "hip:rubble_use");
}
static void earthquake_use(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) {
  (void)vctx; (void)o; (void)a; qa_q1mp_actor *world = g->world;
  if (world) qa_q1mp_world_number(world, "hip:earthquake", fmax(qa_q1mp_number(world,"hip:earthquake"), g->time + e->damage));
}
static void earthquake_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  (void)vctx;
  if (e->damage == 0) e->damage = 0.8;
  e->use = g->named.use(g->named.ctx, e, "hip:earthquake");
  if (g->world) qa_q1mp_world_number(g->world, "hip:quakeactive", 0);
}
static void ambient_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  hm_ctx *c = vctx;
  static const struct { const char *cn; const char *path; } table[] = {
    {"ambient_humming","humming"},{"ambient_rushing","rushing"},{"ambient_running_water","runwater"},{"ambient_fan_blowing","fanblow"},{"ambient_waterfall","waterfal"},{"ambient_riftpower","riftpowr"}};
  for (size_t i = 0; i < 6; i++) if (qa_q1mp_streq(e->classname, table[i].cn)) {
    char path[64]; (void)snprintf(path, sizeof path, "ambient/%s.wav", table[i].path);
    double vol = qa_q1mp_number(e, "volume");
    if (c->game->emit_ambient) c->game->emit_ambient(c->game->ctx, g->body(g, e).origin, path, vol != 0 ? vol : 0.5, 3);
  }
}
static void info_command_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  (void)g; hm_ctx *c = vctx;
  if (e->message[0] && c->game->emit_server_command) c->game->emit_server_command(c->game->ctx, e->message);
}
static void teleport_use(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) {
  (void)o; (void)a; hm_ctx *c = vctx;
  qa_q1mp_effect_a(c->game, "teleport", g->body(g, e).origin, QA_Q1MP_NONE, 0);
  qa_q1mp_sound(c->game, e, "misc/r_tele1.wav");
}
static void teleport_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) { (void)vctx; e->use = g->named.use(g->named.ctx, e, "hip:teleport_effect"); }
void qa_q1mp_register_hipnotic_misc(qa_q1mp_game *game) {
  hm_ctx *c = malloc(sizeof *c); c->game = game; qa_q1b_game *g = game->base;
  qa_q1b_handlers h = {0}; h.ctx = c; h.use = play_use; h.action = play_action;
  g->named.register_named(g->named.ctx, "hip:play_sound", &h);
  static const char *const sounds[] = {"play_sound","play_sound_triggered","random_thunder","random_thunder_triggered"};
  for (size_t i = 0; i < 4; i++) g->register_spawn(g, sounds[i], sound_class_spawn, c);
  static const char *const amb[] = {"ambient_humming","ambient_rushing","ambient_running_water","ambient_fan_blowing","ambient_waterfall","ambient_riftpower"};
  for (size_t i = 0; i < 6; i++) g->register_spawn(g, amb[i], ambient_spawn, c);
  g->register_spawn(g, "info_command", info_command_spawn, c);
  qa_q1b_handlers ht = {0}; ht.ctx = c; ht.use = teleport_use;
  g->named.register_named(g->named.ctx, "hip:teleport_effect", &ht);
  g->register_spawn(g, "effect_teleport", teleport_spawn, c);
  qa_q1b_handlers he = {0}; he.ctx = c; he.action = explode_cb; he.use = explode_use;
  g->named.register_named(g->named.ctx, "hip:explode", &he);
  qa_q1b_handlers hm = {0}; hm.ctx = c; hm.action = multi_cb; hm.use = multi_use;
  g->named.register_named(g->named.ctx, "hip:multi_explode", &hm);
  g->register_spawn(g, "func_exploder", exploder_spawn, c);
  g->register_spawn(g, "func_multi_exploder", exploder_spawn, c);
  qa_q1b_handlers hr = {0}; hr.ctx = c; hr.touch = rubble_touch;
  g->named.register_named(g->named.ctx, "hip:rubble_touch", &hr);
  qa_q1b_handlers hu = {0}; hu.ctx = c; hu.use = rubble_use;
  g->named.register_named(g->named.ctx, "hip:rubble_use", &hu);
  static const char *const rub[] = {"func_rubble","func_rubble1","func_rubble2","func_rubble3"};
  for (size_t i = 0; i < 4; i++) g->register_spawn(g, rub[i], rubble_spawn, c);
  qa_q1b_handlers hq = {0}; hq.ctx = c; hq.use = earthquake_use;
  g->named.register_named(g->named.ctx, "hip:earthquake", &hq);
  g->register_spawn(g, "func_earthquake", earthquake_spawn, c);
}
