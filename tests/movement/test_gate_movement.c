/*
 * Confirmation-gate full-read checks for src/movement (gate-world). Expected
 * values come from tests/movement/oracles/gate_movement.ts run under bun
 * (output in gate_movement.txt). GPL-2.0-or-later.
 */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/contracts/identity.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/client_outputs.h"
#include "qa/movement/q1/pusher.h"
#include "qa/movement/q3/prediction.h"
#include "qa/movement/swept_body.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)

/* ---- 1. sweepBody: a NaN candidate/plane dot rejects the candidate. ---- */
typedef struct sweep_fixture {
  qa_swept_body_state state;
  int bump;
} sweep_fixture;
static const qa_vec3 sweep_normals[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
static bool sf_read(void *ctx, qa_swept_body_state *out) {
  *out = ((sweep_fixture *)ctx)->state;
  return true;
}
static void sf_write_origin(void *ctx, qa_vec3 origin) { ((sweep_fixture *)ctx)->state.origin = origin; }
static void sf_write_velocity(void *ctx, qa_vec3 velocity, qa_swept_velocity_components components) {
  (void)components;
  ((sweep_fixture *)ctx)->state.velocity = velocity;
}
static void sf_trace(void *ctx, qa_vec3 start, qa_vec3 end, qa_swept_body_trace *out) {
  (void)start;
  (void)end;
  sweep_fixture *f = ctx;
  memset(out, 0, sizeof *out);
  out->fraction = 0;
  out->end = f->state.origin;
  out->native = (void *)&sweep_normals[f->bump++];
}
static qa_vec3 sf_normal(void *ctx, const qa_swept_body_trace *trace) {
  (void)ctx;
  return *(const qa_vec3 *)trace->native;
}
static void sf_impact(void *ctx, const qa_swept_body_trace *trace, qa_vec3 normal) {
  (void)ctx;
  (void)trace;
  (void)normal;
}
static bool sf_same_plane(void *ctx, qa_vec3 a, qa_vec3 b, bool same_slot) {
  (void)ctx;
  (void)a;
  (void)b;
  return same_slot;
}
static double sm_dot(void *ctx, qa_vec3 a, qa_vec3 b) {
  (void)ctx;
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
static qa_vec3 sm_advance(void *ctx, qa_vec3 origin, double time, qa_vec3 velocity) {
  (void)ctx;
  (void)time;
  (void)velocity;
  return origin;
}
static double sm_remaining(void *ctx, double time, double fraction) {
  (void)ctx;
  (void)fraction;
  return time;
}
static qa_vec3 sm_clip(void *ctx, qa_vec3 v, qa_vec3 n) {
  const double d = sm_dot(ctx, v, n);
  const qa_vec3 out = {v.x - n.x * d, v.y - n.y * d, v.z - n.z * d};
  return out;
}
static qa_vec3 sm_cross(void *ctx, qa_vec3 a, qa_vec3 b) {
  (void)ctx;
  const qa_vec3 out = {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
  return out;
}
static qa_vec3 sm_scale(void *ctx, qa_vec3 v, double s) {
  (void)ctx;
  const qa_vec3 out = {v.x * s, v.y * s, v.z * s};
  return out;
}

static void test_sweep_nan_candidate(void) {
  sweep_fixture f;
  memset(&f, 0, sizeof f);
  f.state.velocity.x = NAN;
  qa_swept_body_services s;
  memset(&s, 0, sizeof s);
  s.ctx = &f;
  s.read = sf_read;
  s.write_origin = sf_write_origin;
  s.write_velocity = sf_write_velocity;
  s.trace = sf_trace;
  s.normal = sf_normal;
  s.impact = sf_impact;
  s.same_plane = sf_same_plane;
  s.math.advance = sm_advance;
  s.math.remaining = sm_remaining;
  s.math.clip = sm_clip;
  s.math.dot = sm_dot;
  s.math.cross = sm_cross;
  s.math.scale = sm_scale;
  /* bun: sweep_nan_stop crease-blocked bumps 3 */
  CHECK(qa_sweep_body(&s, 1) == QA_SWEEP_CREASE_BLOCKED);
  CHECK(f.bump == 3);
}

/* ---- 2. clientMovementMode with NaN health. ---- */
static void test_client_mode_nan_health(void) {
  qa_mod_client_movement_outputs outputs;
  memset(&outputs, 0, sizeof outputs);
  outputs.has_mode = true;
  outputs.mode = QA_MOD_CLIENT_MOVEMENT_NOCLIP;
  qa_mod_client_movement_mode mode;
  /* bun: mode_nan_health undefined, mode_live_health noclip */
  CHECK(!qa_client_movement_mode(&outputs, NAN, &mode));
  CHECK(qa_client_movement_mode(&outputs, 1, &mode) && mode == QA_MOD_CLIENT_MOVEMENT_NOCLIP);
}

/* ---- 3. pushQ1Pusher: the finally re-enables collision when push throws. ---- */
typedef struct pusher_fixture {
  qa_identity_owner owner;
  qa_actor_id pusher, rider;
  char calls[128];
} pusher_fixture;
static qa_q1_physics_entity pf_entity(qa_actor_id id, double move_type) {
  qa_q1_physics_entity e;
  memset(&e, 0, sizeof e);
  e.actor.id = id;
  e.state.move_type = move_type;
  e.state.ground.kind = QA_TRACE_HIT_NONE;
  const qa_bounds b = {{-1, -1, -1}, {1, 1, 1}};
  e.bounds = b;
  e.absolute_bounds = b;
  e.solid = "box";
  return e;
}
static bool pf_read(void *ctx, qa_actor_id actor, qa_q1_physics_entity *out) {
  pusher_fixture *f = ctx;
  *out = actor.slot == 1 ? pf_entity(f->pusher, 7) : pf_entity(f->rider, 3);
  return true;
}
static const qa_actor_id *pf_candidates(void *ctx, size_t *count) {
  *count = 1;
  return &((pusher_fixture *)ctx)->rider;
}
static void pf_write(void *ctx, qa_q1_physics_entity entity) {
  (void)ctx;
  (void)entity;
}
static void pf_link(void *ctx, qa_owned_actor actor, bool touch) {
  (void)ctx;
  (void)actor;
  (void)touch;
}
static void pf_collision(void *ctx, qa_owned_actor actor, bool enabled) {
  (void)actor;
  pusher_fixture *f = ctx;
  strcat(f->calls, enabled ? "collision true," : "collision false,");
}
static qa_trace_hit pf_test_position(void *ctx, qa_q1_physics_entity entity) {
  (void)ctx;
  (void)entity;
  qa_trace_hit hit;
  memset(&hit, 0, sizeof hit);
  hit.kind = QA_TRACE_HIT_WORLD;
  return hit;
}
static qa_q1_push_result pf_push(void *ctx, qa_q1_physics_entity entity, qa_vec3 displacement) {
  (void)entity;
  (void)displacement;
  strcat(((pusher_fixture *)ctx)->calls, "push,");
  qa_raise("Error", 0, 0, "trigger failed");
}
static void pf_blocked(void *ctx, qa_owned_actor pusher, qa_actor_id obstacle) {
  (void)ctx;
  (void)pusher;
  (void)obstacle;
}
static void pf_run(void *ctx) {
  pusher_fixture *f = ctx;
  qa_q1_pusher_services s;
  memset(&s, 0, sizeof s);
  s.ctx = f;
  s.numeric = *qa_create_numeric_operations(&qa_q1_donor_profile);
  s.read = pf_read;
  s.candidates = pf_candidates;
  s.write = pf_write;
  s.link = pf_link;
  s.collision_enabled = pf_collision;
  s.test_position = pf_test_position;
  s.push = pf_push;
  s.blocked = pf_blocked;
  const qa_q1_push_input input = {f->pusher, {1, 0, 0}, {0, 0, 0}, 0.1};
  (void)qa_push_q1_pusher(input, s);
}
static void test_pusher_finally(void) {
  pusher_fixture f;
  memset(&f, 0, sizeof f);
  CHECK(qa_identity_owner_create(&f.owner, "gate"));
  f.pusher = qa_identity_owner_actor(&f.owner, 1, 1);
  f.rider = qa_identity_owner_actor(&f.owner, 2, 1);
  qa_caught_error caught;
  const bool ok = qa_try(pf_run, &f, &caught);
  /* bun: pusher_finally collision false,push,collision true caught trigger failed */
  CHECK(!ok);
  CHECK(strcmp(caught.message, "trigger failed") == 0);
  CHECK(strcmp(f.calls, "collision false,push,collision true,") == 0);
  qa_identity_owner_destroy(&f.owner);
}

/* ---- 4. Q3 prediction replay ends after command number INT32_MAX. ---- */
static int pr_reads;
static double pr_current(void *ctx) {
  (void)ctx;
  return 2147483647.0;
}
static bool pr_read(void *ctx, double number, qa_q3_user_command *out) {
  (void)ctx;
  (void)number;
  pr_reads++;
  memset(out, 0, sizeof *out);
  return true;
}
static qa_q3_prediction_settings pr_settings(void *ctx) {
  (void)ctx;
  qa_q3_prediction_settings s;
  memset(&s, 0, sizeof s);
  s.movement_milliseconds = 8;
  return s;
}
static void pr_set_msec(void *ctx, double value) {
  (void)ctx;
  (void)value;
}
static void pr_restore(void *ctx, qa_q3_prediction_snapshot snapshot) {
  (void)ctx;
  (void)snapshot;
}
static void pr_warn(void *ctx, const char *message) {
  (void)ctx;
  (void)message;
}
static void test_prediction_int32_max(void) {
  qa_q3_prediction_host host;
  memset(&host, 0, sizeof host);
  host.commands.ctx = NULL;
  host.commands.current_number = pr_current;
  host.commands.read = pr_read;
  host.settings = pr_settings;
  host.set_movement_milliseconds = pr_set_msec;
  host.restore_snapshot = pr_restore;
  host.warn = pr_warn;
  qa_q3_prediction_runtime runtime;
  qa_q3_prediction_runtime_init(&runtime, host);
  qa_q3_prediction_frame frame;
  memset(&frame, 0, sizeof frame);
  frame.health = 100;
  const qa_q3_prediction_output out = qa_q3_prediction_predict(&runtime, frame);
  /* bun: prediction_int32_max unchanged reads 66 */
  CHECK(strcmp(out.status, "unchanged") == 0);
  CHECK(pr_reads == 66);
  free(out.effects);
}

int main(void) {
  test_sweep_nan_candidate();
  test_client_mode_nan_health();
  test_pusher_finally();
  test_prediction_int32_max();
  printf("test_gate_movement: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
