/* Port of src/content/q3/base/game/triggers.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/triggers.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/base/game/format.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#define FRAMETIME_MS 100
#define CONTENTS_TRIGGER_V (double)(int32_t)0x40000000
#define MOD_TRIGGER_HURT 22
static float f32(double v) { return (float)v; }
static qa_q3_entity_callback mkcb(const void *fn, void *ctx) { qa_q3_entity_callback cb; memcpy(&cb.fn, fn, sizeof cb.fn); cb.ctx = ctx; return cb; }
typedef qa_q3_trigger_runtime RT;
static double game_time(RT *r) {
  double t = r->host.entities->options.time(r->host.entities->options.ctx);
  if (t != trunc(t) || t < -2147483648.0 || t > 2147483647.0)
    qa_range_error("Trigger host time must be a signed 32-bit millisecond value");
  return t;
}
static void owned(RT *r, qa_q3_game_entity *e) {
  if (qa_q3_entity_pool_get(r->host.entities, e->slot) != e)
    qa_range_error("Trigger entity does not belong to its entity pool or was replaced");
}
static qa_q3_game_combat_context *combat_ctx(RT *r) {
  qa_q3_game_combat_context *c = r->host.combat(r->host.ctx);
  if (c->entities != r->host.entities) qa_range_error("Trigger combat context does not match its entity pool and world");
  if (c->time != game_time(r)) qa_range_error("Trigger combat context contains stale game time");
  return c;
}
static void dispatch(RT *r, qa_q3_game_entity *e, const qa_q3_use_participant *activator) {
  qa_q3_target_use_context uc; memset(&uc, 0, sizeof uc);
  uc.pool = r->host.entities; uc.time = game_time(r); uc.ctx = r->host.ctx;
  uc.remap_shader = r->host.remap_shader; uc.warn = r->host.warn;
  qa_q3_use_targets(uc, e, activator);
}
static double src_float_int(double v) { return v >= -2147483648.0 && v < 2147483648.0 ? trunc(v) : -2147483648.0; }
static double src_schedule(double time, double wait, double random, double cr) {
  double seconds = f32(f32(wait) + f32(f32(random) * f32(cr)));
  double ms = f32(f32(1000) * seconds); return src_float_int(f32(f32(time) + ms));
}
static double checked_crandom(RT *r) {
  double v = r->host.crandom(r->host.ctx);
  if (!isfinite(v) || v < -1 || v > 1) qa_range_error("Game crandom() must return a value within [-1, 1]");
  return f32(v);
}
static qa_q3_target_selection_context selection(RT *r) {
  qa_q3_target_selection_context s; memset(&s, 0, sizeof s);
  s.pool = r->host.entities; s.ctx = r->host.ctx; s.random_int = r->host.rand_value; s.warn = r->host.warn; return s;
}
void qa_q3_aim_at_target(qa_q3_aim_at_target_context context, qa_q3_game_entity *entity, qa_vec3 origin) {
  qa_q3_game_entity *target = qa_q3_pick_target(context.selection, entity->target);
  if (!target) { qa_q3_entity_pool_free(context.selection.pool, entity); return; }
  double height = f32(target->s.origin.z - origin.z), gravity = f32(context.gravity(context.gravity_ctx));
  double time = f32(sqrt(f32(height / f32(f32(0.5) * gravity))));
  if (time == 0) { qa_q3_entity_pool_free(context.selection.pool, entity); return; }
  qa_vec3 offset = qa_sub3(target->s.origin, origin), horizontal = {offset.x, offset.y, 0};
  double distance = f32(sqrt(qa_dot3(horizontal, horizontal)));
  qa_vec3 direction = distance == 0 ? horizontal : qa_scale3(horizontal, f32(1 / distance));
  double forward = f32(distance / time); qa_vec3 velocity = qa_scale3(direction, forward);
  entity->s.origin2 = (qa_vec3){velocity.x, velocity.y, f32(time * gravity)};
}
static void init_trigger(RT *r, qa_q3_game_entity *e) {
  qa_vec3 a = e->s.angles;
  if (a.x != 0 || a.y != 0 || a.z != 0) {
    qa_q3_move_direction_result m = qa_q3_move_direction(a); e->movedir = m.direction; e->s.angles = m.angles;
  }
  r->host.set_brush_model(r->host.ctx, e, e->model);
  e->r.contents = CONTENTS_TRIGGER_V; e->r.sv_flags = 1; /* SVF_NOCLIENT */
}
static void pool_link(RT *r, qa_q3_game_entity *e) { if (r->host.entities->options.link) r->host.entities->options.link(r->host.entities->options.ctx, e); }
static void multi_trigger(RT *r, qa_q3_game_entity *e, const qa_q3_use_participant *activator) {
  qa_q3_trigger_runtime_bind_save_callbacks(r);
  if (activator) (void)qa_q3_require_use_participant(activator);
  e->has_activation = activator != NULL; if (activator) e->activation = *activator;
  if (e->nextthink != 0) return;
  if (!activator) qa_range_error("trigger_multiple requires an activator");
  qa_q3_game_entity *act = qa_q3_use_client(*activator, r->host.has_participants ? &r->host.participants : NULL);
  qa_q3_game_client *client = act ? act->client : NULL;
  if (client) {
    if (((int)e->spawnflags & 1) != 0 && client->sess.session_team != 1) return;
    if (((int)e->spawnflags & 2) != 0 && client->sess.session_team != 2) return;
  }
  dispatch(r, e, activator);
  if (e->wait > 0) {
    e->think = qa_q3_callback_family_resolve(&r->host.entities->callbacks.think, "q3.base.game.triggers.multiTrigger.think");
    qa_q3_game_entity_set_nextthink(e, src_schedule(game_time(r), e->wait, e->random, checked_crandom(r)));
  } else {
    e->touch.fn = NULL; e->touch.ctx = NULL;
    qa_q3_game_entity_set_nextthink(e, qa_int32(game_time(r) + FRAMETIME_MS));
    e->think = qa_q3_callback_family_resolve(&r->host.entities->callbacks.think, "q3.base.game.triggers.multiTrigger.free");
  }
}
static void timer_think(RT *r, qa_q3_game_entity *e) {
  dispatch(r, e, e->has_activation ? &e->activation : NULL);
  qa_q3_game_entity_set_nextthink(e, src_schedule(game_time(r), e->wait, e->random, checked_crandom(r)));
}
static void sound_at(RT *r, qa_q3_damage_participant other, double sound) {
  qa_vec3 origin; bool have;
  if (other.entity) { origin = other.entity->r.current_origin; have = true; }
  else have = other.origin != NULL && other.origin(other.origin_ctx, &origin);
  if (!have) return;
  qa_q3_game_entity *ev = qa_q3_entity_pool_temp_entity(r->host.entities, origin, QA_Q3_EV_GENERAL_SOUND);
  ev->s.event_parm = sound;
}
/* Save callbacks. */
static void cb_wait(void *ctx, qa_q3_game_entity *s) { (void)ctx; qa_q3_game_entity_set_nextthink(s, 0); }
static void cb_free(void *ctx, qa_q3_game_entity *s) { RT *r = ctx; qa_q3_entity_pool_free(r->host.entities, s); }
static void cb_multi_touch(void *ctx, qa_q3_game_entity *s, qa_q3_damage_participant o, qa_touch_contact c) {
  (void)c; if (o.entity && o.entity->client) { qa_q3_use_participant p = o; multi_trigger(ctx, s, &p); }
}
static void cb_multi_use(void *ctx, qa_q3_game_entity *s, const qa_q3_use_participant *o, const qa_q3_use_participant *a) { (void)o; multi_trigger(ctx, s, a); }
static void cb_always(void *ctx, qa_q3_game_entity *s) { RT *r = ctx; qa_q3_use_participant self = qa_q3_participant_entity(s); dispatch(r, s, &self); qa_q3_entity_pool_free(r->host.entities, s); }
static void cb_push_touch(void *ctx, qa_q3_game_entity *s, qa_q3_damage_participant o, qa_touch_contact c) {
  (void)c; RT *r = ctx; if (o.entity && o.entity->client) r->host.touch_jump_pad(r->host.ctx, o.entity->client, s);
}
static void cb_push_think(void *ctx, qa_q3_game_entity *s) {
  RT *r = ctx; qa_q3_aim_at_target_context ac; ac.selection = selection(r); ac.gravity_ctx = r->host.ctx; ac.gravity = r->host.gravity;
  qa_q3_aim_at_target(ac, s, qa_scale3(qa_add3(s->r.absmin, s->r.absmax), 0.5));
}
static void cb_tele_touch(void *ctx, qa_q3_game_entity *s, qa_q3_damage_participant o, qa_touch_contact c) {
  (void)c; RT *r = ctx; if (!o.entity) return;
  qa_q3_game_client *client = o.entity->client;
  if (!client || (int)client->ps.pm_type == QA_Q3_PM_DEAD) return;
  if (((int)s->spawnflags & 1) != 0 && client->sess.session_team != 3) return;
  qa_q3_game_entity *dest = qa_q3_pick_target(selection(r), s->target);
  if (!dest) { r->host.warn(r->host.ctx, "Couldn't find teleporter destination\n"); return; }
  qa_q3_teleport_context tc; tc.combat = combat_ctx(r); tc.kill_box = r->host.kill_box; tc.world = r->host.world;
  qa_q3_teleport_player(tc, o.entity, dest->s.origin, dest->s.angles);
}
static void cb_hurt_touch(void *ctx, qa_q3_game_entity *s, qa_q3_damage_participant o, qa_touch_contact c) {
  (void)c; RT *r = ctx;
  bool damageable = o.entity ? qa_q3_game_entity_takedamage(o.entity) : r->host.can_take_damage(r->host.ctx, qa_q3_use_actor(o));
  if (!damageable || s->timestamp > game_time(r)) return;
  s->timestamp = qa_int32(game_time(r) + (((int)s->spawnflags & 16) != 0 ? 1000 : FRAMETIME_MS));
  if (((int)s->spawnflags & 4) == 0) sound_at(r, o, s->noise_index);
  double flags = ((int)s->spawnflags & 8) != 0 ? QA_Q3_DAMAGE_NO_PROTECTION : 0;
  qa_q3_damage_participant self = qa_q3_participant_entity(s);
  qa_q3_combat_damage(combat_ctx(r), o, &self, &self, NULL, NULL, s->damage, flags, MOD_TRIGGER_HURT);
}
static void cb_hurt_use(void *ctx, qa_q3_game_entity *s, const qa_q3_use_participant *o, const qa_q3_use_participant *a) {
  (void)o; (void)a; RT *r = ctx;
  if (s->r.linked) { if (r->host.entities->options.unlink) r->host.entities->options.unlink(r->host.entities->options.ctx, s); }
  else pool_link(r, s);
}
static void cb_timer_use(void *ctx, qa_q3_game_entity *s, const qa_q3_use_participant *o, const qa_q3_use_participant *a) {
  (void)o; RT *r = ctx; if (a) (void)qa_q3_require_use_participant(a);
  s->has_activation = a != NULL; if (a) s->activation = *a;
  if (s->nextthink != 0) qa_q3_game_entity_set_nextthink(s, 0); else timer_think(r, s);
}
static void cb_timer_think(void *ctx, qa_q3_game_entity *s) { timer_think(ctx, s); }
void qa_q3_trigger_runtime_bind_save_callbacks(RT *r) {
  qa_q3_callback_catalog *c = &r->host.entities->callbacks;
  qa_q3_entity_think_fn tw = cb_wait, tf = cb_free, ta = cb_always, tp = cb_push_think, tmt = cb_timer_think;
  qa_q3_entity_touch_fn um = cb_multi_touch, up = cb_push_touch, ut = cb_tele_touch, uh = cb_hurt_touch;
  qa_q3_entity_use_fn uu = cb_multi_use, hu = cb_hurt_use, tu = cb_timer_use;
  qa_q3_callback_family_intern(&c->think, "q3.base.game.triggers.multiTrigger.think", mkcb(&tw, r));
  qa_q3_callback_family_intern(&c->think, "q3.base.game.triggers.multiTrigger.free", mkcb(&tf, r));
  qa_q3_callback_family_intern(&c->touch, "q3.base.game.triggers.spawnTriggerMultiple.touch", mkcb(&um, r));
  qa_q3_callback_family_intern(&c->use, "q3.base.game.triggers.spawnTriggerMultiple.use", mkcb(&uu, r));
  qa_q3_callback_family_intern(&c->think, "q3.base.game.triggers.spawnTriggerAlways.think", mkcb(&ta, r));
  qa_q3_callback_family_intern(&c->touch, "q3.base.game.triggers.spawnTriggerPush.touch", mkcb(&up, r));
  qa_q3_callback_family_intern(&c->think, "q3.base.game.triggers.spawnTriggerPush.think", mkcb(&tp, r));
  qa_q3_callback_family_intern(&c->touch, "q3.base.game.triggers.spawnTriggerTeleport.touch", mkcb(&ut, r));
  qa_q3_callback_family_intern(&c->touch, "q3.base.game.triggers.spawnTriggerHurt.touch", mkcb(&uh, r));
  qa_q3_callback_family_intern(&c->use, "q3.base.game.triggers.spawnTriggerHurt.use", mkcb(&hu, r));
  qa_q3_callback_family_intern(&c->use, "q3.base.game.triggers.spawnFuncTimer.use", mkcb(&tu, r));
  qa_q3_callback_family_intern(&c->think, "q3.base.game.triggers.spawnFuncTimer.think", mkcb(&tmt, r));
}
void qa_q3_trigger_runtime_init(RT *r, qa_q3_trigger_host host) { r->host = host; qa_q3_trigger_runtime_bind_save_callbacks(r); }
void qa_q3_bind_trigger_save_callbacks(qa_q3_trigger_host host) {
  RT *r = malloc(sizeof *r); if (!r) qa_range_error("out of memory"); qa_q3_trigger_runtime_init(r, host);
}
/* Spawn handlers. */
static void h_multiple(void *ctx, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) {
  RT *r = ctx; qa_q3_trigger_runtime_bind_save_callbacks(r); owned(r, e);
  e->wait = qa_q3_spawn_variables_float(v, "wait", "0.5").value;
  e->random = qa_q3_spawn_variables_float(v, "random", "0").value;
  if (e->random >= e->wait && e->wait >= 0) { e->random = f32(e->wait - FRAMETIME_MS); r->host.warn(r->host.ctx, "trigger_multiple has random >= wait\n"); }
  e->touch = qa_q3_callback_family_resolve(&r->host.entities->callbacks.touch, "q3.base.game.triggers.spawnTriggerMultiple.touch");
  e->use = qa_q3_callback_family_resolve(&r->host.entities->callbacks.use, "q3.base.game.triggers.spawnTriggerMultiple.use");
  init_trigger(r, e); pool_link(r, e);
}
static void h_always(void *ctx, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) {
  (void)v; RT *r = ctx; qa_q3_trigger_runtime_bind_save_callbacks(r); owned(r, e);
  qa_q3_game_entity_set_nextthink(e, qa_int32(game_time(r) + 300));
  e->think = qa_q3_callback_family_resolve(&r->host.entities->callbacks.think, "q3.base.game.triggers.spawnTriggerAlways.think");
}
static void h_push(void *ctx, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) {
  (void)v; RT *r = ctx; qa_q3_trigger_runtime_bind_save_callbacks(r); owned(r, e); init_trigger(r, e);
  e->r.sv_flags = (int)e->r.sv_flags & ~1; (void)r->host.sound_index(r->host.ctx, "sound/world/jumppad.wav");
  e->s.e_type = 8; /* ET_PUSH_TRIGGER */
  e->touch = qa_q3_callback_family_resolve(&r->host.entities->callbacks.touch, "q3.base.game.triggers.spawnTriggerPush.touch");
  e->think = qa_q3_callback_family_resolve(&r->host.entities->callbacks.think, "q3.base.game.triggers.spawnTriggerPush.think");
  qa_q3_game_entity_set_nextthink(e, qa_int32(game_time(r) + FRAMETIME_MS)); pool_link(r, e);
}
static void h_teleport(void *ctx, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) {
  (void)v; RT *r = ctx; qa_q3_trigger_runtime_bind_save_callbacks(r); owned(r, e); init_trigger(r, e);
  if (((int)e->spawnflags & 1) != 0) e->r.sv_flags = (int)e->r.sv_flags | 1; else e->r.sv_flags = (int)e->r.sv_flags & ~1;
  (void)r->host.sound_index(r->host.ctx, "sound/world/jumppad.wav");
  e->s.e_type = 9; /* ET_TELEPORT_TRIGGER */
  e->touch = qa_q3_callback_family_resolve(&r->host.entities->callbacks.touch, "q3.base.game.triggers.spawnTriggerTeleport.touch");
  pool_link(r, e);
}
static void h_hurt(void *ctx, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) {
  (void)v; RT *r = ctx; qa_q3_trigger_runtime_bind_save_callbacks(r); owned(r, e); init_trigger(r, e);
  e->noise_index = r->host.sound_index(r->host.ctx, "sound/world/electro.wav");
  e->touch = qa_q3_callback_family_resolve(&r->host.entities->callbacks.touch, "q3.base.game.triggers.spawnTriggerHurt.touch");
  if (e->damage == 0) e->damage = 5;
  e->r.contents = CONTENTS_TRIGGER_V;
  if (((int)e->spawnflags & 2) != 0) e->use = qa_q3_callback_family_resolve(&r->host.entities->callbacks.use, "q3.base.game.triggers.spawnTriggerHurt.use");
  if (((int)e->spawnflags & 1) == 0) pool_link(r, e);
}
static void h_timer(void *ctx, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) {
  RT *r = ctx; qa_q3_trigger_runtime_bind_save_callbacks(r); owned(r, e);
  e->random = qa_q3_spawn_variables_float(v, "random", "1").value;
  e->wait = qa_q3_spawn_variables_float(v, "wait", "1").value;
  e->use = qa_q3_callback_family_resolve(&r->host.entities->callbacks.use, "q3.base.game.triggers.spawnFuncTimer.use");
  e->think = qa_q3_callback_family_resolve(&r->host.entities->callbacks.think, "q3.base.game.triggers.spawnFuncTimer.think");
  if (e->random >= e->wait) {
    e->random = f32(e->wait - FRAMETIME_MS);
    qa_q3_game_format_arg arg = qa_q3_game_format_string((const char *)qa_q3_game_utility_scratch_vtos(r->host.entities->utilities, e->s.origin));
    char *msg = qa_q3_game_format("func_timer at %s has random >= wait\n", &arg, 1, 32000);
    r->host.warn(r->host.ctx, msg); free(msg);
  }
  if (((int)e->spawnflags & 1) != 0) {
    qa_q3_game_entity_set_nextthink(e, qa_int32(game_time(r) + FRAMETIME_MS));
    e->activation = qa_q3_participant_entity(e); e->has_activation = true;
  }
  e->r.sv_flags = 1;
}
size_t qa_q3_trigger_spawn_handlers(qa_q3_trigger_host host, qa_q3_spawn_handler_entry *out, size_t cap) {
  RT *r = malloc(sizeof *r); if (!r) qa_range_error("out of memory"); qa_q3_trigger_runtime_init(r, host);
  static const struct { const char *name; qa_q3_spawn_handler_fn fn; } T[] = {
    {"trigger_multiple", h_multiple}, {"trigger_always", h_always}, {"trigger_push", h_push},
    {"trigger_teleport", h_teleport}, {"trigger_hurt", h_hurt}, {"func_timer", h_timer}};
  size_t n = sizeof T / sizeof *T;
  for (size_t i = 0; i < n && i < cap; i++) { out[i].classname = T[i].name; out[i].fn = T[i].fn; out[i].ctx = r; }
  return n;
}
