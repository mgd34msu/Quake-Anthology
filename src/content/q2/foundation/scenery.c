/* Port of src/content/q2/foundation/scenery.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/scenery.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/core/math.h"
#include "qa/foundation/js.h"

#define THINK(name) static void name(void *c, qa_q2_entity *e, qa_q2_game_services *g)
#define USE(name) static void name(void *c, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *o, const qa_actor_id *a)
#define TOUCH(name) static void name(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_touch_contact contact)
#define DIE(name) static void name(void *c, qa_q2_entity *e, qa_q2_game_services *g, qa_death_reaction r)
#define TCB(name) ((qa_q2_think){NULL, name})
#define UCB(name) ((qa_q2_use){NULL, name})
#define XCB(name) ((qa_q2_touch){NULL, name})
#define DCB(name) ((qa_q2_die){NULL, name})

static void combat_create(qa_q2_game_services *g, qa_owned_actor actor, double health, double mass, bool can_take) {
  qa_combat_state st = {0};
  st.health = health; st.mass = mass; st.can_take_damage = can_take;
  st.armor.regular.kind = QA_REGULAR_ARMOR_NONE; st.armor.powered.kind = "none";
  qa_gameplay_authority_create_state(g->host->combat, actor, st, NULL, NULL);
}
static void move_fields(qa_q2_entity *e, qa_q2_game_services *g, qa_q2_body_changes c, bool link) { g->move(g->ctx, e, c, link); }
static qa_q2_body_changes ch_origin(qa_vec3 o) { qa_q2_body_changes c = {0}; c.has_origin = true; c.origin = o; return c; }

/* ---- scenery ---- */
qa_q2_entity *qa_q2_throw_debris(qa_q2_entity *self, qa_q2_game_services *g, const char *model, double speed, qa_vec3 origin) {
  qa_q2_entity *chunk = g->create(g->ctx, "debris", NULL, 0);
  chunk->model = model;
  double x2 = 100 * (g->host->random(g->host->ctx) * 2 - 1);
  double y2 = 100 * (g->host->random(g->host->ctx) * 2 - 1);
  double z2 = 100 + 100 * (g->host->random(g->host->ctx) * 2 - 1);
  qa_vec3 rv = {x2, y2, z2};
  qa_q2_body_changes ch = {0};
  ch.has_origin = true; ch.origin = origin;
  ch.has_velocity = true; ch.velocity = qa_q2_add(qa_q2_game_body(g, self).velocity, qa_q2_scale(rv, speed));
  move_fields(chunk, g, ch, false);
  double x = g->host->random(g->host->ctx) * 600;
  double y = g->host->random(g->host->ctx) * 600;
  double z = g->host->random(g->host->ctx) * 600;
  chunk->angular_velocity = (qa_vec3){x, y, z};
  combat_create(g, chunk->actor, 0, 0, true);
  chunk->die = QA_Q2_FREE_ENTITY_DIE;
  g->motion(g->ctx, chunk, "bounce");
  g->solid(g->ctx, chunk, "none");
  g->show(g->ctx, chunk);
  g->schedule(g->ctx, chunk, 5 + g->host->random(g->host->ctx) * 5, QA_Q2_FREE_ENTITY);
  return chunk;
}

static void explode_scenery(qa_q2_entity *e, qa_q2_game_services *g, int type) {
  static _Thread_local char buf[64];
  snprintf(buf, sizeof buf, "q2:explosion%d", type);
  qa_q2_presentation_event ev = {0};
  ev.kind = "effect"; ev.effect = buf; ev.origin = qa_q2_game_body(g, e).origin; ev.direction = QA_Q2_ZERO; ev.count = 1;
  g->host->emit(g->host->ctx, ev);
  g->remove(g->ctx, e);
}

