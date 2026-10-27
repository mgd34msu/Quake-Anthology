/* Port of src/content/q2/equipment/lmctf-grapple.ts. GPL-2.0-or-later. */
#include "qa/content/q2/equipment/lmctf_grapple.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/foundation/fields.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"

typedef struct state_entry { qa_actor_id id; qa_lmctf_grapple_state v; } state_entry;
struct qa_lmctf_grapple_equipment {
  qa_grapple_hooks hooks;
  qa_lmctf_grapple_policy policy;
  void (*released)(void *, qa_actor_id);
  void *released_ctx;
  state_entry *states; size_t count, cap;
  qa_q2_game_services **bound; size_t bound_count, bound_cap;
  qa_q2_named_think thinks[1];
  qa_q2_named_touch touches[1];
  qa_q2_named_die dies[1];
  qa_q2_callback_definitions defs;
};

static bool live(qa_q2_game_services *g, qa_actor_id id) { return qa_session_actor_registry_is_live(g->host->actors, id); }
static qa_lmctf_grapple_state *find_state(qa_lmctf_grapple_equipment *e, qa_actor_id a) {
  for (size_t i = 0; i < e->count; i++) if (qa_same_actor(e->states[i].id, a)) return &e->states[i].v;
  return NULL;
}
qa_lmctf_grapple_state *qa_lmctf_grapple_state_of(qa_lmctf_grapple_equipment *e, qa_actor_id a) {
  qa_lmctf_grapple_state *s = find_state(e, a);
  if (s) return s;
  if (e->count == e->cap) { e->cap = e->cap ? e->cap * 2 : 8; e->states = realloc(e->states, e->cap * sizeof *e->states); if (!e->states) qa_range_error("out of memory"); }
  e->states[e->count].id = a;
  qa_lmctf_grapple_state_init(&e->states[e->count].v);
  return &e->states[e->count++].v;
}
static void delete_state(qa_lmctf_grapple_equipment *e, qa_actor_id a) {
  for (size_t i = 0; i < e->count; i++)
    if (qa_same_actor(e->states[i].id, a)) { memmove(&e->states[i], &e->states[i + 1], (e->count - i - 1) * sizeof *e->states); e->count--; return; }
}
static void call_released(qa_lmctf_grapple_equipment *e, qa_actor_id a) {
  if (e->released) e->released(e->released_ctx, a);
}

typedef struct release_ctx { qa_lmctf_grapple_equipment *e; qa_q2_game_services *g; } release_ctx;
static release_ctx *release_ctxs;
static size_t release_ctx_count, release_ctx_cap;
static void on_release(void *c, qa_owned_actor actor) {
  release_ctx *rc = c;
  qa_lmctf_grapple_equipment *e = rc->e;
  qa_q2_game_services *g = rc->g;
  qa_lmctf_grapple_state *owned = find_state(e, actor.id);
  if (owned) {
    qa_lmctf_grapple_state copy = *owned;
    delete_state(e, actor.id);
    qa_q2_entity *hook = copy.has_hook ? g->entity(g->ctx, &copy.hook) : NULL;
    if (hook && live(g, hook->actor.id)) { g->cancel(g->ctx, hook); g->remove(g->ctx, hook); }
  }
  for (size_t i = 0; i < e->count; i++) {
    qa_lmctf_grapple_state *st = &e->states[i].v;
    qa_actor_id owner = e->states[i].id;
    if (!st->has_hook || !qa_same_actor(st->hook, actor.id)) continue;
    st->has_hook = false; st->hook_state = 0; st->hook_length = 0;
    if (live(g, owner)) call_released(e, owner);
  }
}

