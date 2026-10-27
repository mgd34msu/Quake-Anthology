/*
 * Port of the scene-free numeric cases in tests/movement/q1/
 * movement.test.ts (the shared-sweep tests and q1PlayerJump values;
 * the BSP-corpus cases skip without ~/qfiles in the TS suite too),
 * plus exact branch cases for the small shared/q1 functions and a
 * stub-scene NetQuake free-fall whose binary32 results are checked
 * as bit patterns. TypeScript toThrow cases run in a child process
 * and must abort. GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/contracts/identity.h"
#include "qa/core/numeric.h"
#include "qa/movement/body_shape.h"
#include "qa/movement/client_outputs.h"
#include "qa/movement/q1.h"
#include "qa/movement/swept_body.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)

#define CHECK_ABORTS(...)                                               \
  do {                                                                  \
    checks++;                                                           \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */ \
      freopen("/dev/null", "w", stderr);                                \
      __VA_ARGS__;                                                      \
      _exit(0);                                                         \
    }                                                                   \
    int status = 0;                                                     \
    waitpid(pid, &status, 0);                                           \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {        \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,     \
              __LINE__, #__VA_ARGS__);                                  \
    }                                                                   \
  } while (0)

static uint32_t fbits(double value) {
  const float stored = (float)value;
  uint32_t bits;
  memcpy(&bits, &stored, sizeof bits);
  return bits;
}

static bool vec_eq(qa_vec3 value, double x, double y, double z) {
  return value.x == x && value.y == y && value.z == z;
}

static const qa_numeric_operations *donor_ops(void) {
  return qa_create_numeric_operations(&qa_q1_donor_profile);
}

/* --- shared sweep (tests/movement/q1/movement.test.ts) --- */

typedef struct sweep_case {
  qa_q1_movement_math math;
  qa_vec3 origin;
  qa_vec3 velocity;
  bool live;
  bool start_solid;
  bool remove_on_impact;
  int traces;
  int writes;
  int impacts;
  qa_trace_result native;
} sweep_case;

static bool sweep_read(void *data, qa_swept_body_state *out) {
  sweep_case *c = data;
  if (!c->live) return false;
  out->origin = c->origin;
  out->velocity = c->velocity;
  return true;
}
static void sweep_write_origin(void *data, qa_vec3 origin) { ((sweep_case *)data)->origin = origin; }
static void sweep_write_velocity(void *data, qa_vec3 velocity, qa_swept_velocity_components components) {
  sweep_case *c = data;
  (void)components;
  c->velocity = velocity;
  c->writes++;
}
static qa_vec3 math_advance(void *data, qa_vec3 origin, double time, qa_vec3 velocity) {
  return qa_q1_math_ma(data, origin, time, velocity);
}
static double math_remaining(void *data, double time, double fraction) {
  const qa_q1_movement_math *m = data;
  return qa_q1_n_subtract(m->n, time, qa_q1_n_multiply(m->n, time, fraction));
}
static qa_vec3 math_clip(void *data, qa_vec3 velocity, qa_vec3 normal) {
  return qa_q1_math_clip(data, velocity, normal, 1);
}
static double math_dot(void *data, qa_vec3 a, qa_vec3 b) { return qa_q1_math_dot(data, a, b); }
static qa_vec3 math_cross(void *data, qa_vec3 a, qa_vec3 b) { return qa_q1_math_cross(data, a, b); }
static qa_vec3 math_scale(void *data, qa_vec3 v, double amount) { return qa_q1_math_scale(data, v, amount); }
static bool math_same_plane(void *data, qa_vec3 a, qa_vec3 b, bool same_slot) {
  (void)data;
  (void)a;
  (void)b;
  return same_slot;
}
static qa_vec3 sweep_normal(void *data, const qa_swept_body_trace *trace) {
  (void)data;
  return qa_q1_trace_source_normal(trace->native);
}

