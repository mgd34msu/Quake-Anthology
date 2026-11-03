/* Port of src/content/q2/equipment/ctf-grapple.ts. GPL-2.0-or-later.
 * Mirrored projection helpers (TS owner: foundation/weapons/projection.ts). */
#include "qa/content/q2/equipment/ctf_grapple.h"
#include "qa/foundation/js.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/foundation/fields.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"

#define MOD_GRAPPLE 34
#define SHOT_MASK (1 | 2 | 0x2000000 | 0x4000000)
#define PLAYER_CONTENTS 0x40000000
#define PROJECTILE_MASK (SHOT_MASK | PLAYER_CONTENTS | 0x4000)

static double shot_mask(qa_q2_game_services *g, bool players_collide) {
  if (g->options.edition == QA_Q2_EDITION_CLASSIC) return SHOT_MASK;
  return players_collide ? PROJECTILE_MASK : (PROJECTILE_MASK & ~PLAYER_CONTENTS);
}
typedef struct shot { qa_vec3 start, direction; } shot;
static shot project_actor(qa_actor_id actor, qa_q2_game_services *g, const char *hand, double view_height, bool players_collide, qa_vec3 angles, qa_vec3 offset) {
  qa_angle_vectors axes = qa_math_angle_vectors_rule(angles, QA_ANGLE_RULE_NATIVE);
  double side = strcmp(hand, "left") == 0 ? -offset.y : strcmp(hand, "center") == 0 ? 0 : offset.y;
  qa_body_state_result body = qa_shared_body_table_read(g->host->bodies, actor);
  if (!body.has_value) qa_range_error("Q2 projection requires a shared actor body");
  qa_vec3 origin = body.value.origin;
  if (g->options.edition == QA_Q2_EDITION_CLASSIC) {
    qa_vec3 start = qa_q2_add(qa_q2_add(qa_q2_add(origin, qa_q2_scale(axes.forward, offset.x)), qa_q2_scale(axes.right, side)), (qa_vec3){0, 0, view_height + offset.z});
    return (shot){start, axes.forward};
  }
  qa_vec3 eye = qa_q2_add(origin, (qa_vec3){0, 0, view_height});
  qa_vec3 start = qa_q2_add(qa_q2_add(qa_q2_add(eye, qa_q2_scale(axes.forward, offset.x)), qa_q2_scale(axes.right, side)), qa_q2_scale(axes.up, offset.z));
  qa_q2_trace_request tr = {.start = eye, .end = qa_q2_add(eye, qa_q2_scale(axes.forward, 8192)), .ignore = &actor,
    .mask = (double)(qa_js_to_int32(shot_mask(g, players_collide)) & ~0x4000000)};
  qa_trace_result trace = g->host->trace(g->host->ctx, tr);
  bool close = strcmp(trace.kind, "q1") != 0 && ((qa_js_to_int32(trace.contents) & (0x2000000 | PLAYER_CONTENTS)) != 0) && trace.fraction * 8192 < 128;
  return (shot){start, (trace.start_solid || close) ? axes.forward : qa_q2_normalize(qa_q2_subtract(trace.end, start))};
}

typedef struct state_entry { qa_actor_id id; qa_ctf_grapple_state v; } state_entry;
struct qa_q2_ctf_grapple_equipment {
  qa_grapple_hooks hooks;
  bool (*can_damage)(void *, qa_actor_id, qa_actor_id);
  void *can_ctx;
  qa_ctf_grapple_settings (*settings)(void *, qa_actor_id, qa_q2_game_services *);
  void *settings_ctx;
  state_entry *states; size_t count, cap;
  struct release_ctx **bound; size_t bound_count, bound_cap; /* one owned binding per bound game */
  qa_q2_named_touch touches[1];
  qa_q2_named_die dies[1];
  qa_q2_callback_definitions defs;
  char *buf;
};