void qa_lmctf_grapple_bind(qa_lmctf_grapple_equipment *e, qa_q2_game_services *g) {
  for (size_t i = 0; i < e->bound_count; i++) if (e->bound[i] == g) return;
  if (e->bound_count == e->bound_cap) { e->bound_cap = e->bound_cap ? e->bound_cap * 2 : 4; e->bound = realloc(e->bound, e->bound_cap * sizeof *e->bound); if (!e->bound) qa_range_error("out of memory"); }
  e->bound[e->bound_count++] = g;
  if (release_ctx_count == release_ctx_cap) { release_ctx_cap = release_ctx_cap ? release_ctx_cap * 2 : 8; release_ctxs = realloc(release_ctxs, release_ctx_cap * sizeof *release_ctxs); if (!release_ctxs) qa_range_error("out of memory"); }
  release_ctxs[release_ctx_count] = (release_ctx){e, g};
  qa_session_actor_registry_on_release(g->host->actors, on_release, &release_ctxs[release_ctx_count++]);
}

void qa_lmctf_grapple_abort(qa_lmctf_grapple_equipment *e, qa_actor_id player, qa_q2_game_services *g) {
  qa_lmctf_grapple_state *st = qa_lmctf_grapple_state_of(e, player);
  qa_body_state_result body = qa_shared_body_table_read(g->host->bodies, player);
  if (body.has_value && body.value.ground) {
    qa_vec3 prev = e->hooks.previous_velocity(e->hooks.ctx, player);
    prev.z = 0;
    e->hooks.set_previous_velocity(e->hooks.ctx, player, prev);
    qa_vec3 vel = body.value.velocity;
    vel.z = 0;
    qa_grapple_velocity(player, g, vel);
  }
  call_released(e, player);
  st->hook_state = 0; st->hook_length = 0;
  qa_q2_entity *hook = st->has_hook ? g->entity(g->ctx, &st->hook) : NULL;
  st->has_hook = false;
  if (hook) {
    g->cancel(g->ctx, hook);
    hook->has_enemy = false;
    qa_shared_body_table_detach(g->host->bodies, hook->actor);
    g->remove(g->ctx, hook);
  }
}