static void init_sweep_case(sweep_case *c) {
  memset(c, 0, sizeof *c);
  qa_q1_movement_math_init(&c->math, donor_ops());
  c->origin = qa_q1_zero;
  const qa_vec3 velocity = {2, 2, 2};
  c->velocity = velocity;
  c->live = true;
  memset(&c->native, 0, sizeof c->native);
  c->native.kind = "q1";
  c->native.in_open = true;
  c->native.contact.has_plane = true;
  const qa_vec3 contact_normal = {0, 0, -1};
  c->native.contact.plane.normal = contact_normal;
}

static void sweep_trace_a(void *data, qa_vec3 start, qa_vec3 end, qa_swept_body_trace *out) {
  sweep_case *c = data;
  c->traces++;
  const double fraction = c->traces == 1 ? 0.25 : c->traces == 2 ? 0 : 1;
  c->native.fraction = fraction;
  c->native.end = qa_q1_math_ma(&c->math, start, fraction, qa_q1_math_sub(&c->math, end, start));
  c->native.hit = fraction == 1 ? qa_q1_none : qa_q1_world;
  const qa_vec3 first = {-1, 0, 0};
  const qa_vec3 second = {0, -1, 0};
  c->native.source_plane_q1.normal = c->traces == 1 ? first : second;
  out->fraction = fraction;
  out->end = c->native.end;
  out->all_solid = false;
  out->start_solid = false;
  out->native = &c->native;
}

static void sweep_impact_a(void *data, const qa_swept_body_trace *trace, qa_vec3 normal) {
  sweep_case *c = data;
  (void)trace;
  (void)normal;
  if (c->remove_on_impact) c->live = false;
  else if (c->traces == 2) {
    const qa_vec3 velocity = {0, 0, 7};
    c->velocity = velocity;
  }
}

static void test_sweep_a(void) {
  static const struct {
    bool remove;
    bool quakeworld;
  } variants[3] = {{false, false}, {true, false}, {false, true}};
  for (int v = 0; v < 3; v++) {
    sweep_case c;
    init_sweep_case(&c);
    c.remove_on_impact = variants[v].remove;
    qa_swept_body_services services;
    memset(&services, 0, sizeof services);
    services.ctx = &c;
    services.read = sweep_read;
    services.write_origin = sweep_write_origin;
    services.write_velocity = sweep_write_velocity;
    services.trace = sweep_trace_a;
    services.normal = sweep_normal;
    services.impact = sweep_impact_a;
    services.stop_when_still = true;
    services.same_plane = math_same_plane;
    services.has_collision_policy = true;
    services.stop_on_start_solid = variants[v].quakeworld;
    services.original_velocity = variants[v].quakeworld ? QA_SWEPT_ORIGINAL_INITIAL : QA_SWEPT_ORIGINAL_LAST_PROGRESS;
    services.crease_velocity = variants[v].quakeworld ? QA_SWEPT_CREASE_LAST_CANDIDATE : QA_SWEPT_CREASE_CURRENT;
    services.math.ctx = &c.math;
    services.math.advance = math_advance;
    services.math.remaining = math_remaining;
    services.math.clip = math_clip;
    services.math.dot = math_dot;
    services.math.cross = math_cross;
    services.math.scale = math_scale;
    const qa_sweep_stop stop = qa_sweep_body(&services, 1);
    CHECK(stop == (variants[v].remove ? QA_SWEEP_REMOVED : QA_SWEEP_COMPLETE));
    CHECK(c.traces == (variants[v].remove ? 1 : 3));
    CHECK(c.writes == (variants[v].remove ? 0 : 2));
    if (variants[v].remove) {
      CHECK(vec_eq(c.origin, 0.5, 0.5, 0.5));
      CHECK(vec_eq(c.velocity, 2, 2, 2));
    } else if (variants[v].quakeworld) {
      CHECK(vec_eq(c.origin, 0.5, 0.5, 2));
      CHECK(vec_eq(c.velocity, 0, 0, 2));
    } else {
      CHECK(vec_eq(c.origin, 0.5, 0.5, 5.75));
      CHECK(vec_eq(c.velocity, 0, 0, 7));
    }
  }
}

