/* Port of src/content/q3/base/game/missile.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/missile.h"
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/base/game/grapple.h"
#include "qa/content/q3/base/game/hitscan.h"
#include "qa/content/q3/base/game/save_module_values.h"
#include "qa/content/q3/base/game/use_participant.h"
#include "qa/content/q3/base/shared/direction_byte.h"
#include "qa/content/q3/base/shared/entity_shared.h"
#include "qa/content/q3/foundation/weapon_behavior.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include "qa/persistence/save_image.h"
#include "qa/foundation/js.h"
enum { MASK_SHOT = 1 | 0x2000000 | 0x4000000, EF_BOUNCE_HALF = 0x20, EF_NODRAW = 0x80, EF_TICKING = 2, SURF_METALSTEPS = 0x1000,
  SVF_NOCLIENT = 1, SVF_USE_CURRENT_ORIGIN = 128, ET_GENERAL = 0, ET_PLAYER = 1, ET_MISSILE = 3, ET_GRAPPLE = 11, GT_TEAM = 3, MOD_JUICED = 27, MOD_PROXIMITY_MINE = 25 };
typedef struct qa_q3_native_projectile {
  qa_q3_projectile p; qa_q3_game_entity *entity;
  bool attached_player; qa_actor_id attachment; bool has_trigger; qa_actor_id trigger;
} native_t;
typedef qa_q3_missile_runtime RT;
#define IMPL(name) "q3.base.game.missile." name

qa_vec3 qa_q3_snap_vector(qa_vec3 v) {
  qa_vec3 r = {(double)qa_qvm_float_to_int(v.x), (double)qa_qvm_float_to_int(v.y), (double)qa_qvm_float_to_int(v.z)}; return r;
}
static double axis_snap(double v, double to) { return qa_int32((double)qa_qvm_float_to_int(v) + (to <= v ? 0 : 1)); }
qa_vec3 qa_q3_snap_vector_towards(qa_vec3 v, qa_vec3 toward) {
  qa_vec3 r = {axis_snap(v.x, toward.x), axis_snap(v.y, toward.y), axis_snap(v.z, toward.z)}; return r;
}
static qa_q3_game_combat_context *combat_of(RT *r) { return r->host.combat(r->host.ctx); }
static qa_q3_entity_pool *pool_of(RT *r) { return combat_of(r)->entities; }
static qa_q3_game_entity *native_of(RT *r, qa_actor_id actor) {
  qa_q3_entity_pool *pool = pool_of(r); return pool->options.records.native_by_actor(pool->options.records.ctx, actor);
}
static qa_q3_game_client *client_of(qa_q3_game_entity *e) { if (!e->client) qa_range_error("Missile rule requires a client entity"); return e->client; }
static qa_q3_game_entity *parent_of(qa_q3_game_entity *e) { if (!e->parent) qa_range_error("Missile rule requires its source parent"); return e->parent; }
static qa_vec3 normalize_direction(qa_q3_missile_direction *d) {
  qa_vec3 n = qa_normalize3(qa_math_vec3(d->x, d->y, d->z)); d->x = n.x; d->y = n.y; d->z = n.z; return n;
}
static qa_vec3 center(qa_q3_game_entity *e) { qa_vec3 min = qa_q3_base_entity_shared_mins(&e->r); qa_vec3 max = qa_q3_base_entity_shared_maxs(&e->r); qa_bounds b = {min, max}; return qa_q3_grapple_target(qa_q3_base_entity_shared_current_origin(&e->r), b); }
static const qa_q3_missionpack_missile_services *missionpack(RT *r) {
  if (!r->host.has_missionpack) qa_range_error("Projectile requires missionpack");
  return &r->host.missionpack;
}
static void owned(RT *r, qa_q3_game_entity *e) { if (qa_q3_entity_pool_get(pool_of(r), e->slot) != e) qa_range_error("Missile entity does not belong to this pool"); }
static void link_entity(RT *r, qa_q3_game_entity *e) { r->host.world.link(r->host.world.ctx, e); }

/* TS Map<GameEntity, NativeProjectile>: insertion ordered; removal retires the record until no projectile
 * operation is on the stack, because TS callers keep using a deleted record after its actor is released. */