static qa_ctf_grapple_settings settings_of(qa_q2_ctf_grapple_equipment *e, qa_actor_id a, qa_q2_game_services *g) {
  return e->settings ? e->settings(e->settings_ctx, a, g) : (qa_ctf_grapple_settings){650, 650, 10, true};
}
static bool can_damage_of(qa_q2_ctf_grapple_equipment *e, qa_actor_id owner, qa_actor_id target) {
  return e->can_damage ? e->can_damage(e->can_ctx, owner, target) : true;
}
static qa_ctf_grapple_state *find_state(qa_q2_ctf_grapple_equipment *e, qa_actor_id a) {
  for (size_t i = 0; i < e->count; i++) if (qa_same_actor(e->states[i].id, a)) return &e->states[i].v;
  return NULL;
}
qa_ctf_grapple_state *qa_q2_ctf_grapple_state(qa_q2_ctf_grapple_equipment *e, qa_actor_id a) {
  qa_ctf_grapple_state *s = find_state(e, a);
  if (s) return s;
  if (e->count == e->cap) { e->cap = e->cap ? e->cap * 2 : 8; e->states = realloc(e->states, e->cap * sizeof *e->states); if (!e->states) qa_range_error("out of memory"); }
  e->states[e->count].id = a;
  qa_ctf_grapple_state_init(&e->states[e->count].v);
  return &e->states[e->count++].v;
}
static void delete_state(qa_q2_ctf_grapple_equipment *e, qa_actor_id a) {
  for (size_t i = 0; i < e->count; i++)
    if (qa_same_actor(e->states[i].id, a)) { memmove(&e->states[i], &e->states[i + 1], (e->count - i - 1) * sizeof *e->states); e->count--; return; }
}
static bool live(qa_q2_game_services *g, qa_actor_id id) { return qa_session_actor_registry_is_live(g->host->actors, id); }

static void restore_knockback(qa_actor_id owner, qa_q2_game_services *g, qa_ctf_grapple_state *st) {
  if (!st->has_no_knockback) return;
  qa_owned_actor actor;
  if (qa_session_actor_registry_resolve_owned(g->host->actors, owner, &actor) && qa_gameplay_authority_read(g->host->combat, owner).has_value) {
    qa_combat_trait_changes tc = {0};
    tc.has_no_knockback = true; tc.no_knockback = st->grapple_no_knockback;
    qa_gameplay_authority_set_traits(g->host->combat, actor, tc);
  }
  st->has_no_knockback = false;
}
static void loop_hook(qa_q2_entity *hook, qa_q2_game_services *g, const char *path) {
  if (strcmp(hook->sound, path) == 0) return;
  qa_vec3 origin = qa_q2_game_body(g, hook).origin;
  if (hook->sound[0]) {
    qa_q2_presentation_event ev = {0};
    ev.kind = "sound"; ev.has_actor = true; ev.actor = hook->actor.id; ev.origin = origin; ev.path = hook->sound;
    ev.channel = 0; ev.volume = 1; ev.attenuation = 1; ev.loop = "stop";
    g->host->emit(g->host->ctx, ev);
  }
  hook->sound = path;
  if (path[0]) {
    qa_q2_presentation_event ev = {0};
    ev.kind = "sound"; ev.has_actor = true; ev.actor = hook->actor.id; ev.origin = origin; ev.path = path;
    ev.channel = 0; ev.volume = 1; ev.attenuation = 1; ev.loop = "start";
    g->host->emit(g->host->ctx, ev);
  }
}

/* TS game.host.actors.onRelease(...) per bound game; C unsubscribes on destroy. */
typedef struct release_ctx { qa_q2_ctf_grapple_equipment *e; qa_q2_game_services *g; qa_session_actor_registry *actors; int64_t subscription; } release_ctx;
static void on_release(void *c, qa_owned_actor actor) {
  release_ctx *rc = c;
  qa_q2_ctf_grapple_equipment *e = rc->e;
  qa_q2_game_services *g = rc->g;
  qa_ctf_grapple_state *owned = find_state(e, actor.id);
  if (owned) {
    qa_ctf_grapple_state copy = *owned;
    delete_state(e, actor.id);
    qa_q2_entity *hook = copy.has_grapple ? g->entity(g->ctx, &copy.grapple) : NULL;
    if (hook && live(g, hook->actor.id)) { loop_hook(hook, g, ""); g->cancel(g->ctx, hook); g->remove(g->ctx, hook); }
  }
  for (size_t i = 0; i < e->count; i++) {
    qa_ctf_grapple_state *st = &e->states[i].v;
    qa_actor_id owner = e->states[i].id;
    if (!st->has_grapple || !qa_same_actor(st->grapple, actor.id)) continue;
    st->has_grapple = false; st->grapple_state = "fly";
    st->grapple_release_time = g->host->now(g->host->ctx) + (g->options.edition == QA_Q2_EDITION_RERELEASE ? 1 : 0);
    restore_knockback(owner, g, st);
    if (g->options.edition == QA_Q2_EDITION_CLASSIC && live(g, owner)) e->hooks.set_grapple_prediction(e->hooks.ctx, owner, false);
  }
}