static void sweep_trace_b(void *data, qa_vec3 start, qa_vec3 end, qa_swept_body_trace *out) {
  sweep_case *c = data;
  c->traces++;
  const double fraction = c->traces <= 2 ? 0.25 : 1;
  c->native.fraction = fraction;
  c->native.end = qa_q1_math_ma(&c->math, start, fraction, qa_q1_math_sub(&c->math, end, start));
  c->native.hit = fraction == 1 ? qa_q1_none : qa_q1_world;
  c->native.contact.has_plane = false;
  const qa_vec3 first = {-1, 0, 0};
  const qa_vec3 second = {0, -1, 0};
  c->native.source_plane_q1.normal = c->traces == 1 ? first : second;
  out->fraction = fraction;
  out->end = c->native.end;
  out->all_solid = false;
  out->start_solid = c->start_solid;
  out->native = &c->native;
}

static void sweep_impact_b(void *data, const qa_swept_body_trace *trace, qa_vec3 normal) {
  sweep_case *c = data;
  (void)trace;
  (void)normal;
  c->impacts++;
}

static void test_sweep_b(void) {
  for (int solid = 0; solid < 2; solid++) {
    sweep_case c;
    init_sweep_case(&c);
    c.start_solid = solid != 0;
    qa_swept_body_services services;
    memset(&services, 0, sizeof services);
    services.ctx = &c;
    services.read = sweep_read;
    services.write_origin = sweep_write_origin;
    services.write_velocity = sweep_write_velocity;
    services.trace = sweep_trace_b;
    services.normal = sweep_normal;
    services.impact = sweep_impact_b;
    services.stop_when_still = false;
    services.same_plane = math_same_plane;
    services.has_collision_policy = true;
    services.stop_on_start_solid = true;
    services.original_velocity = QA_SWEPT_ORIGINAL_INITIAL;
    services.crease_velocity = QA_SWEPT_CREASE_LAST_CANDIDATE;
    services.math.ctx = &c.math;
    services.math.advance = math_advance;
    services.math.remaining = math_remaining;
    services.math.clip = math_clip;
    services.math.dot = math_dot;
    services.math.cross = math_cross;
    services.math.scale = math_scale;
    const qa_sweep_stop stop = qa_sweep_body(&services, 1);
    CHECK(stop == (solid ? QA_SWEEP_SOLID : QA_SWEEP_COMPLETE));
    CHECK(c.impacts == (solid ? 0 : 2));
    CHECK(c.traces == (solid ? 1 : 3));
    if (solid) {
      CHECK(vec_eq(c.origin, 0, 0, 0));
      CHECK(vec_eq(c.velocity, 0, 0, 0));
    } else {
      CHECK(vec_eq(c.origin, 1.625, 0.875, 2));
      CHECK(vec_eq(c.velocity, 2, 0, 2));
    }
  }
}

static bool clear_yes(void *ctx, qa_bounds bounds) {
  (void)ctx;
  (void)bounds;
  return true;
}
static bool clear_no(void *ctx, qa_bounds bounds) {
  int *calls = ctx;
  (*calls)++;
  (void)bounds;
  return false;
}

