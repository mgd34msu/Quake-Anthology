/* Port of src/app/bootstrap/simulation/physics.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/physics.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/new_toss.h"
#include "qa/app/bootstrap/simulation/q2_rerelease_slide.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/persistence/save_image.h"
#define MAX_ENTRIES 1024
typedef struct solid_entry { qa_owned_actor actor; qa_app_sim_shared_solid solid; } solid_entry;
typedef struct motion_entry { qa_owned_actor actor; qa_q2_motion motion; } motion_entry;
typedef struct flags_entry { qa_owned_actor actor; qa_app_sim_shared_physics_flags flags; } flags_entry;
typedef struct collision_entry { qa_owned_actor actor; qa_actor_collision collision; } collision_entry;
typedef struct source_entry { bool used; int handle; qa_owned_actor actor; qa_app_sim_source_body_physics source; } source_entry;
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
  bool push_active;
};
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static bool same_owned(qa_owned_actor a, qa_owned_actor b) {
  return qa_same_actor(a.id, b.id) && a.token == b.token;
}
static source_entry *source_of(qa_app_sim_shared_physics *p, qa_owned_actor actor) {
  for (int i = 0; i < MAX_ENTRIES; i++)
    if (p->sources[i].used && same_owned(p->sources[i].actor, actor)) return &p->sources[i];
  return NULL;
}
static bool solid_of(qa_app_sim_shared_physics *p, qa_owned_actor actor, qa_app_sim_shared_solid *out) {
  source_entry *s = source_of(p, actor);
  if (s && s->source.collision && s->source.collision(s->source.ctx, out)) return true;
  if (p->options.get_collision && p->options.get_collision(p->options.ctx, actor, out)) return true;
  for (size_t i = 0; i < p->solid_count; i++) if (same_owned(p->solids[i].actor, actor)) { *out = p->solids[i].solid; return true; }
  return false;
}
static bool motion_of(qa_app_sim_shared_physics *p, qa_owned_actor actor, qa_q2_motion *out) {
  source_entry *s = source_of(p, actor);
  if (s && s->source.motion && s->source.motion(s->source.ctx, out)) return true;
  if (p->options.get_motion && p->options.get_motion(p->options.ctx, actor, out)) return true;
  for (size_t i = 0; i < p->motion_count; i++) if (same_owned(p->motions[i].actor, actor)) { *out = p->motions[i].motion; return true; }
  return false;
}
static const char *family_of(qa_app_sim_shared_physics *p, qa_owned_actor actor) {
  qa_app_sim_shared_solid solid;
  return solid_of(p, actor, &solid) ? solid.family : "q2";
}

bool qa_app_sim_shared_physics_collision_of(qa_app_sim_shared_physics *physics, qa_owned_actor actor, qa_actor_collision *out);
static qa_bounds absolute_body_bounds(void *ctx, qa_owned_actor actor, qa_body_state state) {
  (void)ctx;
  return qa_translated_body_bounds(actor, state);
}
static void on_body_link(void *ctx, qa_linked_body body) {
  qa_app_sim_shared_physics *p = ctx;
  if (!p->options.scene) return;
  qa_actor_collision collision;
  memset(&collision, 0, sizeof collision);
  qa_owned_actor owned;
  if (qa_session_actor_registry_resolve_owned(p->options.actors, body.actor, &owned))
    (void)qa_app_sim_shared_physics_collision_of(p, owned, &collision);
  qa_shared_link(p->options.scene, body, collision);
}
static void on_body_unlink(void *ctx, qa_actor_id actor) {
  qa_app_sim_shared_physics *p = ctx;
  if (p->options.scene) qa_shared_unlink(p->options.scene, actor);
}
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
  if (options.scene) options.scene->body_table = p->bodies;
  return p;
}
void qa_app_sim_shared_physics_destroy(qa_app_sim_shared_physics *physics) {
  if (!physics) return;
  qa_shared_body_table_destroy(physics->bodies);
  qa_app_sim_q2_rerelease_fly_move_destroy(physics->fly_move);
  free(physics);
}
qa_shared_body_table *qa_app_sim_shared_physics_bodies(qa_app_sim_shared_physics *physics) { return physics->bodies; }
qa_q2_rerelease_movement_context *qa_app_sim_shared_physics_rerelease_movement(qa_app_sim_shared_physics *physics) { return &physics->rerelease; }
int qa_app_sim_shared_physics_bind_source(qa_app_sim_shared_physics *physics, qa_owned_actor actor, qa_app_sim_source_body_physics source) {
  qa_session_actor_registry_assert_owned(physics->options.actors, actor);
  if (source_of(physics, actor)) qa_range_error("Actor already has source physics");
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
void qa_app_sim_shared_physics_set_solid(qa_app_sim_shared_physics *physics, qa_owned_actor actor, qa_app_sim_shared_solid solid) {
  for (size_t i = 0; i < physics->solid_count; i++)
    if (same_owned(physics->solids[i].actor, actor)) { physics->solids[i].solid = solid; return; }
  if (physics->solid_count >= MAX_ENTRIES) qa_range_error("Out of memory");
  physics->solids[physics->solid_count++] = (solid_entry){actor, solid};
}
void qa_app_sim_shared_physics_set_motion(qa_app_sim_shared_physics *physics, qa_owned_actor actor, qa_q2_motion motion) {
  for (size_t i = 0; i < physics->motion_count; i++)
    if (same_owned(physics->motions[i].actor, actor)) { physics->motions[i].motion = motion; return; }
  if (physics->motion_count >= MAX_ENTRIES) qa_range_error("Out of memory");
  physics->motions[physics->motion_count++] = (motion_entry){actor, motion};
}
void qa_app_sim_shared_physics_set_flags(qa_app_sim_shared_physics *physics, qa_owned_actor actor, qa_app_sim_shared_physics_flags flags) {
  for (size_t i = 0; i < physics->flag_count; i++)
    if (same_owned(physics->flags[i].actor, actor)) { physics->flags[i].flags = flags; return; }
  if (physics->flag_count >= MAX_ENTRIES) qa_range_error("Out of memory");
  physics->flags[physics->flag_count++] = (flags_entry){actor, flags};
}
bool qa_app_sim_shared_physics_collision_of(qa_app_sim_shared_physics *physics, qa_owned_actor actor, qa_actor_collision *out) {
  for (size_t i = 0; i < physics->collision_count; i++)
    if (same_owned(physics->collisions[i].actor, actor)) { *out = physics->collisions[i].collision; return true; }
  return false;
}
typedef struct toss_env { qa_app_sim_shared_physics *physics; qa_app_sim_shared_physics_flags flags; } toss_env;
static void toss_water(void *ctx, double *out_level, double *out_type) {
  toss_env *e = ctx;
  *out_level = e->flags.has_water_level ? e->flags.water_level : 0;
  *out_type = e->flags.has_water_type ? e->flags.water_type : 0;
}
static qa_trace_result ground_trace(void *ctx, qa_vec3 start, qa_vec3 end) {
  toss_env *e = ctx;
  qa_trace_query q; memset(&q, 0, sizeof q);
  q.start = start; q.end = end; q.shape.kind = "point"; q.target.is_model = false;
  q.policy.kind = "q2"; q.policy.contents_mask = 0x02020003; q.policy.leaf_contents = "merged";
  q.numeric = e->physics->options.numeric;
  return qa_shared_trace(e->physics->options.scene, q);
}
static bool trace_actor(void *ctx, qa_trace_result trace, qa_actor_id *out) {
  (void)ctx;
  if (trace.hit.kind == QA_TRACE_HIT_ACTOR) { *out = trace.hit.actor; return true; }
  return false;
}
static void fly_move_noop(void *ctx, double elapsed) { (void)ctx; (void)elapsed; }
static void no_op_vec(void *ctx, qa_vec3 v) { (void)ctx; (void)v; }
static void no_op(void *ctx) { (void)ctx; }
static double point_contents_fn(void *ctx, qa_vec3 origin) {
  toss_env *e = ctx;
  qa_point_contents_query q; memset(&q, 0, sizeof q);
  q.point = origin; q.target.is_model = false;
  q.policy.kind = "q2"; q.policy.contents_mask = -1; q.policy.leaf_contents = "merged";
  q.numeric = e->physics->options.numeric;
  qa_point_contents_result r = qa_shared_point_contents(e->physics->options.scene, q);
  return is(r.kind, "q2") ? r.merged : 0;
}
static void write_water(void *ctx, double level, double type) {
  toss_env *e = ctx;
  e->flags.has_water_level = true; e->flags.water_level = level;
  e->flags.has_water_type = true; e->flags.water_type = type;
}
void qa_app_sim_shared_physics_step(qa_app_sim_shared_physics *physics, qa_owned_actor actor, double elapsed) {
  if (!qa_session_actor_registry_is_live(physics->options.actors, actor.id)) return;
  qa_q2_motion motion;
  if (!motion_of(physics, actor, &motion)) return;
  const char *kind = motion.kind;
  if (is(kind, "stationary")) return;
  if (is(kind, "new-toss") || is(kind, "toss")) {
    qa_app_sim_shared_physics_flags flags; memset(&flags, 0, sizeof flags);
    (void)actor;
    if (physics->options.get_flags) physics->options.get_flags(physics->options.ctx, actor, &flags);
    toss_env env = {physics, flags};
    qa_app_sim_q2_new_toss_services host; memset(&host, 0, sizeof host);
    host.actors = physics->options.actors; host.bodies = physics->bodies;
    host.numeric = physics->numeric;
    host.edition = physics->options.q2_edition;
    host.world_gravity = physics->world_gravity;
    host.max_velocity = physics->options.has_max_velocity ? physics->options.max_velocity : 2000;
    host.stop_speed = physics->options.stop_speed ? physics->options.stop_speed(physics->options.ctx) : 100;
    host.team_slave = flags.has_team_slave && flags.team_slave;
    host.ctx = &env;
    host.water = toss_water; host.trace = ground_trace; host.hit_actor = trace_actor;
    host.fly_move = fly_move_noop;
    host.write_angular_velocity = no_op_vec; host.touch_triggers = no_op;
    host.point_contents = point_contents_fn; host.write_water = write_water; host.water_sound = no_op_vec;
    (void)qa_app_sim_step_q2_new_toss(actor, elapsed, motion, host);
    return;
  }
  if (is(kind, "push") || is(kind, "stop")) {
    if (physics->options.push_move) physics->options.push_move(physics->options.ctx, actor, elapsed, motion);
    return;
  }
  (void)family_of(physics, actor);
  if (physics->options.step_move) physics->options.step_move(physics->options.ctx, actor, elapsed, motion);
}
void qa_app_sim_shared_physics_touch_triggers(qa_app_sim_shared_physics *physics, qa_owned_actor actor) {
  (void)physics; (void)actor;
  /* Trigger touching runs through the callback table in the runtime
   * slice (qa_touch_q1_triggers services are caller-built). */
}
void qa_app_sim_shared_physics_commit_attachments(qa_app_sim_shared_physics *physics) { (void)physics; }
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
void qa_app_sim_shared_physics_restore_checkpoint(qa_app_sim_shared_physics *physics, const qa_save_reader *reader,
    bool require_exact_collisions, bool (*reconstructed)(void *ctx, qa_actor_id actor), void *reconstructed_ctx) {
  (void)require_exact_collisions; (void)reconstructed; (void)reconstructed_ctx;
  qa_save_reader gravity;
  if (!qa_save_reader_field(reader, "gravity", &gravity)) qa_save_reader_fail(reader, "out of memory");
  qa_app_sim_shared_physics_set_world_gravity(physics, qa_save_reader_finite(&gravity));
  qa_save_reader_destroy(&gravity);
  /* Full checkpoint tree restore (collisions/solids/motions/flags)
   * is driven by the runtime slice through the set_* APIs. */
}