static void die_fn(void *c, qa_q2_entity *hook, qa_q2_game_services *g, qa_death_reaction r) {
  (void)r;
  qa_lmctf_grapple_equipment *e = c;
  if (!hook->has_owner) g->remove(g->ctx, hook);
  else qa_lmctf_grapple_abort(e, hook->owner, g);
}
static void think_fn(void *c, qa_q2_entity *hook, qa_q2_game_services *g) {
  qa_lmctf_grapple_equipment *e = c;
  qa_lmctf_grapple_state *st = hook->has_owner ? find_state(e, hook->owner) : NULL;
  if (!st) { g->remove(g->ctx, hook); return; }
  if (st->hook_length <= 126) { g->cancel(g->ctx, hook); return; }
  g->sound(g->ctx, hook->actor, !hook->has_enemy ? "weapons/grapple/gflyair.wav" : "weapons/grapple/gpulling.wav", 0, 1, 1);
  g->schedule(g->ctx, hook, !hook->has_enemy ? 0.4 : 0.8, (qa_q2_think){e, think_fn});
}
static void touch_fn(void *c, qa_q2_entity *hook, qa_q2_game_services *g, qa_touch_contact contact) {
  qa_lmctf_grapple_equipment *e = c;
  if (!hook->has_owner) { g->remove(g->ctx, hook); return; }
  qa_actor_id owner = hook->owner;
  if (!live(g, owner) || !qa_shared_body_table_read(g->host->bodies, owner).has_value) { qa_lmctf_grapple_abort(e, owner, g); return; }
  if (qa_same_actor(contact.other, owner) || (hook->has_enemy && !qa_same_actor(hook->enemy, contact.other))) return;
  qa_lmctf_grapple_state *st = qa_lmctf_grapple_state_of(e, owner);
  const char *anchor = e->hooks.anchor(e->hooks.ctx, contact.other, g);
  bool can_attach = !e->policy.can_attach || e->policy.can_attach(e->policy.ctx, owner, contact.other, g);
  if (strcmp(anchor, "none") == 0 || strcmp(anchor, "box") == 0 ||
      (contact.surface && (((int64_t)contact.surface->native_flags & 4) != 0)) ||
      !can_attach || e->hooks.dead(e->hooks.ctx, contact.other, g)) {
    qa_lmctf_grapple_abort(e, owner, g);
    return;
  }
  qa_q2_body_changes ch = {0}; ch.has_velocity = true; ch.velocity = QA_Q2_ZERO;
  g->move(g->ctx, hook, ch, true);
  st->hook_state = 2;
  bool can_damage = !e->policy.can_damage || e->policy.can_damage(e->policy.ctx, contact.other);
  if (can_damage) {
    double frame = round(g->host->now(g->host->ctx) * 10);
    bool repeated = hook->has_enemy && qa_same_actor(hook->enemy, contact.other);
    if (!repeated || (fmod(frame, 7) == 0 && frame != hook->count)) {
      double amount = repeated ? 1 : 8;
      bool player_hit = e->policy.player_hit ? e->policy.player_hit(e->policy.ctx, contact.other, g) : g->host->is_player(g->host->ctx, contact.other);
      if (player_hit) g->sound(g->ctx, hook->actor, repeated ? "weapons/grapple/gkilling.wav" : "weapons/grapple/ghit.wav", 0, 1, 1);
      else if (!repeated) g->sound(g->ctx, hook->actor, "weapons/grapple/ghitwall.wav", 0, 0.8, 1);
      qa_combat_state_result cs = qa_gameplay_authority_read(g->host->combat, contact.other);
      if (cs.has_value && cs.value.can_take_damage) {
        qa_actor_id other = contact.other;
        g->damage(g->ctx, other, hook, hook->actor.id, &owner, amount, amount, QA_Q2_ZERO,
            qa_q2_game_body(g, hook).origin, contact.plane ? contact.plane->normal : QA_Q2_ZERO, 60, 4, "q2:weapon_hook");
        if (!live(g, hook->actor.id) || !live(g, owner)) return;
      }
      if (repeated) hook->count = frame;
    }
  }
  if (e->hooks.dead(e->hooks.ctx, contact.other, g)) { qa_lmctf_grapple_abort(e, owner, g); return; }
  if (!hook->has_enemy) {
    qa_body_state_result body = qa_shared_body_table_read(g->host->bodies, contact.other);
    if (!body.has_value) { qa_lmctf_grapple_abort(e, owner, g); return; }
    hook->has_enemy = true; hook->enemy = contact.other;
    hook->pos1 = qa_q2_subtract(qa_q2_game_body(g, hook).origin, qa_q2_add(body.value.origin, body.value.bounds.min));
    g->solid(g->ctx, hook, "trigger");
    qa_body_attachment att = {.anchor = contact.other, .follow_kind = QA_BODY_FOLLOW_BOUNDS_MIN, .follow_offset = hook->pos1};
    qa_shared_body_table_attach(g->host->bodies, hook->actor, att);
  }
  qa_q2_presentation_event ev = {0};
  ev.kind = "effect"; ev.effect = "blaster"; ev.origin = qa_q2_game_body(g, hook).origin;
  ev.direction = contact.plane ? contact.plane->normal : QA_Q2_ZERO;
  g->host->emit(g->host->ctx, ev);
}