bool qa_q2_kill_box(qa_q2_entity *e, qa_q2_game_services *g) {
  for (;;) {
    qa_body_state body = qa_q2_game_body(g, e);
    qa_q2_trace_request tr = {.start = body.origin, .end = body.origin, .bounds = &body.bounds, .ignore = &e->actor.id, .mask = 0x2010003};
    qa_trace_result trace = g->host->trace(g->host->ctx, tr);
    if (trace.hit.kind != QA_TRACE_HIT_ACTOR) return !trace.start_solid && !trace.all_solid;
    qa_actor_id victim = trace.hit.actor;
    if (!qa_gameplay_authority_read(g->host->combat, victim).has_value) return false;
    qa_actor_id self_id = e->actor.id;
    g->damage(g->ctx, victim, e, e->actor.id, &self_id, 100000, 0, QA_Q2_ZERO, body.origin, QA_Q2_ZERO, 21, 32, NULL);
    qa_q2_entity *live_ent = g->entity(g->ctx, &victim);
    if (!live_ent) {
      qa_body_state b2 = qa_q2_game_body(g, e);
      qa_q2_trace_request tr2 = {.start = b2.origin, .end = b2.origin, .bounds = &b2.bounds, .ignore = &e->actor.id, .mask = 0x2010003};
      qa_trace_result next = g->host->trace(g->host->ctx, tr2);
      return next.hit.kind != QA_TRACE_HIT_ACTOR || !qa_same_actor(next.hit.actor, victim);
    }
    if (strcmp(live_ent->solid, "none") != 0) return false;
  }
}

USE(func_wall_use);
USE(func_explosive_spawn);
USE(func_explosive_use);
DIE(func_explosive_die);
THINK(blast);
DIE(barrel_delay);
TOUCH(barrel_touch);
THINK(barrel_drop);
THINK(banner_think);
THINK(satellite_think);
USE(satellite_use);
DIE(deadsoldier_die);

static void wall(qa_q2_entity *e, qa_q2_game_services *g) {
  g->motion(g->ctx, e, "push");
  int32_t sf = qa_js_to_int32(e->spawnflags); /* TS spawnflags & x: ToInt32 */
  if (sf & 8) e->effects = (double)(qa_js_to_int32(e->effects) | 0x1000);
  if (sf & 16) e->effects = (double)(qa_js_to_int32(e->effects) | 0x2000);
  if ((sf & 7) == 0) { g->solid(g->ctx, e, "brush"); g->show(g->ctx, e); return; }
  e->spawnflags = (double)(sf | 1);
  if ((qa_js_to_int32(e->spawnflags) & 4) != 0) e->spawnflags = (double)(qa_js_to_int32(e->spawnflags) | 2);
  e->visible = (qa_js_to_int32(e->spawnflags) & 4) != 0;
  g->solid(g->ctx, e, e->visible ? "brush" : "none");
  g->show(g->ctx, e);
  e->use = UCB(func_wall_use);
}
static void explosive(qa_q2_entity *e, qa_q2_game_services *g) {
  if (g->options.mode == QA_Q2_MODE_DEATHMATCH) { g->remove(g->ctx, e); return; }
  g->motion(g->ctx, e, "push");
  int32_t sf = qa_js_to_int32(e->spawnflags); /* TS spawnflags & x: ToInt32 */
  if (sf & 2) e->effects = (double)(qa_js_to_int32(e->effects) | 0x1000);
  if (sf & 4) e->effects = (double)(qa_js_to_int32(e->effects) | 0x2000);
  if (sf & 1) { e->visible = false; e->use = UCB(func_explosive_spawn); }
  else if (e->targetname[0]) e->use = UCB(func_explosive_use);
  if ((sf & 1) || e->targetname[0] == '\0') {
    if (e->max_health == 0) e->max_health = 100;
    double mass = qa_q2_number_field(&e->spawn, "mass", 0);
    combat_create(g, e->actor, e->max_health, mass != 0 ? mass : 75, true);
    e->die = DCB(func_explosive_die);
  }
  g->solid(g->ctx, e, e->visible ? "brush" : "none");
  g->show(g->ctx, e);
}
static void barrel(qa_q2_entity *e, qa_q2_game_services *g) {
  if (g->options.mode == QA_Q2_MODE_DEATHMATCH) { g->remove(g->ctx, e); return; }
  e->model = "models/objects/barrels/tris.md2";
  if (e->max_health == 0) e->max_health = 10;
  if (e->damage == 0) e->damage = 150;
  double mass = qa_q2_number_field(&e->spawn, "mass", 0);
  combat_create(g, e->actor, e->max_health, mass != 0 ? mass : 400, true);
  qa_q2_body_changes ch = {0};
  ch.has_bounds = true; ch.bounds = (qa_bounds){{-16, -16, 0}, {16, 16, 40}};
  move_fields(e, g, ch, false);
  g->solid(g->ctx, e, "box");
  g->motion(g->ctx, e, "step");
  g->show(g->ctx, e);
  e->die = DCB(barrel_delay);
  e->touch = XCB(barrel_touch);
  g->schedule(g->ctx, e, 2 * g->host->frame_seconds(g->host->ctx), TCB(barrel_drop));
}