static void test_bounds_and_water(void) {
  const qa_bounds previous = {{-16, -16, -24}, {16, 16, 32}};
  const qa_bounds bigger = {{-20, -16, -24}, {16, 16, 32}};
  const qa_bounds smaller = {{-8, -16, -24}, {16, 16, 32}};
  int calls = 0;
  qa_bounds out = qa_movement_bounds(previous, bigger, clear_no, &calls);
  CHECK(out.min.x == -16 && calls == 1);
  out = qa_movement_bounds(previous, bigger, clear_yes, NULL);
  CHECK(out.min.x == -20);
  calls = 0;
  out = qa_movement_bounds(previous, smaller, clear_no, &calls);
  CHECK(out.min.x == -8 && calls == 0);

  qa_q1_water_transition_result t = qa_q1_water_transition(0, -3);
  CHECK(t.water_type == -3 && t.water_level == 1 && !t.splash);
  t = qa_q1_water_transition(-1, -4);
  CHECK(t.water_type == -4 && t.water_level == 1 && t.splash);
  t = qa_q1_water_transition(-3, -4);
  CHECK(t.water_type == -4 && t.water_level == 1 && !t.splash);
  t = qa_q1_water_transition(-3, -1);
  CHECK(t.water_type == -1 && t.water_level == -1 && t.splash);
  t = qa_q1_water_transition(-1, -1);
  CHECK(t.water_type == -1 && t.water_level == -1 && !t.splash);

  const qa_bounds point = qa_q1_shape_bounds((qa_trace_shape){"point", {{1, 2, 3}, {4, 5, 6}}});
  CHECK(vec_eq(point.min, 0, 0, 0) && vec_eq(point.max, 0, 0, 0));
  CHECK(qa_q1_seconds((qa_source_time){QA_SOURCE_TIME_SECONDS, 1.5}) == 1.5);
  CHECK(qa_q1_seconds((qa_source_time){QA_SOURCE_TIME_MILLISECONDS, 1500}) == 1.5);
  CHECK(qa_q1_ladder_wish_z(10, 1, 0, 300) == 10);
  CHECK(qa_q1_ladder_wish_z(10, 1, 0, 0) == 200);
  CHECK(qa_q1_ladder_wish_z(-10, 1, 0, 0) == -200);
  CHECK(qa_q1_ladder_wish_z(0, 0, 1, 0) == 200);
  CHECK(qa_q1_ladder_wish_z(0, 0, -1, 0) == -200);
  CHECK(qa_q1_ladder_wish_z(5, 0, 0, 0) == 5);
}