void qa_q2_ctf_grapple_bind(qa_q2_ctf_grapple_equipment *e, qa_q2_game_services *g) {
  for (size_t i = 0; i < e->bound_count; i++) if (e->bound[i]->g == g) return;
  if (e->bound_count == e->bound_cap) { e->bound_cap = e->bound_cap ? e->bound_cap * 2 : 4; e->bound = realloc(e->bound, e->bound_cap * sizeof *e->bound); if (!e->bound) qa_range_error("out of memory"); }
  release_ctx *binding = malloc(sizeof *binding);
  if (!binding) qa_range_error("out of memory");
  *binding = (release_ctx){e, g, g->host->actors, 0};
  e->bound[e->bound_count++] = binding;
  binding->subscription = qa_session_actor_registry_on_release(g->host->actors, on_release, binding);
}

void qa_q2_ctf_grapple_sound(qa_q2_ctf_grapple_equipment *e, qa_actor_id entity, qa_actor_id owner, qa_q2_game_services *g, const char *file, bool reliable) {
  if (!e->buf) { e->buf = malloc(512); if (!e->buf) qa_range_error("out of memory"); }
  snprintf(e->buf, 512, "weapons/grapple/%s.wav", file);
  qa_q2_presentation_event ev = {0};
  ev.kind = "sound"; ev.has_actor = true; ev.actor = entity; ev.origin = qa_grapple_body(entity, g).origin;
  ev.path = e->buf; ev.channel = 1; ev.volume = e->hooks.volume(e->hooks.ctx, owner); ev.attenuation = 1;
  ev.reliable = g->options.edition == QA_Q2_EDITION_CLASSIC && reliable; ev.loop = "once";
  g->host->emit(g->host->ctx, ev);
}

static void reset_hook(qa_q2_ctf_grapple_equipment *e, qa_q2_entity *hook, qa_q2_game_services *g) {
  if (!hook->has_owner) { g->remove(g->ctx, hook); return; }
  qa_actor_id owner = hook->owner;
  qa_ctf_grapple_state *source = find_state(e, owner);
  if (!source || !source->has_grapple) { g->remove(g->ctx, hook); return; }
  if (live(g, owner) && qa_shared_body_table_read(g->host->bodies, owner).has_value) qa_q2_ctf_grapple_sound(e, owner, owner, g, "grreset", true);
  source->has_grapple = false; source->grapple_state = "fly";
  source->grapple_release_time = g->host->now(g->host->ctx) + (g->options.edition == QA_Q2_EDITION_RERELEASE ? 1 : 0);
  restore_knockback(owner, g, source);
  loop_hook(hook, g, "");
  if (g->options.edition == QA_Q2_EDITION_CLASSIC) e->hooks.set_grapple_prediction(e->hooks.ctx, owner, false);
  qa_shared_body_table_detach(g->host->bodies, hook->actor);
  g->remove(g->ctx, hook);
}

void qa_q2_ctf_grapple_reset(qa_q2_ctf_grapple_equipment *e, qa_actor_id player, qa_q2_game_services *g) {
  qa_ctf_grapple_state *source = find_state(e, player);
  qa_q2_entity *hook = source && source->has_grapple ? g->entity(g->ctx, &source->grapple) : NULL;
  if (hook) { reset_hook(e, hook, g); return; }
  if (source && source->has_grapple) {
    source->has_grapple = false; source->grapple_state = "fly";
    source->grapple_release_time = g->host->now(g->host->ctx) + (g->options.edition == QA_Q2_EDITION_RERELEASE ? 1 : 0);
    restore_knockback(player, g, source);
    if (g->options.edition == QA_Q2_EDITION_CLASSIC) e->hooks.set_grapple_prediction(e->hooks.ctx, player, false);
  }
}