static bool spawn_scenery(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  (void)c;
  const char *cn = e->classname;
  if (strcmp(cn, "func_wall") == 0) { wall(e, g); return true; }
  if (strcmp(cn, "func_explosive") == 0) { explosive(e, g); return true; }
  if (strcmp(cn, "misc_explobox") == 0) { barrel(e, g); return true; }
  if (strcmp(cn, "misc_model") == 0) {
    const char *m = qa_q2_spawn_get(&e->spawn, "model");
    e->model = m ? m : "";
    g->show(g->ctx, e);
    return true;
  }
  if (strcmp(cn, "misc_banner") == 0) {
    e->model = "models/objects/banner/tris.md2";
    e->frame = floor(g->host->random(g->host->ctx) * 16);
    g->show(g->ctx, e); g->link(g->ctx, e);
    g->schedule(g->ctx, e, 0.1, TCB(banner_think));
    return true;
  }
  if (strcmp(cn, "misc_satellite_dish") == 0) {
    e->model = "models/objects/satellite/tris.md2";
    qa_q2_body_changes ch = {0};
    ch.has_bounds = true; ch.bounds = (qa_bounds){{-64, -64, 0}, {64, 64, 128}};
    move_fields(e, g, ch, false);
    e->use = UCB(satellite_use);
    g->solid(g->ctx, e, "box"); g->show(g->ctx, e);
    return true;
  }
  if (strcmp(cn, "misc_deadsoldier") == 0) {
    if (g->options.mode == QA_Q2_MODE_DEATHMATCH) { g->remove(g->ctx, e); return true; }
    e->model = "models/deadbods/dude/tris.md2";
    int32_t sf = qa_js_to_int32(e->spawnflags); /* TS spawnflags & x: ToInt32 */
    e->frame = (sf & 2) ? 1 : (sf & 4) ? 2 : (sf & 8) ? 3 : (sf & 16) ? 4 : (sf & 32) ? 5 : 0;
    e->server_flags = (double)(qa_js_to_int32(e->server_flags) | 6);
    combat_create(g, e->actor, qa_q2_integer_field(&e->spawn, "health", 0), 200, true);
    qa_q2_body_changes ch = {0};
    ch.has_bounds = true; ch.bounds = (qa_bounds){{-16, -16, 0}, {16, 16, 16}};
    move_fields(e, g, ch, false);
    e->die = DCB(deadsoldier_die);
    g->solid(g->ctx, e, "box"); g->show(g->ctx, e);
    return true;
  }
  if (strcmp(cn, "misc_gib_head") == 0) {
    e->model = "models/objects/gibs/head/tris.md2";
    e->effects = (double)(qa_js_to_int32(e->effects) | 2);
    e->server_flags = (double)(qa_js_to_int32(e->server_flags) | 4);
    double x2 = g->host->random(g->host->ctx) * 200;
    double y = g->host->random(g->host->ctx) * 200;
    double z = g->host->random(g->host->ctx) * 200;
    e->angular_velocity = (qa_vec3){x2, y, z};
    combat_create(g, e->actor, 0, 0, true);
    e->die = QA_Q2_FREE_ENTITY_DIE;
    g->motion(g->ctx, e, "toss"); g->solid(g->ctx, e, "none"); g->show(g->ctx, e);
    g->schedule(g->ctx, e, 30, QA_Q2_FREE_ENTITY);
    return true;
  }
  return false;
}