static void test_client_outputs(void) {
  qa_user_command command;
  memset(&command, 0, sizeof command);
  command.kind = QA_USER_COMMAND_Q2_RERELEASE;
  command.as.q2_rerelease.buttons = 3;
  qa_user_command out = qa_client_stance_command(command, true, true);
  CHECK(out.as.q2_rerelease.buttons == 19);
  out = qa_client_stance_command(out, true, false);
  CHECK(out.as.q2_rerelease.buttons == 3);
  out = qa_client_stance_command(command, false, true);
  CHECK(out.as.q2_rerelease.buttons == 3);

  memset(&command, 0, sizeof command);
  command.kind = QA_USER_COMMAND_QW;
  command.as.qw.up_move = 0;
  command.as.qw.buttons = 7;
  out = qa_client_stance_command(command, true, true);
  CHECK(out.as.qw.up_move == -1 && out.as.qw.buttons == 5);
  command.as.qw.up_move = -40;
  out = qa_client_stance_command(command, true, false);
  CHECK(out.as.qw.up_move == 0 && out.as.qw.buttons == 7);

  memset(&command, 0, sizeof command);
  command.kind = QA_USER_COMMAND_Q3;
  command.as.q3.up_move = 30;
  out = qa_client_stance_command(command, true, true);
  CHECK(out.as.q3.up_move == -30);

  memset(&command, 0, sizeof command);
  command.kind = QA_USER_COMMAND_Q1;
  CHECK_ABORTS((void)qa_client_stance_command(command, true, true));

  CHECK(qa_client_movement_type(QA_MOVEMENT_STATE_Q1, QA_MOD_CLIENT_MOVEMENT_NORMAL) == 3);
  CHECK(qa_client_movement_type(QA_MOVEMENT_STATE_Q1, QA_MOD_CLIENT_MOVEMENT_NOCLIP) == 8);
  CHECK(qa_client_movement_type(QA_MOVEMENT_STATE_Q1, QA_MOD_CLIENT_MOVEMENT_FREEZE) == 0);
  CHECK(qa_client_movement_type(QA_MOVEMENT_STATE_QW, QA_MOD_CLIENT_MOVEMENT_NOCLIP) == 1);
  CHECK(qa_client_movement_type(QA_MOVEMENT_STATE_QW, QA_MOD_CLIENT_MOVEMENT_NORMAL) == 0);
  CHECK(qa_client_movement_type(QA_MOVEMENT_STATE_Q2_RERELEASE, QA_MOD_CLIENT_MOVEMENT_FREEZE) == 6);
  CHECK(qa_client_movement_type(QA_MOVEMENT_STATE_Q2_RERELEASE, QA_MOD_CLIENT_MOVEMENT_NOCLIP) == 2);
  CHECK(qa_client_movement_type(QA_MOVEMENT_STATE_Q2, QA_MOD_CLIENT_MOVEMENT_FREEZE) == 4);
  CHECK(qa_client_movement_type(QA_MOVEMENT_STATE_Q3, QA_MOD_CLIENT_MOVEMENT_NORMAL) == 0);

  qa_mod_client_movement_outputs outputs;
  memset(&outputs, 0, sizeof outputs);
  outputs.has_mode = true;
  outputs.mode = QA_MOD_CLIENT_MOVEMENT_NOCLIP;
  qa_mod_client_movement_mode mode = QA_MOD_CLIENT_MOVEMENT_NORMAL;
  CHECK(qa_client_movement_mode(&outputs, 100, &mode) && mode == QA_MOD_CLIENT_MOVEMENT_NOCLIP);
  CHECK(!qa_client_movement_mode(&outputs, 0, &mode));
  CHECK(!qa_client_movement_mode(NULL, 100, &mode));
  outputs.has_mode = false;
  CHECK(!qa_client_movement_mode(&outputs, 100, &mode));
}

static qa_movement_services stub_services_base(void) {
  qa_movement_services services;
  memset(&services, 0, sizeof services);
  services.numeric = *donor_ops();
  return services;
}

static void test_player_jump(void) {
  const qa_movement_services services = stub_services_base();
  qa_q1_movement_state state;
  memset(&state, 0, sizeof state);
  state.flags = QA_Q1_FLAG_ONGROUND | QA_Q1_FLAG_JUMPRELEASED;
  qa_q1_jump_result result = qa_q1_player_jump(state, services);
  CHECK(strcmp(result.action, "jump") == 0);
  CHECK(result.state.velocity.z == 270);
  CHECK(fbits(result.state.velocity.z) == fbits(270));
  CHECK(result.state.flags == 0);

  state.velocity.z = 5;
  result = qa_q1_player_jump(state, services);
  CHECK(result.state.velocity.z == 275);

  memset(&state, 0, sizeof state);
  state.water_level = 2;
  state.water_type = QA_Q1_CONTENTS_WATER;
  result = qa_q1_player_jump(state, services);
  CHECK(strcmp(result.action, "swim") == 0 && result.state.velocity.z == 100);
  state.water_type = QA_Q1_CONTENTS_SLIME;
  CHECK(qa_q1_player_jump(state, services).state.velocity.z == 80);
  state.water_type = QA_Q1_CONTENTS_LAVA;
  CHECK(qa_q1_player_jump(state, services).state.velocity.z == 50);

  memset(&state, 0, sizeof state);
  state.flags = QA_Q1_FLAG_WATERJUMP | QA_Q1_FLAG_ONGROUND | QA_Q1_FLAG_JUMPRELEASED;
  result = qa_q1_player_jump(state, services);
  CHECK(strcmp(result.action, "none") == 0);
  state.flags = QA_Q1_FLAG_ONGROUND;
  CHECK(strcmp(qa_q1_player_jump(state, services).action, "none") == 0);
}