static bool is_crush(qa_death_reaction r) {
  if (!r.pain.attack) return false;
  const qa_attack_cause *cause = &r.pain.attack->cause;
  switch (cause->kind) {
    case QA_ATTACK_CAUSE_Q1: return cause->death_type && strcmp(cause->death_type, "crush") == 0;
    case QA_ATTACK_CAUSE_Q2: return cause->means_of_death == 20;
    case QA_ATTACK_CAUSE_Q3: return cause->means_of_death == 17;
    case QA_ATTACK_CAUSE_ENVIRONMENT: return cause->hazard && strcmp(cause->hazard, "crush") == 0;
  }
  return false;
}
static void source_touch_fn(void *c, qa_q2_entity *hook, qa_q2_game_services *g, qa_touch_contact contact) {
  qa_q2_ctf_grapple_touch(c, hook, g, contact);
}
static void source_die_fn(void *c, qa_q2_entity *hook, qa_q2_game_services *g, qa_death_reaction r) {
  if (is_crush(r)) reset_hook(c, hook, g);
}

void qa_q2_ctf_grapple_touch(qa_q2_ctf_grapple_equipment *e, qa_q2_entity *hook, qa_q2_game_services *g, qa_touch_contact contact) {
  if (!hook->has_owner || !live(g, hook->owner) || !qa_shared_body_table_read(g->host->bodies, hook->owner).has_value) { reset_hook(e, hook, g); return; }
  qa_actor_id owner = hook->owner;
  qa_ctf_grapple_state *source = qa_q2_ctf_grapple_state(e, owner);
  if (qa_same_actor(contact.other, owner) || strcmp(source->grapple_state, "fly") != 0) return;
  if (contact.surface && ((qa_js_to_int32(contact.surface->native_flags) & 4) != 0)) { reset_hook(e, hook, g); return; }
  qa_q2_body_changes ch = {0}; ch.has_velocity = true; ch.velocity = QA_Q2_ZERO;
  g->move(g->ctx, hook, ch, true);
  e->hooks.noise(e->hooks.ctx, owner, g, qa_q2_game_body(g, hook).origin, "impact");
  qa_combat_state_result cs = qa_gameplay_authority_read(g->host->combat, contact.other);
  if (cs.has_value && cs.value.can_take_damage) {
    if (g->options.edition == QA_Q2_EDITION_CLASSIC || hook->damage != 0)
      g->damage(g->ctx, contact.other, hook, hook->actor.id, &owner, hook->damage, 1, QA_Q2_ZERO,
          qa_q2_game_body(g, hook).origin, contact.plane ? contact.plane->normal : QA_Q2_ZERO, MOD_GRAPPLE, 0, "q2:weapon_grapple");
    if (!live(g, hook->actor.id) || !live(g, owner)) return;
    reset_hook(e, hook, g);
    return;
  }
  source->grapple_state = "pull";
  hook->has_enemy = true; hook->enemy = contact.other;
  g->solid(g->ctx, hook, "none");
  const char *anchor = e->hooks.anchor(e->hooks.ctx, contact.other, g);
  qa_body_state_result body = qa_shared_body_table_read(g->host->bodies, contact.other);
  if (strcmp(anchor, "none") == 0 || !body.has_value) { reset_hook(e, hook, g); return; }
  qa_body_attachment att = {.anchor = contact.other};
  if (strcmp(anchor, "box") == 0 || strcmp(anchor, "player") == 0 || strcmp(anchor, "corpse") == 0) att.follow_kind = QA_BODY_FOLLOW_CENTER;
  else { att.follow_kind = QA_BODY_FOLLOW_TRANSLATION; att.follow_offset = qa_q2_subtract(qa_q2_game_body(g, hook).origin, body.value.origin); }
  qa_shared_body_table_attach(g->host->bodies, hook->actor, att);
  if (g->options.edition == QA_Q2_EDITION_CLASSIC) qa_q2_ctf_grapple_sound(e, owner, owner, g, "grpull", true);
  qa_q2_ctf_grapple_sound(e, hook->actor.id, owner, g, "grhit", false);
  if (g->options.edition == QA_Q2_EDITION_RERELEASE) loop_hook(hook, g, "weapons/grapple/grpull.wav");
  qa_q2_presentation_event ev = {0};
  ev.kind = "effect"; ev.effect = "sparks"; ev.origin = qa_q2_game_body(g, hook).origin;
  ev.direction = contact.plane ? contact.plane->normal : QA_Q2_ZERO;
  g->host->emit(g->host->ctx, ev);
}

