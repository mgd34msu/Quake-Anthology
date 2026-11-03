/* Port of src/content/q3/base/game/targets.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/targets.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/base/game/format.h"
#include "qa/content/q3/base/game/triggers.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include "qa/foundation/js.h"
#define CS_LOCATIONS 608
#define MASK_TARGET_LASER (0x1 | 0x2000000 | 0x4000000)
static float f32(double v) { return (float)v; }
static qa_q3_entity_callback mkcb(const void *fn, void *ctx) { qa_q3_entity_callback cb; memcpy(&cb.fn, fn, sizeof cb.fn); cb.ctx = ctx; return cb; }
typedef qa_q3_target_runtime RT;
void qa_q3_target_location_state_init(qa_q3_target_location_state *s) { s->linked = false; s->head = NULL; }
void qa_q3_target_location_state_reset(qa_q3_target_location_state *s) { qa_q3_target_location_state_init(s); }
qa_q3_target_location_save qa_q3_target_location_capture(const qa_q3_target_location_state *s) {
  qa_q3_target_location_save out = {s->linked, s->head != NULL, s->head ? s->head->slot : 0}; return out;
}
void qa_q3_target_location_restore(qa_q3_target_location_state *s, qa_q3_target_location_save save, qa_q3_entity_pool *pool) {
  s->linked = save.linked; s->head = save.has_head ? qa_q3_entity_pool_at(pool, save.head_slot) : NULL;
}
void qa_q3_target_runtime_init(RT *r, void *ctx, qa_q3_entity_pool *entities) {
  memset(r, 0, sizeof *r); r->ctx = ctx; r->entities = entities; qa_q3_target_location_state_init(&r->location);
}
static double game_time(RT *r) {
  double t = r->entities->options.time(r->entities->options.ctx);
  if (t != trunc(t) || t < -2147483648.0 || t > 2147483647.0)
    qa_range_error("Target runtime time must be a signed 32-bit millisecond value");
  return t;
}
static void owned(RT *r, qa_q3_game_entity *e) {
  if (qa_q3_entity_pool_get(r->entities, e->slot) != e)
    qa_range_error("Target entity does not belong to its entity pool or was replaced");
}
static qa_q3_game_combat_context *combat_ctx(RT *r) {
  qa_q3_game_combat_context *c = r->combat(r->ctx);
  if (c->entities != r->entities) qa_range_error("Target combat context does not match its entity pool and world");
  if (c->time != game_time(r)) qa_range_error("Target combat context contains stale game time");
  return c;
}
static void dispatch(RT *r, qa_q3_game_entity *e, const qa_q3_use_participant *activator) {
  qa_q3_target_use_context uc; memset(&uc, 0, sizeof uc);
  uc.pool = r->entities; uc.time = game_time(r); uc.ctx = r->ctx; uc.remap_shader = r->remap_shader; uc.warn = r->warn;
  qa_q3_use_targets(uc, e, activator);
}
/* TS useClient(requireActivator(runtime, a)): a path_corner fires its targets with no activator; the QVM read a
   null client from low memory and carried on, so a NULL activator yields no client. */