static native_t *find_projectile(RT *r, qa_q3_game_entity *e) { for (size_t i = 0; i < r->count; i++) if (r->projectiles[i]->entity == e) return r->projectiles[i]; return NULL; }
static void retire(RT *r, native_t *n) {
  if (r->retired_count == r->retired_capacity) {
    size_t cap = r->retired_capacity ? r->retired_capacity * 2 : 8; void *grown = realloc(r->retired, cap * sizeof *r->retired);
    if (!grown) qa_range_error("out of memory");
    r->retired = grown; r->retired_capacity = cap;
  }
  r->retired[r->retired_count++] = n;
}
static void flush_retired(RT *r) {
  if (r->depth != 0) return;
  for (size_t i = 0; i < r->retired_count; i++) free(r->retired[i]);
  r->retired_count = 0;
}
static void remove_projectile(RT *r, native_t *n) {
  for (size_t i = 0; i < r->count; i++) if (r->projectiles[i] == n) {
    memmove(&r->projectiles[i], &r->projectiles[i + 1], (r->count - i - 1) * sizeof *r->projectiles); r->count--; retire(r, n); return;
  }
}
static void set_projectile(RT *r, native_t *n) {
  for (size_t i = 0; i < r->count; i++) if (r->projectiles[i]->entity == n->entity) { retire(r, r->projectiles[i]); r->projectiles[i] = n; return; }
  if (r->count == r->capacity) {
    size_t cap = r->capacity ? r->capacity * 2 : 16; void *grown = realloc(r->projectiles, cap * sizeof *r->projectiles);
    if (!grown) qa_range_error("out of memory");
    r->projectiles = grown; r->capacity = cap;
  }
  r->projectiles[r->count++] = n;
}
/* bindProjectile: TS getters read bolt live, so p is loaded from the entity around every callback. */
static void load_live(native_t *n) {
  qa_q3_game_entity *b = n->entity; qa_q3_projectile *p = &n->p;
  p->weapon = b->s.weapon; p->direct = b->damage; p->splash = b->splash_damage; p->radius = b->splash_radius;
  p->method = b->method_of_death; p->splash_method = b->splash_method_of_death; p->damage_point = b->s.origin;
  p->trajectory = b->s.pos; p->flags = b->s.e_flags;
}
static native_t *bind_projectile(qa_q3_game_entity *bolt, qa_actor_id owner, const qa_actor_id *pass, const qa_actor_id *attachment, const qa_actor_id *trigger) {
  native_t *n = calloc(1, sizeof *n); if (!n) qa_range_error("out of memory");
  n->entity = bolt; n->p.actor = qa_q3_game_entity_actor(bolt); n->p.owner = owner;
  if (pass) { n->p.has_pass = true; n->p.pass = *pass; }
  if (attachment) { n->attached_player = true; n->attachment = *attachment; }
  if (trigger) { n->has_trigger = true; n->trigger = *trigger; }
  load_live(n); return n;
}
static native_t *projectile_of(RT *r, qa_q3_game_entity *e) {
  native_t *n = find_projectile(r, e);
  if (!n || native_of(r, n->p.actor.id) != e) qa_range_error("Missile continuation does not own this actor lifetime");
  return n;
}
static qa_q3_game_client *owner_client(RT *r, native_t *n) { qa_q3_game_entity *o = native_of(r, n->p.owner); return o ? o->client : NULL; }
static void release_projectile(RT *r, native_t *n) { qa_q3_game_entity *e = native_of(r, n->p.actor.id); if (e) qa_q3_entity_pool_free(pool_of(r), e); }
static void released(void *ctx, qa_owned_actor released_actor) {
  RT *r = ctx; qa_actor_id actor = released_actor.id;
  size_t count = r->count; if (count == 0) return;
  qa_q3_entity_pool *pool = pool_of(r);
  native_t **snapshot = malloc(count * sizeof *snapshot); if (!snapshot) qa_range_error("out of memory");
  memcpy(snapshot, r->projectiles, count * sizeof *snapshot);
  r->depth++;
  for (size_t i = 0; i < count; i++) {
    native_t *n = snapshot[i]; qa_q3_game_entity *entity = n->entity;
    if (find_projectile(r, entity) != n) continue;
    if (qa_same_actor(n->p.actor.id, actor)) {
      remove_projectile(r, n);
      if (entity->s.weapon == QA_Q3_WP_GRAPPLING_HOOK) {
        qa_q3_game_client *client = owner_client(r, n);
        if (client && client->hook == entity) { client->hook = NULL; client->ps.pm_flags = qa_js_to_int32(client->ps.pm_flags) & ~QA_Q3_FLAG_GRAPPLE_PULL; }
      }
      if (n->attached_player && entity->s.weapon == QA_Q3_WP_PROX_LAUNCHER) {
        qa_q3_game_entity *target = native_of(r, n->attachment);
        if (target && target->client && target->activator == entity) { target->client->ps.e_flags = qa_js_to_int32(target->client->ps.e_flags) & ~EF_TICKING; target->activator = NULL; }
      }
      if (n->has_trigger) { qa_q3_game_entity *trigger = native_of(r, n->trigger); if (trigger) qa_q3_entity_pool_free(pool, trigger); }
    } else if ((entity->s.weapon == QA_Q3_WP_GRAPPLING_HOOK && qa_same_actor(n->p.owner, actor)) || (n->attached_player && qa_same_actor(n->attachment, actor))) {
      release_projectile(r, n);
    } else if (n->has_trigger && qa_same_actor(n->trigger, actor)) {
      n->has_trigger = false;
      if (native_of(r, n->p.actor.id) == entity) entity->activator = NULL;
    }
  }
  r->depth--; free(snapshot); flush_retired(r);
}