void qa_q2_ctf_grapple_offhand(qa_q2_ctf_grapple_equipment *e, qa_actor_id player, qa_q2_game_services *g, bool pressed) {
  if (!pressed) { qa_q2_ctf_grapple_reset(e, player, g); return; }
  if (qa_q2_ctf_grapple_state(e, player)->has_grapple) return;
  qa_q2_ctf_grapple_fire_from_pose(e, player, g);
}

void qa_q2_ctf_grapple_fire_from_pose(qa_q2_ctf_grapple_equipment *e, qa_actor_id player, qa_q2_game_services *g) {
  if (strcmp(qa_q2_ctf_grapple_state(e, player)->grapple_state, "fly") != 0) return;
  qa_grapple_pose pose = e->hooks.pose(e->hooks.ctx, player, g);
  qa_ctf_grapple_settings st = settings_of(e, player, g);
  shot s = project_actor(player, g, pose.hand, pose.view_height, st.players_collide, pose.angles, (qa_vec3){24, 8, -6});
  if (g->options.edition == QA_Q2_EDITION_CLASSIC) qa_q2_ctf_grapple_sound(e, player, player, g, "grfire", true);
  bool launched = qa_q2_ctf_grapple_fire(e, player, g, s.start, s.direction,
      g->options.edition == QA_Q2_EDITION_CLASSIC ? 10 : st.damage,
      g->options.edition == QA_Q2_EDITION_CLASSIC ? 650 : st.fly_speed, 0);
  if (!live(g, player)) return;
  if (g->options.edition == QA_Q2_EDITION_RERELEASE && launched) qa_q2_ctf_grapple_sound(e, player, player, g, "grfire", false);
  e->hooks.noise(e->hooks.ctx, player, g, s.start, "weapon");
}