static qa_q2_entity *launch(qa_lmctf_grapple_equipment *e, qa_actor_id player, qa_q2_game_services *g, qa_vec3 start, qa_vec3 direction) {
  qa_q2_entity *hook = g->create(g->ctx, "noclass", NULL, 0);
  qa_vec3 angles = qa_vector_to_angles_with_rule(direction, QA_VECTOR_TO_ANGLES_NATIVE);
  qa_lmctf_grapple_state *st = qa_lmctf_grapple_state_of(e, player);
  st->has_hook = true; st->hook = hook->actor.id;
  hook->has_owner = true; hook->owner = player;
  hook->touch = (qa_q2_touch){e, touch_fn};
  hook->die = (qa_q2_die){e, die_fn};
  hook->damage = 2; hook->max_health = 59; hook->model = "models/objects/ghook/tris.md2"; hook->clip_mask = 0x6000003;
  angles.x += 90;
  qa_q2_body_changes ch = {0};
  ch.has_origin = true; ch.origin = start;
  ch.has_angles = true; ch.angles = angles;
  ch.has_velocity = true; ch.velocity = qa_q2_scale(qa_q2_normalize(direction), 800);
  g->move(g->ctx, hook, ch, true);
  g->solid(g->ctx, hook, "box");
  g->motion(g->ctx, hook, "fly-missile");
  qa_combat_state cst = {0};
  cst.health = 59; cst.can_take_damage = true;
  cst.armor.regular.kind = QA_REGULAR_ARMOR_NONE; cst.armor.powered.kind = "none";
  qa_gameplay_authority_create_state(g->host->combat, hook->actor, cst, NULL, NULL);
  g->schedule(g->ctx, hook, 1, (qa_q2_think){e, think_fn});
  qa_q2_presentation_event ev = {0};
  ev.kind = "sound"; ev.has_actor = true; ev.actor = player; ev.origin = qa_grapple_body(player, g).origin;
  ev.path = "weapons/grapple/grfire.wav"; ev.channel = 0; ev.volume = 0.8; ev.attenuation = 1; ev.loop = "once";
  g->host->emit(g->host->ctx, ev);
  qa_weapon_trajectory_update_result trajectory = {0};
  if (g->host->is_player(g->host->ctx, player) && g->host->weapon_behavior) {
    const qa_weapon_behavior_projectile_port *port = g->host->weapon_behavior;
    qa_weapon_behavior_launch l = {.projectile = hook->actor, .shooter = player, .weapon = "q2:weapon_hook",
      .role = "grapple", .time_seconds = g->host->now(g->host->ctx), .body = qa_q2_game_body(g, hook)};
    trajectory = port->launch(port->ctx, l);
  }
  if (trajectory.has_value) g->project_trajectory(g->ctx, hook, trajectory.value);
  qa_vec3 launch_origin = qa_q2_game_body(g, hook).origin;
  g->show(g->ctx, hook);
  qa_q2_trace_request tr = {.start = qa_grapple_body(player, g).origin, .end = launch_origin, .ignore = &player, .mask = 0x6000003};
  qa_trace_result trace = g->host->trace(g->host->ctx, tr);
  if (trace.fraction < 1) {
    qa_q2_body_changes oc = {0}; oc.has_origin = true;
    oc.origin = qa_q2_add(launch_origin, qa_q2_scale(trajectory.has_value ? qa_q2_normalize(trajectory.value.velocity) : direction, -10));
    g->move(g->ctx, hook, oc, true);
    qa_actor_id other = trace.hit.kind == QA_TRACE_HIT_ACTOR ? trace.hit.actor : g->host->world_actor(g->host->ctx);
    qa_touch_contact contact = {.self = hook->actor, .other = other};
    touch_fn(e, hook, g, contact);
  }
  return hook;
}

static void draw(qa_lmctf_grapple_equipment *e, qa_actor_id player, qa_vec3 start, qa_vec3 end) {
  if (qa_q2_length(qa_q2_subtract(end, start)) > 64)
    e->hooks.emit(e->hooks.ctx, (qa_grapple_cable_event){"grapple-cable", player, start, end, QA_Q2_ZERO});
}