USE(func_wall_use) {
  (void)c; (void)o; (void)a;
  e->visible = strcmp(e->solid, "none") == 0;
  g->solid(g->ctx, e, e->visible ? "brush" : "none");
  if (e->visible) qa_q2_kill_box(e, g);
  g->show(g->ctx, e);
  if ((qa_js_to_int32(e->spawnflags) & 2) == 0) e->use = (qa_q2_use){0};
}
static void break_apart(qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *inflictor, const qa_actor_id *attacker) {
  qa_body_state body = qa_q2_game_body(g, e);
  qa_vec3 size = qa_q2_scale(qa_q2_subtract(body.bounds.max, body.bounds.min), 0.5);
  qa_vec3 origin = qa_q2_add(qa_q2_add(body.origin, body.bounds.min), size);
  move_fields(e, g, ch_origin(origin), false);
  if (qa_gameplay_authority_read(g->host->combat, e->actor.id).has_value) {
    qa_combat_trait_changes tc = {0};
    tc.has_can_take_damage = true; tc.can_take_damage = false;
    qa_gameplay_authority_set_traits(g->host->combat, e->actor, tc);
  }
  if (e->damage != 0) g->radius_damage(g->ctx, e, attacker, e->damage, NULL, e->damage + 40, 25, 0, NULL);
  qa_vec3 from = origin;
  if (inflictor) {
    qa_body_state_result br = qa_shared_body_table_read(g->host->bodies, *inflictor);
    if (br.has_value) from = br.value.origin;
  }
  qa_q2_body_changes ch = {0};
  ch.has_velocity = true; ch.velocity = qa_q2_scale(qa_q2_normalize(qa_q2_subtract(origin, from)), 150);
  move_fields(e, g, ch, false);
  double mass = qa_q2_number_field(&e->spawn, "mass", 0);
  if (mass == 0 || isnan(mass)) mass = 75; /* numberField(...) || 75: NaN is falsy */
  /* Double loop bounds: a huge negative mass is never cast to int. */
  double n1 = qa_js_min(trunc(mass / 100), 8), n2 = qa_js_min(trunc(mass / 25), 16);
  for (double i = 0; i < n1; i++) {
    double x3 = (g->host->random(g->host->ctx) * 2 - 1) * size.x * 0.5;
    double y3 = (g->host->random(g->host->ctx) * 2 - 1) * size.y * 0.5;
    double z3 = (g->host->random(g->host->ctx) * 2 - 1) * size.z * 0.5;
    qa_vec3 p = qa_q2_add(origin, (qa_vec3){x3, y3, z3});
    qa_q2_throw_debris(e, g, "models/objects/debris1/tris.md2", 1, p);
  }
  for (double i = 0; i < n2; i++) {
    double x2 = (g->host->random(g->host->ctx) * 2 - 1) * size.x * 0.5;
    double y2 = (g->host->random(g->host->ctx) * 2 - 1) * size.y * 0.5;
    double z2 = (g->host->random(g->host->ctx) * 2 - 1) * size.z * 0.5;
    qa_vec3 p = qa_q2_add(origin, (qa_vec3){x2, y2, z2});
    qa_q2_throw_debris(e, g, "models/objects/debris2/tris.md2", 2, p);
  }
  qa_q2_game_use_targets_entity(g, e, attacker, false);
  if (e->damage != 0) explode_scenery(e, g, 1);
  else g->remove(g->ctx, e);
}
USE(func_explosive_spawn) {
  (void)c; (void)o; (void)a;
  e->visible = true; e->use = (qa_q2_use){0};
  g->solid(g->ctx, e, "brush");
  qa_q2_kill_box(e, g);
  g->show(g->ctx, e);
}
USE(func_explosive_use) { (void)c; (void)a; qa_actor_id self = e->actor.id; break_apart(e, g, &self, o); }
DIE(func_explosive_die) { (void)c; break_apart(e, g, r.inflictor, r.pain.attacker); }
THINK(blast) {
  (void)c;
  qa_actor_id act = e->has_activator ? e->activator : e->actor.id;
  g->radius_damage(g->ctx, e, &act, e->damage, NULL, e->damage + 40, 26, 0, NULL);
  qa_body_state body = qa_q2_game_body(g, e);
  qa_vec3 size = qa_q2_subtract(body.bounds.max, body.bounds.min);
  qa_vec3 low = qa_q2_add(body.origin, body.bounds.min), center = qa_q2_add(low, qa_q2_scale(size, 0.5));
  for (int i = 0; i < 2; i++) {
    double x3 = (g->host->random(g->host->ctx) * 2 - 1) * size.x;
    double y3 = (g->host->random(g->host->ctx) * 2 - 1) * size.y;
    double z3 = (g->host->random(g->host->ctx) * 2 - 1) * size.z;
    qa_vec3 p = qa_q2_add(center, (qa_vec3){x3, y3, z3});
    qa_q2_throw_debris(e, g, "models/objects/debris1/tris.md2", 1.5 * e->damage / 200, p);
  }
  qa_vec3 corners[4] = {low, qa_q2_add(low, (qa_vec3){size.x, 0, 0}), qa_q2_add(low, (qa_vec3){0, size.y, 0}), qa_q2_add(low, (qa_vec3){size.x, size.y, 0})};
  for (int i = 0; i < 4; i++) qa_q2_throw_debris(e, g, "models/objects/debris3/tris.md2", 1.75 * e->damage / 200, corners[i]);
  for (int i = 0; i < 8; i++) {
    double x2 = (g->host->random(g->host->ctx) * 2 - 1) * size.x;
    double y2 = (g->host->random(g->host->ctx) * 2 - 1) * size.y;
    double z2 = (g->host->random(g->host->ctx) * 2 - 1) * size.z;
    qa_vec3 p = qa_q2_add(center, (qa_vec3){x2, y2, z2});
    qa_q2_throw_debris(e, g, "models/objects/debris2/tris.md2", trunc(2 * e->damage / 200), p); /* TS Math.trunc */
  }
  explode_scenery(e, g, body.ground == NULL ? 1 : 2);
}
DIE(barrel_delay) {
  (void)c;
  qa_combat_trait_changes tc = {0};
  tc.has_can_take_damage = true; tc.can_take_damage = false;
  qa_gameplay_authority_set_traits(g->host->combat, e->actor, tc);
  e->has_activator = r.pain.attacker != NULL;
  if (r.pain.attacker) e->activator = *r.pain.attacker;
  g->schedule(g->ctx, e, 2 * g->host->frame_seconds(g->host->ctx), TCB(blast));
}
TOUCH(barrel_touch) {
  (void)c;
  qa_body_state_result other = qa_shared_body_table_read(g->host->bodies, contact.other);
  qa_combat_state_result state = qa_gameplay_authority_read(g->host->combat, contact.other);
  if (!other.has_value || !state.has_value || other.value.ground == NULL || qa_same_actor(*other.value.ground, e->actor.id)) return;
  qa_body_state body = qa_q2_game_body(g, e);
  qa_vec3 delta = qa_q2_subtract(body.origin, other.value.origin);
  qa_vec3 dir = qa_q2_normalize((qa_vec3){delta.x, delta.y, 0});
  qa_combat_state_result self_state = qa_gameplay_authority_read(g->host->combat, e->actor.id);
  qa_vec3 destination = qa_q2_add(body.origin, qa_q2_scale(dir, 20 * state.value.mass / (self_state.has_value ? self_state.value.mass : 400) * g->host->frame_seconds(g->host->ctx)));
  /* M_walkmove: step up as much as 18 units, refuse to leave a ledge. */
  const double mask = qa_q2m_monster_solid_mask(g);
  qa_vec3 start = qa_q2_add(destination, (qa_vec3){0, 0, 18}), end = qa_q2_add(start, (qa_vec3){0, 0, -36});
  qa_q2_trace_request tr = {.start = start, .end = end, .bounds = &body.bounds, .ignore = &e->actor.id, .mask = mask};
  qa_trace_result trace = g->host->trace(g->host->ctx, tr);
  if (trace.all_solid) return;
  if (trace.start_solid) {
    qa_vec3 lowered = start; lowered.z = start.z - 18;
    tr.start = lowered;
    trace = g->host->trace(g->host->ctx, tr);
    if (trace.start_solid || trace.all_solid) return;
  }
  if (trace.fraction == 1 || !qa_q2m_check_entity_bottom(g, e, trace.end)) return;
  move_fields(e, g, ch_origin(trace.end), true);
}
THINK(barrel_drop) {
  (void)c;
  qa_body_state body = qa_q2_game_body(g, e);
  qa_vec3 start = qa_q2_add(body.origin, (qa_vec3){0, 0, 1});
  /* M_droptofloor lifts the origin by one unit before it traces, even when the trace then fails. */
  move_fields(e, g, ch_origin(start), true);
  qa_q2_trace_request tr = {.start = start, .end = qa_q2_add(start, (qa_vec3){0, 0, -256}), .bounds = &body.bounds, .ignore = &e->actor.id, .mask = 0x2020003};
  qa_trace_result trace = g->host->trace(g->host->ctx, tr);
  if (!trace.all_solid && trace.fraction < 1) {
    qa_q2_body_changes ch = ch_origin(trace.end);
    ch.has_ground = true;
    if (trace.hit.kind == QA_TRACE_HIT_ACTOR) ch.ground = trace.hit.actor;
    else if (trace.hit.kind == QA_TRACE_HIT_WORLD) ch.ground = g->host->world_actor(g->host->ctx);
    else ch.ground_null = true;
    move_fields(e, g, ch, true);
  }
}
THINK(banner_think) {
  (void)c;
  e->frame = fmod(e->frame + 1, 16);
  g->show(g->ctx, e);
  g->schedule(g->ctx, e, 0.1, TCB(banner_think));
}
THINK(satellite_think) {
  (void)c;
  e->frame++;
  g->show(g->ctx, e);
  if (e->frame < 38) g->schedule(g->ctx, e, 0.1, TCB(satellite_think));
}
USE(satellite_use) { (void)c; (void)o; (void)a; e->frame = 0; g->schedule(g->ctx, e, 0.1, TCB(satellite_think)); }
DIE(deadsoldier_die) {
  (void)c;
  qa_combat_state_result cs = qa_gameplay_authority_read(g->host->combat, e->actor.id);
  if ((cs.has_value ? cs.value.health : 0) > -80) return;
  g->sound(g->ctx, e->actor, "misc/udeath.wav", 4, 1, 1);
  for (int i = 0; i < 4; i++) (void)qa_q2m_throw_gib(e, g, "models/objects/gibs/sm_meat/tris.md2", r.pain.damage, (qa_q2m_gib_options){0});
  (void)qa_q2m_throw_head(e, g, "models/objects/gibs/head2/tris.md2", r.pain.damage);
}