bool qa_q2_ctf_grapple_fire(qa_q2_ctf_grapple_equipment *e, qa_actor_id owner, qa_q2_game_services *g, qa_vec3 start, qa_vec3 direction, double damage, double speed, double effects) {
  qa_q2_ctf_grapple_bind(e, g);
  qa_ctf_grapple_state *source = qa_q2_ctf_grapple_state(e, owner);
  if (source->has_grapple) return false;
  qa_q2_entity *hook = g->create(g->ctx, "grapple", NULL, 0);
  qa_vec3 normalized = qa_q2_normalize(direction);
  hook->clip_mask = shot_mask(g, settings_of(e, owner, g).players_collide);
  if (g->options.edition == QA_Q2_EDITION_RERELEASE) {
    hook->flags = (double)(qa_js_to_int32(hook->flags) | 0x800 | 0x100000);
    hook->die = (qa_q2_die){e, source_die_fn};
    qa_combat_state st = {0};
    st.can_take_damage = true; st.has_no_knockback = true; st.no_knockback = true;
    st.armor.regular.kind = QA_REGULAR_ARMOR_NONE; st.armor.powered.kind = "none";
    qa_gameplay_authority_create_state(g->host->combat, hook->actor, st, NULL, NULL);
  }
  hook->projectile = true; hook->effects = effects; hook->model = "models/weapons/grapple/hook/tris.md2";
  hook->has_owner = true; hook->owner = owner;
  hook->touch = (qa_q2_touch){e, source_touch_fn};
  hook->damage = damage;
  qa_q2_body_changes ch = {0};
  ch.has_origin = true; ch.origin = start;
  ch.has_angles = true; ch.angles = qa_vector_to_angles_with_rule(normalized, QA_VECTOR_TO_ANGLES_NATIVE);
  ch.has_velocity = true; ch.velocity = qa_q2_scale(normalized, speed);
  ch.has_bounds = true; ch.bounds = (qa_bounds){QA_Q2_ZERO, QA_Q2_ZERO};
  g->move(g->ctx, hook, ch, false);
  source->has_grapple = true; source->grapple = hook->actor.id; source->grapple_state = "fly";
  g->solid(g->ctx, hook, "box");
  g->motion(g->ctx, hook, "fly-missile");
  qa_weapon_trajectory_update_result trajectory = {0};
  if (g->host->is_player(g->host->ctx, owner) && g->host->weapon_behavior) {
    const qa_weapon_behavior_projectile_port *port = g->host->weapon_behavior;
    qa_owned_actor value2 = hook->actor;
    qa_actor_id value3 = owner;
    double value4 = g->host->now(g->host->ctx);
    qa_body_state value5 = qa_q2_game_body(g, hook);
    qa_weapon_behavior_launch launch = {.projectile = value2, .shooter = value3, .weapon = "q2:weapon_grapple",
      .role = "grapple", .time_seconds = value4, .body = value5};
    trajectory = port->launch(port->ctx, launch);
  }
  if (trajectory.has_value) g->project_trajectory(g->ctx, hook, trajectory.value);
  qa_vec3 launch_origin = qa_q2_game_body(g, hook).origin;
  g->show(g->ctx, hook);
  qa_q2_trace_request tr = {.start = qa_grapple_body(owner, g).origin, .end = launch_origin, .ignore = &hook->actor.id, .mask = hook->clip_mask};
  qa_trace_result trace = g->host->trace(g->host->ctx, tr);
  if (trace.fraction < 1) {
    qa_vec3 new_origin;
    if (g->options.edition == QA_Q2_EDITION_CLASSIC)
      new_origin = qa_q2_add(launch_origin, qa_q2_scale(trajectory.has_value ? qa_q2_normalize(trajectory.value.velocity) : normalized, -10));
    else {
      qa_vec3 n = trace.source_plane.plane.normal;
      new_origin = qa_q2_add(trace.end, n);
    }
    qa_q2_body_changes oc = {0}; oc.has_origin = true; oc.origin = new_origin;
    g->move(g->ctx, hook, oc, true);
    qa_actor_id other = trace.hit.kind == QA_TRACE_HIT_ACTOR ? trace.hit.actor : g->host->world_actor(g->host->ctx);
    qa_plane plane = trace.source_plane.plane;
    qa_touch_surface surface = {0};
    const qa_touch_surface *surface_ptr = NULL;
    bool classic = g->options.edition == QA_Q2_EDITION_CLASSIC;
    if (!classic) {
      if (strcmp(trace.kind, "q2") == 0 && trace.surface) { surface = (qa_touch_surface){trace.surface->name, trace.surface->flags, trace.surface->value}; surface_ptr = &surface; }
      else if (strcmp(trace.kind, "q2") != 0) { surface = (qa_touch_surface){"", trace.has_surface_flags ? trace.surface_flags : 0, 0}; surface_ptr = &surface; }
    }
    qa_touch_contact contact = {.self = hook->actor, .other = other, .plane = classic ? NULL : &plane, .surface = classic ? NULL : surface_ptr};
    qa_q2_ctf_grapple_touch(e, hook, g, contact);
    return false;
  }
  if (g->options.edition == QA_Q2_EDITION_RERELEASE) loop_hook(hook, g, "weapons/grapple/grfly.wav");
  return true;
}