static void test_slices_and_math(void) {
  qa_qw_user_command command;
  memset(&command, 0, sizeof command);
  command.milliseconds = 53;
  command.impulse = 9;
  size_t count = 0;
  qa_qw_user_command *slices = qa_quakeworld_command_slices(command, 50, &count);
  CHECK(count == 2);
  CHECK(slices[0].milliseconds == 26 && slices[0].impulse == 9);
  CHECK(slices[1].milliseconds == 26 && slices[1].impulse == 0);
  free(slices);
  command.milliseconds = 255;
  slices = qa_quakeworld_command_slices(command, 50, &count);
  CHECK(count == 8);
  for (size_t i = 0; i < count; i++) CHECK(slices[i].milliseconds == 31);
  CHECK(slices[0].impulse == 9 && slices[1].impulse == 0 && slices[7].impulse == 0);
  free(slices);
  command.milliseconds = 20;
  slices = qa_quakeworld_command_slices(command, 50, &count);
  CHECK(count == 1 && slices[0].milliseconds == 20);
  free(slices);
  command.milliseconds = 256;
  CHECK_ABORTS(free(qa_quakeworld_command_slices(command, 50, &count)));
  command.milliseconds = 20;
  CHECK_ABORTS(free(qa_quakeworld_command_slices(command, 0, &count)));

  qa_q1_movement_math math;
  qa_q1_movement_math_init(&math, donor_ops());
  const qa_vec3 stored = qa_q1_math_vec(&math, 1.1, -0.0, 2);
  CHECK(stored.x == (double)(float)1.1);
  CHECK(fbits(stored.x) == 0x3f8ccccdu);
  CHECK(fbits(stored.y) == 0x80000000u);
  const qa_vec3 velocity = {2, 2, 2};
  const qa_vec3 normal = {-1, 0, 0};
  CHECK(vec_eq(qa_q1_math_clip(&math, velocity, normal, 1), 0, 2, 2));
  const qa_vec3 tiny = {0.05, 3, 0};
  const qa_vec3 up = {0, 0, 1};
  CHECK(vec_eq(qa_q1_math_clip(&math, tiny, up, 1), 0, 3, 0));
  CHECK(qa_q1_math_dot(&math, velocity, normal) == -2);
  CHECK(qa_q1_math_horizontal(&math, (qa_vec3){3, 4, 99}) == 5);
  const qa_q1_normalized zero = qa_q1_math_normalize(&math, qa_q1_zero);
  CHECK(zero.length == 0 && vec_eq(zero.direction, 0, 0, 0));
}

/* --- stub-scene NetQuake free fall --- */

static qa_trace_result empty_trace(void *ctx, qa_trace_query query) {
  (void)ctx;
  qa_trace_result result;
  memset(&result, 0, sizeof result);
  result.kind = "q1";
  result.fraction = 1;
  result.end = query.end;
  result.hit = qa_q1_none;
  return result;
}
static qa_point_contents_result empty_contents(void *ctx, qa_point_contents_query query) {
  (void)ctx;
  (void)query;
  qa_point_contents_result result;
  memset(&result, 0, sizeof result);
  result.kind = "q1";
  result.contents = QA_Q1_CONTENTS_EMPTY;
  return result;
}
static qa_movement_continuation keep_touch(void *ctx, qa_movement_touch_contact contact, qa_movement_state state) {
  (void)ctx;
  (void)contact;
  qa_movement_continuation next;
  memset(&next, 0, sizeof next);
  next.is_continue = true;
  next.state = state;
  return next;
}
static qa_weapon_step_result keep_weapon(void *ctx, qa_weapon_step_input input, qa_movement_state state) {
  (void)ctx;
  (void)state;
  qa_weapon_step_result result;
  memset(&result, 0, sizeof result);
  result.arsenal = input.arsenal;
  result.animation = input.animation;
  return result;
}
static qa_animation_step_result keep_animation(void *ctx, qa_animation_step_input input) {
  (void)ctx;
  qa_animation_step_result result;
  memset(&result, 0, sizeof result);
  result.animation = input.animation;
  return result;
}