/* Q3ProjectileHost over one projectile. */
typedef struct hctx { RT *r; qa_q3_game_entity *e; native_t *n; } hctx;
static bool h_live(void *ctx) { hctx *h = ctx; qa_q3_game_entity * left = native_of(h->r, h->n->p.actor.id); return left == h->e; }
static void h_begin(hctx *h) { if (h_live(h)) { h->e->s.pos = h->n->p.trajectory; h->e->s.e_flags = h->n->p.flags; } }
static void h_end(hctx *h) { if (h_live(h)) load_live(h->n); }
static qa_q3_projectile_phase h_phase(void *ctx) {
  hctx *h = ctx; return h->e->free_after_event ? QA_Q3_PHASE_EVENT : h->e->s.e_type == ET_MISSILE ? QA_Q3_PHASE_FLIGHT : QA_Q3_PHASE_ATTACHED;
}
static double h_event_time(void *ctx) { return ((hctx *)ctx)->e->event_time; }
static void h_clear_event(void *ctx) { ((hctx *)ctx)->e->s.event = 0; }
static qa_vec3 h_origin(void *ctx) { return qa_q3_base_entity_shared_current_origin(&((hctx *)ctx)->e->r); }
static bool tumbling(qa_q3_game_entity *e) { return e->s.weapon == QA_Q3_WP_GRENADE_LAUNCHER || e->s.weapon == QA_Q3_WP_PROX_LAUNCHER; }
static void h_move(void *ctx, qa_vec3 origin, qa_vec3 velocity) {
  hctx *h = ctx; qa_body_state_result body = qa_shared_body_table_read(h->r->host.bodies, h->n->p.actor.id);
  if (body.has_value) {
    body.value.origin = origin; body.value.velocity = velocity;
    /* Native cgame derives orientation from velocity; grenades and prox mines tumble on their angle trajectory. */
    if (!tumbling(h->e)) body.value.angles = qa_vector_to_angles(velocity);
    qa_shared_body_table_write(h->r->host.bodies, h->n->p.actor, body.value);
    qa_q3_base_entity_shared_set_current_origin(&h->e->r, origin);
  }
  if (!tumbling(h->e)) { h->e->s.apos.type = QA_Q3_BASE_TR_STATIONARY; h->e->s.apos.base = qa_vector_to_angles(velocity); h->e->s.apos.delta = qa_math_vec3(0, 0, 0); }
}
static void h_set_origin(void *ctx, qa_vec3 origin) {
  hctx *h = ctx; h_begin(h); qa_q3_set_origin(h->e, origin);
  qa_body_state_result body = qa_shared_body_table_read(h->r->host.bodies, h->n->p.actor.id);
  if (body.has_value) { body.value.origin = origin; body.value.velocity = qa_math_vec3(0, 0, 0); qa_shared_body_table_write(h->r->host.bodies, h->n->p.actor, body.value); }
  h_end(h);
}
static void h_link(void *ctx) { hctx *h = ctx; h_begin(h); link_entity(h->r, h->e); h_end(h); }
static void h_release(void *ctx) { hctx *h = ctx; h_begin(h); qa_q3_entity_pool * pool = pool_of(h->r); qa_q3_entity_pool_free(pool, h->e); h_end(h); }
static qa_q3_base_actor_trace_result trace_box(RT *r, qa_q3_game_entity *e, qa_vec3 start, qa_vec3 end, const qa_actor_id *pass) {
  qa_q3_base_actor_trace_query q; memset(&q, 0, sizeof q);
  q.start = start; q.end = end; q.pass_actor = pass; q.mask = e->clipmask;
  q.shape.kind = QA_Q3_BASE_SHAPE_BOX; q.shape.mins = qa_q3_base_entity_shared_mins(&e->r); q.shape.maxs = qa_q3_base_entity_shared_maxs(&e->r);
  return r->host.spatial->trace_actor(r->host.spatial->ctx, q);
}
static qa_q3_base_actor_trace_result h_trace(void *ctx, qa_vec3 start, qa_vec3 end, const qa_actor_id *pass) {
  hctx *h = ctx; return trace_box(h->r, h->e, start, end, pass);
}
static qa_actor_id h_world_actor(void *ctx) { return qa_q3_game_entity_actor(qa_q3_entity_pool_at(pool_of(((hctx *)ctx)->r), QA_Q3_BASE_ENTITYNUM_WORLD)).id; }
static bool h_target(void *ctx, qa_actor_id actor, qa_q3_projectile_target *out) {
  hctx *h = ctx; qa_q3_game_combat_context *c = combat_of(h->r);
  qa_combat_state_result state = qa_gameplay_authority_read(c->authority, actor); if (!state.has_value) return false;
  qa_q3_game_entity *native = native_of(h->r, actor), *owner = native_of(h->r, h->n->p.owner);
  qa_combat_state_result attacker = qa_gameplay_authority_read(c->authority, h->n->p.owner);
  bool player = c->is_player(c->services_ctx, actor);
  qa_q3_accuracy_subject subject_target = {actor, state.value.can_take_damage, player, state.value.health, state.value.team};
  qa_q3_accuracy_subject subject_owner = {h->n->p.owner, attacker.has_value && attacker.value.can_take_damage, owner && owner->client,
      attacker.has_value ? attacker.value.health : 0, attacker.has_value ? attacker.value.team : NULL};
  out->damageable = state.value.can_take_damage; out->player = player;
  out->invulnerable = native && native->client && native->client->invulnerability_time > c->time;
  out->accuracy_eligible = qa_q3_accuracy_hit(c->game_type >= GT_TEAM, subject_target, subject_owner);
  return true;
}
static void h_emit(void *ctx, qa_q3_projectile_impact ev) {
  hctx *h = ctx; h_begin(h); qa_q3_entity_pool *pool = pool_of(h->r);
  if (ev.kind == QA_Q3_IMPACT_BOUNCE) qa_q3_entity_pool_add_event(pool, h->e, QA_Q3_EV_GRENADE_BOUNCE, 0);
  else if (ev.flesh && ev.has_target) {
    qa_q3_game_entity *target = native_of(h->r, ev.target);
    if (!target || !target->client) qa_range_error("Q3 admitted player has no native hit-event client");
    qa_q3_entity_pool_add_event(pool, h->e, QA_Q3_EV_MISSILE_HIT, qa_q3_base_direction_to_byte(&ev.normal)); h->e->s.other_entity_num = target->s.number;
  } else qa_q3_entity_pool_add_event(pool, h->e, (qa_js_to_int32(ev.surface_flags) & SURF_METALSTEPS) ? QA_Q3_EV_MISSILE_MISS_METAL : QA_Q3_EV_MISSILE_MISS, qa_q3_base_direction_to_byte(&ev.normal));
  h_end(h);
}
static void h_retain(void *ctx) { hctx *h = ctx; h_begin(h); h->e->free_after_event = true; h->e->s.e_type = ET_GENERAL; h_end(h); }
static void h_damage(void *ctx, qa_actor_id target, qa_vec3 direction, qa_vec3 point) {
  hctx *h = ctx; h_begin(h); qa_q3_game_combat_context *c = combat_of(h->r);
  qa_q3_damage_participant t = c->participant(c->services_ctx, target), src = qa_q3_participant_entity(h->e), own = c->participant(c->services_ctx, h->n->p.owner);
  qa_q3_combat_damage_from(c, t, &src, &own, &direction, &point, h->e->damage, 0, h->e->method_of_death, &h->n->p.actor.id);
  h_end(h);
}
static bool h_radius(void *ctx, qa_vec3 origin, const qa_actor_id *ignore) {
  hctx *h = ctx; h_begin(h); qa_q3_game_combat_context *c = combat_of(h->r);
  qa_q3_damage_participant own = c->participant(c->services_ctx, h->n->p.owner), ig; if (ignore) ig = c->participant(c->services_ctx, *ignore);
  bool hit = qa_q3_combat_radius_damage_from(c, origin, own, h->e->splash_damage, h->e->splash_radius, ignore ? &ig : NULL, h->e->splash_method_of_death, &h->n->p.actor.id);
  h_end(h); return hit;
}
static void h_accuracy(void *ctx) {
  hctx *h = ctx; qa_q3_game_entity *current = native_of(h->r, h->n->p.owner);
  if (current && current->client) { qa_q3_game_client *client = client_of(current); client->accuracy_hits = qa_int32(client->accuracy_hits + 1); }
}
static void h_think(void *ctx) { hctx *h = ctx; h_begin(h); qa_q3_game_entity * entity = h->e; double time2 = combat_of(h->r)->time; qa_q3_run_think(entity, time2); h_end(h); }
static void h_moved(void *ctx) { (void)ctx; }
static qa_q3_reflection_result h_reflection(void *ctx, qa_actor_id target, qa_vec3 direction, qa_vec3 point) {
  hctx *h = ctx; h_begin(h); qa_q3_game_entity *current = native_of(h->r, target);
  if (!current || !current->client) qa_range_error("Invulnerable Q3 player lost its native client");
  const qa_q3_missionpack_missile_services *m = missionpack(h->r);
  qa_q3_reflection_result out = m->invulnerability_impact(m->ctx, current, direction, point); h_end(h); return out;
}
static void proximity_player(RT *r, qa_q3_game_entity *mine, qa_q3_game_entity *player);
static bool special_impact(RT *r, qa_q3_game_entity *entity, qa_q3_base_actor_trace_result trace, qa_actor_id actor) {
  qa_q3_game_combat_context *c = combat_of(r); qa_q3_entity_pool *pool = c->entities; qa_q3_game_entity *other = native_of(r, actor);
  qa_vec3 normal = trace.contact_is_plane ? trace.contact_plane.normal : qa_math_vec3(0, 0, 0);
  if (!other && c->is_player(c->services_ctx, actor)) qa_range_error("Admitted Q3 map player has no native client behavior record");
  if (r->host.has_missionpack && entity->s.weapon == QA_Q3_WP_PROX_LAUNCHER) {
    if (entity->s.pos.type != QA_Q3_BASE_TR_GRAVITY) return true;
    if (other && other->s.e_type == ET_PLAYER && qa_q3_game_entity_health(other) > 0) { proximity_player(r, entity, other); return true; }
    qa_q3_set_origin(entity, qa_q3_snap_vector_towards(trace.end, entity->s.pos.base));
    qa_q3_entity_pool_add_event(pool, entity, QA_Q3_EV_PROXIMITY_MINE_STICK, trace.surface_flags);
    entity->think = qa_q3_callback_family_resolve(&pool->callbacks.think, IMPL("specialImpact.think")); qa_q3_game_entity_set_nextthink(entity, qa_int32(c->time + 2000));
    qa_vec3 angles = qa_vector_to_angles(normal); entity->s.angles = qa_math_vec3(angles.x + 90, angles.y, angles.z);
    entity->enemy = other; entity->die = qa_q3_callback_family_resolve(&pool->callbacks.die, IMPL("specialImpact.die")); entity->movedir = normal;
    qa_q3_base_entity_shared_set_mins(&entity->r, qa_math_vec3(-4, -4, -4)); qa_q3_base_entity_shared_set_maxs(&entity->r, qa_math_vec3(4, 4, 4));
    link_entity(r, entity); return true;
  }
  const char *name = qa_q3_game_entity_classname(entity);
  if (name && strcmp(name, "hook") == 0) {
    qa_q3_game_entity *event = qa_q3_entity_pool_spawn(pool); qa_vec3 position;
    if (other && qa_q3_game_entity_takedamage(other) && other->client) {
      qa_q3_entity_pool_add_event(pool, event, QA_Q3_EV_MISSILE_HIT, qa_q3_base_direction_to_byte(&normal)); event->s.other_entity_num = other->s.number;
      native_t *n = projectile_of(r, entity); n->attached_player = true; n->attachment = qa_q3_game_entity_actor(other).id;
      position = qa_q3_snap_vector_towards(center(other), entity->s.pos.base);
    } else { position = trace.end; qa_q3_entity_pool_add_event(pool, event, QA_Q3_EV_MISSILE_MISS, qa_q3_base_direction_to_byte(&normal)); entity->enemy = NULL; }
    position = qa_q3_snap_vector_towards(position, entity->s.pos.base);
    event->free_after_event = true; event->s.e_type = ET_GENERAL; entity->s.e_type = ET_GRAPPLE;
    qa_q3_set_origin(entity, position); qa_q3_set_origin(event, position);
    entity->think = qa_q3_callback_family_resolve(&pool->callbacks.think, IMPL("hookThink")); qa_q3_game_entity_set_nextthink(entity, qa_int32(c->time + QA_Q3_GRAPPLE_THINK_INTERVAL));
    qa_q3_game_client *client = owner_client(r, projectile_of(r, entity));
    if (!client) { qa_q3_entity_pool_free(pool, entity); qa_q3_entity_pool_free(pool, event); return true; }
    client->ps.pm_flags = qa_js_to_int32(client->ps.pm_flags) | QA_Q3_FLAG_GRAPPLE_PULL; client->ps.grapple_point = qa_q3_base_entity_shared_current_origin(&entity->r);
    link_entity(r, entity); link_entity(r, event); return true;
  }
  return false;
}
static bool h_special_impact(void *ctx, qa_q3_base_actor_trace_result trace, qa_actor_id target) {
  hctx *h = ctx; h_begin(h); bool out = special_impact(h->r, h->e, trace, target); h_end(h); return out;
}
static void h_special_after_move(void *ctx) {
  hctx *h = ctx; h_begin(h);
  if (h->r->host.has_missionpack && h->e->s.weapon == QA_Q3_WP_PROX_LAUNCHER && h->e->count == 0) {
    RT * r2 = h->r;
    qa_q3_game_entity * e2 = h->e;
    qa_vec3 start = qa_q3_base_entity_shared_current_origin(&h->e->r);
    qa_vec3 end = qa_q3_base_entity_shared_current_origin(&h->e->r);
    qa_q3_base_actor_trace_result t = trace_box(r2, e2, start, end, NULL);
    if (t.solidity == QA_Q3_BASE_SOLID_CLEAR || t.hit_kind != QA_Q3_BASE_HIT_ACTOR || !qa_same_actor(t.hit_actor, h->n->p.owner)) { h->e->count = 1; h->n->p.has_pass = false; }
  }
  h_end(h);
}
static void h_special_no_impact(void *ctx) {
  hctx *h = ctx; h_begin(h); qa_q3_game_client *client = owner_client(h->r, h->n);
  if (client && client->hook == h->e) client->hook = NULL;
  h_end(h);
}
static qa_q3_projectile_host make_host(hctx *h) {
  qa_q3_game_combat_context *c = combat_of(h->r); qa_q3_projectile_host t; memset(&t, 0, sizeof t);
  t.time = c->time; t.previous_time = h->r->host.previous_time; t.ctx = h;
  t.live = h_live; t.phase = h_phase; t.event_time = h_event_time; t.clear_event = h_clear_event; t.origin = h_origin; t.move = h_move;
  t.set_origin = h_set_origin; t.link = h_link; t.release = h_release; t.trace = h_trace; t.target = h_target; t.world_actor = h_world_actor;
  t.emit = h_emit; t.retain = h_retain; t.damage = h_damage; t.radius = h_radius; t.accuracy = h_accuracy; t.think = h_think; t.moved = h_moved;
  t.has_reflection = h->r->host.has_missionpack; t.reflection_impact = h_reflection;
  const char *name = qa_q3_game_entity_classname(h->e);
  t.has_special = (name && strcmp(name, "hook") == 0) || h->e->s.weapon == QA_Q3_WP_PROX_LAUNCHER;
  t.special_impact = h_special_impact; t.special_after_move = h_special_after_move; t.special_no_impact = h_special_no_impact;
  return t;
}
typedef enum op_kind { OP_STEP, OP_EXPLODE, OP_BOUNCE, OP_IMPACT } op_kind;
static void run_op(RT *r, qa_q3_game_entity *e, native_t *n, op_kind kind, const qa_q3_base_actor_trace_result *trace) {
  hctx h = {r, e, n}; r->depth++; load_live(n);
  qa_q3_projectile_host host = make_host(&h);
  switch (kind) {
    case OP_STEP: qa_q3_step_projectile(&n->p, host); break;
    case OP_EXPLODE: qa_q3_explode_projectile(&n->p, host); break;
    case OP_BOUNCE: qa_q3_bounce_projectile(&n->p, host, *trace); break;
    case OP_IMPACT: qa_q3_impact_projectile(&n->p, host, *trace); break;
  }
  h_begin(&h); r->depth--; flush_retired(r);
}
static void step(RT *r, qa_q3_game_entity *e, native_t *n) {
  if (e->s.e_type == ET_MISSILE && !e->free_after_event && r->host.weapon_behavior) {
    qa_body_state_result body = qa_shared_body_table_read(r->host.bodies, n->p.actor.id);
    if (body.has_value) {
      const qa_weapon_behavior_projectile_port *port = r->host.weapon_behavior;
      qa_weapon_trajectory_update_result update = port->step(port->ctx, n->p.actor, body.value, r->host.previous_time / 1000);
      if (update.has_value) {
        body.value.origin = update.value.origin; body.value.velocity = update.value.velocity; body.value.angles = update.value.angles;
        qa_shared_body_table_write(r->host.bodies, n->p.actor, body.value);
        e->s.pos.base = update.value.origin; e->s.pos.delta = update.value.velocity; e->s.pos.time = r->host.previous_time;
        qa_q3_base_entity_shared_set_current_origin(&e->r, update.value.origin);
      }
    }
  }
  run_op(r, e, n, OP_STEP, NULL);
}
bool qa_q3_missile_runtime_owner_of(RT *r, qa_actor_id actor, qa_actor_id *out) {
  qa_q3_game_entity *e = native_of(r, actor); native_t *n = e ? find_projectile(r, e) : NULL;
  if (!n || !qa_same_actor(n->p.actor.id, actor)) return false;
  *out = n->p.owner; return true;
}
bool qa_q3_missile_runtime_run_owned(RT *r, qa_owned_actor actor) {
  qa_q3_game_entity *e = native_of(r, actor.id); if (!e) return false;
  native_t *n = find_projectile(r, e); if (!n || !qa_same_actor(n->p.actor.id, actor.id)) return false;
  step(r, e, n); return true;
}
static qa_q3_base_actor_trace_result server_trace(RT *r, qa_q3_base_server_trace_result t) {
  qa_q3_base_actor_trace_result m; memset(&m, 0, sizeof m);
  m.fraction = t.fraction; m.surface_flags = t.surface_flags; m.contents = t.contents; m.end = t.end;
  m.contact_is_plane = t.contact_is_plane; m.contact_plane = t.contact_plane; m.solidity = t.solidity;
  m.hit_kind = QA_Q3_BASE_HIT_ACTOR; m.hit_actor = qa_q3_game_entity_actor(qa_q3_entity_pool_at(pool_of(r), t.entity_num)).id;
  return m;
}
void qa_q3_missile_runtime_bounce(RT *r, qa_q3_game_entity *e, qa_q3_base_server_trace_result trace) {
  native_t *n = projectile_of(r, e); qa_q3_base_actor_trace_result t = server_trace(r, trace); run_op(r, e, n, OP_BOUNCE, &t);
}
void qa_q3_missile_runtime_explode(RT *r, qa_q3_game_entity *e) { run_op(r, e, projectile_of(r, e), OP_EXPLODE, NULL); }
void qa_q3_missile_runtime_impact(RT *r, qa_q3_game_entity *e, qa_q3_base_server_trace_result trace) {
  native_t *n = projectile_of(r, e); qa_q3_base_actor_trace_result t = server_trace(r, trace); run_op(r, e, n, OP_IMPACT, &t);
}
void qa_q3_missile_runtime_run(RT *r, qa_q3_game_entity *e) { step(r, e, projectile_of(r, e)); }
bool qa_q3_missile_runtime_is_proximity_trigger(RT *r, qa_q3_game_entity *e) {
  owned(r, e); return e->touch.fn == r->proximity_touch.fn && e->touch.ctx == r->proximity_touch.ctx;
}
void qa_q3_missile_runtime_hook_free(RT *r, qa_q3_game_entity *e) { owned(r, e); release_projectile(r, projectile_of(r, e)); }
void qa_q3_missile_runtime_hook_think(RT *r, qa_q3_game_entity *e) {
  native_t *n = projectile_of(r, e); qa_q3_game_client *client = owner_client(r, n);
  if (!client) { release_projectile(r, n); return; }
  if (n->attached_player) {
    qa_q3_game_entity *target = native_of(r, n->attachment);
    if (!target) { release_projectile(r, n); return; }
    qa_vec3 v = center(target);
    qa_vec3 toward = qa_q3_base_entity_shared_current_origin(&e->r);
    qa_q3_set_origin(e, qa_q3_snap_vector_towards(v, toward));
  }
  client->ps.grapple_point = qa_q3_base_entity_shared_current_origin(&e->r);
}
static void proximity_explode(RT *r, qa_q3_game_entity *mine) {
  native_t *n = projectile_of(r, mine); bool has = n->has_trigger; qa_actor_id trigger = n->trigger;
  qa_q3_missile_runtime_explode(r, mine);
  if (has) { qa_q3_game_entity *e = native_of(r, trigger); if (e) qa_q3_entity_pool_free(pool_of(r), e); }
}
static void proximity_die(RT *r, qa_q3_game_entity *mine) {
  qa_q3_game_combat_context *c = combat_of(r);
  mine->think = qa_q3_callback_family_resolve(&c->entities->callbacks.think, IMPL("proximityDie.think")); qa_q3_game_entity_set_nextthink(mine, qa_int32(c->time + 1));
}
static void proximity_trigger(RT *r, qa_q3_game_entity *trigger, qa_q3_game_entity *other) {
  if (!other->client) return;
  qa_q3_game_entity *mine = parent_of(trigger); qa_q3_game_combat_context *c = combat_of(r);
  if (qa_length3(qa_sub3(trigger->s.pos.base, other->s.pos.base)) > mine->splash_radius) return;
  if (c->game_type >= GT_TEAM && mine->s.generic1 == other->client->sess.session_team) return;
  if (!qa_q3_combat_can_damage(c, qa_q3_participant_entity(other), trigger->s.pos.base)) return;
  mine->s.loop_sound = 0; qa_q3_entity_pool_add_event(c->entities, mine, QA_Q3_EV_PROXIMITY_MINE_TRIGGER, 0);
  qa_q3_game_entity_set_nextthink(mine, qa_int32(c->time + 500)); qa_q3_entity_pool_free(c->entities, trigger);
}
static void proximity_activate(RT *r, qa_q3_game_entity *mine) {
  const qa_q3_missionpack_missile_services *m = missionpack(r); qa_q3_game_combat_context *c = combat_of(r); qa_q3_entity_pool *pool = c->entities;
  mine->think = qa_q3_callback_family_resolve(&pool->callbacks.think, IMPL("proximityDie.think"));
  qa_q3_game_entity_set_nextthink(mine, qa_int32(c->time + m->prox_mine_timeout(m->ctx)));
  qa_q3_game_entity_set_takedamage(mine, true); qa_q3_game_entity_set_health(mine, 1); mine->die = qa_q3_callback_family_resolve(&pool->callbacks.die, IMPL("specialImpact.die"));
  mine->s.loop_sound = m->sound_index(m->ctx, "sound/weapons/proxmine/wstbtick.wav");
  qa_q3_game_entity *trigger = qa_q3_entity_pool_spawn(pool); double radius = (float)mine->splash_radius;
  qa_q3_game_entity_set_classname(trigger, "proxmine_trigger");
  qa_q3_base_entity_shared_set_mins(&trigger->r, qa_math_vec3(-radius, -radius, -radius)); qa_q3_base_entity_shared_set_maxs(&trigger->r, qa_math_vec3(radius, radius, radius));
  qa_q3_set_origin(trigger, mine->s.pos.base); trigger->parent = mine; trigger->r.contents = 0x40000000;
  trigger->touch = qa_q3_callback_family_resolve(&pool->callbacks.touch, IMPL("proximityActivate.touch"));
  link_entity(r, trigger); mine->activator = trigger;
  native_t *n = projectile_of(r, mine); n->has_trigger = true; n->trigger = qa_q3_game_entity_actor(trigger).id;
}
static void proximity_explode_on_player(RT *r, qa_q3_game_entity *mine) {
  native_t *n = projectile_of(r, mine);
  if (!n->attached_player) qa_range_error("Attached proximity mine requires its player lifetime");
  qa_q3_game_entity *player = native_of(r, n->attachment);
  if (!player) { release_projectile(r, n); return; }
  qa_q3_game_client *client = client_of(player); qa_q3_game_combat_context *c = combat_of(r);
  client->ps.e_flags = qa_js_to_int32(client->ps.e_flags) & ~EF_TICKING;
  if (client->invulnerability_time > c->time) {
    qa_q3_damage_participant owner = c->participant(c->services_ctx, n->p.owner);
    qa_vec3 zero = {0, 0, 0}, point = mine->s.origin;
    qa_q3_combat_damage_from(c, qa_q3_participant_entity(player), &owner, &owner, &zero, &point, 1000, QA_Q3_DAMAGE_NO_KNOCKBACK, MOD_JUICED, &n->p.actor.id);
    if (native_of(r, n->attachment) != player) return;
    client->invulnerability_time = 0; (void)qa_q3_entity_pool_temp_entity(c->entities, qa_q3_base_player_state_record_origin(&client->ps), QA_Q3_EV_JUICED);
  } else {
    qa_q3_set_origin(mine, player->s.pos.base); mine->r.sv_flags = qa_js_to_int32(mine->r.sv_flags) & ~SVF_NOCLIENT;
    mine->splash_method_of_death = MOD_PROXIMITY_MINE; qa_q3_missile_runtime_explode(r, mine);
  }
}
static void proximity_player(RT *r, qa_q3_game_entity *mine, qa_q3_game_entity *player) {
  if (qa_js_to_int32(mine->s.e_flags) & EF_NODRAW) return;
  qa_q3_game_combat_context *c = combat_of(r); qa_q3_entity_pool *pool = c->entities;
  qa_q3_entity_pool_add_event(pool, mine, QA_Q3_EV_PROXIMITY_MINE_STICK, 0);
  if (qa_js_to_int32(player->s.e_flags) & EF_TICKING) {
    if (!player->activator) qa_range_error("Ticking player requires its proximity mine");
    player->activator->splash_damage = qa_int32(player->activator->splash_damage + mine->splash_damage);
    player->activator->splash_radius = (float)(player->activator->splash_radius * (float)1.5);
    mine->think = qa_q3_callback_family_resolve(&pool->callbacks.think, IMPL("proximityPlayer.think")); qa_q3_game_entity_set_nextthink(mine, c->time); return;
  }
  qa_q3_game_client *client = client_of(player); client->ps.e_flags = qa_js_to_int32(client->ps.e_flags) | EF_TICKING; player->activator = mine;
  mine->s.e_flags = qa_js_to_int32(mine->s.e_flags) | EF_NODRAW; mine->r.sv_flags = qa_js_to_int32(mine->r.sv_flags) | SVF_NOCLIENT;
  mine->s.pos.type = QA_Q3_BASE_TR_LINEAR; mine->s.pos.delta = qa_math_vec3(0, 0, 0);
  native_t *n = projectile_of(r, mine); n->attached_player = true; n->attachment = qa_q3_game_entity_actor(player).id;
  mine->think = qa_q3_callback_family_resolve(&pool->callbacks.think, IMPL("proximityExplodeOnPlayer"));
  qa_q3_game_entity_set_nextthink(mine, qa_int32(c->time + (client->invulnerability_time > c->time ? 2000 : 10000)));
}
static void apply_behavior_launch(RT *r, qa_q3_game_entity *bolt, qa_actor_id owner) {
  qa_owned_actor actor = qa_q3_game_entity_actor(bolt); double time = combat_of(r)->time;
  qa_body_state_result body = qa_shared_body_table_read(r->host.bodies, actor.id);
  if (!body.has_value) qa_range_error("Launched Q3 missile lost its body");
  if (!r->host.weapon_behavior) return;
  const qa_weapon_behavior_projectile_port *port = r->host.weapon_behavior;
  qa_q3_projectile_behavior_result behavior = qa_q3_projectile_behavior(bolt->s.weapon);
  qa_weapon_behavior_launch input; memset(&input, 0, sizeof input);
  input.projectile = actor; input.shooter = owner; input.weapon = behavior.weapon; input.role = behavior.role; input.time_seconds = time / 1000;
  input.body = body.value; input.body.velocity = bolt->s.pos.delta;
  qa_weapon_trajectory_update_result update = port->launch(port->ctx, input);
  if (update.has_value) {
    body.value.origin = update.value.origin; body.value.velocity = update.value.velocity; body.value.angles = update.value.angles;
    qa_shared_body_table_write(r->host.bodies, actor, body.value);
    bolt->s.pos.base = update.value.origin; bolt->s.pos.delta = update.value.velocity; bolt->s.pos.time = time;
    qa_q3_base_entity_shared_set_current_origin(&bolt->r, update.value.origin);
  }
}
static qa_q3_game_entity *launch(RT *r, qa_q3_game_entity *self, qa_vec3 start, qa_vec3 direction, double weapon, const char *classname, double speed,
    double duration, bool gravity, double direct, double splash, double radius, double method, double splash_method) {
  owned(r, self);
  qa_q3_game_combat_context *c = combat_of(r); qa_q3_entity_pool *pool = c->entities; qa_actor_id owner = qa_q3_game_entity_actor(self).id;
  double time = c->time; qa_q3_game_entity *bolt = qa_q3_entity_pool_spawn(pool);
  qa_q3_launch_result launched = qa_q3_launch_projectile(start, direction, speed, gravity, duration, time);
  qa_q3_game_entity_set_classname(bolt, classname); qa_q3_game_entity_set_nextthink(bolt, launched.expires);
  bolt->think = qa_q3_callback_family_resolve(&pool->callbacks.think, IMPL("launch.think"));
  bolt->s.e_type = ET_MISSILE; bolt->r.sv_flags = SVF_USE_CURRENT_ORIGIN; bolt->s.weapon = weapon;
  bolt->r.owner_num = self->s.number; bolt->parent = weapon == QA_Q3_WP_GRAPPLING_HOOK || weapon == QA_Q3_WP_PROX_LAUNCHER ? self : NULL;
  bolt->damage = direct; bolt->splash_damage = splash; bolt->splash_radius = radius;
  bolt->method_of_death = method; bolt->splash_method_of_death = splash_method; bolt->clipmask = MASK_SHOT; bolt->target_ent = NULL;
  bolt->s.pos = launched.trajectory;
  /* Foreign-family presentation orients models from apos; mirror velocity into apos for non-tumbling projectiles. */
  qa_vec3 delta = launched.trajectory.delta; bool spin_free = weapon != QA_Q3_WP_GRENADE_LAUNCHER && weapon != QA_Q3_WP_PROX_LAUNCHER && (delta.x != 0 || delta.y != 0 || delta.z != 0);
  if (spin_free) { bolt->s.apos.type = QA_Q3_BASE_TR_STATIONARY; bolt->s.apos.base = qa_vector_to_angles(delta); bolt->s.apos.delta = qa_math_vec3(0, 0, 0); bolt->s.apos.time = time; }
  qa_owned_actor actor = qa_q3_game_entity_actor(bolt);
  qa_q3_base_entity_shared_set_current_origin(&bolt->r, qa_math_vec3(start.x, start.y, start.z));
  set_projectile(r, bind_projectile(bolt, owner, &owner, NULL, NULL));
  if (spin_free) {
    qa_body_state_result body = qa_shared_body_table_read(r->host.bodies, actor.id);
    if (body.has_value) { body.value.angles = qa_vector_to_angles(delta); qa_shared_body_table_write(r->host.bodies, actor, body.value); }
  }
  if (weapon != QA_Q3_WP_NAILGUN) apply_behavior_launch(r, bolt, owner);
  return bolt;
}
static qa_q3_game_entity *fire_spec(RT *r, qa_q3_game_entity *self, qa_vec3 start, qa_q3_missile_direction *d, double weapon, const char *classname) {
  qa_q3_missile_params spec = qa_q3_get_missile_parameters(weapon);
  return launch(r, self, start, normalize_direction(d), weapon, classname, spec.speed, spec.duration, spec.gravity, spec.direct, spec.splash, spec.radius, spec.method, spec.splash_method);
}
qa_q3_game_entity *qa_q3_missile_runtime_fire_plasma(RT *r, qa_q3_game_entity *self, qa_vec3 start, qa_q3_missile_direction *d) { return fire_spec(r, self, start, d, QA_Q3_WP_PLASMAGUN, "plasma"); }
qa_q3_game_entity *qa_q3_missile_runtime_fire_grenade(RT *r, qa_q3_game_entity *self, qa_vec3 start, qa_q3_missile_direction *d) {
  qa_q3_game_entity *bolt = fire_spec(r, self, start, d, QA_Q3_WP_GRENADE_LAUNCHER, "grenade"); bolt->s.e_flags = EF_BOUNCE_HALF; return bolt;
}
qa_q3_game_entity *qa_q3_missile_runtime_fire_rocket(RT *r, qa_q3_game_entity *self, qa_vec3 start, qa_q3_missile_direction *d) { return fire_spec(r, self, start, d, QA_Q3_WP_ROCKET_LAUNCHER, "rocket"); }
qa_q3_game_entity *qa_q3_missile_runtime_fire_bfg(RT *r, qa_q3_game_entity *self, qa_vec3 start, qa_q3_missile_direction *d) { return fire_spec(r, self, start, d, QA_Q3_WP_BFG, "bfg"); }
qa_q3_game_entity *qa_q3_missile_runtime_fire_grapple(RT *r, qa_q3_game_entity *self, qa_vec3 start, qa_q3_missile_direction *d) {
  qa_q3_game_combat_context *c = combat_of(r);
  qa_q3_game_entity *bolt = launch(r, self, start, normalize_direction(d), QA_Q3_WP_GRAPPLING_HOOK, "hook", QA_Q3_GRAPPLE_SPEED, QA_Q3_GRAPPLE_LIFETIME, false, 0, 0, 0,
      c->product == QA_Q3_BASE_PRODUCT_BASEQ3 ? 23 : 28, 0);
  bolt->think = qa_q3_callback_family_resolve(&c->entities->callbacks.think, IMPL("fireGrapple.think")); bolt->s.other_entity_num = self->s.number; client_of(self)->hook = bolt; return bolt;
}
qa_q3_game_entity *qa_q3_missile_runtime_fire_prox(RT *r, qa_q3_game_entity *self, qa_vec3 start, qa_q3_missile_direction *d) {
  (void)missionpack(r);
  qa_q3_game_entity *bolt = launch(r, self, start, normalize_direction(d), QA_Q3_WP_PROX_LAUNCHER, "prox mine", 700, 3000, true, 0, 100, 150, MOD_PROXIMITY_MINE, MOD_PROXIMITY_MINE);
  bolt->s.generic1 = client_of(self)->sess.session_team; return bolt;
}
qa_q3_game_entity *qa_q3_missile_runtime_fire_nail(RT *r, qa_q3_game_entity *self, qa_vec3 start, qa_vec3 forward, qa_vec3 right, qa_vec3 up) {
  qa_q3_game_random_pick random = missionpack(r)->random;
  qa_q3_game_entity *bolt = launch(r, self, start, qa_math_vec3(0, 0, 0), QA_Q3_WP_NAILGUN, "nail", 0, 10000, false, 20, 0, 0, 23, 0);
  qa_vec3 velocity = qa_q3_nail_velocity(start, forward, right, up, random);
  bolt->s.pos.time = combat_of(r)->time; bolt->s.pos.delta = qa_q3_snap_vector(velocity);
  apply_behavior_launch(r, bolt, qa_q3_game_entity_actor(self).id);
  return bolt;
}
/* Save callbacks. */
static qa_q3_entity_callback mkcb(const void *fn, void *ctx) { qa_q3_entity_callback cb; memcpy(&cb.fn, fn, sizeof cb.fn); cb.ctx = ctx; return cb; }
static void cb_activate(void *ctx, qa_q3_game_entity *self) { proximity_activate(ctx, self); }
static void cb_die(void *ctx, qa_q3_game_entity *self, qa_q3_damage_inflictor inflictor, qa_q3_damage_participant attacker, double damage, double mod) {
  (void)inflictor; (void)attacker; (void)damage; (void)mod; proximity_die(ctx, self);
}
static void cb_hook_think(void *ctx, qa_q3_game_entity *self) { qa_q3_missile_runtime_hook_think(ctx, self); }
static void cb_die_think(void *ctx, qa_q3_game_entity *self) { proximity_explode(ctx, self); }
static void cb_touch(void *ctx, qa_q3_game_entity *self, qa_q3_damage_participant other, qa_touch_contact contact) {
  (void)contact; if (other.entity) proximity_trigger(ctx, self, other.entity);
}
static void cb_free_think(void *ctx, qa_q3_game_entity *self) { RT *r = ctx; qa_q3_entity_pool_free(pool_of(r), self); }
static void cb_explode_on_player(void *ctx, qa_q3_game_entity *self) { proximity_explode_on_player(ctx, self); }
static void cb_launch_think(void *ctx, qa_q3_game_entity *self) { qa_q3_missile_runtime_explode(ctx, self); }
static void cb_grapple_think(void *ctx, qa_q3_game_entity *self) { qa_q3_missile_runtime_hook_free(ctx, self); }
void qa_q3_missile_bind_save_callbacks(RT *r) {
  qa_q3_callback_catalog *cat = &pool_of(r)->callbacks;
  qa_q3_entity_think_fn activate = cb_activate, hook = cb_hook_think, die_think = cb_die_think, free_think = cb_free_think, on_player = cb_explode_on_player, launch_think = cb_launch_think, grapple = cb_grapple_think;
  qa_q3_entity_die_fn die = cb_die; qa_q3_entity_touch_fn touch = cb_touch;
  qa_q3_callback_family_intern(&cat->think, IMPL("specialImpact.think"), mkcb(&activate, r));
  qa_q3_callback_family_intern(&cat->die, IMPL("specialImpact.die"), mkcb(&die, r));
  qa_q3_callback_family_intern(&cat->think, IMPL("hookThink"), mkcb(&hook, r));
  qa_q3_callback_family_intern(&cat->think, IMPL("proximityDie.think"), mkcb(&die_think, r));
  r->proximity_touch = qa_q3_callback_family_intern(&cat->touch, IMPL("proximityActivate.touch"), mkcb(&touch, r));
  qa_q3_callback_family_intern(&cat->think, IMPL("proximityPlayer.think"), mkcb(&free_think, r));
  qa_q3_callback_family_intern(&cat->think, IMPL("proximityExplodeOnPlayer"), mkcb(&on_player, r));
  qa_q3_callback_family_intern(&cat->think, IMPL("launch.think"), mkcb(&launch_think, r));
  qa_q3_callback_family_intern(&cat->think, IMPL("fireGrapple.think"), mkcb(&grapple, r));
}
void qa_q3_missile_runtime_bind(RT *r) {
  if (r->bound) return;
  qa_q3_game_combat_context *c = combat_of(r);
  if (c->entities->options.product != c->product) qa_range_error("Missile product does not match its entity pool");
  if (!c->authority || !c->participant || !c->is_player) qa_range_error("Missile runtime requires bound combat services");
  r->release_subscription = qa_session_actor_registry_on_release(r->host.actors, released, r);
  qa_q3_missile_bind_save_callbacks(r); r->bound = true;
}
void qa_q3_missile_runtime_init(RT *r, qa_q3_missile_host host) {
  memset(r, 0, sizeof *r); r->host = host;
  if (host.combat) qa_q3_missile_runtime_bind(r);
}
void qa_q3_missile_runtime_close(RT *r) {
  if (r->bound) { qa_session_actor_registry_off_release(r->host.actors, r->release_subscription); r->bound = false; }
  for (size_t i = 0; i < r->count; i++) retire(r, r->projectiles[i]);
  r->count = 0; flush_retired(r);
  if (r->depth == 0) { free(r->projectiles); free(r->retired); r->projectiles = NULL; r->retired = NULL; r->capacity = r->retired_capacity = 0; }
}
/* Save state. */
static bool saved_actor(qa_save_value *object, const char *key, const qa_actor_id *actor) {
  if (!actor) return qa_save_value_set(object, key, qa_save_value_null());
  qa_saved_actor_id id = qa_saved_actor_id_of(*actor); qa_save_value *v = qa_save_value_object();
  return v && qa_save_value_set(v, "slot", qa_save_value_number(id.slot)) && qa_save_value_set(v, "generation", qa_save_value_number(id.generation)) && qa_save_value_set(object, key, v);
}
qa_save_value *qa_q3_missile_runtime_capture_save_state(RT *r) {
  qa_save_value *out = qa_save_value_array(); if (!out) return NULL;
  for (size_t i = 0; i < r->count; i++) {
    native_t *n = r->projectiles[i]; qa_save_value *e = qa_save_value_object(), *att = qa_save_value_object();
    if (!e || !att) { qa_save_value_free(out); return NULL; }
    bool ok = qa_save_value_set(e, "entity", qa_save_value_number(n->entity->slot)) && saved_actor(e, "actor", &n->p.actor.id) && saved_actor(e, "owner", &n->p.owner)
        && saved_actor(e, "pass", n->p.has_pass ? &n->p.pass : NULL) && saved_actor(e, "trigger", n->has_trigger ? &n->trigger : NULL)
        && qa_save_value_set(att, "kind", qa_save_value_string(n->attached_player ? "player" : "none"));
    if (ok && n->attached_player) ok = saved_actor(att, "actor", &n->attachment);
    ok = ok && qa_save_value_set(e, "attachment", att) && qa_save_value_push(out, e);
    if (!ok) { qa_save_value_free(out); return NULL; }
  }
  return out;
}
static qa_actor_id resolve_field(const qa_save_reader *entry, const char *name, qa_q3_missile_resolve_actor_fn resolve, void *ctx) {
  qa_save_reader f; qa_save_reader_field(entry, name, &f); qa_actor_id a = resolve(ctx, qa_read_saved_actor(&f)); qa_save_reader_destroy(&f); return a;
}
static bool resolve_nullable(const qa_save_reader *entry, const char *name, qa_q3_missile_resolve_actor_fn resolve, void *ctx, qa_actor_id *out) {
  qa_save_reader f; qa_save_reader_field(entry, name, &f); bool has = !qa_save_reader_is_null(&f);
  if (has) *out = resolve(ctx, qa_read_saved_actor(&f));
  qa_save_reader_destroy(&f); return has;
}
void qa_q3_missile_runtime_restore_save_state(RT *r, const qa_save_value *value, qa_q3_missile_resolve_actor_fn resolve, void *ctx) {
  qa_save_reader reader; qa_save_reader_init(&reader, value, "q3.missiles");
  size_t count = qa_save_reader_list_count(&reader);
  native_t **entries = calloc(count ? count : 1, sizeof *entries); if (!entries) qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry, f; qa_save_reader_item(&reader, i, &entry);
    qa_save_reader_field(&entry, "entity", &f); qa_q3_game_entity *entity = qa_q3_read_module_entity(&f, pool_of(r)); qa_save_reader_destroy(&f);
    qa_actor_id actor = resolve_field(&entry, "actor", resolve, ctx);
    if (!qa_same_actor(qa_q3_game_entity_actor(entity).id, actor)) qa_save_reader_fail(&entry, "projectile actor differs from its source record");
    qa_actor_id owner = resolve_field(&entry, "owner", resolve, ctx), pass, trigger, attachment;
    bool has_pass = resolve_nullable(&entry, "pass", resolve, ctx, &pass), has_trigger = resolve_nullable(&entry, "trigger", resolve, ctx, &trigger);
    static const char *const kinds[] = {"none", "player"};
    qa_save_reader att; qa_save_reader_field(&entry, "attachment", &att);
    qa_save_reader_field(&att, "kind", &f); bool player = strcmp(qa_save_reader_choice_string(&f, kinds, 2), "player") == 0; qa_save_reader_destroy(&f);
    if (player) attachment = resolve_field(&att, "actor", resolve, ctx);
    qa_save_reader_destroy(&att);
    entries[i] = bind_projectile(entity, owner, has_pass ? &pass : NULL, player ? &attachment : NULL, has_trigger ? &trigger : NULL);
    qa_save_reader_destroy(&entry);
  }
  for (size_t i = 0; i < count; i++) for (size_t j = 0; j < i; j++) if (entries[i]->entity == entries[j]->entity) qa_save_reader_fail(&reader, "duplicate native projectile");
  for (size_t i = 0; i < r->count; i++) retire(r, r->projectiles[i]);
  r->count = 0; flush_retired(r);
  for (size_t i = 0; i < count; i++) set_projectile(r, entries[i]);
  free(entries); qa_save_reader_destroy(&reader);
}