static void cable(qa_q2_ctf_grapple_equipment *e, qa_q2_entity *hook, qa_actor_id owner, qa_q2_game_services *g) {
  qa_vec3 origin = qa_grapple_body(owner, g).origin;
  qa_vec3 end = qa_q2_game_body(g, hook).origin;
  qa_grapple_pose pose = e->hooks.pose(e->hooks.ctx, owner, g);
  if (g->options.edition == QA_Q2_EDITION_RERELEASE) {
    if (strcmp(qa_q2_ctf_grapple_state(e, owner)->grapple_state, "hang") == 0) return;
    shot s = project_actor(owner, g, pose.hand, pose.view_height, settings_of(e, owner, g).players_collide, pose.angles, (qa_vec3){7, 2, -9});
    e->hooks.emit(e->hooks.ctx, (qa_grapple_cable_event){"grapple-cable", owner, s.start, end, QA_Q2_ZERO});
    return;
  }
  qa_angle_vectors axes = qa_math_angle_vectors_rule(pose.angles, QA_ANGLE_RULE_NATIVE);
  double side = strcmp(pose.hand, "left") == 0 ? -16 : strcmp(pose.hand, "center") == 0 ? 0 : 16;
  qa_vec3 start = qa_q2_add(qa_q2_add(qa_q2_add(origin, qa_q2_scale(axes.forward, 16)), qa_q2_scale(axes.right, side)), (qa_vec3){0, 0, pose.view_height - 8});
  if (qa_q2_length(qa_q2_subtract(start, end)) < 64) return;
  e->hooks.emit(e->hooks.ctx, (qa_grapple_cable_event){"grapple-cable", owner, origin, end, qa_q2_subtract(start, origin)});
}

void qa_q2_ctf_grapple_pull(qa_q2_ctf_grapple_equipment *e, qa_q2_entity *hook, qa_q2_game_services *g, bool damage_pulse) {
  double left;
  double right;
  if (!hook->has_owner || !live(g, hook->owner) || !qa_shared_body_table_read(g->host->bodies, hook->owner).has_value) { reset_hook(e, hook, g); return; }
  qa_actor_id owner = hook->owner;
  qa_grapple_pose pose = e->hooks.pose(e->hooks.ctx, owner, g);
  if (hook->has_enemy) {
    const char *anchor = e->hooks.anchor(e->hooks.ctx, hook->enemy, g);
    qa_body_state_result body = qa_shared_body_table_read(g->host->bodies, hook->enemy);
    if (!body.has_value || strcmp(anchor, "none") == 0) { reset_hook(e, hook, g); return; }
    qa_q2_body_changes ch = {0};
    if (strcmp(anchor, "box") == 0 || strcmp(anchor, "player") == 0 || strcmp(anchor, "corpse") == 0) {
      ch.has_origin = true;
      ch.origin = qa_q2_add(body.value.origin, qa_q2_scale(qa_q2_add(body.value.bounds.min, body.value.bounds.max), 0.5));
    } else { ch.has_velocity = true; ch.velocity = body.value.velocity; }
    g->move(g->ctx, hook, ch, true);
    qa_combat_state_result cs = qa_gameplay_authority_read(g->host->combat, hook->enemy);
    if (g->options.edition == QA_Q2_EDITION_CLASSIC && damage_pulse && cs.has_value && cs.value.can_take_damage && can_damage_of(e, owner, hook->enemy)) {
      qa_actor_id enemy = hook->enemy;
      void * value2 = g->ctx;
      qa_actor_id value5 = hook->actor.id;
      const qa_actor_id * value6 = &owner;
      qa_vec3 value7 = qa_q2_game_body(g, hook).velocity;
      qa_vec3 value8 = qa_q2_game_body(g, hook).origin;
      g->damage(value2, enemy, hook, value5, value6, 1, 1, value7, value8, QA_Q2_ZERO, MOD_GRAPPLE, 0, "q2:weapon_grapple");
      if (!live(g, hook->actor.id) || !live(g, owner)) return;
      qa_q2_ctf_grapple_sound(e, hook->actor.id, owner, g, "grhurt", false);
    }
    if (e->hooks.dead(e->hooks.ctx, hook->enemy, g)) { reset_hook(e, hook, g); return; }
  }
  cable(e, hook, owner, g);
  qa_ctf_grapple_state *source = qa_q2_ctf_grapple_state(e, owner);
  if (strcmp(source->grapple_state, "fly") == 0) return;
  qa_body_state body = qa_grapple_body(owner, g);
  qa_vec3 direction = qa_q2_subtract(qa_q2_game_body(g, hook).origin, qa_q2_add(body.origin, (qa_vec3){0, 0, pose.view_height}));
  if (strcmp(source->grapple_state, "pull") == 0 && qa_q2_length(direction) < 64) {
    source->grapple_state = "hang";
    if (g->options.edition == QA_Q2_EDITION_CLASSIC) {
      e->hooks.set_grapple_prediction(e->hooks.ctx, owner, true);
      qa_q2_ctf_grapple_sound(e, owner, owner, g, "grhang", true);
    } else loop_hook(hook, g, "weapons/grapple/grhang.wav");
  }
  if (g->options.edition == QA_Q2_EDITION_RERELEASE) {
    qa_owned_actor actor;
    qa_combat_state_result combat = qa_gameplay_authority_read(g->host->combat, owner);
    if (qa_session_actor_registry_resolve_owned(g->host->actors, owner, &actor) && combat.has_value) {
      if (!source->has_no_knockback) {
        source->has_no_knockback = true;
        source->grapple_no_knockback = combat.value.has_no_knockback ? combat.value.no_knockback : false;
      }
      qa_combat_trait_changes tc = {0}; tc.has_no_knockback = true; tc.no_knockback = true;
      qa_gameplay_authority_set_traits(g->host->combat, actor, tc);
    }
  }
  qa_vec3 velocity = qa_q2_scale(qa_q2_normalize(direction), g->options.edition == QA_Q2_EDITION_CLASSIC ? 650 : settings_of(e, owner, g).pull_speed);
  qa_grapple_velocity(owner, g, qa_q2_add(velocity, qa_q2_scale(pose.gravity_vector, (left = pose.gravity * e->hooks.gravity(e->hooks.ctx), right = g->host->frame_seconds(g->host->ctx), left * right))));
}