static qa_q3_game_entity *use_client_of(RT *r, const qa_q3_use_participant *a) {
  return qa_q3_use_client(a, r->has_participants ? &r->participants : NULL);
}
static qa_q3_game_entity *pick(RT *r, const char *name) {
  qa_q3_target_selection_context s; memset(&s, 0, sizeof s);
  s.pool = r->entities; s.ctx = r->ctx; s.random_int = r->rand_value; s.warn = r->warn;
  return qa_q3_pick_target(s, name);
}
static const char *vtos_of(RT *r, qa_vec3 v) { return (const char *)qa_q3_game_utility_scratch_vtos(r->entities->utilities, v); }
static void warn_fmt(RT *r, const char *fmt, qa_q3_game_format_arg *args, size_t n) {
  char *m = qa_q3_game_format(fmt, args, n, 32000); r->warn(r->ctx, m); free(m);
}
static void pool_link(RT *r, qa_q3_game_entity *e) { if (r->entities->options.link) r->entities->options.link(r->entities->options.ctx, e); }
static void pool_unlink(RT *r, qa_q3_game_entity *e) { if (r->entities->options.unlink) r->entities->options.unlink(r->entities->options.ctx, e); }
/* Use bodies. */
static void use_give(void *ctx, qa_q3_game_entity *e, const qa_q3_use_participant *o, const qa_q3_use_participant *a) {
  (void)o; RT *r = ctx; qa_q3_game_entity *act = use_client_of(r, a);
  if (!act || !act->client || !e->target) return;
  qa_q3_game_entity *t = NULL;
  while ((t = qa_q3_find_entity(r->entities, t, QA_Q3_FIELD_TARGETNAME, e->target)) != NULL) {
    if (!t->item) continue;
    r->touch_item(r->ctx, t, act); qa_q3_game_entity_set_nextthink(t, 0); pool_unlink(r, t);
  }
}
static void use_remove_powerups(void *ctx, qa_q3_game_entity *e, const qa_q3_use_participant *o, const qa_q3_use_participant *a) {
  (void)e; (void)o; RT *r = ctx; qa_q3_game_entity *act = use_client_of(r, a);
  if (!act || !act->client) return;
  qa_q3_base_player_state_slots *p = &act->client->ps.powerups;
  if (qa_q3_base_player_state_slots_get(p, QA_Q3_PW_REDFLAG) != 0) r->return_flag(r->ctx, 1);
  else if (qa_q3_base_player_state_slots_get(p, QA_Q3_PW_BLUEFLAG) != 0) r->return_flag(r->ctx, 2);
  else if (qa_q3_base_player_state_slots_get(p, QA_Q3_PW_NEUTRALFLAG) != 0) r->return_flag(r->ctx, 0);
  for (size_t i = 0; i < p->length; i++) qa_q3_base_player_state_slots_set(p, i, 0);
}
static double target_crandom(RT *r) {
  double v = r->crandom(r->ctx);
  if (!isfinite(v) || v < -1 || v > 1) qa_range_error("Game crandom() must return a value within [-1, 1]");
  return f32(v);
}
static void think_delay(void *ctx, qa_q3_game_entity *e) { RT *r = ctx; dispatch(r, e, e->has_activation ? &e->activation : NULL); }
static void use_delay(void *ctx, qa_q3_game_entity *e, const qa_q3_use_participant *o, const qa_q3_use_participant *a) {
  (void)o; RT *r = ctx; qa_q3_bind_target_save_callbacks(r);
  double variance = f32(f32(e->random) * target_crandom(r));
  double seconds = f32(f32(e->wait) + variance);
  qa_q3_game_entity_set_nextthink(e, qa_qvm_float_to_int(f32(f32(game_time(r)) + f32(f32(seconds) * 1000))));
  e->think = qa_q3_callback_family_resolve(&r->entities->callbacks.think, "q3.base.game.targets.useTargetDelay.think");
  e->has_activation = a != NULL; if (a) e->activation = *a;
}
static void use_score(void *ctx, qa_q3_game_entity *e, const qa_q3_use_participant *o, const qa_q3_use_participant *a) {
  (void)o; RT *r = ctx; qa_q3_game_entity *p = use_client_of(r, a);
  if (p) r->add_score(r->ctx, p, qa_q3_base_entity_shared_current_origin(&e->r), e->count);
}
static void use_print(void *ctx, qa_q3_game_entity *e, const qa_q3_use_participant *o, const qa_q3_use_participant *a) {
  (void)o; RT *r = ctx;
  qa_q3_game_entity *player = use_client_of(r, a);
  qa_q3_game_format_arg arg = e->message ? qa_q3_game_format_string(e->message) : qa_q3_game_format_null();
  char *command = qa_q3_game_format("cp \"%s\"", &arg, 1, 32000);
  if (player && (qa_js_to_int32(e->spawnflags) & 4) != 0) { r->send_server_command(r->ctx, player->slot, command); free(command); return; }
  if ((qa_js_to_int32(e->spawnflags) & 3) != 0) {
    /* teamCommand send: bounce through file-scope-free adapter via runtime fields. */
    if ((qa_js_to_int32(e->spawnflags) & 1) != 0) qa_q3_team_command(r->entities, 1, command, (qa_q3_team_command_send_fn)r->send_server_command, r->ctx);
    if ((qa_js_to_int32(e->spawnflags) & 2) != 0) qa_q3_team_command(r->entities, 2, command, (qa_q3_team_command_send_fn)r->send_server_command, r->ctx);
    free(command); return;
  }
  r->send_server_command(r->ctx, -1, command); free(command);
}
static void use_speaker(void *ctx, qa_q3_game_entity *e, const qa_q3_use_participant *o, const qa_q3_use_participant *a) {
  (void)o; RT *r = ctx;
  if ((qa_js_to_int32(e->spawnflags) & 3) != 0) { e->s.loop_sound = e->s.loop_sound != 0 ? 0 : e->noise_index; return; }
  if ((qa_js_to_int32(e->spawnflags) & 8) != 0) {
    if (!a) return;
    qa_q3_use_participant req = *a;
    if (req.entity) qa_q3_entity_pool_add_event(r->entities, req.entity, QA_Q3_EV_GENERAL_SOUND, e->noise_index);
    else {
      if (!r->has_participants) qa_range_error("Shared target sound requires actor events");
      r->participants.event(r->participants.ctx, qa_q3_use_actor(req), QA_Q3_EV_GENERAL_SOUND, e->noise_index);
    }
  } else if ((qa_js_to_int32(e->spawnflags) & 4) != 0) qa_q3_entity_pool_add_event(r->entities, e, QA_Q3_EV_GLOBAL_SOUND, e->noise_index);
  else qa_q3_entity_pool_add_event(r->entities, e, QA_Q3_EV_GENERAL_SOUND, e->noise_index);
}
static void use_push(void *ctx, qa_q3_game_entity *e, const qa_q3_use_participant *o, const qa_q3_use_participant *a) {
  (void)o; RT *r = ctx; qa_q3_game_entity *act = use_client_of(r, a);
  if (!act || !act->client) return;
  qa_q3_game_client *c = act->client;
  if (qa_js_to_int32(c->ps.pm_type) != QA_Q3_PM_NORMAL || qa_q3_base_player_state_slots_get(&c->ps.powerups, QA_Q3_PW_FLIGHT) != 0) return;
  qa_q3_base_player_state_record_set_velocity(&c->ps, e->s.origin2);
  double t = game_time(r);
  if (act->fly_sound_debounce_time < t) {
    act->fly_sound_debounce_time = qa_int32(t + 1500);
    qa_q3_game_entity *s = qa_q3_entity_pool_temp_entity(r->entities, qa_q3_base_entity_shared_current_origin(&act->r), QA_Q3_EV_GENERAL_SOUND);
    s->s.event_parm = e->noise_index;
  }
}
static void laser_think(RT *r, qa_q3_game_entity *e) {
  if (e->enemy) {
    qa_q3_game_entity *en = e->enemy;
    qa_vec3 a = qa_add3(en->s.origin, qa_scale3(qa_q3_base_entity_shared_mins(&en->r), 0.5));
    qa_vec3 b = qa_scale3(qa_q3_base_entity_shared_maxs(&en->r), 0.5);
    qa_vec3 point = qa_add3(a, b);
    e->movedir = qa_normalize3(qa_sub3(point, e->s.origin));
  }
  qa_vec3 end = qa_add3(e->s.origin, qa_scale3(e->movedir, 2048));
  qa_q3_game_combat_context *c = combat_ctx(r);
  qa_q3_base_actor_trace_query query; memset(&query, 0, sizeof query);
  qa_actor_id pass = qa_q3_game_entity_actor(e).id;
  query.start = e->s.origin; query.end = end; query.shape.kind = QA_Q3_BASE_SHAPE_POINT;
  query.pass_actor = &pass; query.mask = MASK_TARGET_LASER;
  qa_q3_base_actor_trace_result tr = c->spatial.trace_actor(c->spatial.ctx, query);
  if (tr.hit_kind == QA_Q3_BASE_HIT_ACTOR) {
    qa_q3_damage_participant target = c->participant(c->services_ctx, tr.hit_actor);
    if (!target.entity || target.entity->slot != 0) {
      qa_q3_damage_participant self = qa_q3_participant_entity(e);
      qa_vec3 dir = e->movedir;
      qa_q3_combat_damage(c, target, &self, e->has_activation ? &e->activation : NULL, &dir, &tr.end, e->damage, QA_Q3_DAMAGE_NO_KNOCKBACK, 21);
    }
  }
  e->s.origin2 = tr.end; pool_link(r, e);
  qa_q3_game_entity_set_nextthink(e, qa_int32(game_time(r) + 100));
}
static void laser_on(RT *r, qa_q3_game_entity *e) {
  if (!e->has_activation) { e->activation = qa_q3_participant_entity(e); e->has_activation = true; }
  laser_think(r, e);
}
static void laser_off(RT *r, qa_q3_game_entity *e) { pool_unlink(r, e); qa_q3_game_entity_set_nextthink(e, 0); }
static void use_laser(void *ctx, qa_q3_game_entity *e, const qa_q3_use_participant *o, const qa_q3_use_participant *a) {
  (void)o; RT *r = ctx; e->has_activation = a != NULL; if (a) e->activation = *a;
  if (e->nextthink > 0) laser_off(r, e); else laser_on(r, e);
}
static void cb_laser_think(void *ctx, qa_q3_game_entity *e) { laser_think(ctx, e); }
static void start_laser(RT *r, qa_q3_game_entity *e) {
  qa_q3_bind_target_save_callbacks(r); e->s.e_type = 5; /* ET_BEAM */
  if (e->target) {
    qa_q3_game_entity *t = qa_q3_find_entity(r->entities, NULL, QA_Q3_FIELD_TARGETNAME, e->target);
    if (!t) {
      qa_q3_game_format_arg value = qa_q3_game_format_string(qa_q3_game_entity_classname(e));
      qa_q3_game_format_arg value2 = qa_q3_game_format_string(vtos_of(r, e->s.origin));
      qa_q3_game_format_arg value3 = qa_q3_game_format_string(e->target);
      qa_q3_game_format_arg args[3] = {value,
        value2, value3};
      warn_fmt(r, "%s at %s: %s is a bad target\n", args, 3);
    }
    e->enemy = t;
  } else { qa_q3_move_direction_result m = qa_q3_move_direction(e->s.angles); e->s.angles = m.angles; e->movedir = m.direction; }
  e->use = qa_q3_callback_family_resolve(&r->entities->callbacks.use, "q3.base.game.targets.startTargetLaser.use");
  e->think = qa_q3_callback_family_resolve(&r->entities->callbacks.think, "q3.base.game.targets.startTargetLaser.think");
  if (e->damage == 0) e->damage = 1;
  if ((qa_js_to_int32(e->spawnflags) & 1) != 0) laser_on(r, e); else laser_off(r, e);
}
static void cb_start_laser(void *ctx, qa_q3_game_entity *e) { start_laser(ctx, e); }
static void teleport_to(RT *r, qa_q3_game_entity *player, qa_vec3 origin, qa_vec3 angles) {
  qa_q3_teleport_context tc; tc.combat = combat_ctx(r); tc.kill_box = r->kill_box; tc.world = r->world;
  qa_q3_teleport_player(tc, player, origin, angles);
}
static void use_teleporter(void *ctx, qa_q3_game_entity *e, const qa_q3_use_participant *o, const qa_q3_use_participant *a) {
  (void)o; RT *r = ctx; qa_q3_game_entity *act = use_client_of(r, a);
  if (!act || !act->client) return;
  qa_q3_game_entity *dest = pick(r, e->target);
  if (!dest) { r->warn(r->ctx, "Couldn't find teleporter destination\n"); return; }
  teleport_to(r, act, dest->s.origin, dest->s.angles);
}
static void use_kill(void *ctx, qa_q3_game_entity *e, const qa_q3_use_participant *o, const qa_q3_use_participant *a) {
  (void)e; (void)o; RT *r = ctx;
  if (!a) return;
  qa_q3_use_participant req = *a;
  qa_q3_combat_damage(combat_ctx(r), req, NULL, NULL, NULL, NULL, 100000, QA_Q3_DAMAGE_NO_PROTECTION, 18);
}
static void link_locations(RT *r) {
  qa_q3_target_location_state *st = &r->location;
  if (st->linked) return;
  st->linked = true; st->head = NULL;
  r->set_configstring(r->ctx, CS_LOCATIONS, "unknown");
  double number = 1;
  for (double i = 0; i < r->entities->num_entities; i++) {
    qa_q3_game_entity *e = qa_q3_entity_pool_at(r->entities, i);
    const char *cn = qa_q3_game_entity_classname(e); if (!cn) continue;
    char folded[256]; size_t k = 0; for (; cn[k] && k < sizeof folded - 1; k++) folded[k] = (cn[k] >= 'A' && cn[k] <= 'Z') ? (char)(cn[k] + 32) : cn[k]; folded[k] = 0;
    if (strcmp(folded, "target_location") != 0) continue;
    qa_q3_game_entity_set_health(e, number);
    r->set_configstring(r->ctx, CS_LOCATIONS + number, e->message ? e->message : "");
    number++; e->next_train = st->head; st->head = e;
  }
}
static void cb_location(void *ctx, qa_q3_game_entity *e) { (void)e; link_locations(ctx); }
static void use_relay(void *ctx, qa_q3_game_entity *e, const qa_q3_use_participant *o, const qa_q3_use_participant *a) {
  (void)o; RT *r = ctx;
  qa_q3_game_entity *player = use_client_of(r, a);
  if ((qa_js_to_int32(e->spawnflags) & 1) != 0 && player && player->client && player->client->sess.session_team != 1) return;
  if ((qa_js_to_int32(e->spawnflags) & 2) != 0 && player && player->client && player->client->sess.session_team != 2) return;
  if ((qa_js_to_int32(e->spawnflags) & 4) != 0) {
    qa_q3_game_entity *sel = pick(r, e->target);
    if (sel && sel->use.fn) {
      qa_q3_entity_use_fn fn; memcpy(&fn, &sel->use.fn, sizeof fn);
      qa_q3_use_participant other = qa_q3_participant_entity(e); fn(sel->use.ctx, sel, &other, a);
    }
    return;
  }
  dispatch(r, e, a);
}
static void cb_push_think(void *ctx, qa_q3_game_entity *e) {
  RT *r = ctx; qa_q3_aim_at_target_context ac; ac.selection.pool = r->entities; ac.selection.ctx = r->ctx;
  ac.selection.random_int = r->rand_value; ac.selection.warn = r->warn; ac.gravity_ctx = r->ctx; ac.gravity = r->gravity;
  qa_q3_aim_at_target(ac, e, qa_scale3(qa_add3(qa_q3_base_entity_shared_absmin(&e->r), qa_q3_base_entity_shared_absmax(&e->r)), 0.5));
}
void qa_q3_bind_target_save_callbacks(RT *r) {
  qa_q3_callback_catalog *c = &r->entities->callbacks;
  qa_q3_entity_use_fn ug = use_give, ur = use_remove_powerups, ud = use_delay, us = use_score, up = use_print,
    usp = use_speaker, upu = use_push, ul = use_laser, ut = use_teleporter, uk = use_kill, ure = use_relay;
  qa_q3_entity_think_fn td = think_delay, tp = cb_push_think, tl = cb_laser_think, ts = cb_start_laser, tlo = cb_location;
  qa_q3_callback_family_intern(&c->use, "q3.base.game.targets.spawnTargetGive.use", mkcb(&ug, r));
  qa_q3_callback_family_intern(&c->use, "q3.base.game.targets.spawnTargetRemovePowerups.use", mkcb(&ur, r));
  qa_q3_callback_family_intern(&c->think, "q3.base.game.targets.useTargetDelay.think", mkcb(&td, r));
  qa_q3_callback_family_intern(&c->use, "q3.base.game.targets.spawnTargetDelay.use", mkcb(&ud, r));
  qa_q3_callback_family_intern(&c->use, "q3.base.game.targets.spawnTargetScore.use", mkcb(&us, r));
  qa_q3_callback_family_intern(&c->use, "q3.base.game.targets.spawnTargetPrint.use", mkcb(&up, r));
  qa_q3_callback_family_intern(&c->use, "q3.base.game.targets.spawnTargetSpeaker.use", mkcb(&usp, r));
  qa_q3_callback_family_intern(&c->think, "q3.base.game.targets.spawnTargetPush.think", mkcb(&tp, r));
  qa_q3_callback_family_intern(&c->use, "q3.base.game.targets.spawnTargetPush.use", mkcb(&upu, r));
  qa_q3_callback_family_intern(&c->use, "q3.base.game.targets.startTargetLaser.use", mkcb(&ul, r));
  qa_q3_callback_family_intern(&c->think, "q3.base.game.targets.startTargetLaser.think", mkcb(&tl, r));
  qa_q3_callback_family_intern(&c->think, "q3.base.game.targets.spawnTargetLaser.think", mkcb(&ts, r));
  qa_q3_callback_family_intern(&c->use, "q3.base.game.targets.spawnTargetTeleporter.use", mkcb(&ut, r));
  qa_q3_callback_family_intern(&c->use, "q3.base.game.targets.spawnTargetKill.use", mkcb(&uk, r));
  qa_q3_callback_family_intern(&c->think, "q3.base.game.targets.spawnTargetLocation.think", mkcb(&tlo, r));
  qa_q3_callback_family_intern(&c->use, "q3.base.game.targets.spawnTargetRelay.use", mkcb(&ure, r));
}
/* Spawn handlers. */
static void set_use(RT *r, qa_q3_game_entity *e, const char *id) { e->use = qa_q3_callback_family_resolve(&r->entities->callbacks.use, id); }
static void h_give(void *c, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) { (void)v; RT *r = c; qa_q3_bind_target_save_callbacks(r); owned(r, e); set_use(r, e, "q3.base.game.targets.spawnTargetGive.use"); }
static void h_remove(void *c, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) { (void)v; RT *r = c; qa_q3_bind_target_save_callbacks(r); owned(r, e); set_use(r, e, "q3.base.game.targets.spawnTargetRemovePowerups.use"); }
static void h_delay(void *c, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) {
  RT *r = c; qa_q3_bind_target_save_callbacks(r); owned(r, e);
  qa_q3_spawn_value_number d = qa_q3_spawn_variables_float(v, "delay", "0");
  e->wait = d.present ? d.value : qa_q3_spawn_variables_float(v, "wait", "1").value;
  if (e->wait == 0) e->wait = 1;
  set_use(r, e, "q3.base.game.targets.spawnTargetDelay.use");
}
static void h_score(void *c, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) { (void)v; RT *r = c; qa_q3_bind_target_save_callbacks(r); owned(r, e); if (e->count == 0) e->count = 1; set_use(r, e, "q3.base.game.targets.spawnTargetScore.use"); }
static void h_print(void *c, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) { (void)v; RT *r = c; qa_q3_bind_target_save_callbacks(r); owned(r, e); set_use(r, e, "q3.base.game.targets.spawnTargetPrint.use"); }
static void h_speaker(void *c, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) {
  RT *r = c; qa_q3_bind_target_save_callbacks(r); owned(r, e);
  e->wait = qa_q3_spawn_variables_float(v, "wait", "0").value;
  e->random = qa_q3_spawn_variables_float(v, "random", "0").value;
  qa_q3_spawn_value_string noise = qa_q3_spawn_variables_string(v, "noise", "NOSOUND");
  if (!noise.present) {
    qa_q3_game_format_arg arg = qa_q3_game_format_string(vtos_of(r, e->s.origin));
    char *m = qa_q3_game_format("target_speaker without a noise key at %s", &arg, 1, 32000);
    qa_range_error("%s", m);
  }
  if (noise.value[0] == '*') e->spawnflags = qa_js_to_int32(e->spawnflags) | 8;
  qa_q3_game_format_arg arg = qa_q3_game_format_string(noise.value);
  char *path = qa_q3_game_format(strstr(noise.value, ".wav") ? "%s" : "%s.wav", &arg, 1, 64);
  e->noise_index = r->sound_index(r->ctx, path); free(path);
  e->s.e_type = 7; /* ET_SPEAKER */ e->s.event_parm = e->noise_index;
  e->s.frame = qa_qvm_float_to_int(f32(e->wait * 10));
  e->s.client_num = qa_qvm_float_to_int(f32(e->random * 10));
  if ((qa_js_to_int32(e->spawnflags) & 1) != 0) e->s.loop_sound = e->noise_index;
  set_use(r, e, "q3.base.game.targets.spawnTargetSpeaker.use");
  if ((qa_js_to_int32(e->spawnflags) & 4) != 0) e->r.sv_flags = qa_js_to_int32(e->r.sv_flags) | 32; /* BROADCAST */
  e->s.pos.base = e->s.origin; pool_link(r, e);
}
static void h_laser(void *c, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) {
  (void)v; RT *r = c; qa_q3_bind_target_save_callbacks(r); owned(r, e);
  e->think = qa_q3_callback_family_resolve(&r->entities->callbacks.think, "q3.base.game.targets.spawnTargetLaser.think");
  qa_q3_game_entity_set_nextthink(e, qa_int32(game_time(r) + 100));
}
static void h_teleporter(void *c, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) {
  (void)v; RT *r = c; qa_q3_bind_target_save_callbacks(r); owned(r, e);
  if (!e->targetname) {
    qa_q3_game_format_arg value = qa_q3_game_format_string(qa_q3_game_entity_classname(e));
    qa_q3_game_format_arg value2 = qa_q3_game_format_string(vtos_of(r, e->s.origin));
    qa_q3_game_format_arg args[2] = {value, value2};
    warn_fmt(r, "untargeted %s at %s\n", args, 2);
  }
  set_use(r, e, "q3.base.game.targets.spawnTargetTeleporter.use");
}
static void h_relay(void *c, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) { (void)v; RT *r = c; qa_q3_bind_target_save_callbacks(r); owned(r, e); set_use(r, e, "q3.base.game.targets.spawnTargetRelay.use"); }
static void h_position(void *c, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) { (void)v; owned(c, e); qa_q3_set_origin(e, e->s.origin); }
static void h_push(void *c, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) {
  (void)v; RT *r = c; qa_q3_bind_target_save_callbacks(r); owned(r, e);
  if (e->speed == 0) e->speed = 1000;
  qa_q3_move_direction_result m = qa_q3_move_direction(e->s.angles); e->s.angles = m.angles;
  e->s.origin2 = qa_scale3(m.direction, e->speed);
  e->noise_index = r->sound_index(r->ctx, (qa_js_to_int32(e->spawnflags) & 1) != 0 ? "sound/world/jumppad.wav" : "sound/misc/windfly.wav");
  if (e->target) {
    qa_q3_base_entity_shared_set_absmin(&e->r, e->s.origin); qa_q3_base_entity_shared_set_absmax(&e->r, e->s.origin);
    qa_q3_game_entity_set_nextthink(e, qa_int32(game_time(r) + 100));
    e->think = qa_q3_callback_family_resolve(&r->entities->callbacks.think, "q3.base.game.targets.spawnTargetPush.think");
  }
  set_use(r, e, "q3.base.game.targets.spawnTargetPush.use");
}
static void h_kill(void *c, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) { (void)v; RT *r = c; qa_q3_bind_target_save_callbacks(r); owned(r, e); set_use(r, e, "q3.base.game.targets.spawnTargetKill.use"); }
static void h_location(void *c, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) {
  (void)v; RT *r = c; qa_q3_bind_target_save_callbacks(r); owned(r, e);
  e->think = qa_q3_callback_family_resolve(&r->entities->callbacks.think, "q3.base.game.targets.spawnTargetLocation.think");
  qa_q3_game_entity_set_nextthink(e, qa_int32(game_time(r) + 200)); qa_q3_set_origin(e, e->s.origin);
}
size_t qa_q3_target_spawn_handlers(RT *r, qa_q3_spawn_handler_entry *out, size_t cap) {
  qa_q3_bind_target_save_callbacks(r);
  if (!r->item_lifecycle_matches) qa_range_error("Target item lifecycle context does not match its entity pool and world");
  static const struct { const char *name; qa_q3_spawn_handler_fn fn; } T[] = {
    {"target_give", h_give}, {"target_remove_powerups", h_remove}, {"target_delay", h_delay},
    {"target_score", h_score}, {"target_print", h_print}, {"target_speaker", h_speaker},
    {"target_laser", h_laser}, {"target_teleporter", h_teleporter}, {"target_relay", h_relay},
    {"target_position", h_position}, {"target_push", h_push}, {"target_kill", h_kill}, {"target_location", h_location}};
  size_t n = sizeof T / sizeof *T;
  for (size_t i = 0; i < n && i < cap; i++) { out[i].classname = T[i].name; out[i].fn = T[i].fn; out[i].ctx = r; }
  return n;
}
