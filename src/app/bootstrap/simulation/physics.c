/* Port of src/app/bootstrap/simulation/physics.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/physics.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/new_toss.h"
#include "qa/app/bootstrap/simulation/q2_rerelease_slide.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/swept_body.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/shared.h"
#include "qa/world/actors/triggers.h"
#include "qa/foundation/js.h"
#define MAX_ENTRIES 1024
#define MAX_PUSHED 1024
typedef struct solid_entry { qa_owned_actor actor; qa_app_sim_shared_solid solid; qa_actor_id owner_storage; bool owner_is_storage; } solid_entry;
typedef struct motion_entry { qa_owned_actor actor; qa_q2_motion motion; } motion_entry;
typedef struct flags_entry { qa_owned_actor actor; qa_app_sim_shared_physics_flags flags; qa_actor_id enemy_storage; bool enemy_is_storage; } flags_entry;
typedef struct collision_entry { qa_owned_actor actor; qa_actor_collision collision; qa_actor_id owner_storage; bool owner_is_storage; } collision_entry;
typedef struct source_entry { bool used; int handle; qa_owned_actor actor; qa_app_sim_source_body_physics source; } source_entry;
typedef struct pushed_entry { qa_owned_actor actor; qa_vec3 origin, angles; double delta_yaw; } pushed_entry;
struct qa_app_sim_shared_physics {
  qa_app_sim_shared_physics_options options;
  qa_numeric_operations numeric;
  qa_q2_rerelease_movement_context rerelease;
  qa_shared_body_table *bodies;
  qa_app_sim_q2_rerelease_fly_move *fly_move;
  solid_entry solids[MAX_ENTRIES]; size_t solid_count;
  motion_entry motions[MAX_ENTRIES]; size_t motion_count;
  flags_entry flags[MAX_ENTRIES]; size_t flag_count;
  collision_entry collisions[MAX_ENTRIES]; size_t collision_count;
  source_entry sources[MAX_ENTRIES]; int next_handle;
  double world_gravity;
  qa_app_sim_physics_event events[8192]; size_t event_count;
  pushed_entry *push_transaction; size_t push_count; bool push_active;
  int64_t release_subscription;
  qa_actor_id link_owner_scratch, ground_scratch;
  qa_plane plane_scratch;
  qa_touch_surface surface_scratch;
  qa_touch_source_trace source_trace_scratch;
  qa_owned_actor *candidates; size_t candidate_count, candidate_capacity;
  qa_actor_id *candidate_ids; size_t candidate_id_capacity;
};
static const qa_vec3 ZERO3 = {0, 0, 0};
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static bool same_owned(qa_owned_actor a, qa_owned_actor b) {
  return qa_same_actor(a.id, b.id) && a.token == b.token;
}
static qa_vec3 vec_of(qa_app_sim_shared_physics *p, double x, double y, double z) {
  qa_numeric_operations *n = &p->numeric;
  return (qa_vec3){n->store(n->ctx, x), n->store(n->ctx, y), n->store(n->ctx, z)};
}
static qa_vec3 vadd(qa_app_sim_shared_physics *p, qa_vec3 a, qa_vec3 b) {
  qa_numeric_operations *n = &p->numeric;
  return vec_of(p, n->add(n->ctx, a.x, b.x), n->add(n->ctx, a.y, b.y), n->add(n->ctx, a.z, b.z));
}
static qa_vec3 vsub(qa_app_sim_shared_physics *p, qa_vec3 a, qa_vec3 b) {
  qa_numeric_operations *n = &p->numeric;
  return vec_of(p, n->subtract(n->ctx, a.x, b.x), n->subtract(n->ctx, a.y, b.y), n->subtract(n->ctx, a.z, b.z));
}
static qa_vec3 vscale(qa_app_sim_shared_physics *p, qa_vec3 v, double value) {
  qa_numeric_operations *n = &p->numeric;
  return vec_of(p, n->multiply(n->ctx, v.x, value), n->multiply(n->ctx, v.y, value), n->multiply(n->ctx, v.z, value));
}
static double vdot(qa_app_sim_shared_physics *p, qa_vec3 a, qa_vec3 b) {
  qa_numeric_operations *n = &p->numeric;
  return n->add(n->ctx, n->add(n->ctx, n->multiply(n->ctx, a.x, b.x), n->multiply(n->ctx, a.y, b.y)),
      n->multiply(n->ctx, a.z, b.z));
}
static bool moving3(qa_vec3 v) { return v.x != 0 || v.y != 0 || v.z != 0; }
static bool live(qa_app_sim_shared_physics *p, qa_owned_actor actor) {
  return qa_session_actor_registry_is_live(p->options.actors, actor.id);
}
static source_entry *source_of_entry(qa_app_sim_shared_physics *p, qa_owned_actor actor) {
  for (int i = 0; i < MAX_ENTRIES; i++)
    if (p->sources[i].used && same_owned(p->sources[i].actor, actor)) return &p->sources[i];
  return NULL;
}
static solid_entry *solid_entry_of(qa_app_sim_shared_physics *p, qa_owned_actor actor) {
  for (size_t i = 0; i < p->solid_count; i++) if (same_owned(p->solids[i].actor, actor)) return &p->solids[i];
  return NULL;
}
static motion_entry *motion_entry_of(qa_app_sim_shared_physics *p, qa_owned_actor actor) {
  for (size_t i = 0; i < p->motion_count; i++) if (same_owned(p->motions[i].actor, actor)) return &p->motions[i];
  return NULL;
}
static flags_entry *flags_entry_of(qa_app_sim_shared_physics *p, qa_owned_actor actor) {
  for (size_t i = 0; i < p->flag_count; i++) if (same_owned(p->flags[i].actor, actor)) return &p->flags[i];
  return NULL;
}
static collision_entry *collision_entry_of(qa_app_sim_shared_physics *p, qa_owned_actor actor) {
  for (size_t i = 0; i < p->collision_count; i++) if (same_owned(p->collisions[i].actor, actor)) return &p->collisions[i];
  return NULL;
}
static bool solid_of(qa_app_sim_shared_physics *p, qa_owned_actor actor, qa_app_sim_shared_solid *out) {
  source_entry *s = source_of_entry(p, actor);
  if (s && s->source.collision && s->source.collision(s->source.ctx, out)) return true;
  if (p->options.get_collision && p->options.get_collision(p->options.ctx, actor, out)) return true;
  solid_entry *e = solid_entry_of(p, actor);
  if (e) { *out = e->solid; return true; }
  return false;
}
static bool motion_of(qa_app_sim_shared_physics *p, qa_owned_actor actor, qa_q2_motion *out) {
  source_entry *s = source_of_entry(p, actor);
  if (s && s->source.motion && s->source.motion(s->source.ctx, out)) return true;
  if (p->options.get_motion && p->options.get_motion(p->options.ctx, actor, out)) return true;
  motion_entry *e = motion_entry_of(p, actor);
  if (e) { *out = e->motion; return true; }
  return false;
}
static const char *family_of(qa_app_sim_shared_physics *p, qa_owned_actor actor) {
  qa_app_sim_shared_solid solid;
  return solid_of(p, actor, &solid) && solid.family != NULL ? solid.family : "q2";
}
bool qa_app_sim_physics_flags_has(const qa_app_sim_shared_physics_flags *f, qa_app_sim_physics_flag_key key) {
  switch (key) {
  case QA_APP_SIM_FLAG_TEAM_SLAVE: return f->has_team_slave;
  case QA_APP_SIM_FLAG_ALWAYS_TOUCH: return f->has_always_touch;
  case QA_APP_SIM_FLAG_FLY: return f->has_fly;
  case QA_APP_SIM_FLAG_SWIM: return f->has_swim;
  case QA_APP_SIM_FLAG_PARTIAL_GROUND: return f->has_partial_ground;
  case QA_APP_SIM_FLAG_DEAD: return f->has_dead;
  case QA_APP_SIM_FLAG_PLAYER: return f->has_player;
  case QA_APP_SIM_FLAG_WATER_LEVEL: return f->has_water_level;
  case QA_APP_SIM_FLAG_WATER_TYPE: return f->has_water_type;
  case QA_APP_SIM_FLAG_DELTA_YAW: return f->has_delta_yaw;
  case QA_APP_SIM_FLAG_ENEMY: return f->has_enemy;
  case QA_APP_SIM_FLAG_COUNT: break;
  }
  return false;
}
static bool flag_key_listed(const unsigned char *keys, size_t count, unsigned char key) {
  for (size_t i = 0; i < count; i++) if (keys[i] == key) return true;
  return false;
}
size_t qa_app_sim_physics_flags_keys(const qa_app_sim_shared_physics_flags *f, unsigned char out[QA_APP_SIM_FLAG_COUNT]) {
  size_t count = 0;
  for (size_t i = 0; i < f->key_count && i < QA_APP_SIM_FLAG_COUNT; i++)
    if (qa_app_sim_physics_flags_has(f, (qa_app_sim_physics_flag_key)f->key_order[i]) && !flag_key_listed(out, count, f->key_order[i]))
      out[count++] = f->key_order[i];
  for (unsigned char k = 0; k < QA_APP_SIM_FLAG_COUNT; k++)
    if (qa_app_sim_physics_flags_has(f, (qa_app_sim_physics_flag_key)k) && !flag_key_listed(out, count, k)) out[count++] = k;
  return count;
}
void qa_app_sim_physics_flags_order(qa_app_sim_shared_physics_flags *f, const unsigned char *keys, size_t count) {
  unsigned char order[QA_APP_SIM_FLAG_COUNT]; size_t n = 0;
  for (size_t i = 0; i < count; i++)
    if (keys[i] < QA_APP_SIM_FLAG_COUNT && qa_app_sim_physics_flags_has(f, (qa_app_sim_physics_flag_key)keys[i]) && !flag_key_listed(order, n, keys[i]))
      order[n++] = keys[i];
  for (unsigned char k = 0; k < QA_APP_SIM_FLAG_COUNT; k++)
    if (qa_app_sim_physics_flags_has(f, (qa_app_sim_physics_flag_key)k) && !flag_key_listed(order, n, k)) order[n++] = k;
  memcpy(f->key_order, order, n); f->key_count = (unsigned char)n;
}
void qa_app_sim_physics_flags_merge(qa_app_sim_shared_physics_flags *dst, const qa_app_sim_shared_physics_flags *src) {
  unsigned char order[QA_APP_SIM_FLAG_COUNT], added[QA_APP_SIM_FLAG_COUNT];
  size_t n = qa_app_sim_physics_flags_keys(dst, order), m = qa_app_sim_physics_flags_keys(src, added);
  for (size_t i = 0; i < m; i++) if (!flag_key_listed(order, n, added[i])) order[n++] = added[i];
  if (src->has_always_touch) { dst->has_always_touch = true; dst->always_touch = src->always_touch; }
  if (src->has_fly) { dst->has_fly = true; dst->fly = src->fly; }
  if (src->has_swim) { dst->has_swim = true; dst->swim = src->swim; }
  if (src->has_partial_ground) { dst->has_partial_ground = true; dst->partial_ground = src->partial_ground; }
  if (src->has_dead) { dst->has_dead = true; dst->dead = src->dead; }
  if (src->has_player) { dst->has_player = true; dst->player = src->player; }
  if (src->has_water_level) { dst->has_water_level = true; dst->water_level = src->water_level; }
  if (src->has_water_type) { dst->has_water_type = true; dst->water_type = src->water_type; }
  if (src->has_enemy) { dst->has_enemy = true; dst->enemy = src->enemy; }
  if (src->has_delta_yaw) { dst->has_delta_yaw = true; dst->delta_yaw = src->delta_yaw; }
  if (src->has_team_slave) { dst->has_team_slave = true; dst->team_slave = src->team_slave; }
  memcpy(dst->key_order, order, n); dst->key_count = (unsigned char)n;
}
static void merge_flags(qa_app_sim_shared_physics_flags *dst, qa_app_sim_shared_physics_flags src) {
  qa_app_sim_physics_flags_merge(dst, &src);
}
static qa_app_sim_shared_physics_flags actor_flags(qa_app_sim_shared_physics *p, qa_owned_actor actor) {
  qa_app_sim_shared_physics_flags out; memset(&out, 0, sizeof out);
  flags_entry *e = flags_entry_of(p, actor);
  if (e) out = e->flags;
  source_entry *s = source_of_entry(p, actor);
  if (s && s->source.flags) merge_flags(&out, s->source.flags(s->source.ctx));
  else if (p->options.get_flags) {
    qa_app_sim_shared_physics_flags overlay; memset(&overlay, 0, sizeof overlay);
    if (p->options.get_flags(p->options.ctx, actor, &overlay)) merge_flags(&out, overlay);
  }
  return out;
}
static void store_flags(qa_app_sim_shared_physics *p, qa_owned_actor actor, qa_app_sim_shared_physics_flags flags) {
  flags_entry *e = flags_entry_of(p, actor);
  if (e) { e->flags = flags; e->enemy_is_storage = false; return; }
  if (p->flag_count >= MAX_ENTRIES) qa_range_error("Out of memory");
  p->flags[p->flag_count++] = (flags_entry){actor, flags, actor.id, false};
}
static void store_motion(qa_app_sim_shared_physics *p, qa_q2_motion motion) {
  motion_entry *e = motion_entry_of(p, motion.actor);
  if (e) { e->motion = motion; return; }
  if (p->motion_count >= MAX_ENTRIES) qa_range_error("Out of memory");
  p->motions[p->motion_count++] = (motion_entry){motion.actor, motion};
}
/* TS stores ActorId values: the owner is copied into the entry, never borrowed from the caller. */
static void store_solid(qa_app_sim_shared_physics *p, qa_owned_actor actor, qa_app_sim_shared_solid solid) {
  solid_entry *e = solid_entry_of(p, actor);
  if (!e) {
    if (p->solid_count >= MAX_ENTRIES) qa_range_error("Out of memory");
    e = &p->solids[p->solid_count++];
    e->actor = actor;
  }
  e->solid = solid;
  e->owner_is_storage = solid.owner != NULL;
  if (solid.owner != NULL) { e->owner_storage = *solid.owner; e->solid.owner = &e->owner_storage; }
}
static void emit_event(qa_app_sim_shared_physics *p, qa_app_sim_physics_event event) {
  if (p->options.event) { p->options.event(p->options.ctx, event); return; }
  if (p->event_count < 8192) p->events[p->event_count++] = event;
}
static void write_angular(qa_app_sim_shared_physics *p, qa_owned_actor actor, qa_vec3 value) {
  source_entry *s = source_of_entry(p, actor);
  if (s && s->source.write_angular_velocity) s->source.write_angular_velocity(s->source.ctx, value);
  else if (p->options.write_angular_velocity) p->options.write_angular_velocity(p->options.ctx, actor, value);
}
static bool read_body(qa_app_sim_shared_physics *p, qa_actor_id actor, qa_body_state *out) {
  qa_body_state_result r = qa_shared_body_table_read(p->bodies, actor);
  if (r.has_value) *out = r.value;
  return r.has_value;
}
typedef struct body_changes {
  bool origin, angles, velocity, bounds, ground;
  qa_vec3 origin_v, angles_v, velocity_v;
  qa_bounds bounds_v;
  bool has_ground; qa_actor_id ground_v;
} body_changes;
static bool write_live(qa_app_sim_shared_physics *p, qa_owned_actor actor, body_changes changes, bool link) {
  qa_body_state body;
  if (!read_body(p, actor.id, &body)) return false;
  if (changes.origin) body.origin = changes.origin_v;
  if (changes.angles) body.angles = changes.angles_v;
  if (changes.velocity) body.velocity = changes.velocity_v;
  if (changes.bounds) body.bounds = changes.bounds_v;
  if (changes.ground) {
    if (changes.has_ground) { p->ground_scratch = changes.ground_v; body.ground = &p->ground_scratch; }
    else body.ground = NULL;
  }
  qa_shared_body_table_write(p->bodies, actor, body);
  if (link) qa_shared_body_table_link(p->bodies, actor, NULL);
  return true;
}
static qa_bounds absolute_bounds_of(qa_app_sim_shared_physics *p, qa_owned_actor actor, qa_body_state state) {
  qa_app_sim_shared_solid solid; bool has_solid = solid_of(p, actor, &solid);
  qa_bounds bounds = {vadd(p, state.origin, state.bounds.min), vadd(p, state.origin, state.bounds.max)};
  if (has_solid && is(solid.solid, "brush") && moving3(state.angles)) {
    qa_vec3 extent = {qa_js_max(fabs(state.bounds.min.x), fabs(state.bounds.max.x)),
        qa_js_max(fabs(state.bounds.min.y), fabs(state.bounds.max.y)),
        qa_js_max(fabs(state.bounds.min.z), fabs(state.bounds.max.z))};
    double radius = p->numeric.square_root(p->numeric.ctx, vdot(p, extent, extent));
    qa_vec3 offset = {radius, radius, radius};
    bounds.min = vsub(p, state.origin, offset);
    bounds.max = vadd(p, state.origin, offset);
  }
  qa_vec3 pad = (has_solid && is(solid.family, "q1") && solid.has_item && solid.item)
      ? (qa_vec3){15, 15, 0} : (qa_vec3){1, 1, 1};
  return (qa_bounds){vsub(p, bounds.min, pad), vadd(p, bounds.max, pad)};
}
static qa_bounds absolute_body_bounds(void *ctx, qa_owned_actor actor, qa_body_state state) {
  return absolute_bounds_of(ctx, actor, state);
}
static void linked_body(qa_app_sim_shared_physics *p, qa_linked_body body) {
  qa_owned_actor actor;
  if (!qa_session_actor_registry_resolve_owned(p->options.actors, body.actor, &actor)) return;
  collision_entry *exact = collision_entry_of(p, actor);
  if (exact) { qa_shared_link(p->options.scene, body, exact->collision); return; }
  qa_app_sim_shared_solid solid;
  qa_actor_id world; bool has_world = p->options.world_actor && p->options.world_actor(p->options.ctx, &world);
  if (!solid_of(p, actor, &solid) || is(solid.solid, "none")
      || (has_world && qa_same_actor(world, actor.id))) {
    qa_shared_unlink(p->options.scene, actor.id); return;
  }
  qa_actor_collision collision; memset(&collision, 0, sizeof collision);
  collision.family = solid.family;
  collision.shape_kind = (is(solid.solid, "brush") && solid.has_model) ? "model" : "box";
  if (is(solid.solid, "brush") && solid.has_model) collision.shape_model = solid.model;
  collision.contents = is(solid.family, "q1") ? -2 : is(solid.solid, "brush") ? 1
      : (solid.has_dead_monster && solid.dead_monster) ? 0x4000000 : 0x2000000;
  qa_q2_motion motion;
  if (motion_of(p, actor, &motion)) {
    if (motion.has_owner) {
      motion_entry *entry = motion_entry_of(p, actor);
      if (entry && qa_same_actor(entry->motion.owner, motion.owner)) collision.owner = &entry->motion.owner;
      else { p->link_owner_scratch = motion.owner; collision.owner = &p->link_owner_scratch; }
    }
  } else collision.owner = solid.owner;
  collision.q1_corpse = solid.q1_corpse;
  collision.role = is(solid.solid, "trigger") ? "trigger" : "solid";
  collision.monster = solid.has_monster && solid.monster;
  collision.dead_monster = solid.has_dead_monster && solid.dead_monster;
  qa_shared_link(p->options.scene, body, collision);
}
static void on_body_link(void *ctx, qa_linked_body body) { linked_body(ctx, body); }
static void on_body_unlink(void *ctx, qa_actor_id actor) {
  qa_app_sim_shared_physics *p = ctx;
  if (p->options.scene) qa_shared_unlink(p->options.scene, actor);
}
static void erase_actor(qa_app_sim_shared_physics *p, qa_owned_actor actor) {
  for (int i = 0; i < MAX_ENTRIES; i++)
    if (p->sources[i].used && same_owned(p->sources[i].actor, actor)) p->sources[i].used = false;
  /* TS Map.delete keeps the remaining entries in insertion order (the checkpoint writes that order). */
  for (size_t i = 0; i < p->solid_count;) if (same_owned(p->solids[i].actor, actor)) {
    memmove(&p->solids[i], &p->solids[i + 1], (p->solid_count - i - 1) * sizeof *p->solids); p->solid_count--;
  } else i++;
  for (size_t i = 0; i < p->solid_count; i++) if (p->solids[i].owner_is_storage) p->solids[i].solid.owner = &p->solids[i].owner_storage;
  for (size_t i = 0; i < p->collision_count;) if (same_owned(p->collisions[i].actor, actor)) {
    memmove(&p->collisions[i], &p->collisions[i + 1], (p->collision_count - i - 1) * sizeof *p->collisions); p->collision_count--;
  } else i++;
  for (size_t i = 0; i < p->collision_count; i++)
    if (p->collisions[i].owner_is_storage) p->collisions[i].collision.owner = &p->collisions[i].owner_storage;
  for (size_t i = 0; i < p->motion_count;) if (same_owned(p->motions[i].actor, actor)) {
    memmove(&p->motions[i], &p->motions[i + 1], (p->motion_count - i - 1) * sizeof *p->motions); p->motion_count--;
  } else i++;
  for (size_t i = 0; i < p->flag_count;) if (same_owned(p->flags[i].actor, actor)) {
    memmove(&p->flags[i], &p->flags[i + 1], (p->flag_count - i - 1) * sizeof *p->flags); p->flag_count--;
  } else i++;
  for (size_t i = 0; i < p->flag_count; i++) if (p->flags[i].enemy_is_storage) p->flags[i].flags.enemy = &p->flags[i].enemy_storage;
}
static void on_actor_release(void *ctx, qa_owned_actor actor) { erase_actor(ctx, actor); }
qa_app_sim_shared_physics *qa_app_sim_shared_physics_create(qa_app_sim_shared_physics_options options) {
  qa_app_sim_shared_physics *p = calloc(1, sizeof *p);
  if (!p) qa_range_error("Out of memory");
  p->options = options;
  p->world_gravity = options.has_gravity ? options.gravity : 800;
  p->numeric = *qa_create_numeric_operations(&options.numeric);
  qa_q2_rerelease_movement_context_init(&p->rerelease);
  p->fly_move = qa_app_sim_create_q2_rerelease_fly_move(p->numeric, &p->rerelease);
  qa_body_link_hooks hooks; memset(&hooks, 0, sizeof hooks);
  hooks.ctx = p; hooks.absolute_bounds = absolute_body_bounds; hooks.on_link = on_body_link; hooks.on_unlink = on_body_unlink;
  p->bodies = qa_shared_body_table_create(options.actors, hooks);
  if (!p->bodies) qa_range_error("Out of memory");
  if (options.scene) qa_shared_bind_body_table(options.scene, p->bodies);
  p->release_subscription = qa_session_actor_registry_on_release(options.actors, on_actor_release, p);
  return p;
}
void qa_app_sim_shared_physics_destroy(qa_app_sim_shared_physics *physics) {
  if (!physics) return;
  qa_session_actor_registry_off_release(physics->options.actors, physics->release_subscription);
  qa_shared_body_table_destroy(physics->bodies);
  qa_app_sim_q2_rerelease_fly_move_destroy(physics->fly_move);
  free(physics->push_transaction);
  free(physics->candidates);
  free(physics->candidate_ids);
  free(physics);
}
qa_shared_body_table *qa_app_sim_shared_physics_bodies(qa_app_sim_shared_physics *physics) { return physics->bodies; }
qa_q2_rerelease_movement_context *qa_app_sim_shared_physics_rerelease_movement(qa_app_sim_shared_physics *physics) { return &physics->rerelease; }
int qa_app_sim_shared_physics_bind_source(qa_app_sim_shared_physics *physics, qa_owned_actor actor, qa_app_sim_source_body_physics source) {
  qa_session_actor_registry_assert_owned(physics->options.actors, actor);
  if (source_of_entry(physics, actor)) qa_range_error("Actor already has source physics");
  for (int i = 0; i < MAX_ENTRIES; i++) if (!physics->sources[i].used) {
    int handle = physics->next_handle++;
    physics->sources[i] = (source_entry){true, handle, actor, source};
    return handle;
  }
  qa_range_error("Out of memory");
}
void qa_app_sim_shared_physics_unbind_source(qa_app_sim_shared_physics *physics, int handle) {
  for (int i = 0; i < MAX_ENTRIES; i++)
    if (physics->sources[i].used && physics->sources[i].handle == handle) physics->sources[i].used = false;
}
double qa_app_sim_shared_physics_gravity(qa_app_sim_shared_physics *physics) { return physics->world_gravity; }
void qa_app_sim_shared_physics_set_world_gravity(qa_app_sim_shared_physics *physics, double value) {
  if (!isfinite(value)) qa_range_error("World gravity must be finite");
  physics->world_gravity = value;
}
static void relink_if_linked(qa_app_sim_shared_physics *p, qa_actor_id actor) {
  qa_linked_body_result linked = qa_shared_body_table_linked(p->bodies, actor);
  if (linked.has_value) linked_body(p, linked.value);
}
void qa_app_sim_shared_physics_set_solid(qa_app_sim_shared_physics *physics, qa_owned_actor actor, qa_app_sim_shared_solid solid) {
  qa_session_actor_registry_assert_owned(physics->options.actors, actor);
  if (is(solid.solid, "brush") && !solid.has_model) qa_range_error("Brush solidity requires an inline model");
  store_solid(physics, actor, solid);
  relink_if_linked(physics, actor.id);
}
void qa_app_sim_shared_physics_set_collision(qa_app_sim_shared_physics *physics, qa_owned_actor actor, qa_actor_collision collision) {
  qa_session_actor_registry_assert_owned(physics->options.actors, actor);
  collision_entry *e = collision_entry_of(physics, actor);
  if (!e) {
    if (physics->collision_count >= MAX_ENTRIES) qa_range_error("Out of memory");
    e = &physics->collisions[physics->collision_count++];
    e->actor = actor;
  }
  e->collision = collision;
  e->owner_is_storage = collision.owner != NULL;
  if (collision.owner != NULL) { e->owner_storage = *collision.owner; e->collision.owner = &e->owner_storage; }
  qa_app_sim_shared_solid solid; memset(&solid, 0, sizeof solid);
  solid.solid = is(collision.role, "trigger") ? "trigger" : is(collision.shape_kind, "model") ? "brush" : "box";
  solid.has_model = is(collision.shape_kind, "model");
  if (solid.has_model) solid.model = collision.shape_model;
  solid.family = collision.family;
  solid.owner = collision.owner;
  solid.has_monster = true; solid.monster = collision.monster;
  solid.has_dead_monster = true; solid.dead_monster = collision.dead_monster;
  solid.q1_corpse = collision.q1_corpse;
  store_solid(physics, actor, solid);
  relink_if_linked(physics, actor.id);
}
void qa_app_sim_shared_physics_set_motion(qa_app_sim_shared_physics *physics, qa_owned_actor actor, qa_q2_motion motion) {
  motion.actor = actor;
  qa_session_actor_registry_assert_owned(physics->options.actors, motion.actor);
  store_motion(physics, motion);
  qa_body_state body;
  if (read_body(physics, actor.id, &body))
    write_live(physics, actor, (body_changes){.velocity = true, .velocity_v = motion.velocity}, false);
  relink_if_linked(physics, actor.id);
}
void qa_app_sim_shared_physics_set_flags(qa_app_sim_shared_physics *physics, qa_owned_actor actor, qa_app_sim_shared_physics_flags changes) {
  if (!live(physics, actor)) return;
  qa_app_sim_shared_physics_flags merged = actor_flags(physics, actor);
  merge_flags(&merged, changes);
  store_flags(physics, actor, merged);
  source_entry *s = source_of_entry(physics, actor);
  if (s && s->source.write_flags) s->source.write_flags(s->source.ctx, changes);
  else if (physics->options.write_flags) physics->options.write_flags(physics->options.ctx, actor, changes);
}
bool qa_app_sim_shared_physics_collision_of(qa_app_sim_shared_physics *physics, qa_owned_actor actor, qa_actor_collision *out) {
  collision_entry *e = collision_entry_of(physics, actor);
  if (e) { *out = e->collision; return true; }
  return false;
}
bool qa_app_sim_shared_physics_solid_of_actor(qa_app_sim_shared_physics *physics, qa_actor_id actor, qa_app_sim_shared_solid *out) {
  qa_owned_actor owned;
  return qa_session_actor_registry_resolve_owned(physics->options.actors, actor, &owned) && solid_of(physics, owned, out);
}
bool qa_app_sim_shared_physics_motion_of_actor(qa_app_sim_shared_physics *physics, qa_actor_id actor, qa_q2_motion *out) {
  qa_owned_actor owned;
  return qa_session_actor_registry_resolve_owned(physics->options.actors, actor, &owned) && motion_of(physics, owned, out);
}
bool qa_app_sim_shared_physics_is_brush(qa_app_sim_shared_physics *physics, qa_actor_id actor) {
  qa_app_sim_shared_solid solid;
  return qa_app_sim_shared_physics_solid_of_actor(physics, actor, &solid) && is(solid.solid, "brush");
}
const qa_app_sim_physics_event *qa_app_sim_shared_physics_drain_events(qa_app_sim_shared_physics *physics, size_t *out_count) {
  *out_count = physics->event_count;
  physics->event_count = 0;
  return physics->events;
}
static qa_trace_policy policy_of(const qa_app_sim_shared_physics *physics, const char *family, double mask, const char *move) {
  qa_trace_policy policy; memset(&policy, 0, sizeof policy);
  if (is(family, "q1")) { policy.kind = "q1"; policy.move = move; policy.has_hull = false; }
  else if (is(family, "q2")) { policy.kind = "q2"; policy.contents_mask = mask; policy.leaf_contents = is(physics->options.q2_edition, "rerelease") ? "merged" : "stored"; }
  else { policy.kind = "q3"; policy.contents_mask = mask; policy.curves = true; policy.player_curve_clip = true; }
  return policy;
}
qa_trace_result qa_app_sim_shared_physics_trace(qa_app_sim_shared_physics *physics, qa_q2_trace_request request, qa_app_sim_physics_family family) {
  qa_trace_query query; memset(&query, 0, sizeof query);
  query.start = request.start; query.end = request.end;
  if (request.bounds == NULL) query.shape.kind = "point";
  else { query.shape.kind = "box"; query.shape.bounds = *request.bounds; }
  query.target.is_model = false;
  query.policy = policy_of(physics, family != NULL ? family : "q2", request.mask, "normal");
  query.numeric = physics->options.numeric;
  query.pass_actor = request.ignore;
  return (request.exclude == NULL || request.exclude_count == 0)
      ? qa_shared_trace(physics->options.scene, query)
      : qa_shared_trace_excluding(physics->options.scene, query, request.exclude, request.exclude_count);
}
static qa_trace_result body_trace(qa_app_sim_shared_physics *p, qa_owned_actor actor, qa_vec3 start, qa_vec3 end,
    const qa_actor_id *exclude, size_t exclude_count, bool exact_mask, bool has_bounds, qa_bounds bounds) {
  qa_body_state body;
  if (!read_body(p, actor.id, &body)) qa_range_error("Cannot trace an actor without a body");
  const char *family = family_of(p, actor);
  qa_q2_motion motion; bool has_motion = motion_of(p, actor, &motion);
  qa_app_sim_shared_solid solid; bool has_solid = solid_of(p, actor, &solid);
  const char *move = "normal";
  if (is(family, "q1") && has_motion && is(motion.kind, "fly-missile")) move = "missile";
  else if (is(family, "q1") && has_solid && (is(solid.solid, "none") || is(solid.solid, "trigger"))) move = "no-monsters";
  qa_trace_query query; memset(&query, 0, sizeof query);
  query.start = start; query.end = end;
  query.shape.kind = "box"; query.shape.bounds = has_bounds ? bounds : body.bounds;
  query.target.is_model = false;
  double mask = exact_mask ? (has_motion ? motion.clip_mask : 0) : ((has_motion && motion.clip_mask != 0) ? motion.clip_mask : 3);
  query.policy = policy_of(p, family, mask, move);
  query.numeric = p->options.numeric;
  query.pass_actor = &actor.id;
  return exclude_count == 0 ? qa_shared_trace(p->options.scene, query)
      : qa_shared_trace_excluding(p->options.scene, query, exclude, exclude_count);
}
static bool hit_actor(qa_app_sim_shared_physics *p, qa_trace_result trace, qa_actor_id *out) {
  if (trace.hit.kind == QA_TRACE_HIT_ACTOR) { *out = trace.hit.actor; return true; }
  if (trace.hit.kind == QA_TRACE_HIT_WORLD && p->options.world_actor) return p->options.world_actor(p->options.ctx, out);
  return false;
}
static qa_vec3 trace_source_normal(qa_trace_result trace) {
  return is(trace.kind, "q1") ? trace.source_plane_q1.normal : trace.source_plane.plane.normal;
}
static qa_vec3 trace_normal(qa_trace_result trace) {
  return trace.contact.has_plane ? trace.contact.plane.normal : trace_source_normal(trace);
}
static bool trigger_is(void *ctx, qa_owned_actor trigger) {
  qa_app_sim_shared_solid solid;
  return solid_of(ctx, trigger, &solid) && is(solid.solid, "trigger");
}
static void trigger_touch(void *ctx, qa_touch_contact contact) {
  qa_app_sim_shared_physics *p = ctx;
  (void)qa_actor_callback_table_touch(p->options.callbacks, contact);
}
void qa_app_sim_shared_physics_touch_triggers(qa_app_sim_shared_physics *physics, qa_owned_actor actor) {
  qa_linked_body_result linked = qa_shared_body_table_linked(physics->bodies, actor.id);
  if (!linked.has_value || !live(physics, actor)) return;
  qa_app_sim_shared_physics_flags flags = actor_flags(physics, actor);
  const char *family = family_of(physics, actor);
  if (is(family, "q1")) {
    qa_q1_trigger_services services; memset(&services, 0, sizeof services);
    services.actors = physics->options.actors;
    services.bodies = physics->bodies;
    services.spatial = physics->options.scene->spatial;
    services.ctx = physics;
    services.is_trigger = trigger_is;
    services.touch = trigger_touch;
    qa_touch_q1_triggers(services, actor);
    return;
  }
  qa_app_sim_shared_solid self_solid; bool has_self = solid_of(physics, actor, &self_solid);
  if (flags.has_dead && flags.dead && ((flags.has_player && flags.player) || (has_self && self_solid.has_monster && self_solid.monster))) return;
  size_t count = 0;
  const qa_spatial_actor *candidates = qa_shared_query_actors(physics->options.scene, linked.value.absolute_bounds, "trigger", &count);
  /* Copy ids: touch callbacks may relink and invalidate scene scratch. */
  qa_actor_id *ids = count ? malloc(count * sizeof *ids) : NULL;
  if (count && !ids) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) ids[i] = candidates[i].body.actor;
  for (size_t i = 0; i < count; i++) {
    if (!live(physics, actor)) break;
    qa_owned_actor trigger;
    qa_linked_body_result current = qa_shared_body_table_linked(physics->bodies, ids[i]);
    qa_linked_body_result moving = qa_shared_body_table_linked(physics->bodies, actor.id);
    if (!qa_session_actor_registry_resolve_owned(physics->options.actors, ids[i], &trigger)
        || !current.has_value || !moving.has_value || qa_same_actor(ids[i], actor.id)
        || !trigger_is(physics, trigger)) continue;
    if (!qa_bounds_intersect(current.value.absolute_bounds, moving.value.absolute_bounds)) continue;
    qa_touch_contact contact; memset(&contact, 0, sizeof contact);
    contact.self = trigger; contact.other = actor.id;
    (void)qa_actor_callback_table_touch(physics->options.callbacks, contact);
  }
  free(ids);
}
static void impact(qa_app_sim_shared_physics *p, qa_owned_actor actor, qa_trace_result trace) {
  qa_actor_id other_id;
  if (!hit_actor(p, trace, &other_id) || !live(p, actor)) return;
  qa_owned_actor other; bool has_other = qa_session_actor_registry_resolve_owned(p->options.actors, other_id, &other);
  qa_plane plane_value;
  if (trace.contact.has_plane) plane_value = trace.contact.plane;
  else if (is(trace.kind, "q1")) plane_value = trace.source_plane_q1;
  else plane_value = trace.source_plane.plane;
  p->plane_scratch = plane_value;
  bool has_surface = false;
  if (is(trace.kind, "q2") && trace.surface != NULL) {
    p->surface_scratch = (qa_touch_surface){trace.surface->name, trace.surface->flags, trace.surface->value};
    has_surface = true;
  } else if (is(trace.kind, "q1") && trace.has_surface_flags) {
    p->surface_scratch = (qa_touch_surface){"", (double)(qa_js_to_int32(trace.surface_flags) & 0x86), 0};
    has_surface = true;
  }
  qa_app_sim_shared_solid solid; bool has_solid = solid_of(p, actor, &solid);
  const char *self_solid = has_solid ? solid.solid : NULL;
  if (is(family_of(p, actor), "q2") && is(p->options.q2_edition, "rerelease") && is(trace.kind, "q2")) {
    qa_app_sim_shared_physics_flags own_flags = actor_flags(p, actor);
    if (!is(self_solid, "none") || (own_flags.has_always_touch && own_flags.always_touch)) {
      p->source_trace_scratch = (qa_touch_source_trace){trace, other_id, false};
      qa_touch_contact contact; memset(&contact, 0, sizeof contact);
      contact.self = actor; contact.other = other_id;
      contact.plane = &p->plane_scratch;
      contact.surface = has_surface ? &p->surface_scratch : NULL;
      contact.source_trace = &p->source_trace_scratch;
      (void)qa_actor_callback_table_touch(p->options.callbacks, contact);
    }
    if (has_other && live(p, other)) {
      qa_app_sim_shared_solid other_solid;
      const char *other_solid_name = solid_of(p, other, &other_solid) ? other_solid.solid : NULL;
      qa_app_sim_shared_physics_flags other_flags = actor_flags(p, other);
      if (!is(other_solid_name, "none") || (other_flags.has_always_touch && other_flags.always_touch)) {
        p->source_trace_scratch = (qa_touch_source_trace){trace, other_id, true};
        qa_touch_contact contact; memset(&contact, 0, sizeof contact);
        contact.self = other; contact.other = actor.id;
        contact.plane = &p->plane_scratch;
        contact.surface = has_surface ? &p->surface_scratch : NULL;
        contact.source_trace = &p->source_trace_scratch;
        (void)qa_actor_callback_table_touch(p->options.callbacks, contact);
      }
    }
    return;
  }
  if (!is(self_solid, "none")) {
    qa_touch_contact contact; memset(&contact, 0, sizeof contact);
    contact.self = actor; contact.other = other_id;
    contact.plane = &p->plane_scratch;
    contact.surface = has_surface ? &p->surface_scratch : NULL;
    (void)qa_actor_callback_table_touch(p->options.callbacks, contact);
  }
  if (has_other && live(p, actor) && live(p, other)) {
    qa_app_sim_shared_solid other_solid;
    const char *other_solid_name = solid_of(p, other, &other_solid) ? other_solid.solid : NULL;
    if (!is(other_solid_name, "none")) {
      qa_touch_contact contact; memset(&contact, 0, sizeof contact);
      contact.self = other; contact.other = actor.id;
      (void)qa_actor_callback_table_touch(p->options.callbacks, contact);
    }
  }
}
/* SV_PushEntity runs once per toss; `callbacks: false` is the sliding retry, which must not repeat touch and impact callbacks. */
static qa_trace_result push_entity(qa_app_sim_shared_physics *p, qa_owned_actor actor, qa_vec3 displacement,
    const qa_actor_id *exclude, size_t exclude_count, bool callbacks) {
  qa_body_state initial;
  if (!read_body(p, actor.id, &initial)) qa_range_error("Cannot push an actor without a body");
  qa_vec3 end = vadd(p, initial.origin, displacement);
  for (;;) {
    qa_trace_result trace = body_trace(p, actor, initial.origin, end, exclude, exclude_count, false, false, initial.bounds);
    qa_body_state current;
    if (!read_body(p, actor.id, &current)) return trace;
    write_live(p, actor, (body_changes){.origin = true, .origin_v = trace.end}, false);
    qa_shared_body_table_link(p->bodies, actor, NULL);
    bool q1 = is(family_of(p, actor), "q1");
    if (q1 && callbacks && live(p, actor)) qa_app_sim_shared_physics_touch_triggers(p, actor);
    /* SV_PushEntity impacts on `trace.ent`, which is also set when the move starts inside a solid. */
    if (trace.fraction != 1 || (q1 && trace.start_solid)) {
      qa_actor_id hit; bool has_hit = hit_actor(p, trace, &hit);
      if (callbacks && live(p, actor)) impact(p, actor, trace);
      if (!q1 && has_hit && !qa_session_actor_registry_is_live(p->options.actors, hit) && live(p, actor)) {
        qa_body_state changed;
        if (read_body(p, actor.id, &changed)) {
          write_live(p, actor, (body_changes){.origin = true, .origin_v = initial.origin}, false);
          qa_shared_body_table_link(p->bodies, actor, NULL);
          continue;
        }
      }
    }
    if (!q1 && callbacks && live(p, actor)) qa_app_sim_shared_physics_touch_triggers(p, actor);
    return trace;
  }
}
static qa_vec3 clip_velocity(qa_app_sim_shared_physics *p, qa_vec3 velocity, qa_vec3 normal, double overbounce) {
  qa_numeric_operations *n = &p->numeric;
  double backoff = n->multiply(n->ctx, vdot(p, velocity, normal), overbounce);
  qa_vec3 clipped = vec_of(p,
      n->subtract(n->ctx, velocity.x, n->multiply(n->ctx, normal.x, backoff)),
      n->subtract(n->ctx, velocity.y, n->multiply(n->ctx, normal.y, backoff)),
      n->subtract(n->ctx, velocity.z, n->multiply(n->ctx, normal.z, backoff)));
  double sx = clipped.x > -0.1 && clipped.x < 0.1 ? 0 : clipped.x;
  double sy = clipped.y > -0.1 && clipped.y < 0.1 ? 0 : clipped.y;
  double sz = clipped.z > -0.1 && clipped.z < 0.1 ? 0 : clipped.z;
  return vec_of(p, sx, sy, sz);
}
static bool test_position(qa_app_sim_shared_physics *p, qa_owned_actor actor, qa_trace_result *out) {
  qa_body_state body;
  if (!read_body(p, actor.id, &body)) return false;
  qa_trace_result trace = body_trace(p, actor, body.origin, body.origin, NULL, 0, false, false, body.bounds);
  if (trace.start_solid) { *out = trace; return true; }
  return false;
}
static qa_app_sim_shared_physics *candidate_sort_physics;
static int candidate_cmp(const void *a, const void *b) {
  qa_app_sim_shared_physics *p = candidate_sort_physics;
  double v = p->options.source_order(p->options.ctx, *(const qa_actor_id *)a, *(const qa_actor_id *)b);
  return v < 0 ? -1 : v > 0 ? 1 : 0;
}
static const qa_owned_actor *candidates(qa_app_sim_shared_physics *p, size_t *out_count) {
  size_t count = 0;
  const qa_actor_observation *observations = qa_session_actor_registry_observations(p->options.actors, &count);
  if (count > p->candidate_id_capacity) {
    p->candidate_ids = realloc(p->candidate_ids, count * sizeof *p->candidate_ids);
    p->candidates = realloc(p->candidates, count * sizeof *p->candidates);
    if (!p->candidate_ids || !p->candidates) qa_range_error("Out of memory");
    p->candidate_id_capacity = count; p->candidate_capacity = count;
  }
  for (size_t i = 0; i < count; i++) p->candidate_ids[i] = observations[i].id;
  if (p->options.source_order && count > 1) {
    candidate_sort_physics = p;
    qa_js_sort(p->candidate_ids, count, sizeof p->candidate_ids[0], candidate_cmp);
  }
  p->candidate_count = 0;
  for (size_t i = 0; i < count; i++) {
    qa_owned_actor actor;
    if (qa_session_actor_registry_resolve_owned(p->options.actors, p->candidate_ids[i], &actor))
      p->candidates[p->candidate_count++] = actor;
  }
  *out_count = p->candidate_count;
  return p->candidates;
}
bool qa_app_sim_shared_physics_read_q1_pusher(qa_app_sim_shared_physics *physics, qa_actor_id actor, qa_q1_physics_entity *out) {
  qa_owned_actor owned; qa_body_state body;
  qa_linked_body_result linked = qa_shared_body_table_linked(physics->bodies, actor);
  if (!qa_session_actor_registry_resolve_owned(physics->options.actors, actor, &owned)
      || !read_body(physics, actor, &body) || !linked.has_value) return false;
  qa_app_sim_shared_physics_flags flags = actor_flags(physics, owned);
  qa_q2_motion motion; bool has_motion = motion_of(physics, owned, &motion);
  qa_app_sim_shared_solid solid; bool has_solid = solid_of(physics, owned, &solid);
  double move_type = (flags.has_player && flags.player) ? 3
      : has_motion && (is(motion.kind, "push") || is(motion.kind, "stop")) ? 7
      : has_motion && is(motion.kind, "step") ? 4
      : has_motion && is(motion.kind, "fly") ? 5
      : has_motion && is(motion.kind, "fly-missile") ? 9
      : has_motion && (is(motion.kind, "bounce") || is(motion.kind, "wall-bounce")) ? 10
      : has_motion && (is(motion.kind, "toss") || is(motion.kind, "new-toss")) ? 6 : 0;
  memset(out, 0, sizeof *out);
  out->actor = owned;
  out->bounds = body.bounds;
  out->absolute_bounds = linked.value.absolute_bounds;
  out->solid = (has_solid && solid.has_dead_monster && solid.dead_monster) ? "corpse"
      : has_solid && is(solid.solid, "brush") ? "bsp"
      : has_solid && is(solid.solid, "trigger") ? "trigger"
      : has_solid && is(solid.solid, "box") ? "box" : "not";
  out->state.origin = body.origin;
  out->state.angles = body.angles;
  out->state.velocity = body.velocity;
  out->state.old_origin = body.origin;
  out->state.angular_velocity = has_motion ? motion.angular_velocity : ZERO3;
  out->state.flags = (double)((body.ground == NULL ? 0 : 512)
      | ((flags.has_fly && flags.fly) ? 1 : 0) | ((flags.has_swim && flags.swim) ? 2 : 0));
  out->state.move_type = move_type;
  if (body.ground == NULL) out->state.ground.kind = QA_TRACE_HIT_NONE;
  else { out->state.ground.kind = QA_TRACE_HIT_ACTOR; out->state.ground.actor = *body.ground; }
  out->state.water_level = flags.has_water_level ? flags.water_level : 0;
  out->state.water_type = flags.has_water_type ? flags.water_type : -1;
  return true;
}
void qa_app_sim_shared_physics_write_q1_pusher(qa_app_sim_shared_physics *physics, qa_q1_physics_entity entity) {
  body_changes changes; memset(&changes, 0, sizeof changes);
  changes.origin = true; changes.origin_v = entity.state.origin;
  changes.angles = true; changes.angles_v = entity.state.angles;
  changes.bounds = true; changes.bounds_v = entity.bounds;
  changes.ground = true;
  if ((qa_js_to_int32(entity.state.flags) & 512) == 0) changes.has_ground = false;
  else if (entity.state.ground.kind == QA_TRACE_HIT_ACTOR) {
    changes.has_ground = true; changes.ground_v = entity.state.ground.actor;
  } else if (entity.state.ground.kind == QA_TRACE_HIT_WORLD && physics->options.world_actor) {
    changes.has_ground = physics->options.world_actor(physics->options.ctx, &changes.ground_v);
  }
  write_live(physics, entity.actor, changes, false);
}
static bool q1_services_read(void *ctx, qa_actor_id actor, qa_q1_physics_entity *out) {
  qa_app_sim_q1_pusher_env *env = ctx;
  return env->read(env->projection_ctx, actor, out);
}
static const qa_actor_id *q1_services_candidates(void *ctx, size_t *out_count) {
  qa_app_sim_q1_pusher_env *env = ctx;
  size_t count = 0;
  const qa_owned_actor *list = candidates(env->physics, &count);
  free(env->candidates);
  env->candidates = count ? malloc(count * sizeof *env->candidates) : NULL;
  if (count && !env->candidates) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) env->candidates[i] = list[i].id;
  env->candidate_count = count;
  *out_count = count;
  return env->candidates;
}
static void q1_services_write(void *ctx, qa_q1_physics_entity entity) {
  qa_app_sim_q1_pusher_env *env = ctx;
  env->write(env->projection_ctx, entity);
}
static void q1_services_collision_enabled(void *ctx, qa_owned_actor actor, bool enabled) {
  qa_app_sim_q1_pusher_env *env = ctx;
  if (enabled) env->link(env->projection_ctx, actor, false);
  else qa_shared_unlink(env->physics->options.scene, actor.id);
}
static qa_trace_hit q1_services_test_position(void *ctx, qa_q1_physics_entity entity) {
  qa_app_sim_q1_pusher_env *env = ctx;
  qa_trace_result trace;
  qa_trace_hit none; memset(&none, 0, sizeof none); none.kind = QA_TRACE_HIT_NONE;
  return test_position(env->physics, entity.actor, &trace) ? trace.hit : none;
}
static qa_q1_push_result q1_services_push(void *ctx, qa_q1_physics_entity entity, qa_vec3 displacement) {
  qa_app_sim_q1_pusher_env *env = ctx;
  env->write(env->projection_ctx, entity);
  qa_q1_push_result result; memset(&result, 0, sizeof result);
  result.trace = push_entity(env->physics, entity.actor, displacement, NULL, 0, true);
  result.has_entity = env->read(env->projection_ctx, entity.actor.id, &result.entity);
  return result;
}
static void q1_services_blocked_link(void *ctx, qa_owned_actor actor, bool touch) {
  qa_app_sim_q1_pusher_env *env = ctx;
  env->link(env->projection_ctx, actor, touch);
}
static void q1_services_blocked(void *ctx, qa_owned_actor pusher, qa_actor_id obstacle) {
  qa_app_sim_q1_pusher_env *env = ctx;
  env->blocked(env->projection_ctx, pusher, obstacle);
}
qa_q1_pusher_services qa_app_sim_shared_physics_q1_pusher_services(qa_app_sim_q1_pusher_env *env) {
  qa_q1_pusher_services services; memset(&services, 0, sizeof services);
  services.ctx = env;
  services.numeric = env->physics->numeric;
  services.read = q1_services_read;
  services.candidates = q1_services_candidates;
  services.write = q1_services_write;
  services.link = q1_services_blocked_link;
  services.collision_enabled = q1_services_collision_enabled;
  services.test_position = q1_services_test_position;
  services.push = q1_services_push;
  services.blocked = q1_services_blocked;
  return services;
}
typedef struct push_q1_env {
  qa_app_sim_shared_physics *physics;
  qa_app_sim_q1_pusher_env services_env;
  qa_actor_id obstacle; bool has_obstacle;
} push_q1_env;
static bool push_q1_read(void *ctx, qa_actor_id actor, qa_q1_physics_entity *out) {
  return qa_app_sim_shared_physics_read_q1_pusher(((push_q1_env *)ctx)->physics, actor, out);
}
static void push_q1_write(void *ctx, qa_q1_physics_entity entity) {
  qa_app_sim_shared_physics_write_q1_pusher(((push_q1_env *)ctx)->physics, entity);
}
static void push_q1_link(void *ctx, qa_owned_actor actor, bool touch) {
  qa_app_sim_shared_physics *p = ((push_q1_env *)ctx)->physics;
  qa_shared_body_table_link(p->bodies, actor, NULL);
  if (touch) qa_app_sim_shared_physics_touch_triggers(p, actor);
}
static void push_q1_blocked(void *ctx, qa_owned_actor pusher, qa_actor_id other) {
  push_q1_env *env = ctx;
  env->obstacle = other; env->has_obstacle = true;
  if (env->physics->options.on_blocked) env->physics->options.on_blocked(env->physics->options.ctx, pusher, other);
}
static bool push_q1(qa_app_sim_shared_physics *p, qa_owned_actor actor, qa_vec3 displacement, qa_vec3 angular, qa_actor_id *out) {
  push_q1_env env; memset(&env, 0, sizeof env);
  env.physics = p;
  env.services_env.physics = p;
  env.services_env.projection_ctx = &env;
  env.services_env.read = push_q1_read;
  env.services_env.write = push_q1_write;
  env.services_env.link = push_q1_link;
  env.services_env.blocked = push_q1_blocked;
  qa_q1_pusher_services services = qa_app_sim_shared_physics_q1_pusher_services(&env.services_env);
  qa_q1_pusher_result result = qa_push_q1_pusher(
      (qa_q1_push_input){actor.id, displacement, angular, 0}, services);
  free(result.moved);
  free(env.services_env.candidates);
  if (env.has_obstacle) *out = env.obstacle;
  return env.has_obstacle;
}
static void restore_pushed(qa_app_sim_shared_physics *p, pushed_entry entry, bool angles) {
  qa_body_state current;
  if (!read_body(p, entry.actor.id, &current)) return;
  body_changes changes; memset(&changes, 0, sizeof changes);
  changes.origin = true; changes.origin_v = entry.origin;
  if (angles) { changes.angles = true; changes.angles_v = entry.angles; }
  write_live(p, entry.actor, changes, false);
  if (angles) {
    qa_app_sim_shared_physics_flags flags = actor_flags(p, entry.actor);
    if (flags.has_player && flags.player) {
      qa_app_sim_shared_physics_flags change; memset(&change, 0, sizeof change);
      change.has_delta_yaw = true; change.delta_yaw = entry.delta_yaw;
      qa_app_sim_shared_physics_set_flags(p, entry.actor, change);
    }
  }
  qa_shared_body_table_link(p->bodies, entry.actor, NULL);
}
static void push_saved(qa_app_sim_shared_physics *p, pushed_entry entry) {
  if (!p->push_transaction) {
    p->push_transaction = malloc(MAX_PUSHED * sizeof *p->push_transaction);
    if (!p->push_transaction) qa_range_error("Out of memory");
  }
  if (p->push_count >= MAX_PUSHED) qa_range_error("Out of memory");
  p->push_transaction[p->push_count++] = entry;
}
bool qa_app_sim_shared_physics_push_move(qa_app_sim_shared_physics *physics, qa_owned_actor actor,
    qa_vec3 displacement, qa_vec3 angular_displacement, qa_actor_id *out_blocked) {
  qa_body_state original;
  if (!read_body(physics, actor.id, &original)) return false;
  if (is(family_of(physics, actor), "q1")) return push_q1(physics, actor, displacement, angular_displacement, out_blocked);
  qa_numeric_operations *n = &physics->numeric;
  double sx = n->store(n->ctx, trunc(displacement.x * 8 + (displacement.x > 0 ? 0.5 : -0.5)) * 0.125);
  double sy = n->store(n->ctx, trunc(displacement.y * 8 + (displacement.y > 0 ? 0.5 : -0.5)) * 0.125);
  double sz = n->store(n->ctx, trunc(displacement.z * 8 + (displacement.z > 0 ? 0.5 : -0.5)) * 0.125);
  qa_vec3 move = vec_of(physics, sx, sy, sz);
  if (!moving3(move) && !moving3(angular_displacement)) return false;
  size_t base = physics->push_active ? physics->push_count : 0;
  if (!physics->push_active) physics->push_count = 0;
  qa_app_sim_shared_physics_flags actor_flag_values = actor_flags(physics, actor);
  push_saved(physics, (pushed_entry){actor, original.origin, original.angles,
      actor_flag_values.has_delta_yaw ? actor_flag_values.delta_yaw : 0});
  body_changes own; memset(&own, 0, sizeof own);
  own.origin = true; own.origin_v = vadd(physics, original.origin, move);
  own.angles = true; own.angles_v = vadd(physics, original.angles, angular_displacement);
  write_live(physics, actor, own, true);
  qa_linked_body_result linked = qa_shared_body_table_linked(physics->bodies, actor.id);
  if (!linked.has_value) { if (!physics->push_active) physics->push_count = base; return false; }
  qa_bounds bounds = linked.value.absolute_bounds;
  qa_vec3 angles = vscale(physics, angular_displacement, -1);
  qa_vec3 forward = ZERO3, right = ZERO3, up = ZERO3;
  if (physics->options.numeric.arithmetic.kind == QA_ARITHMETIC_DONOR_BINARY64)
    qa_donor_angle_vectors(angles, &forward, &right, &up);
  else qa_angle_vectors_into(&forward, &right, &up, angles);
  size_t count = 0;
  const qa_owned_actor *list = candidates(physics, &count);
  qa_owned_actor *copy = count ? malloc(count * sizeof *copy) : NULL;
  if (count && !copy) qa_range_error("Out of memory");
  if (count) memcpy(copy, list, count * sizeof *copy);
  bool blocked_result = false;
  for (size_t i = 0; i < count; i++) {
    qa_owned_actor candidate = copy[i];
    if (qa_same_actor(candidate.id, actor.id) || !live(physics, actor)) continue;
    qa_body_state body;
    qa_linked_body_result candidate_linked = qa_shared_body_table_linked(physics->bodies, candidate.id);
    qa_q2_motion candidate_motion; bool has_candidate_motion = motion_of(physics, candidate, &candidate_motion);
    if (!read_body(physics, candidate.id, &body) || !candidate_linked.has_value || !has_candidate_motion
        || is(candidate_motion.kind, "push") || is(candidate_motion.kind, "stop")
        || is(candidate_motion.kind, "stationary")) continue;
    bool rider = body.ground != NULL && qa_same_actor(*body.ground, actor.id);
    qa_trace_result probe;
    if (!rider && (!qa_bounds_intersect(candidate_linked.value.absolute_bounds, bounds)
        || !test_position(physics, candidate, &probe))) continue;
    qa_q2_motion own_motion;
    bool blocked = motion_of(physics, actor, &own_motion) && is(own_motion.kind, "stop") && !rider;
    if (!blocked) {
      qa_app_sim_shared_physics_flags candidate_flags = actor_flags(physics, candidate);
      push_saved(physics, (pushed_entry){candidate, body.origin, body.angles,
          candidate_flags.has_delta_yaw ? candidate_flags.delta_yaw : 0});
      qa_body_state pusher_body;
      if (!read_body(physics, actor.id, &pusher_body)) { free(copy); if (!physics->push_active) physics->push_count = base; return false; }
      qa_vec3 translated = vadd(physics, body.origin, move);
      qa_vec3 offset = vsub(physics, translated, pusher_body.origin);
      qa_vec3 rotated = vec_of(physics, vdot(physics, offset, forward), -vdot(physics, offset, right), vdot(physics, offset, up));
      qa_vec3 delta = vadd(physics, move, vsub(physics, rotated, offset));
      body_changes change; memset(&change, 0, sizeof change);
      change.origin = true; change.origin_v = vadd(physics, body.origin, delta);
      change.ground = true;
      change.has_ground = rider && body.ground != NULL;
      if (change.has_ground) change.ground_v = *body.ground;
      write_live(physics, candidate, change, false);
      candidate_flags = actor_flags(physics, candidate);
      if (candidate_flags.has_player && candidate_flags.player) {
        qa_app_sim_shared_physics_flags yaw; memset(&yaw, 0, sizeof yaw);
        yaw.has_delta_yaw = true;
        yaw.delta_yaw = n->add(n->ctx, candidate_flags.has_delta_yaw ? candidate_flags.delta_yaw : 0, angular_displacement.y);
        qa_app_sim_shared_physics_set_flags(physics, candidate, yaw);
      }
      if (!live(physics, actor)) { free(copy); if (!physics->push_active) physics->push_count = base; return false; }
      if (!live(physics, candidate)) continue;
      blocked = test_position(physics, candidate, &probe);
      if (!blocked) { qa_shared_body_table_link(physics->bodies, candidate, NULL); continue; }
      qa_body_state state;
      if (read_body(physics, candidate.id, &state))
        write_live(physics, candidate, (body_changes){.origin = true, .origin_v = vsub(physics, state.origin, move)}, false);
      if (!test_position(physics, candidate, &probe)) { physics->push_count--; continue; }
    }
    if (blocked) {
      /* TS rolls back the whole shared transaction, including earlier
       * team members, before the blocked callback runs. */
      size_t rollback_base = physics->push_active ? 0 : base;
      for (size_t j = physics->push_count; j-- > rollback_base;) restore_pushed(physics, physics->push_transaction[j], true);
      if (live(physics, actor) && physics->options.on_blocked)
        physics->options.on_blocked(physics->options.ctx, actor, candidate.id);
      *out_blocked = candidate.id;
      blocked_result = true;
      break;
    }
  }
  free(copy);
  if (blocked_result) { if (!physics->push_active) physics->push_count = base; return true; }
  if (!physics->push_active) {
    for (size_t j = physics->push_count; j-- > base;) {
      pushed_entry entry = physics->push_transaction[j];
      if (live(physics, entry.actor)) qa_app_sim_shared_physics_touch_triggers(physics, entry.actor);
    }
    physics->push_count = base;
  }
  return false;
}
bool qa_app_sim_shared_physics_push_team(qa_app_sim_shared_physics *physics, const qa_owned_actor *actors,
    size_t actor_count, double elapsed, qa_actor_id *out_blocked) {
  if (physics->push_active) qa_range_error("Nested pusher team movement is not permitted by the source frame");
  physics->push_active = true;
  physics->push_count = 0;
  bool blocked = false;
  for (size_t i = 0; i < actor_count; i++) {
    qa_owned_actor actor = actors[i];
    if (!live(physics, actor)) continue;
    qa_body_state body; qa_q2_motion motion;
    if (!read_body(physics, actor.id, &body) || !motion_of(physics, actor, &motion)) continue;
    if (qa_app_sim_shared_physics_push_move(physics, actor, vscale(physics, body.velocity, elapsed),
        vscale(physics, motion.angular_velocity, elapsed), out_blocked)) { blocked = true; break; }
  }
  physics->push_active = false;
  if (blocked) { physics->push_count = 0; return true; }
  for (size_t j = physics->push_count; j-- > 0;) {
    pushed_entry entry = physics->push_transaction[j];
    if (live(physics, entry.actor)) qa_app_sim_shared_physics_touch_triggers(physics, entry.actor);
  }
  physics->push_count = 0;
  return false;
}
#define DEFAULT_MASK 0x02020003
static qa_point_contents_result point_contents_at(qa_app_sim_shared_physics *p, qa_vec3 point,
    const char *family, double mask, const qa_actor_id *pass) {
  qa_point_contents_query query; memset(&query, 0, sizeof query);
  query.point = point;
  query.target.is_model = false;
  query.policy = policy_of(p, family, mask, "normal");
  query.numeric = p->options.numeric;
  query.pass_actor = pass;
  return qa_shared_point_contents(p->options.scene, query);
}
static bool check_bottom(qa_app_sim_shared_physics *p, qa_owned_actor actor, qa_body_state body) {
  qa_vec3 min = vadd(p, body.origin, body.bounds.min), max = vadd(p, body.origin, body.bounds.max);
  qa_q2_motion motion;
  qa_vec3 direction = motion_of(p, actor, &motion) ? motion.gravity_vector : (qa_vec3){0, 0, -1};
  double floor_z = direction.z > 0 ? max.z : min.z;
  const char *family = family_of(p, actor);
  qa_trace_result middle;
  {
    qa_trace_query query; memset(&query, 0, sizeof query);
    double x = (min.x + max.x) * 0.5, y = (min.y + max.y) * 0.5;
    query.start = (qa_vec3){x, y, floor_z};
    query.end = (qa_vec3){x, y, floor_z + direction.z * 36};
    query.shape.kind = "point";
    query.target.is_model = false;
    query.pass_actor = &actor.id;
    query.policy = policy_of(p, family, DEFAULT_MASK, "no-monsters");
    query.numeric = p->options.numeric;
    middle = qa_shared_trace(p->options.scene, query);
  }
  if (middle.fraction == 1) return false;
  double xs[2] = {min.x, max.x}, ys[2] = {min.y, max.y};
  for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) {
    qa_trace_query query; memset(&query, 0, sizeof query);
    query.start = (qa_vec3){xs[i], ys[j], floor_z};
    query.end = (qa_vec3){xs[i], ys[j], floor_z + direction.z * 36};
    query.shape.kind = "point";
    query.target.is_model = false;
    query.pass_actor = &actor.id;
    query.policy = policy_of(p, family, DEFAULT_MASK, "no-monsters");
    query.numeric = p->options.numeric;
    qa_trace_result corner = qa_shared_trace(p->options.scene, query);
    if (corner.fraction == 1 || (corner.end.z - middle.end.z) * direction.z > 18) return false;
  }
  return true;
}
bool qa_app_sim_shared_physics_walk_move(qa_app_sim_shared_physics *physics, qa_owned_actor actor, double yaw, double distance) {
  qa_body_state body;
  if (!read_body(physics, actor.id, &body)) return false;
  double radians = yaw * acos(-1.0) * 2 / 360;
  qa_vec3 move = vec_of(physics, cos(radians) * distance, sin(radians) * distance, 0);
  qa_app_sim_shared_physics_flags flags = actor_flags(physics, actor);
  const char *family = family_of(physics, actor);
  bool fly = flags.has_fly && flags.fly, swim = flags.has_swim && flags.swim;
  bool partial = flags.has_partial_ground && flags.partial_ground;
  if (fly || swim) {
    qa_body_state enemy_body; memset(&enemy_body, 0, sizeof enemy_body); bool has_enemy = false;
    if (flags.has_enemy && flags.enemy != NULL) has_enemy = read_body(physics, *flags.enemy, &enemy_body);
    for (int attempt = 0; attempt < (has_enemy ? 2 : 1); attempt++) {
      qa_vec3 end = vadd(physics, body.origin, move);
      if (attempt == 0 && has_enemy) {
        double dz = body.origin.z - enemy_body.origin.z;
        end = vadd(physics, end, (qa_vec3){0, 0, dz > 40 ? -8 : dz < 30 ? 8 : 0});
      }
      qa_trace_result trace = body_trace(physics, actor, body.origin, end, NULL, 0, false, false, body.bounds);
      if (trace.fraction != 1) continue;
      qa_point_contents_result contents = point_contents_at(physics, trace.end, family, DEFAULT_MASK, &actor.id);
      bool water = is(contents.kind, "q1")
          ? (contents.contents <= -3 && contents.contents >= -5)
          : ((qa_js_to_int32(is(contents.kind, "q2") ? contents.merged : contents.contents) & 56) != 0);
      if (swim && !water) return false;
      write_live(physics, actor, (body_changes){.origin = true, .origin_v = trace.end}, true);
      qa_app_sim_shared_physics_touch_triggers(physics, actor);
      return live(physics, actor);
    }
    return false;
  }
  qa_vec3 desired = vadd(physics, body.origin, move);
  qa_vec3 raised = vadd(physics, desired, (qa_vec3){0, 0, 18});
  qa_vec3 down = vadd(physics, desired, (qa_vec3){0, 0, -18});
  qa_trace_result trace = body_trace(physics, actor, raised, down, NULL, 0, false, false, body.bounds);
  if (trace.all_solid) return false;
  if (trace.start_solid) {
    trace = body_trace(physics, actor, desired, down, NULL, 0, false, false, body.bounds);
    if (trace.all_solid || trace.start_solid) return false;
  }
  if (trace.fraction == 1) {
    if (!partial) return false;
    body_changes change; memset(&change, 0, sizeof change);
    change.origin = true; change.origin_v = desired;
    change.ground = true; change.has_ground = false;
    write_live(physics, actor, change, true);
    qa_app_sim_shared_physics_touch_triggers(physics, actor);
    return live(physics, actor);
  }
  qa_body_state landed = body; landed.origin = trace.end;
  if (!check_bottom(physics, actor, landed)) {
    if (!partial) return false;
    write_live(physics, actor, (body_changes){.origin = true, .origin_v = trace.end}, true);
    qa_app_sim_shared_physics_touch_triggers(physics, actor);
    return live(physics, actor);
  }
  qa_actor_id ground;
  body_changes change; memset(&change, 0, sizeof change);
  change.origin = true; change.origin_v = trace.end;
  change.ground = true; change.has_ground = hit_actor(physics, trace, &ground);
  if (change.has_ground) change.ground_v = ground;
  write_live(physics, actor, change, true);
  qa_app_sim_shared_physics_flags clear_partial; memset(&clear_partial, 0, sizeof clear_partial);
  clear_partial.has_partial_ground = true; clear_partial.partial_ground = false;
  qa_app_sim_shared_physics_set_flags(physics, actor, clear_partial);
  qa_app_sim_shared_physics_touch_triggers(physics, actor);
  return live(physics, actor);
}
typedef struct fly_env {
  qa_app_sim_shared_physics *physics;
  qa_owned_actor actor;
  bool new_toss;
  qa_trace_result trace_storage;
} fly_env;
static qa_trace_result fly_trace_bounds(void *ctx, qa_vec3 start, qa_vec3 end, qa_bounds bounds) {
  fly_env *env = ctx;
  return body_trace(env->physics, env->actor, start, end, NULL, 0, env->new_toss, true, bounds);
}
static bool fly_hit_actor(void *ctx, qa_trace_result trace, qa_actor_id *out) {
  return hit_actor(((fly_env *)ctx)->physics, trace, out);
}
static void fly_impact(void *ctx, qa_trace_result trace) {
  fly_env *env = ctx;
  impact(env->physics, env->actor, trace);
}
static bool fly_take_kill_velocity(void *ctx) {
  fly_env *env = ctx;
  return env->physics->options.take_kill_velocity
      ? env->physics->options.take_kill_velocity(env->physics->options.ctx, env->actor) : false;
}
static bool sweep_read(void *ctx, qa_swept_body_state *out) {
  fly_env *env = ctx;
  qa_body_state body;
  if (!live(env->physics, env->actor) || !read_body(env->physics, env->actor.id, &body)) return false;
  out->origin = body.origin; out->velocity = body.velocity;
  return true;
}
static void sweep_write_origin(void *ctx, qa_vec3 origin) {
  fly_env *env = ctx;
  write_live(env->physics, env->actor, (body_changes){.origin = true, .origin_v = origin}, false);
}
static void sweep_write_velocity(void *ctx, qa_vec3 velocity, qa_swept_velocity_components components) {
  fly_env *env = ctx;
  (void)components;
  write_live(env->physics, env->actor, (body_changes){.velocity = true, .velocity_v = velocity}, false);
}
static void sweep_trace(void *ctx, qa_vec3 start, qa_vec3 end, qa_swept_body_trace *out) {
  fly_env *env = ctx;
  env->trace_storage = body_trace(env->physics, env->actor, start, end, NULL, 0, env->new_toss, false, (qa_bounds){ZERO3, ZERO3});
  out->fraction = env->trace_storage.fraction;
  out->end = env->trace_storage.end;
  out->all_solid = env->trace_storage.all_solid;
  out->start_solid = env->trace_storage.start_solid;
  out->native = &env->trace_storage;
}
static qa_vec3 sweep_normal(void *ctx, const qa_swept_body_trace *trace) {
  (void)ctx;
  return trace_normal(*(const qa_trace_result *)trace->native);
}
static uintptr_t sweep_plane_identity(void *ctx, const qa_swept_body_trace *trace) {
  (void)ctx;
  return ((const qa_trace_result *)trace->native)->plane_identity;
}
static bool sweep_same_plane(void *ctx, qa_vec3 first, qa_vec3 second, bool same_slot) {
  fly_env *env = ctx;
  return same_slot || (!is(family_of(env->physics, env->actor), "q1")
      && first.x == second.x && first.y == second.y && first.z == second.z);
}
static void sweep_impact(void *ctx, const qa_swept_body_trace *swept_trace, qa_vec3 normal) {
  fly_env *env = ctx;
  qa_app_sim_shared_physics *p = env->physics;
  qa_trace_result trace = *(const qa_trace_result *)swept_trace->native;
  qa_actor_id hit; bool has_hit = hit_actor(p, trace, &hit);
  qa_owned_actor target; bool has_target = has_hit
      && qa_session_actor_registry_resolve_owned(p->options.actors, hit, &target);
  qa_q2_motion motion;
  qa_vec3 down = motion_of(p, env->actor, &motion) ? motion.gravity_vector : (qa_vec3){0, 0, -1};
  bool grounded_normal = env->new_toss ? normal.z > 0.7 : vdot(p, normal, down) < -0.7;
  if (grounded_normal && has_hit && (trace.hit.kind == QA_TRACE_HIT_WORLD
      || (has_target && qa_app_sim_shared_physics_is_brush(p, hit)))) {
    body_changes change; memset(&change, 0, sizeof change);
    change.ground = true; change.has_ground = true; change.ground_v = hit;
    write_live(p, env->actor, change, false);
  }
  impact(p, env->actor, trace);
}
static qa_vec3 sweep_advance(void *ctx, qa_vec3 origin, double time, qa_vec3 velocity) {
  fly_env *env = ctx;
  return vadd(env->physics, origin, vscale(env->physics, velocity, time));
}
static double sweep_remaining(void *ctx, double time, double fraction) {
  qa_numeric_operations *n = &((fly_env *)ctx)->physics->numeric;
  return n->subtract(n->ctx, time, n->multiply(n->ctx, time, fraction));
}
static qa_vec3 sweep_clip(void *ctx, qa_vec3 velocity, qa_vec3 normal) {
  return clip_velocity(((fly_env *)ctx)->physics, velocity, normal, 1);
}
static double sweep_dot(void *ctx, qa_vec3 first, qa_vec3 second) {
  return vdot(((fly_env *)ctx)->physics, first, second);
}
static qa_vec3 sweep_cross(void *ctx, qa_vec3 a, qa_vec3 b) {
  return vec_of(((fly_env *)ctx)->physics,
      a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static qa_vec3 sweep_scale(void *ctx, qa_vec3 vector, double amount) {
  return vscale(((fly_env *)ctx)->physics, vector, amount);
}
static void fly_move(qa_app_sim_shared_physics *p, qa_owned_actor actor, double elapsed, bool new_toss) {
  if (is(family_of(p, actor), "q2") && is(p->options.q2_edition, "rerelease")) {
    fly_env env = {p, actor, new_toss, {0}};
    qa_app_sim_q2_rerelease_fly_move_services services; memset(&services, 0, sizeof services);
    services.actors = p->options.actors;
    services.bodies = p->bodies;
    services.ctx = &env;
    services.trace = fly_trace_bounds;
    services.hit_actor = fly_hit_actor;
    services.impact = fly_impact;
    services.take_kill_velocity = fly_take_kill_velocity;
    qa_app_sim_q2_rerelease_fly_move_run(p->fly_move, actor, elapsed, services);
    return;
  }
  qa_body_state body;
  if (!read_body(p, actor.id, &body)) return;
  body_changes clear_ground; memset(&clear_ground, 0, sizeof clear_ground);
  clear_ground.ground = true; clear_ground.has_ground = false;
  write_live(p, actor, clear_ground, false);
  fly_env env = {p, actor, new_toss, {0}};
  qa_swept_body_services services; memset(&services, 0, sizeof services);
  services.ctx = &env;
  services.read = sweep_read;
  services.write_origin = sweep_write_origin;
  services.write_velocity = sweep_write_velocity;
  services.trace = sweep_trace;
  services.normal = sweep_normal;
  services.impact = sweep_impact;
  services.stop_when_still = false;
  services.same_plane = sweep_same_plane;
  services.plane_identity = sweep_plane_identity;
  services.math.ctx = &env;
  services.math.advance = sweep_advance;
  services.math.remaining = sweep_remaining;
  services.math.clip = sweep_clip;
  services.math.dot = sweep_dot;
  services.math.cross = sweep_cross;
  services.math.scale = sweep_scale;
  (void)qa_sweep_body(&services, elapsed);
}
void qa_app_sim_shared_physics_water_transition(qa_app_sim_shared_physics *physics, qa_owned_actor actor, qa_vec3 previous_origin) {
  qa_body_state body;
  if (!read_body(physics, actor.id, &body)) return;
  const char *family = family_of(physics, actor);
  qa_app_sim_shared_physics_flags before = actor_flags(physics, actor);
  qa_point_contents_result contents = point_contents_at(physics, body.origin, family, DEFAULT_MASK, &actor.id);
  double value = is(contents.kind, "q2") ? (is(physics->options.q2_edition, "rerelease") ? contents.merged : contents.stored) : contents.contents;
  bool wet = is(family, "q1")
      ? ((value <= -3 && value >= -5) || (value <= -9 && value >= -14))
      : ((qa_js_to_int32(value) & 56) != 0);
  bool was_wet = (before.has_water_level ? before.water_level : 0) != 0;
  qa_app_sim_shared_physics_flags change; memset(&change, 0, sizeof change);
  change.has_water_level = true; change.water_level = wet ? 1 : 0;
  change.has_water_type = true; change.water_type = value;
  qa_app_sim_shared_physics_set_flags(physics, actor, change);
  if (wet != was_wet)
    emit_event(physics, (qa_app_sim_physics_event){actor.id, wet ? "water-enter" : "water-leave",
        wet ? previous_origin : body.origin});
}
typedef struct toss_env { qa_app_sim_shared_physics *physics; qa_owned_actor actor; qa_q2_motion motion; } toss_env;
static void toss_water(void *ctx, double *out_level, double *out_type) {
  toss_env *e = ctx;
  qa_app_sim_shared_physics_flags flags = actor_flags(e->physics, e->actor);
  *out_level = flags.has_water_level ? flags.water_level : 0;
  *out_type = flags.has_water_type ? flags.water_type : 0;
}
static qa_trace_result toss_trace(void *ctx, qa_vec3 start, qa_vec3 end) {
  toss_env *e = ctx;
  qa_body_state body;
  (void)read_body(e->physics, e->actor.id, &body);
  return body_trace(e->physics, e->actor, start, end, NULL, 0, true, false, body.bounds);
}
static bool toss_hit_actor(void *ctx, qa_trace_result trace, qa_actor_id *out) {
  toss_env *e = ctx;
  if (trace.hit.kind == QA_TRACE_HIT_ACTOR) { *out = trace.hit.actor; return true; }
  return e->physics->options.world_actor && e->physics->options.world_actor(e->physics->options.ctx, out);
}
static void toss_fly_move(void *ctx, double elapsed) {
  toss_env *e = ctx;
  fly_move(e->physics, e->actor, elapsed, true);
}
static void toss_write_angular(void *ctx, qa_vec3 velocity) {
  toss_env *e = ctx;
  e->motion.angular_velocity = velocity;
  store_motion(e->physics, e->motion);
  write_angular(e->physics, e->actor, velocity);
}
static void toss_touch_triggers(void *ctx) {
  toss_env *e = ctx;
  qa_app_sim_shared_physics_touch_triggers(e->physics, e->actor);
}
static double toss_point_contents(void *ctx, qa_vec3 point) {
  toss_env *e = ctx;
  qa_point_contents_result contents = point_contents_at(e->physics, point, "q2", -1, &e->actor.id);
  if (!is(contents.kind, "q2")) qa_range_error("NewToss requires Q2 contents representation");
  return contents.stored;
}
static void toss_write_water(void *ctx, double level, double type) {
  toss_env *e = ctx;
  qa_app_sim_shared_physics_flags change; memset(&change, 0, sizeof change);
  change.has_water_level = true; change.water_level = level;
  change.has_water_type = true; change.water_type = type;
  qa_app_sim_shared_physics_set_flags(e->physics, e->actor, change);
}
static void toss_water_sound(void *ctx, qa_vec3 origin) {
  toss_env *e = ctx;
  qa_actor_id event_actor = e->actor.id;
  if (e->physics->options.world_actor) (void)e->physics->options.world_actor(e->physics->options.ctx, &event_actor);
  emit_event(e->physics, (qa_app_sim_physics_event){event_actor, "water-enter", origin});
}
static double clamp_velocity(double v, double maximum) {
  return isfinite(v) ? qa_js_max(-maximum, qa_js_min(maximum, v)) : 0;
}
const char *qa_app_sim_shared_physics_step(qa_app_sim_shared_physics *physics, qa_owned_actor actor, double elapsed) {
  if (!isfinite(elapsed) || elapsed < 0) qa_range_error("Invalid physics interval");
  qa_body_state state;
  bool has_state = read_body(physics, actor.id, &state);
  qa_q2_motion motion;
  bool has_motion = motion_of(physics, actor, &motion);
  if (!has_state || !has_motion || elapsed == 0 || is(motion.kind, "stationary")) return NULL;
  if (qa_shared_body_table_attachment(physics->bodies, actor.id).has_value) {
    if (is(motion.kind, "fly") || is(motion.kind, "fly-missile"))
      write_live(physics, actor, (body_changes){.angles = true,
          .angles_v = vadd(physics, state.angles, vscale(physics, motion.angular_velocity, elapsed))}, false);
    (void)push_entity(physics, actor, ZERO3, NULL, 0, true);
    if (live(physics, actor)) qa_app_sim_shared_physics_water_transition(physics, actor, state.origin);
    return NULL;
  }
  if (is(motion.kind, "push") || is(motion.kind, "stop")) {
    qa_actor_id blocked;
    (void)qa_app_sim_shared_physics_push_move(physics, actor,
        vscale(physics, state.velocity, elapsed), vscale(physics, motion.angular_velocity, elapsed), &blocked);
    return NULL;
  }
  const char *family = family_of(physics, actor);
  qa_app_sim_shared_physics_flags flags = actor_flags(physics, actor);
  bool flag_fly = flags.has_fly && flags.fly, flag_swim = flags.has_swim && flags.swim;
  bool flag_dead = flags.has_dead && flags.dead;
  double water_level = flags.has_water_level ? flags.water_level : 0;
  if (is(motion.kind, "new-toss")) {
    toss_env env = {physics, actor, motion};
    qa_app_sim_q2_new_toss_services host; memset(&host, 0, sizeof host);
    host.actors = physics->options.actors;
    host.bodies = physics->bodies;
    host.numeric = physics->numeric;
    host.edition = physics->options.q2_edition != NULL ? physics->options.q2_edition : "classic";
    host.world_gravity = physics->world_gravity;
    host.max_velocity = physics->options.has_max_velocity ? physics->options.max_velocity : 2000;
    host.stop_speed = physics->options.stop_speed ? physics->options.stop_speed(physics->options.ctx) : 100;
    host.team_slave = flags.has_team_slave && flags.team_slave;
    host.ctx = &env;
    host.water = toss_water;
    host.trace = toss_trace;
    host.hit_actor = toss_hit_actor;
    host.fly_move = toss_fly_move;
    host.write_angular_velocity = toss_write_angular;
    host.touch_triggers = toss_touch_triggers;
    host.point_contents = toss_point_contents;
    host.write_water = toss_write_water;
    host.water_sound = toss_water_sound;
    return qa_app_sim_step_q2_new_toss(actor, elapsed, motion, host);
  }
  if (state.ground != NULL && !qa_session_actor_registry_is_live(physics->options.actors, *state.ground)) {
    /* WinQuake leaves FL_ONGROUND set when the supporting edict is freed, so Q1 items and monsters stay put;
     * the dangling reference is rebound to world rather than kept. */
    body_changes rebind; memset(&rebind, 0, sizeof rebind);
    rebind.ground = true;
    rebind.has_ground = is(family, "q1") && physics->options.world_actor
        && physics->options.world_actor(physics->options.ctx, &rebind.ground_v);
    write_live(physics, actor, rebind, false);
    has_state = read_body(physics, actor.id, &state);
  } else if (state.ground != NULL && !is(family, "q1") && vdot(physics, state.velocity, motion.gravity_vector) < 0) {
    body_changes clear_ground; memset(&clear_ground, 0, sizeof clear_ground);
    clear_ground.ground = true; clear_ground.has_ground = false;
    write_live(physics, actor, clear_ground, false);
    has_state = read_body(physics, actor.id, &state);
  }
  if (!has_state) return NULL;
  double maximum = physics->options.has_max_velocity ? physics->options.max_velocity : 2000;
  qa_vec3 velocity = vec_of(physics, clamp_velocity(state.velocity.x, maximum),
      clamp_velocity(state.velocity.y, maximum), clamp_velocity(state.velocity.z, maximum));
  if (is(motion.kind, "step")) {
    if (is(family, "q1")) {
      if (state.ground != NULL || flag_fly || flag_swim) return NULL;
      bool hit_sound = state.velocity.z < -physics->world_gravity * 0.1;
      velocity = vadd(physics, state.velocity,
          vscale(physics, motion.gravity_vector, motion.gravity * physics->world_gravity * elapsed));
      write_live(physics, actor, (body_changes){.velocity = true, .velocity_v = vec_of(physics,
          clamp_velocity(velocity.x, maximum), clamp_velocity(velocity.y, maximum),
          clamp_velocity(velocity.z, maximum))}, false);
      fly_move(physics, actor, elapsed, false);
      if (live(physics, actor)) {
        qa_shared_body_table_link(physics->bodies, actor, NULL);
        qa_app_sim_shared_physics_touch_triggers(physics, actor);
      }
      qa_body_state landed;
      if (hit_sound && read_body(physics, actor.id, &landed) && landed.ground != NULL)
        emit_event(physics, (qa_app_sim_physics_event){actor.id, "land", landed.origin});
      return NULL;
    }
    if (state.ground == NULL && vdot(physics, velocity, motion.gravity_vector) >= -100) {
      qa_trace_result floor_trace = body_trace(physics, actor, state.origin,
          vadd(physics, state.origin, vscale(physics, motion.gravity_vector, 0.25)), NULL, 0, false, false, state.bounds);
      if (floor_trace.fraction < 1 && !floor_trace.start_solid
          && vdot(physics, trace_source_normal(floor_trace), motion.gravity_vector) <= -0.7) {
        /* M_CheckGround snaps to the trace end point and stops vertical motion on landing. */
        velocity = vec_of(physics, velocity.x, velocity.y, 0);
        qa_actor_id ground; bool has_ground = hit_actor(physics, floor_trace, &ground);
        body_changes change; memset(&change, 0, sizeof change);
        change.ground = true; change.has_ground = has_ground;
        if (has_ground) change.ground_v = ground;
        change.origin = true; change.origin_v = floor_trace.end;
        change.velocity = true; change.velocity_v = vec_of(physics, state.velocity.x, state.velocity.y, 0);
        write_live(physics, actor, change, false);
        qa_body_state refreshed;
        if (read_body(physics, actor.id, &refreshed)) state = refreshed;
      }
    }
    bool was_grounded = state.ground != NULL;
    bool falling_fast = vdot(physics, state.velocity, motion.gravity_vector) > physics->world_gravity * 0.1;
    write_live(physics, actor, (body_changes){.angles = true,
        .angles_v = vadd(physics, state.angles, vscale(physics, motion.angular_velocity, elapsed))}, false);
    if (moving3(motion.angular_velocity)) {
      double adjustment = elapsed * 600;
      qa_q2_motion updated = motion;
      updated.angular_velocity = vec_of(physics,
          motion.angular_velocity.x > 0 ? qa_js_max(0, motion.angular_velocity.x - adjustment) : qa_js_min(0, motion.angular_velocity.x + adjustment),
          motion.angular_velocity.y > 0 ? qa_js_max(0, motion.angular_velocity.y - adjustment) : qa_js_min(0, motion.angular_velocity.y + adjustment),
          motion.angular_velocity.z > 0 ? qa_js_max(0, motion.angular_velocity.z - adjustment) : qa_js_min(0, motion.angular_velocity.z + adjustment));
      store_motion(physics, updated);
      write_angular(physics, actor, updated.angular_velocity);
    }
    if (state.ground == NULL && !flag_fly && !(flag_swim && water_level > 2) && water_level == 0)
      velocity = vadd(physics, velocity, vscale(physics, motion.gravity_vector, motion.gravity * physics->world_gravity * elapsed));
    if (flag_fly && velocity.z != 0) {
      double speed = fabs(velocity.z);
      double factor = qa_js_max(0, speed - elapsed * qa_js_max(speed, 100) * 2) / speed;
      velocity = vec_of(physics, velocity.x, velocity.y, velocity.z * factor);
    }
    if (flag_swim && velocity.z != 0) {
      double speed = fabs(velocity.z);
      double factor = qa_js_max(0, speed - elapsed * qa_js_max(speed, 100) * water_level) / speed;
      velocity = vec_of(physics, velocity.x, velocity.y, velocity.z * factor);
    }
    if (moving3(velocity)) {
      if (state.ground != NULL || flag_fly || flag_swim) {
        double speed = hypot(velocity.x, velocity.y);
        if (speed > 0 && (!flag_dead || check_bottom(physics, actor, state))) {
          double fraction = qa_js_max(0, speed - elapsed * qa_js_max(speed, 100) * 6) / speed;
          velocity = vec_of(physics, velocity.x * fraction, velocity.y * fraction, velocity.z);
        }
      }
      write_live(physics, actor, (body_changes){.velocity = true, .velocity_v = velocity}, false);
      fly_move(physics, actor, elapsed, false);
      if (live(physics, actor)) {
        qa_shared_body_table_link(physics->bodies, actor, NULL);
        qa_app_sim_shared_physics_touch_triggers(physics, actor);
      }
      qa_body_state landed;
      if (!was_grounded && falling_fast && read_body(physics, actor.id, &landed) && landed.ground != NULL)
        emit_event(physics, (qa_app_sim_physics_event){actor.id, "land", landed.origin});
    } else {
      write_live(physics, actor, (body_changes){.velocity = true, .velocity_v = velocity}, false);
    }
    return NULL;
  }
  if (state.ground != NULL) return NULL;
  if (!is(motion.kind, "fly") && !is(motion.kind, "fly-missile") && !is(motion.kind, "wall-bounce"))
    velocity = vadd(physics, velocity, vscale(physics, motion.gravity_vector, motion.gravity * physics->world_gravity * elapsed));
  {
    body_changes change; memset(&change, 0, sizeof change);
    change.velocity = true; change.velocity_v = velocity;
    change.angles = true; change.angles_v = vadd(physics, state.angles, vscale(physics, motion.angular_velocity, elapsed));
    write_live(physics, actor, change, false);
  }
  qa_vec3 previous_origin = state.origin;
  qa_trace_result trace = push_entity(physics, actor, vscale(physics, velocity, elapsed), NULL, 0, true);
  if (!is(family, "q1")) qa_app_sim_shared_physics_water_transition(physics, actor, previous_origin);
  if (!read_body(physics, actor.id, &state) || trace.fraction == 1) return NULL;
  for (int bump = 0;; bump++) {
    qa_q2_motion current_motion;
    if (!motion_of(physics, actor, &current_motion)) current_motion = motion;
    qa_vec3 normal = trace_normal(trace);
    velocity = clip_velocity(physics, state.velocity, normal,
        is(current_motion.kind, "wall-bounce") ? 2 : is(current_motion.kind, "bounce") ? 1.5 : 1);
    if (!is(current_motion.kind, "wall-bounce") && vdot(physics, normal, current_motion.gravity_vector) < -0.7
        && (vdot(physics, velocity, current_motion.gravity_vector) > -60 || !is(current_motion.kind, "bounce"))) {
      qa_actor_id ground; bool has_ground = hit_actor(physics, trace, &ground);
      body_changes change; memset(&change, 0, sizeof change);
      change.velocity = true; change.velocity_v = ZERO3;
      change.ground = true; change.has_ground = has_ground;
      if (has_ground) change.ground_v = ground;
      write_live(physics, actor, change, false);
      qa_q2_motion stopped = current_motion;
      stopped.velocity = ZERO3; stopped.angular_velocity = ZERO3;
      store_motion(physics, stopped);
      write_angular(physics, actor, ZERO3);
      break;
    }
    write_live(physics, actor, (body_changes){.velocity = true, .velocity_v = velocity}, false);
    if (trace.fraction > 0 || bump >= 3 || !live(physics, actor)) break;
    trace = push_entity(physics, actor, vscale(physics, velocity, elapsed), NULL, 0, false);
    if (!read_body(physics, actor.id, &state) || trace.fraction == 1) break;
  }
  if (is(family, "q1") && live(physics, actor)) {
    source_entry *source = source_of_entry(physics, actor);
    if (source && source->source.water_transition) source->source.water_transition(source->source.ctx);
    else if (physics->options.q1_water_transition) physics->options.q1_water_transition(physics->options.ctx, actor);
  }
  return NULL;
}
void qa_app_sim_shared_physics_commit_attachments(qa_app_sim_shared_physics *physics) {
  qa_shared_body_table_transport_attachments(physics->bodies, physics->numeric);
}
qa_app_sim_physics_capture qa_app_sim_shared_physics_capture(qa_app_sim_shared_physics *physics) {
  if (physics->push_active || physics->event_count != 0)
    qa_range_error("Physics save requires a completed event boundary");
  qa_app_sim_physics_capture out; memset(&out, 0, sizeof out);
  out.gravity = physics->world_gravity;
  out.rerelease_movement = qa_q2_rerelease_movement_context_capture(&physics->rerelease);
  out.collision_count = physics->collision_count;
  out.collisions = out.collision_count ? malloc(out.collision_count * sizeof *out.collisions) : NULL;
  for (size_t i = 0; i < out.collision_count; i++) {
    out.collisions[i].actor = qa_saved_actor_id_of(physics->collisions[i].actor.id);
    out.collisions[i].collision = physics->collisions[i].collision;
  }
  if (physics->options.scene && physics->options.scene->spatial) {
    qa_bounds everywhere = {{-INFINITY, -INFINITY, -INFINITY}, {INFINITY, INFINITY, INFINITY}};
    size_t count = 0;
    const qa_spatial_actor *spatial = qa_spatial_index_query(physics->options.scene->spatial, everywhere, "both", &count);
    out.spatial_count = count;
    out.spatial = count ? malloc(count * sizeof *out.spatial) : NULL;
    for (size_t i = 0; i < count; i++) {
      out.spatial[i].actor = qa_saved_actor_id_of(spatial[i].body.actor);
      out.spatial[i].collision = spatial[i].collision;
    }
  }
  out.solid_count = physics->solid_count;
  out.solids = out.solid_count ? malloc(out.solid_count * sizeof *out.solids) : NULL;
  for (size_t i = 0; i < out.solid_count; i++) {
    out.solids[i].actor = qa_saved_actor_id_of(physics->solids[i].actor.id);
    out.solids[i].solid = physics->solids[i].solid;
    out.solids[i].has_owner = physics->solids[i].solid.owner != NULL;
    if (out.solids[i].has_owner) out.solids[i].owner = qa_saved_actor_id_of(*physics->solids[i].solid.owner);
  }
  out.motion_count = physics->motion_count;
  out.motions = out.motion_count ? malloc(out.motion_count * sizeof *out.motions) : NULL;
  for (size_t i = 0; i < out.motion_count; i++) {
    out.motions[i].actor = qa_saved_actor_id_of(physics->motions[i].actor.id);
    out.motions[i].motion = physics->motions[i].motion;
    out.motions[i].has_owner = physics->motions[i].motion.has_owner;
    if (out.motions[i].has_owner) out.motions[i].owner = qa_saved_actor_id_of(physics->motions[i].motion.owner);
  }
  out.flag_count = physics->flag_count;
  out.flags = out.flag_count ? malloc(out.flag_count * sizeof *out.flags) : NULL;
  for (size_t i = 0; i < out.flag_count; i++) {
    out.flags[i].actor = qa_saved_actor_id_of(physics->flags[i].actor.id);
    out.flags[i].flags = physics->flags[i].flags;
    if (physics->flags[i].flags.has_enemy && physics->flags[i].flags.enemy)
      out.flags[i].enemy = qa_saved_actor_id_of(*physics->flags[i].flags.enemy);
  }
  return out;
}
void qa_app_sim_physics_capture_free(qa_app_sim_physics_capture *capture) {
  free(capture->collisions); free(capture->spatial); free(capture->solids);
  free(capture->motions); free(capture->flags);
  memset(capture, 0, sizeof *capture);
}
static void reader_field_or_fail(const qa_save_reader *reader, const char *name, qa_save_reader *out) {
  if (!qa_save_reader_field(reader, name, out)) qa_save_reader_fail(reader, "out of memory");
}
static bool reader_item_or_fail(const qa_save_reader *reader, size_t index, qa_save_reader *out) {
  if (!qa_save_reader_item(reader, index, out)) qa_save_reader_fail(reader, "out of memory");
  return true;
}
static qa_owned_actor reader_owner(qa_app_sim_shared_physics *p, const qa_save_reader *value, const char *name) {
  qa_save_reader field;
  reader_field_or_fail(value, name, &field);
  qa_saved_actor_id saved = qa_read_saved_actor(&field);
  qa_save_reader_destroy(&field);
  qa_owned_actor actor;
  if (!qa_session_actor_registry_resolve_saved(p->options.actors, saved, &actor))
    qa_save_reader_fail(value, "Missing shared physics actor");
  return actor;
}
static bool reader_reference(qa_app_sim_shared_physics *p, const qa_save_reader *value, const char *name, qa_actor_id *out) {
  qa_save_reader field;
  reader_field_or_fail(value, name, &field);
  bool present = !qa_save_reader_is_null(&field);
  if (present) *out = qa_session_actor_registry_reference_saved(p->options.actors, qa_read_saved_actor(&field), NULL);
  qa_save_reader_destroy(&field);
  return present;
}
static bool reader_corpse(const qa_save_reader *value) {
  qa_save_reader corpse;
  reader_field_or_fail(value, "q1Corpse", &corpse);
  bool result = false;
  if (!qa_save_reader_is_undefined(&corpse)) {
    if (!qa_save_reader_boolean(&corpse)) qa_save_reader_fail(&corpse, "Saved Q1 corpse flag must be true or absent");
    result = true;
  }
  qa_save_reader_destroy(&corpse);
  return result;
}
typedef struct collision_read_env {
  qa_app_sim_shared_physics *physics;
  qa_actor_id owner_id;
} collision_read_env;
static qa_actor_collision read_collision(collision_read_env *env, const qa_save_reader *collision) {
  qa_app_sim_shared_physics *p = env->physics;
  qa_actor_collision out; memset(&out, 0, sizeof out);
  qa_save_reader field;
  reader_field_or_fail(collision, "family", &field);
  static const char *const families[] = {"q1", "q2", "q3"};
  out.family = qa_save_reader_choice_string(&field, families, 3);
  qa_save_reader_destroy(&field);
  reader_field_or_fail(collision, "shape", &field);
  qa_save_reader kind_reader;
  reader_field_or_fail(&field, "kind", &kind_reader);
  static const char *const shapes[] = {"box", "capsule", "model"};
  out.shape_kind = qa_save_reader_choice_string(&kind_reader, shapes, 3);
  qa_save_reader_destroy(&kind_reader);
  if (is(out.shape_kind, "model")) {
    qa_save_reader model;
    reader_field_or_fail(&field, "model", &model);
    out.shape_model = qa_save_reader_integer_min(&model, 0);
    qa_save_reader_destroy(&model);
  }
  qa_save_reader_destroy(&field);
  reader_field_or_fail(collision, "contents", &field);
  out.contents = qa_save_reader_number(&field);
  qa_save_reader_destroy(&field);
  if (reader_reference(p, collision, "owner", &env->owner_id)) out.owner = &env->owner_id;
  reader_field_or_fail(collision, "q3Owner", &field);
  if (!qa_save_reader_is_undefined(&field)) {
    qa_save_reader part;
    out.has_q3_owner = true;
    reader_field_or_fail(&field, "entityNumber", &part);
    out.q3_entity_number = qa_save_reader_integer(&part);
    qa_save_reader_destroy(&part);
    reader_field_or_fail(&field, "ownerNumber", &part);
    out.q3_owner_number = qa_save_reader_integer(&part);
    qa_save_reader_destroy(&part);
  }
  qa_save_reader_destroy(&field);
  out.q1_corpse = reader_corpse(collision);
  reader_field_or_fail(collision, "role", &field);
  static const char *const roles[] = {"solid", "trigger"};
  out.role = qa_save_reader_choice_string(&field, roles, 2);
  qa_save_reader_destroy(&field);
  reader_field_or_fail(collision, "monster", &field);
  out.monster = qa_save_reader_boolean(&field);
  qa_save_reader_destroy(&field);
  reader_field_or_fail(collision, "deadMonster", &field);
  out.dead_monster = qa_save_reader_boolean(&field);
  qa_save_reader_destroy(&field);
  return out;
}
typedef bool (*reconstructed_fn)(void *ctx, qa_actor_id actor);
static bool never_reconstructed(void *ctx, qa_actor_id actor) { (void)ctx; (void)actor; return false; }
static void optional_bool_field(const qa_save_reader *value, const char *name, bool *has, bool *out) {
  qa_save_reader field;
  reader_field_or_fail(value, name, &field);
  if (!qa_save_reader_is_undefined(&field)) { *has = true; *out = qa_save_reader_boolean(&field); }
  qa_save_reader_destroy(&field);
}
static void optional_number_field(const qa_save_reader *value, const char *name, bool *has, double *out) {
  qa_save_reader field;
  reader_field_or_fail(value, name, &field);
  if (!qa_save_reader_is_undefined(&field)) { *has = true; *out = qa_save_reader_number(&field); }
  qa_save_reader_destroy(&field);
}
void qa_app_sim_shared_physics_restore_checkpoint(qa_app_sim_shared_physics *physics, const qa_save_reader *reader,
    bool require_exact_collisions, bool (*reconstructed)(void *ctx, qa_actor_id actor), void *reconstructed_ctx) {
  reconstructed_fn is_reconstructed = reconstructed ? reconstructed : never_reconstructed;
  solid_entry *kept_solids = physics->solid_count ? malloc(physics->solid_count * sizeof *kept_solids) : NULL;
  motion_entry *kept_motions = physics->motion_count ? malloc(physics->motion_count * sizeof *kept_motions) : NULL;
  flags_entry *kept_flags = physics->flag_count ? malloc(physics->flag_count * sizeof *kept_flags) : NULL;
  collision_entry *kept_collisions = physics->collision_count ? malloc(physics->collision_count * sizeof *kept_collisions) : NULL;
  size_t kept_solid_count = 0, kept_motion_count = 0, kept_flag_count = 0, kept_collision_count = 0;
  for (size_t i = 0; i < physics->solid_count; i++)
    if (is_reconstructed(reconstructed_ctx, physics->solids[i].actor.id)) kept_solids[kept_solid_count++] = physics->solids[i];
  for (size_t i = 0; i < physics->motion_count; i++)
    if (is_reconstructed(reconstructed_ctx, physics->motions[i].actor.id)) kept_motions[kept_motion_count++] = physics->motions[i];
  for (size_t i = 0; i < physics->flag_count; i++)
    if (is_reconstructed(reconstructed_ctx, physics->flags[i].actor.id)) kept_flags[kept_flag_count++] = physics->flags[i];
  for (size_t i = 0; i < physics->collision_count; i++)
    if (is_reconstructed(reconstructed_ctx, physics->collisions[i].actor.id)) kept_collisions[kept_collision_count++] = physics->collisions[i];
  qa_save_reader field;
  reader_field_or_fail(reader, "gravity", &field);
  qa_app_sim_shared_physics_set_world_gravity(physics, qa_save_reader_finite(&field));
  qa_save_reader_destroy(&field);
  reader_field_or_fail(reader, "rereleaseMovement", &field);
  qa_q2_rerelease_movement_context_restore(&physics->rerelease, qa_read_vector(&field));
  qa_save_reader_destroy(&field);
  physics->solid_count = 0; physics->motion_count = 0; physics->flag_count = 0; physics->collision_count = 0;
  reader_field_or_fail(reader, "collisions", &field);
  if (require_exact_collisions || !qa_save_reader_is_undefined(&field)) {
    size_t count = qa_save_reader_list_count(&field);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader value;
      (void)reader_item_or_fail(&field, i, &value);
      qa_owned_actor actor = reader_owner(physics, &value, "actor");
      if (collision_entry_of(physics, actor)) qa_save_reader_fail(&value, "Duplicate saved collision actor");
      qa_save_reader collision_reader;
      reader_field_or_fail(&value, "collision", &collision_reader);
      collision_read_env env = {physics, actor.id};
      qa_actor_collision collision = read_collision(&env, &collision_reader);
      qa_save_reader_destroy(&collision_reader);
      if (physics->collision_count >= MAX_ENTRIES) qa_range_error("Out of memory");
      size_t slot = physics->collision_count++;
      physics->collisions[slot].actor = actor;
      physics->collisions[slot].owner_storage = env.owner_id;
      physics->collisions[slot].owner_is_storage = collision.owner != NULL;
      collision.owner = collision.owner != NULL ? &physics->collisions[slot].owner_storage : NULL;
      physics->collisions[slot].collision = collision;
      qa_save_reader_destroy(&value);
    }
  }
  qa_save_reader_destroy(&field);
  reader_field_or_fail(reader, "solids", &field);
  {
    size_t count = qa_save_reader_list_count(&field);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader value;
      (void)reader_item_or_fail(&field, i, &value);
      qa_owned_actor actor = reader_owner(physics, &value, "actor");
      qa_app_sim_shared_solid solid; memset(&solid, 0, sizeof solid);
      qa_save_reader part;
      reader_field_or_fail(&value, "solid", &part);
      static const char *const solids[] = {"none", "trigger", "box", "brush"};
      solid.solid = qa_save_reader_choice_string(&part, solids, 4);
      qa_save_reader_destroy(&part);
      reader_field_or_fail(&value, "model", &part);
      solid.has_model = !qa_save_reader_is_null(&part);
      if (solid.has_model) solid.model = qa_save_reader_integer_min(&part, 0);
      qa_save_reader_destroy(&part);
      reader_field_or_fail(&value, "family", &part);
      static const char *const families[] = {"q1", "q2", "q3"};
      solid.family = qa_save_reader_choice_string(&part, families, 3);
      qa_save_reader_destroy(&part);
      optional_bool_field(&value, "monster", &solid.has_monster, &solid.monster);
      optional_bool_field(&value, "deadMonster", &solid.has_dead_monster, &solid.dead_monster);
      solid.q1_corpse = reader_corpse(&value);
      optional_bool_field(&value, "item", &solid.has_item, &solid.item);
      if (physics->solid_count >= MAX_ENTRIES) qa_range_error("Out of memory");
      size_t slot = physics->solid_count;
      qa_actor_id owner_id = actor.id;
      bool has_owner = reader_reference(physics, &value, "owner", &owner_id);
      physics->solids[slot].actor = actor;
      physics->solids[slot].owner_storage = owner_id;
      physics->solids[slot].owner_is_storage = has_owner;
      solid.owner = has_owner ? &physics->solids[slot].owner_storage : NULL;
      physics->solids[slot].solid = solid;
      physics->solid_count++;
      qa_save_reader_destroy(&value);
    }
  }
  qa_save_reader_destroy(&field);
  reader_field_or_fail(reader, "motions", &field);
  {
    size_t count = qa_save_reader_list_count(&field);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader value;
      (void)reader_item_or_fail(&field, i, &value);
      qa_q2_motion motion; memset(&motion, 0, sizeof motion);
      motion.actor = reader_owner(physics, &value, "actor");
      qa_save_reader part;
      reader_field_or_fail(&value, "kind", &part);
      static const char *const kinds[] = {"stationary", "push", "stop", "toss", "new-toss", "bounce", "wall-bounce", "fly", "fly-missile", "step"};
      motion.kind = qa_save_reader_choice_string(&part, kinds, 10);
      qa_save_reader_destroy(&part);
      reader_field_or_fail(&value, "velocity", &part);
      motion.velocity = qa_read_vector(&part);
      qa_save_reader_destroy(&part);
      reader_field_or_fail(&value, "angularVelocity", &part);
      motion.angular_velocity = qa_read_vector(&part);
      qa_save_reader_destroy(&part);
      reader_field_or_fail(&value, "gravity", &part);
      motion.gravity = qa_save_reader_number(&part);
      qa_save_reader_destroy(&part);
      reader_field_or_fail(&value, "gravityVector", &part);
      motion.gravity_vector = qa_read_vector(&part);
      qa_save_reader_destroy(&part);
      reader_field_or_fail(&value, "clipMask", &part);
      motion.clip_mask = qa_save_reader_number(&part);
      qa_save_reader_destroy(&part);
      motion.has_owner = reader_reference(physics, &value, "owner", &motion.owner);
      motion.kind_first = true; /* the restored literal: actor, kind, velocity, ... */
      store_motion(physics, motion);
      qa_save_reader_destroy(&value);
    }
  }
  qa_save_reader_destroy(&field);
  reader_field_or_fail(reader, "flags", &field);
  {
    size_t count = qa_save_reader_list_count(&field);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader value;
      (void)reader_item_or_fail(&field, i, &value);
      qa_owned_actor actor = reader_owner(physics, &value, "actor");
      qa_app_sim_shared_physics_flags flags; memset(&flags, 0, sizeof flags);
      optional_bool_field(&value, "teamSlave", &flags.has_team_slave, &flags.team_slave);
      optional_bool_field(&value, "alwaysTouch", &flags.has_always_touch, &flags.always_touch);
      optional_bool_field(&value, "fly", &flags.has_fly, &flags.fly);
      optional_bool_field(&value, "swim", &flags.has_swim, &flags.swim);
      optional_bool_field(&value, "partialGround", &flags.has_partial_ground, &flags.partial_ground);
      optional_bool_field(&value, "dead", &flags.has_dead, &flags.dead);
      optional_bool_field(&value, "player", &flags.has_player, &flags.player);
      optional_number_field(&value, "waterLevel", &flags.has_water_level, &flags.water_level);
      optional_number_field(&value, "waterType", &flags.has_water_type, &flags.water_type);
      optional_number_field(&value, "deltaYaw", &flags.has_delta_yaw, &flags.delta_yaw);
      if (physics->flag_count >= MAX_ENTRIES) qa_range_error("Out of memory");
      size_t slot = physics->flag_count;
      qa_save_reader enemy_reader;
      reader_field_or_fail(&value, "enemy", &enemy_reader);
      bool enemy_present = !qa_save_reader_is_undefined(&enemy_reader);
      qa_save_reader_destroy(&enemy_reader);
      qa_actor_id enemy_id = actor.id;
      bool has_enemy_ref = enemy_present && reader_reference(physics, &value, "enemy", &enemy_id);
      physics->flags[slot].actor = actor;
      physics->flags[slot].enemy_storage = enemy_id;
      physics->flags[slot].enemy_is_storage = has_enemy_ref;
      flags.has_enemy = enemy_present;
      flags.enemy = has_enemy_ref ? &physics->flags[slot].enemy_storage : NULL;
      physics->flags[slot].flags = flags;
      physics->flag_count++;
      qa_save_reader_destroy(&value);
    }
  }
  qa_save_reader_destroy(&field);
  for (size_t i = 0; i < physics->solid_count;) {
    if (is_reconstructed(reconstructed_ctx, physics->solids[i].actor.id)) {
      physics->solids[i] = physics->solids[--physics->solid_count];
      if (physics->solids[i].owner_is_storage) physics->solids[i].solid.owner = &physics->solids[i].owner_storage;
    } else i++;
  }
  for (size_t i = 0; i < kept_solid_count; i++) {
    solid_entry *e = solid_entry_of(physics, kept_solids[i].actor);
    if (!e) {
      if (physics->solid_count >= MAX_ENTRIES) qa_range_error("Out of memory");
      e = &physics->solids[physics->solid_count++];
    }
    *e = kept_solids[i];
    if (e->owner_is_storage) e->solid.owner = &e->owner_storage;
  }
  for (size_t i = 0; i < physics->motion_count;)
    if (is_reconstructed(reconstructed_ctx, physics->motions[i].actor.id)) physics->motions[i] = physics->motions[--physics->motion_count]; else i++;
  for (size_t i = 0; i < kept_motion_count; i++) store_motion(physics, kept_motions[i].motion);
  for (size_t i = 0; i < physics->flag_count;) {
    if (is_reconstructed(reconstructed_ctx, physics->flags[i].actor.id)) {
      physics->flags[i] = physics->flags[--physics->flag_count];
      if (physics->flags[i].enemy_is_storage) physics->flags[i].flags.enemy = &physics->flags[i].enemy_storage;
    } else i++;
  }
  for (size_t i = 0; i < kept_flag_count; i++) {
    flags_entry *e = flags_entry_of(physics, kept_flags[i].actor);
    if (!e) {
      if (physics->flag_count >= MAX_ENTRIES) qa_range_error("Out of memory");
      e = &physics->flags[physics->flag_count++];
    }
    *e = kept_flags[i];
    if (e->enemy_is_storage) e->flags.enemy = &e->enemy_storage;
  }
  for (size_t i = 0; i < physics->collision_count;) {
    if (is_reconstructed(reconstructed_ctx, physics->collisions[i].actor.id)) {
      physics->collisions[i] = physics->collisions[--physics->collision_count];
      if (physics->collisions[i].owner_is_storage) physics->collisions[i].collision.owner = &physics->collisions[i].owner_storage;
    } else i++;
  }
  for (size_t i = 0; i < kept_collision_count; i++) {
    collision_entry *e = collision_entry_of(physics, kept_collisions[i].actor);
    if (!e) {
      if (physics->collision_count >= MAX_ENTRIES) qa_range_error("Out of memory");
      e = &physics->collisions[physics->collision_count++];
    }
    *e = kept_collisions[i];
    if (e->owner_is_storage) e->collision.owner = &e->owner_storage;
  }
  free(kept_solids); free(kept_motions); free(kept_flags); free(kept_collisions);
}
void qa_app_sim_shared_physics_restore_spatial(qa_app_sim_shared_physics *physics, const qa_save_reader *reader,
    bool (*reconstructed)(void *ctx, qa_actor_id actor), void *reconstructed_ctx) {
  reconstructed_fn is_reconstructed = reconstructed ? reconstructed : never_reconstructed;
  qa_bounds everywhere = {{-INFINITY, -INFINITY, -INFINITY}, {INFINITY, INFINITY, INFINITY}};
  size_t retained_count = 0;
  const qa_spatial_actor *current = qa_spatial_index_query(physics->options.scene->spatial, everywhere, "both", &retained_count);
  qa_spatial_actor *retained = retained_count ? malloc(retained_count * sizeof *retained) : NULL;
  size_t kept = 0;
  for (size_t i = 0; i < retained_count; i++)
    if (is_reconstructed(reconstructed_ctx, current[i].body.actor)) retained[kept++] = current[i];
  qa_spatial_index_clear(physics->options.scene->spatial);
  qa_save_reader field;
  reader_field_or_fail(reader, "spatial", &field);
  size_t count = qa_save_reader_list_count(&field);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader value;
    (void)reader_item_or_fail(&field, i, &value);
    qa_save_reader actor_reader;
    reader_field_or_fail(&value, "actor", &actor_reader);
    qa_saved_actor_id saved = qa_read_saved_actor(&actor_reader);
    qa_save_reader_destroy(&actor_reader);
    qa_owned_actor actor;
    if (!qa_session_actor_registry_resolve_saved(physics->options.actors, saved, &actor))
      qa_save_reader_fail(&value, "Saved spatial actor has no restored identity");
    qa_save_reader collision_reader;
    reader_field_or_fail(&value, "collision", &collision_reader);
    collision_read_env env = {physics, actor.id};
    qa_actor_collision collision = read_collision(&env, &collision_reader);
    qa_save_reader_destroy(&collision_reader);
    if (!is_reconstructed(reconstructed_ctx, actor.id)) {
      qa_linked_body_result body = qa_shared_body_table_linked(physics->bodies, actor.id);
      if (!body.has_value) qa_save_reader_fail(&value, "Saved spatial actor has no retained body link");
      if (collision.owner != NULL) {
        /* Spatial copies the collision by value; keep the restored
         * owner in the matching collision entry when one exists. */
        collision_entry *entry = collision_entry_of(physics, actor);
        if (entry && entry->owner_is_storage) collision.owner = &entry->owner_storage;
        else collision.owner = &env.owner_id;
      }
      qa_shared_link(physics->options.scene, body.value, collision);
    }
    qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&field);
  for (size_t i = 0; i < kept; i++) qa_shared_link(physics->options.scene, retained[i].body, retained[i].collision);
  free(retained);
}