void qa_lmctf_grapple_fire(qa_lmctf_grapple_equipment *e, qa_actor_id player, qa_q2_game_services *g) {
  qa_lmctf_grapple_bind(e, g);
  qa_lmctf_grapple_state *st = qa_lmctf_grapple_state_of(e, player);
  qa_grapple_pose pose = e->hooks.pose(e->hooks.ctx, player, g);
  qa_body_state body = qa_grapple_body(player, g);
  qa_angle_vectors basis = qa_math_angle_vectors_rule(pose.angles, QA_ANGLE_RULE_NATIVE);
  double side = strcmp(pose.hand, "left") == 0 ? -8 : strcmp(pose.hand, "center") == 0 ? 0 : 8;
  qa_vec3 start = qa_q2_add(qa_q2_add(qa_q2_add(body.origin, qa_q2_scale(basis.forward, 8)), qa_q2_scale(basis.right, side)), (qa_vec3){0, 0, pose.view_height - 8});
  if (st->hook_state == 0) {
    st->hook_state = 1;
    qa_q2_entity *hook = launch(e, player, g, start, basis.forward);
    /* launch may have reallocated states */
    st = qa_lmctf_grapple_state_of(e, player);
    st->has_hook = true; st->hook = hook->actor.id;
    if (!live(g, hook->actor.id)) { st->has_hook = false; st->hook_state = 0; return; }
    draw(e, player, start, qa_q2_game_body(g, hook).origin);
    draw(e, player, start, qa_q2_game_body(g, hook).origin);
    return;
  }
  qa_q2_entity *hook = st->has_hook ? g->entity(g->ctx, &st->hook) : NULL;
  if (!hook) { st->hook_state = 0; return; }
  if (st->hook_state == 1) { draw(e, player, start, qa_q2_game_body(g, hook).origin); return; }
  if (hook->has_enemy) {
    qa_body_state_result target = qa_shared_body_table_read(g->host->bodies, hook->enemy);
    if (target.has_value) {
      qa_q2_body_changes ch = {0}; ch.has_origin = true;
      ch.origin = qa_q2_add(qa_q2_add(target.value.origin, target.value.bounds.min), hook->pos1);
      g->move(g->ctx, hook, ch, true);
    }
  }
  qa_vec3 end = qa_q2_game_body(g, hook).origin;
  draw(e, player, start, end);
  double distance = trunc(qa_q2_length(qa_q2_subtract(end, start)));
  st->hook_length = distance;
  double speed = distance > 120 ? 800 : distance > 100 ? distance * 5 : distance > 80 ? distance * 4 : distance > 40 ? distance * 3 : distance > 20 ? distance * 2 : distance > 10 ? distance : 1;
  qa_vec3 velocity = qa_q2_scale(qa_q2_normalize(qa_q2_subtract(end, start)), speed);
  qa_grapple_velocity(player, g, velocity);
  e->hooks.set_previous_velocity(e->hooks.ctx, player, velocity);
}

double qa_lmctf_grapple_gravity_scale(qa_lmctf_grapple_equipment *e, qa_actor_id a) {
  qa_lmctf_grapple_state *st = find_state(e, a);
  return st && st->hook_state == 2 && st->hook_length < 50 ? 0 : 1;
}

qa_lmctf_grapple_equipment *qa_lmctf_grapple_create(qa_grapple_hooks hooks, qa_lmctf_grapple_policy policy,
    void (*released)(void *, qa_actor_id), void *released_ctx) {
  qa_lmctf_grapple_equipment *e = calloc(1, sizeof *e);
  if (!e) return NULL;
  e->hooks = hooks; e->policy = policy; e->released = released; e->released_ctx = released_ctx;
  e->thinks[0] = (qa_q2_named_think){"lmctf:Grapple_Bolt_Think", {e, think_fn}};
  e->touches[0] = (qa_q2_named_touch){"lmctf:hook_touch", {e, touch_fn}};
  e->dies[0] = (qa_q2_named_die){"lmctf:hook_die", {e, die_fn}};
  e->defs = (qa_q2_callback_definitions){.think = e->thinks, .think_count = 1, .touch = e->touches, .touch_count = 1, .die = e->dies, .die_count = 1};
  return e;
}
void qa_lmctf_grapple_destroy(qa_lmctf_grapple_equipment *e) {
  if (!e) return;
  free(e->states); free(e->bound);
  free(e);
}
qa_grapple_hooks qa_lmctf_grapple_hooks(qa_lmctf_grapple_equipment *e) { return e->hooks; }
qa_q2_callback_definitions qa_lmctf_grapple_callbacks(qa_lmctf_grapple_equipment *e) { return e->defs; }
