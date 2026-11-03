/* Port of src/content/q2/rerelease/triggers.ts. GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/triggers.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/rerelease/killbox.h"
#include "rr_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

double qa_q2rr_sound_time_get(qa_q2_rerelease_triggers *t, qa_actor_id a) {
  for (size_t i = 0; i < t->count; i++) if (qa_same_actor(t->sound_times[i].actor, a)) return t->sound_times[i].time;
  return 0;
}
bool qa_q2rr_sound_time_set(qa_q2_rerelease_triggers *t, qa_actor_id a, double v) {
  for (size_t i = 0; i < t->count; i++) if (qa_same_actor(t->sound_times[i].actor, a)) { t->sound_times[i].time = v; return true; }
  if (t->count == t->cap) { size_t nc = t->cap ? t->cap * 2 : 8; void *q = realloc(t->sound_times, nc * sizeof *t->sound_times); if (!q) return false; t->sound_times = q; t->cap = nc; }
  t->sound_times[t->count].actor = a; t->sound_times[t->count].time = v; t->count++; return true;
}
void qa_q2rr_sound_time_delete(qa_q2_rerelease_triggers *t, qa_actor_id a) {
  for (size_t i = 0; i < t->count; i++) if (qa_same_actor(t->sound_times[i].actor, a)) {
    memmove(&t->sound_times[i], &t->sound_times[i + 1], (t->count - i - 1) * sizeof *t->sound_times); t->count--; return;
  }
}
void qa_q2rr_sound_time_clear(qa_q2_rerelease_triggers *t) { t->count = 0; }

static void trig_init(qa_q2_entity *e, qa_q2_game_services *g) {
  e->visible = false; e->server_flags = rr_bor(e->server_flags, 1);
  qa_vec3 angles = rr_body_of(g, e).angles;
  e->movedir = angles.x == 0 && angles.y == 0 && angles.z == 0 && !qa_q2_spawn_has(&e->spawn, "angle") ? QA_Q2_ZERO : qa_q2_movedir(angles);
  qa_q2_body_changes ch; memset(&ch, 0, sizeof ch); ch.has_angles = true; ch.angles = QA_Q2_ZERO;
  g->move(g->ctx, e, ch, false); g->solid(g->ctx, e, "trigger");
}
static void toggle_use(void *c, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *o, const qa_actor_id *a) {
  (void)c; (void)o; (void)a;
  g->solid(g->ctx, e, (e->solid && strcmp(e->solid, "none") == 0) ? "trigger" : "none");
  g->link(g->ctx, e);
}
static void killbox_use(void *c, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *o, const qa_actor_id *a) {
  (void)o; (void)a; qa_q2_rerelease_triggers *t = c;
  t->players->deadly_kill_box = rr_band(e->spawnflags, 2) != 0;
  g->solid(g->ctx, e, "trigger"); g->link(g->ctx, e);
  qa_q2_kill_rerelease_box(e, g, t->players, false, rr_band(e->spawnflags, 4) != 0);
  g->solid(g->ctx, e, "none"); g->link(g->ctx, e);
  t->players->deadly_kill_box = false;
}
static void hurt_use(void *c, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *o, const qa_actor_id *a) {
  toggle_use(c, e, g, o, a);
  if (rr_band(e->spawnflags, 2) == 0) e->use = (qa_q2_use){0};
}
static void world_gravity_use(void *c, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *o, const qa_actor_id *a) {
  (void)g; (void)o; (void)a; qa_q2_rerelease_triggers *t = c;
  t->hooks.set_world_gravity(t->hooks.ctx, e->gravity);
}
static void sound_fx_think(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  (void)c; rr_sound_ent(g, e, e->noise, 2, e->volume, e->attenuation);
}
static void sound_fx_use(void *c, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *o, const qa_actor_id *a) {
  (void)o; (void)a; qa_q2_rerelease_triggers *t = c;
  g->schedule(g->ctx, e, e->delay, t->sound_fx_think);
}
static void push_touch(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_touch_contact contact) {
  qa_q2_rerelease_triggers *t = c;
  if (rr_band(e->spawnflags, 16) != 0 && !t->hooks.clip_trigger(t->hooks.ctx, e, contact.other, g)) return;
  qa_q2_entity *other = rr_ent(g, &contact.other);
  qa_owned_actor owner; bool has_owner = qa_session_actor_registry_resolve_owned(g->host->actors, contact.other, &owner);
  qa_body_state_result br = qa_shared_body_table_read(g->host->bodies, contact.other);
  bool grenade = other && other->classname && strcmp(other->classname, "grenade") == 0;
  if (has_owner && br.has_value && (grenade || rr_health(g, contact.other) > 0)) {
    qa_vec3 velocity = qa_q2_scale(e->movedir, e->speed * 10);
    qa_body_state nb = br.value; nb.velocity = velocity;
    qa_shared_body_table_write(g->host->bodies, owner, nb);
    if (other) g->motion(g->ctx, other, other->motion);
    if (g->host->is_player(g->host->ctx, contact.other)) {
      qa_q2_base_player_state *st = qa_q2_players_get_state(t->players->base, contact.other);
      qa_q2rr_player_extra_state *extra = qa_q2_rerelease_players_get_extra(t->players, contact.other);
      if (st) st->old_velocity = velocity;
      t->hooks.push_player(t->hooks.ctx, contact.other, velocity);
      if (rr_band(e->spawnflags, 4) == 0 && extra->wind_sound_time < rr_now(g)) {
        extra->wind_sound_time = rr_now(g) + 1.5;
        qa_q2_presentation_event ev; memset(&ev, 0, sizeof ev);
        ev.kind = "sound"; ev.has_actor = true; ev.actor = contact.other; ev.origin = br.value.origin;
        ev.path = "misc/windfly.wav"; ev.channel = 0; ev.volume = 1; ev.attenuation = 1; ev.loop = "once";
        rr_emit_host(g, ev);
      }
    }
  }
  if (rr_band(e->spawnflags, 1) != 0) g->remove(g->ctx, e);
}
static void push_active(void *c, qa_q2_entity *e, qa_q2_game_services *g);
static void push_inactive(void *c, qa_q2_entity *e, qa_q2_game_services *g);
static void push_active(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_rerelease_triggers *t = c;
  if (e->delay > rr_now(g)) {
    qa_body_state b = rr_body_of(g, e);
    qa_vec3 origin = qa_q2_add(b.origin, qa_q2_scale(qa_q2_add(b.bounds.min, b.bounds.max), 0.5));
    for (int i = 0; i < 10; i++) {
      origin = qa_q2_add(origin, rr_v3(0, 0, e->speed * 0.01 * ((double)i + g->host->random(g->host->ctx))));
      qa_q2_presentation_event ev; memset(&ev, 0, sizeof ev);
      ev.kind = "effect"; ev.effect = "q2:tunnel-sparks"; ev.origin = origin; ev.direction = QA_Q2_ZERO;
      ev.count = 1; ev.color = 0x74 + floor(g->host->random(g->host->ctx) * 8);
      rr_emit_host(g, ev);
    }
    g->schedule(g->ctx, e, 0.1, t->push_active); return;
  }
  e->touch = (qa_q2_touch){0}; e->delay = rr_now(g) + 0.1 + e->wait;
  g->schedule(g->ctx, e, 0.1, t->push_inactive);
}
static void push_inactive(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_rerelease_triggers *t = c;
  if (e->delay > rr_now(g)) { g->schedule(g->ctx, e, 0.1, t->push_inactive); return; }
  e->touch = t->push_touch; e->delay = rr_now(g) + 0.1 + e->wait;
  g->schedule(g->ctx, e, 0.1, t->push_active);
}
static void hurt_touch(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_touch_contact contact) {
  double left;
  double right;
  qa_q2_rerelease_triggers *t = c;
  qa_q2_entity *target = rr_ent(g, &contact.other);
  qa_body_state_result br = qa_shared_body_table_read(g->host->bodies, contact.other);
  bool player = g->host->is_player(g->host->ctx, contact.other);
  bool monster = g->host->is_monster(g->host->ctx, contact.other);
  if (!br.has_value || !rr_can_take_damage(g, contact.other)) return;
  if (!player && !monster && !(target && target->damageable_target) && !(target && target->classname && strcmp(target->classname, "misc_explobox") == 0)) return;
  if (((rr_band(e->spawnflags, 32) != 0 && player) || (rr_band(e->spawnflags, 64) != 0 && monster)) || e->timestamp > rr_now(g)) return;
  if (rr_band(e->spawnflags, 128) != 0 && !t->hooks.clip_trigger(t->hooks.ctx, e, contact.other, g)) return;
  e->timestamp = rr_now(g) + (rr_band(e->spawnflags, 16) != 0 ? 1 : 0.1);
  if (rr_band(e->spawnflags, 4) == 0 && (left = qa_q2rr_sound_time_get(t, e->actor.id), right = rr_now(g), left < right)) {
    qa_q2rr_sound_time_set(t, e->actor.id, rr_now(g) + 1);
    qa_q2_presentation_event ev; memset(&ev, 0, sizeof ev);
    ev.kind = "sound"; ev.has_actor = true; ev.actor = contact.other; ev.origin = br.value.origin;
    ev.path = e->noise; ev.channel = 0; ev.volume = 1; ev.attenuation = 1; ev.loop = "once";
    rr_emit_host(g, ev);
  }
  g->damage(g->ctx, contact.other, e, e->actor.id, &e->actor.id, e->damage, e->damage, QA_Q2_ZERO, br.value.origin, QA_Q2_ZERO, 31, rr_band(e->spawnflags, 8) != 0 ? 32 : 0, NULL);
}
static void gravity_touch(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_touch_contact contact) {
  qa_q2_rerelease_triggers *t = c;
  if (rr_band(e->spawnflags, 4) != 0 && !t->hooks.clip_trigger(t->hooks.ctx, e, contact.other, g)) return;
  qa_q2_entity *target = rr_ent(g, &contact.other);
  if (target) { target->gravity = e->gravity; g->motion(g->ctx, target, target->motion); }
  t->hooks.set_actor_gravity(t->hooks.ctx, contact.other, e->gravity);
}
static void monster_jump_touch(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_touch_contact contact) {
  qa_q2_rerelease_triggers *t = c;
  qa_q2_entity *target = rr_ent(g, &contact.other);
  qa_body_state_result br = qa_shared_body_table_read(g->host->bodies, contact.other);
  if (!target || !br.has_value || !g->host->is_monster(g->host->ctx, contact.other)) return;
  if (rr_band(target->flags, 3) != 0 || rr_band(target->server_flags, 2) != 0) return;
  if (rr_band(e->spawnflags, 4) != 0 && !t->hooks.clip_trigger(t->hooks.ctx, e, contact.other, g)) return;
  bool grounded = br.value.ground != NULL;
  if (!grounded) {
    qa_q2_trace_request r; memset(&r, 0, sizeof r);
    r.start = br.value.origin; r.end = qa_q2_add(br.value.origin, rr_v3(0, 0, -0.25));
    r.bounds = &br.value.bounds; r.ignore = &target->actor.id; r.mask = target->clip_mask;
    grounded = g->host->trace(g->host->ctx, r).fraction < 1;
  }
  qa_q2_body_changes ch; memset(&ch, 0, sizeof ch);
  ch.has_velocity = true;
  ch.velocity = rr_v3(e->movedir.x * e->speed, e->movedir.y * e->speed, grounded ? e->movedir.z : br.value.velocity.z);
  ch.has_ground = true; ch.ground_null = true;
  g->move(g->ctx, target, ch, false);
  g->motion(g->ctx, target, target->motion);
}

void qa_q2_rerelease_triggers_init(qa_q2_rerelease_triggers *t, qa_q2_rerelease_players *p, qa_q2_rerelease_hooks h) {
  memset(t, 0, sizeof *t); t->players = p; t->hooks = h;
  t->toggle_use = (qa_q2_use){t, toggle_use}; t->killbox_use = (qa_q2_use){t, killbox_use};
  t->hurt_use = (qa_q2_use){t, hurt_use}; t->world_gravity_use = (qa_q2_use){t, world_gravity_use};
  t->sound_fx_use = (qa_q2_use){t, sound_fx_use};
  t->sound_fx_think = (qa_q2_think){t, sound_fx_think};
  t->push_active = (qa_q2_think){t, push_active}; t->push_inactive = (qa_q2_think){t, push_inactive};
  t->push_touch = (qa_q2_touch){t, push_touch}; t->hurt_touch = (qa_q2_touch){t, hurt_touch};
  t->gravity_touch = (qa_q2_touch){t, gravity_touch}; t->monster_jump_touch = (qa_q2_touch){t, monster_jump_touch};
}
void qa_q2_rerelease_triggers_destroy(qa_q2_rerelease_triggers *t) { free(t->sound_times); memset(t, 0, sizeof *t); }

bool qa_q2_rerelease_triggers_spawn(qa_q2_rerelease_triggers *t, qa_q2_entity *e, qa_q2_game_services *g) {
  const char *cn = e->classname; if (!cn) return false;
  if (strcmp(cn, "func_killbox") == 0) {
    e->visible = false; e->server_flags = rr_bor(e->server_flags, 1);
    g->solid(g->ctx, e, "none"); e->use = t->killbox_use; return true;
  }
  if (strcmp(cn, "trigger_push") == 0) {
    trig_init(e, g); if (e->speed == 0) e->speed = 1000; e->touch = t->push_touch;
    if (rr_band(e->spawnflags, 2) != 0) { if (e->wait == 0) e->wait = 10; e->delay = rr_now(g) + 0.1 + e->wait; g->schedule(g->ctx, e, 0.1, t->push_active); }
    if (e->targetname && e->targetname[0]) { e->use = t->toggle_use; if (rr_band(e->spawnflags, 8) != 0) g->solid(g->ctx, e, "none"); }
    else if (rr_band(e->spawnflags, 8) != 0) { e->server_flags = 0; e->touch = (qa_q2_touch){0}; e->visible = true; g->solid(g->ctx, e, "brush"); g->motion(g->ctx, e, "push"); }
    g->link(g->ctx, e); return true;
  }
  if (strcmp(cn, "trigger_hurt") == 0) {
    trig_init(e, g); if (e->damage == 0) e->damage = 5; e->touch = t->hurt_touch; e->noise = "world/electro.wav";
    if (rr_band(e->spawnflags, 1) != 0) g->solid(g->ctx, e, "none");
    if (rr_band(e->spawnflags, 2) != 0) e->use = t->hurt_use;
    g->link(g->ctx, e); return true;
  }
  if (strcmp(cn, "trigger_gravity") == 0) {
    if (!qa_q2_spawn_has(&e->spawn, "gravity")) { rr_diag(g, "trigger_gravity: no gravity set"); g->remove(g->ctx, e); return true; }
    trig_init(e, g); e->gravity = qa_q2_number_field(&e->spawn, "gravity", 0); e->touch = t->gravity_touch;
    if (rr_band(e->spawnflags, 3) != 0) e->use = t->toggle_use;
    if (rr_band(e->spawnflags, 2) != 0) g->solid(g->ctx, e, "none");
    g->link(g->ctx, e); return true;
  }
  if (strcmp(cn, "trigger_monsterjump") == 0) {
    qa_vec3 angles = rr_body_of(g, e).angles;
    if (angles.y == 0) {
      qa_q2_body_changes ch; memset(&ch, 0, sizeof ch); ch.has_angles = true; ch.angles = angles; ch.angles.y = 360;
      g->move(g->ctx, e, ch, false);
    }
    trig_init(e, g); if (e->speed == 0) e->speed = 200;
    double h = qa_q2_number_field(&e->spawn, "height", 0);
    e->movedir.z = h != 0 ? h : 200;
    e->touch = t->monster_jump_touch;
    if (rr_band(e->spawnflags, 3) != 0) e->use = t->toggle_use;
    if (rr_band(e->spawnflags, 2) != 0) g->solid(g->ctx, e, "none");
    g->link(g->ctx, e); return true;
  }
  if (strcmp(cn, "target_gravity") == 0) {
    e->gravity = qa_q2_number_field(&e->spawn, "gravity", 0); e->use = t->world_gravity_use; return true;
  }
  if (strcmp(cn, "target_soundfx") == 0) {
    if (e->volume == 0) e->volume = 1;
    e->attenuation = e->attenuation == -1 ? 0 : (e->attenuation != 0 ? e->attenuation : 1);
    double key = qa_q2_number_field(&e->spawn, "noise", 0);
    const char *sound = key == 1 ? "world/x_alarm.wav" : key == 2 ? "world/flyby1.wav" : key == 4 ? "world/amb12.wav"
                      : key == 5 ? "world/amb17.wav" : key == 7 ? "world/bigpump2.wav" : NULL;
    if (!sound) {
      char b[512]; const char *nv = qa_q2_spawn_get(&e->spawn, "noise");
      snprintf(b, sizeof b, "target_soundfx: unknown noise %s", nv ? nv : "0");
      rr_diag(g, b); return true;
    }
    e->noise = sound; e->use = t->sound_fx_use; return true;
  }
  return false;
}
static qa_q2_named_think tr_thinks[3]; static qa_q2_named_touch tr_touches[4]; static qa_q2_named_use tr_uses[5];
qa_q2_callback_definitions qa_q2_rerelease_triggers_callbacks(qa_q2_rerelease_triggers *t) {
  tr_thinks[0] = (qa_q2_named_think){"rr.trigger_push_active", t->push_active};
  tr_thinks[1] = (qa_q2_named_think){"rr.trigger_push_inactive", t->push_inactive};
  tr_thinks[2] = (qa_q2_named_think){"rr.update_target_soundfx", t->sound_fx_think};
  tr_touches[0] = (qa_q2_named_touch){"rr.trigger_push_touch", t->push_touch};
  tr_touches[1] = (qa_q2_named_touch){"rr.hurt_touch", t->hurt_touch};
  tr_touches[2] = (qa_q2_named_touch){"rr.trigger_gravity_touch", t->gravity_touch};
  tr_touches[3] = (qa_q2_named_touch){"rr.trigger_monsterjump_touch", t->monster_jump_touch};
  tr_uses[0] = (qa_q2_named_use){"rr.trigger_gravity_use", t->toggle_use};
  tr_uses[1] = (qa_q2_named_use){"rr.hurt_use", t->hurt_use};
  tr_uses[2] = (qa_q2_named_use){"rr.use_target_gravity", t->world_gravity_use};
  tr_uses[3] = (qa_q2_named_use){"rr.use_target_soundfx", t->sound_fx_use};
  tr_uses[4] = (qa_q2_named_use){"rr.use_killbox", t->killbox_use};
  qa_q2_callback_definitions d = {0};
  d.think = tr_thinks; d.think_count = 3; d.touch = tr_touches; d.touch_count = 4; d.use = tr_uses; d.use_count = 5;
  return d;
}
static bool trig_spawn_cb(void *c, qa_q2_entity *e, qa_q2_game_services *g) { return qa_q2_rerelease_triggers_spawn(c, e, g); }
qa_q2_spawn_module qa_q2_rerelease_triggers_as_spawn(qa_q2_rerelease_triggers *t) {
  qa_q2_spawn_module m = {0}; m.ctx = t; m.spawn = trig_spawn_cb; return m;
}