void qa_q2_ctf_grapple_player_frame(qa_q2_ctf_grapple_equipment *e, qa_actor_id player, qa_q2_game_services *g, bool damage_pulse) {
  qa_ctf_grapple_state *st = find_state(e, player);
  qa_q2_entity *hook = st && st->has_grapple ? g->entity(g->ctx, &st->grapple) : NULL;
  if (hook) qa_q2_ctf_grapple_pull(e, hook, g, damage_pulse);
}

qa_q2_ctf_grapple_equipment *qa_q2_ctf_grapple_create(qa_grapple_hooks hooks,
    bool (*can_damage)(void *, qa_actor_id, qa_actor_id), void *can_ctx,
    qa_ctf_grapple_settings (*settings)(void *, qa_actor_id, qa_q2_game_services *), void *settings_ctx) {
  qa_q2_ctf_grapple_equipment *e = calloc(1, sizeof *e);
  if (!e) return NULL;
  e->hooks = hooks; e->can_damage = can_damage; e->can_ctx = can_ctx; e->settings = settings; e->settings_ctx = settings_ctx;
  e->touches[0] = (qa_q2_named_touch){"CTFGrappleTouch", {e, source_touch_fn}};
  e->dies[0] = (qa_q2_named_die){"grapple_die", {e, source_die_fn}};
  e->defs = (qa_q2_callback_definitions){.touch = e->touches, .touch_count = 1, .die = e->dies, .die_count = 1};
  return e;
}
void qa_q2_ctf_grapple_destroy(qa_q2_ctf_grapple_equipment *e) {
  if (!e) return;
  for (size_t i = 0; i < e->bound_count; i++) {
    qa_session_actor_registry_off_release(e->bound[i]->actors, e->bound[i]->subscription);
    free(e->bound[i]);
  }
  free(e->states); free(e->bound); free(e->buf);
  free(e);
}
qa_grapple_hooks qa_q2_ctf_grapple_hooks(qa_q2_ctf_grapple_equipment *e) { return e->hooks; }
qa_q2_callback_definitions qa_q2_ctf_grapple_callbacks(qa_q2_ctf_grapple_equipment *e) { return e->defs; }
qa_ctf_grapple_state *qa_q2_ctf_grapple_find_state(qa_q2_ctf_grapple_equipment *e, qa_actor_id a) { return find_state(e, a); }
size_t qa_q2_ctf_grapple_state_count(const qa_q2_ctf_grapple_equipment *e) { return e->count; }
qa_ctf_grapple_state *qa_q2_ctf_grapple_state_at(qa_q2_ctf_grapple_equipment *e, size_t index, qa_actor_id *out_actor) {
  *out_actor = e->states[index].id;
  return &e->states[index].v;
}
void qa_q2_ctf_grapple_clear_states(qa_q2_ctf_grapple_equipment *e) { e->count = 0; }