static const qa_q2_named_think THINKS[] = {
  {"barrel_explode", {NULL, blast}}, {"barrel_drop_to_floor", {NULL, barrel_drop}},
  {"misc_banner_think", {NULL, banner_think}}, {"misc_satellite_dish_think", {NULL, satellite_think}}};
static const qa_q2_named_use USES[] = {
  {"func_wall_use", {NULL, func_wall_use}}, {"func_explosive_spawn", {NULL, func_explosive_spawn}},
  {"func_explosive_use", {NULL, func_explosive_use}}, {"satellite_use", {NULL, satellite_use}}};
static const qa_q2_named_touch TOUCHES[] = {{"barrel_touch", {NULL, barrel_touch}}};
static const qa_q2_named_die DIES[] = {
  {"func_explosive_die", {NULL, func_explosive_die}}, {"barrel_delay", {NULL, barrel_delay}},
  {"misc_deadsoldier_die", {NULL, deadsoldier_die}}};
static const qa_q2_callback_definitions DEFS = {
  .think = THINKS, .think_count = 4, .use = USES, .use_count = 4, .touch = TOUCHES, .touch_count = 1, .die = DIES, .die_count = 3};

qa_q2_spawn_module qa_q2_create_scenery_module(void) {
  return (qa_q2_spawn_module){.callbacks = &DEFS, .spawn = spawn_scenery};
}