static void test_netquake_freefall(void) {
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "q1-movement-test"));
  const qa_owned_actor actor = qa_identity_owner_owned_actor(&owner, qa_identity_owner_actor(&owner, 1, 0), "q1:test");
  qa_movement_services services = stub_services_base();
  services.scene.trace = empty_trace;
  services.scene.point_contents = empty_contents;
  services.touch = keep_touch;
  services.weapon_step = keep_weapon;
  services.animation_step = keep_animation;

  qa_q1_movement_input input;
  memset(&input, 0, sizeof input);
  input.kind = "q1-netquake";
  input.actor = actor;
  input.execution = "authoritative";
  input.frame.time = (qa_source_time){QA_SOURCE_TIME_SECONDS, 1};
  input.frame.elapsed = (qa_source_time){QA_SOURCE_TIME_SECONDS, 0.02};
  input.shape.kind = "box";
  input.shape.bounds = (qa_bounds){{-15, -15, -24}, {15, 15, 32}};
  input.environment.health = 100;
  input.environment.gravity_multiplier = 1;
  input.state.kind = QA_MOVEMENT_STATE_Q1;
  input.state.as.q1.origin = (qa_vec3){0, 0, 100};
  input.state.as.q1.old_origin = input.state.as.q1.origin;
  input.state.as.q1.move_type = QA_Q1_MOVE_WALK;
  input.state.as.q1.flags = QA_Q1_FLAG_JUMPRELEASED;
  input.state.as.q1.ground = qa_q1_none;
  input.state.as.q1.water_type = QA_Q1_CONTENTS_EMPTY;
  input.state.as.q1.health = 100;
  input.command.kind = QA_USER_COMMAND_Q1;
  input.profile.kind = "q1-netquake";
  input.profile.edition = "classic";
  input.profile.numeric = qa_q1_donor_profile;
  input.profile.clock.kind = QA_CLOCK_Q1_NETQUAKE;
  input.profile.parameters = (qa_q1_movement_parameters){800, 100, 320, 500, 10, 0.7, 10, 4, 4, 1};
  input.profile.edge_friction = 2;

  qa_q1_movement_options options;
  memset(&options, 0, sizeof options);
  const qa_movement_provider provider = qa_create_q1_movement_provider("q1:classic", options);
  CHECK(strcmp(provider.kind, "q1-netquake") == 0);
  qa_movement_result result = provider.move(provider.ctx, input, services);
  CHECK(strcmp(result.status, "active") == 0);
  CHECK(strcmp(result.kind, "q1-netquake") == 0);
  CHECK(result.state.as.q1.velocity.z == -16);
  CHECK(fbits(result.state.as.q1.velocity.z) == fbits(-16));
  CHECK(result.state.as.q1.origin.z < 100);
  CHECK(result.state.as.q1.origin.z == (double)(float)(100.0 - 0.32));
  CHECK(result.ground.kind == QA_TRACE_HIT_NONE);
  CHECK(result.effect_count == 0);
  const qa_q1_movement_state first = result.state.as.q1;
  free((void *)result.contacts);
  free((void *)result.effects);

  input.state.as.q1 = first;
  result = provider.move(provider.ctx, input, services);
  CHECK(result.state.as.q1.velocity.z == -32);
  CHECK(fbits(result.state.as.q1.velocity.z) == fbits(-32));
  free((void *)result.contacts);
  free((void *)result.effects);
  free(provider.ctx);
  qa_identity_owner_destroy(&owner);
}

int main(void) {
  test_sweep_a();
  test_sweep_b();
  test_bounds_and_water();
  test_client_outputs();
  test_player_jump();
  test_slices_and_math();
  test_netquake_freefall();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
