/*
 * Port of the scene-free cases in tests/movement/q2/movement.test.ts
 * (the shared-sweep cases; the retail-corpus cases skip without
 * ~/qfiles in the TS suite too), plus exact binary32 cases for the
 * q2 math/view/dimensions helpers and stub-trace classic/rerelease
 * pmoves whose expected values come from the TypeScript oracle run
 * under bun with the q2:binary32 profile. TypeScript toThrow cases
 * run in a child process and must abort. GPL-2.0-or-later.
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
#include "qa/contracts/world.h"
#include "qa/core/numeric.h"
#include "qa/movement/q2.h"

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

static const qa_numeric_profile q2_binary32_profile = {
    "q2:binary32",
    {QA_ARITHMETIC_BINARY32, NULL, 0, QA_FLOATING_ROUNDING_NEAREST_EVEN, false, false},
    QA_FLOAT_TO_INT_CHECKED_C_TRUNCATION};

static const qa_numeric_operations *binary32_ops(void) {
  return qa_create_numeric_operations(&q2_binary32_profile);
}

static void test_types(void) {
  const qa_q2_cplane plane = qa_q2_plane();
  CHECK(plane.normal[0] == 0 && plane.normal[1] == 0 && plane.normal[2] == 0);
  CHECK(plane.dist == 0 && plane.type == 0 && plane.signbits == 0);
  CHECK(qa_q2_axes[0] == 0 && qa_q2_axes[1] == 1 && qa_q2_axes[2] == 2);
  CHECK(qa_q2_pm_config_default.airaccel == 0 && !qa_q2_pm_config_default.n64_physics);
  CHECK(QA_Q2_MASK_SOLID == 3);
  CHECK(QA_Q2_MASK_DEADSOLID == (3 | (1 << 16)));
  CHECK(QA_Q2_MASK_PLAYERSOLID == (3 | (1 << 16) | (1 << 25) | (1 << 30)));
  CHECK(QA_Q2_MASK_CLASSIC_PLAYERSOLID == (3 | (1 << 16) | (1 << 25)));
  CHECK(QA_Q2_MASK_WATER == (32 | 8 | 16));
  CHECK(QA_Q2_PMF_ON_LADDER == 128 && QA_Q2_PMF_TIME_TRICK == 1024);
  CHECK(QA_Q2_BUTTON_JUMP == 8 && QA_Q2_BUTTON_CROUCH == 16);
  const double values[3] = {10, 20, 30};
  CHECK(qa_q2_element(values, 3, 0) == 10);
  CHECK(qa_q2_element(values, 3, 2) == 30);
  CHECK_ABORTS((void)qa_q2_element(values, 3, 3));
  CHECK_ABORTS((void)qa_q2_element(values, 3, -1));
  CHECK_ABORTS((void)qa_q2_element(values, 3, 1.5));
}

static void test_dimensions(void) {
  const qa_numeric_operations *n = binary32_ops();
  CHECK(qa_q2_player_bounds.min.x == -16 && qa_q2_player_bounds.min.z == -24);
  CHECK(qa_q2_player_bounds.max.x == 16 && qa_q2_player_bounds.max.z == 32);
  CHECK(fbits(qa_q2_character_height(qa_q2_player_bounds, -24, n)) == 0xc1c00000u);
  CHECK(fbits(qa_q2_character_height(qa_q2_player_bounds, -2, n)) == 0xc0000000u);
  CHECK(fbits(qa_q2_character_height(qa_q2_player_bounds, 22, n)) == 0x41b00000u);
  CHECK(fbits(qa_q2_character_height(qa_q2_player_bounds, 32, n)) == 0x42000000u);
  const qa_bounds custom = {{-15, -15, -20}, {15, 15, 40}};
  CHECK(fbits(qa_q2_character_height(custom, -2, n)) == 0x40649248u);
  CHECK(fbits(qa_q2_character_height(custom, 22, n)) == 0x41ea4924u);
  const qa_trace_shape shape = qa_q2_player_shape();
  CHECK(strcmp(shape.kind, "box") == 0);
  CHECK(shape.bounds.min.z == -24 && shape.bounds.max.z == 32);
}

static void test_math(void) {
  const qa_numeric_operations *n = binary32_ops();
  qa_q2_movement_math math;
  qa_q2_movement_math_init(&math, n, QA_Q2_MATH_CLASSIC);
  qa_q2_vec3 v;
  qa_q2_math_vec3(&math, 0.1, -2.5, 300, v);
  CHECK(fbits(v[0]) == 0x3dcccccdU && fbits(v[1]) == 0xc0200000U && fbits(v[2]) == 0x43960000U);
  const qa_q2_vec3 a = {1.5, -2.25, 3.125};
  const qa_q2_vec3 b = {0.1, 0.2, 0.3};
  CHECK(fbits(qa_q2_math_dot_product(&math, a, b)) == 0x3f233334U);
  CHECK(qa_q2_math_vec3_dot(&math, a, b) == qa_q2_math_dot_product(&math, a, b));
  qa_q2_vec3 norm = {3, 4, 12};
  CHECK(fbits(qa_q2_math_vector_normalize(&math, norm)) == 0x41500000U);
  CHECK(fbits(norm[0]) == 0x3e6c4ec6U && fbits(norm[1]) == 0x3e9d89d9U);
  CHECK(fbits(norm[2]) == 0x3f6c4ec6U);
  const qa_q2_vec3 len_v = {1.0 / 3.0, 2.0 / 3.0, 7};
  CHECK(fbits(qa_q2_math_vector_length(&math, len_v)) == 0x40e1442aU);
  const qa_q2_vec3 c1 = {1, 2, 3};
  const qa_q2_vec3 c2 = {-4, 5, -6};
  qa_q2_vec3 cross;
  qa_q2_math_vec3_cross(&math, c1, c2, cross);
  CHECK(fbits(cross[0]) == 0xc1d80000U && fbits(cross[1]) == 0xc0c00000U);
  CHECK(fbits(cross[2]) == 0x41500000U);
  const qa_q2_vec3 vel = {10, 10, 10};
  const qa_q2_vec3 nrm = {(double)(float)-0.6, (double)(float)-0.8, 0};
  qa_q2_vec3 clipped;
  qa_q2_math_slide_clip_velocity(&math, vel, nrm, 1.01, clipped);
  CHECK(fbits(clipped[0]) == 0x3fc20c48U && fbits(clipped[1]) == 0xbfa7ef98U);
  CHECK(fbits(clipped[2]) == 0x41200000U);
  const qa_q2_vec3 vel2 = {100, -50, 25};
  const qa_q2_vec3 up_n = {0, 0, 1};
  qa_q2_math_slide_clip_velocity(&math, vel2, up_n, 1.01, clipped);
  CHECK(fbits(clipped[0]) == 0x42c80000U && fbits(clipped[1]) == 0xc2480000U);
  CHECK(fbits(clipped[2]) == 0xbe800000U);
  const qa_q2_vec3 angles = {30, 45, 10};
  qa_q2_vec3 forward, right, up;
  qa_q2_math_angle_vectors(&math, angles, forward, right, up);
  CHECK(fbits(forward[0]) == 0x3f1cc470U && fbits(forward[2]) == 0xbf000000U);
  CHECK(fbits(right[0]) == 0x3f228d6aU && fbits(right[1]) == 0xbf41fc70U);
  CHECK(fbits(right[2]) == 0xbe19fe31U);
  CHECK(fbits(up[0]) == 0x3ef122f8U && fbits(up[1]) == 0x3e66cdc4U);
  CHECK(fbits(up[2]) == 0x3f5a5597U);
  qa_q2_movement_math rr_math;
  qa_q2_movement_math_init(&rr_math, n, QA_Q2_MATH_RERELEASE);
  qa_q2_vec3 forward2, right2, up2;
  qa_q2_math_angle_vectors(&rr_math, angles, forward2, right2, up2);
  CHECK(fbits(forward2[0]) == 0x3f1cc470U && fbits(up2[2]) == 0x3f5a5597U);
  qa_q2_vec4 blend = {0.1, 0.2, 0.3, 0.4};
  qa_q2_math_g_add_blend(&math, 0.5, 0.3, 0.2, 0.4, blend);
  CHECK(fbits(blend[0]) == 0x3e800000U && fbits(blend[1]) == 0x3e733333U);
  CHECK(fbits(blend[2]) == 0x3e866666U && fbits(blend[3]) == 0x3f23d70aU);
  qa_q2_vec4 untouched = {0.1, 0.2, 0.3, 0.4};
  qa_q2_math_g_add_blend(&math, 1, 1, 1, 0, untouched);
  CHECK(untouched[3] == 0.4);
  CHECK(fbits(qa_q2_math_short2angle(&math, 8192)) == 0x42340000U);
  CHECK(fbits(qa_q2_math_short2angle(&math, -16384)) == 0xc2b40000U);
  CHECK(qa_q2_math_clamp(&math, 250.5, -200, 200) == 200);
  CHECK(isnan(qa_q2_math_clamp(&math, NAN, -200, 200)));
}

static void test_view(void) {
  const qa_numeric_operations *n = binary32_ops();
  qa_q2_vec3 view = {0, 0, 0};
  const qa_q2_vec3 angles = {1820, 8192, -910};
  const qa_q2_vec3 delta = {100, -200, 50};
  qa_q2_classic_view_angles(view, angles, delta, 0, n);
  CHECK(fbits(view[0]) == 0x4128c000U && fbits(view[1]) == 0x422f9b00U);
  CHECK(fbits(view[2]) == 0xc0972c00U);
  const qa_q2_vec3 t_angles = {0, 16384, 0};
  const qa_q2_vec3 t_delta = {0, 4096, 0};
  qa_q2_classic_view_angles(view, t_angles, t_delta, QA_Q2_PMF_TIME_TELEPORT, n);
  CHECK(view[0] == 0 && fbits(view[1]) == 0x42e10000U && view[2] == 0);
  const qa_q2_vec3 p_angles = {20000, 0, 0};
  const qa_q2_vec3 zero3 = {0, 0, 0};
  qa_q2_classic_view_angles(view, p_angles, zero3, 0, n);
  CHECK(fbits(view[0]) == 0x42b20000U);
  const qa_q2_vec3 r_angles = {45.5, -90.25, 3.5};
  const qa_q2_vec3 r_delta = {1.25, 0.5, -1};
  qa_q2_rerelease_view_angles(view, r_angles, r_delta, 0, n);
  CHECK(fbits(view[0]) == 0x423b0000U && fbits(view[1]) == 0xc2b38000U);
  CHECK(fbits(view[2]) == 0x40200000U);
  const qa_q2_vec3 rp_angles = {120, 0, 0};
  const qa_q2_vec3 rp_delta = {100, 0, 0};
  qa_q2_rerelease_view_angles(view, rp_angles, rp_delta, 0, n);
  CHECK(fbits(view[0]) == 0x43878000U);
}

/* --- shared sweep (tests/movement/q2/movement.test.ts) --- */

typedef struct sweep_case {
  qa_q2_movement_math math;
  qa_q2_vec3 *origin;
  bool allsolid;
  int traces;
  int touches;
} sweep_case;

static qa_q2_trace sweep_trace(void *data, qa_q2_vec3 start, qa_q2_vec3 end) {
  sweep_case *c = data;
  (void)start;
  c->traces++;
  qa_q2_trace trace;
  memset(&trace, 0, sizeof trace);
  trace.allsolid = c->allsolid;
  trace.startsolid = c->allsolid;
  trace.fraction = c->traces <= 2 ? 0 : 1;
  if (c->traces <= 2)
    memcpy(trace.endpos, *c->origin, sizeof trace.endpos);
  else
    memcpy(trace.endpos, end, sizeof trace.endpos);
  trace.plane = qa_q2_plane();
  if (c->traces == 1) {
    trace.plane.normal[0] = -1;
  } else {
    trace.plane.normal[0] = (double)(float)-0.6;
    trace.plane.normal[1] = (double)(float)-0.8;
  }
  trace.contents = 0;
  trace.has_ent = true;
  trace.ent.kind = QA_TRACE_HIT_WORLD;
  trace.ent.model = 0;
  trace.plane2 = qa_q2_plane();
  trace.source.kind = "q1";
  trace.source.in_open = true;
  trace.source.hit.kind = QA_TRACE_HIT_WORLD;
  return trace;
}

static void sweep_clip(void *data, qa_q2_vec3 velocity, qa_q2_vec3 normal, qa_q2_vec3 out) {
  sweep_case *c = data;
  qa_q2_math_slide_clip_velocity(&c->math, velocity, normal, 1.01, out);
}

static void sweep_touch(void *data, qa_q2_trace *trace) {
  sweep_case *c = data;
  (void)trace;
  c->touches++;
}

static void test_sweep(void) {
  const qa_numeric_operations *n = binary32_ops();
  for (int variant = 0; variant < 2; variant++) {
    const bool allsolid = variant == 1;
    qa_q2_vec3 origin = {0, 0, 0};
    qa_q2_vec3 velocity;
    if (allsolid) {
      velocity[0] = 1.0 / 3.0;
      velocity[1] = 2.0 / 3.0;
      velocity[2] = 7;
    } else {
      velocity[0] = 10;
      velocity[1] = 10;
      velocity[2] = 10;
    }
    sweep_case c;
    memset(&c, 0, sizeof c);
    qa_q2_movement_math_init(&c.math, n, QA_Q2_MATH_CLASSIC);
    c.origin = &origin;
    c.allsolid = allsolid;
    qa_q2_body_sweep options;
    memset(&options, 0, sizeof options);
    options.ctx = &c;
    options.origin = &origin;
    options.velocity = &velocity;
    options.elapsed = 1;
    options.numeric = n;
    options.trace = sweep_trace;
    options.clip = sweep_clip;
    options.touch = sweep_touch;
    const qa_sweep_stop stop = qa_sweep_q2_body(&options);
    CHECK(stop == (allsolid ? QA_SWEEP_SOLID : QA_SWEEP_COMPLETE));
    CHECK(c.touches == (allsolid ? 0 : 2));
    if (allsolid) {
      CHECK(velocity[0] == 1.0 / 3.0 && velocity[1] == 2.0 / 3.0 && velocity[2] == 0);
      CHECK(origin[0] == 0 && origin[1] == 0 && origin[2] == 0);
    } else {
      CHECK(fbits(velocity[0]) == 0xc09b22d1U && fbits(velocity[1]) == 0x40624dd2U);
      CHECK(fbits(velocity[2]) == 0x41200000U);
      CHECK(fbits(origin[0]) == 0xc09b22d1U && fbits(origin[1]) == 0x40624dd2U);
      CHECK(velocity[0] < -4 && velocity[1] > 3 && velocity[2] == 10);
    }
  }
}

/* --- stub-trace pmoves (free space) --- */

static qa_q2_trace free_trace(qa_q2_vec3 end) {
  qa_q2_trace trace;
  memset(&trace, 0, sizeof trace);
  trace.fraction = 1;
  memcpy(trace.endpos, end, sizeof trace.endpos);
  trace.plane = qa_q2_plane();
  trace.plane2 = qa_q2_plane();
  trace.source.kind = "q1";
  trace.source.in_open = true;
  trace.source.hit.kind = QA_TRACE_HIT_WORLD;
  return trace;
}

static qa_q2_trace classic_free_trace(void *data, qa_q2_vec3 start, qa_q2_vec3 mins,
                                      qa_q2_vec3 maxs, qa_q2_vec3 end) {
  (void)data;
  (void)start;
  (void)mins;
  (void)maxs;
  return free_trace(end);
}

static qa_q2_trace kex_free_trace(void *data, qa_q2_vec3 start, qa_q2_vec3 mins, qa_q2_vec3 maxs,
                                  qa_q2_vec3 end, bool has_pass, qa_q2_movement_entity pass,
                                  double mask) {
  (void)data;
  (void)start;
  (void)mins;
  (void)maxs;
  (void)has_pass;
  (void)pass;
  (void)mask;
  return free_trace(end);
}

static qa_q2_trace kex_free_clip(void *data, qa_q2_vec3 start, qa_q2_vec3 mins, qa_q2_vec3 maxs,
                                 qa_q2_vec3 end, double mask) {
  (void)data;
  (void)start;
  (void)mins;
  (void)maxs;
  (void)mask;
  return free_trace(end);
}

static double zero_contents(void *data, qa_q2_vec3 point) {
  (void)data;
  (void)point;
  return 0;
}

static void test_classic_pmove(void) {
  const qa_numeric_operations *n = binary32_ops();
  qa_q2_classic_pmove pm;
  memset(&pm, 0, sizeof pm);
  pm.s.pm_type = QA_Q2_PM_NORMAL;
  pm.s.origin[2] = 800;
  pm.s.gravity = 800;
  pm.cmd.msec = 16;
  pm.cmd.forwardmove = 200;
  pm.mins[0] = -16;
  pm.mins[1] = -16;
  pm.mins[2] = -24;
  pm.maxs[0] = 16;
  pm.maxs[1] = 16;
  pm.maxs[2] = 32;
  pm.trace = classic_free_trace;
  pm.pointcontents = zero_contents;
  pm.character_bounds = qa_q2_player_bounds;
  qa_q2_pmove_classic(&pm, n, 0, false, false, 1);
  CHECK(pm.s.origin[0] == 0 && pm.s.origin[1] == 0 && pm.s.origin[2] == 798);
  CHECK(pm.s.velocity[0] == 25 && pm.s.velocity[1] == 0 && pm.s.velocity[2] == -102);
  CHECK(pm.s.pm_flags == 0 && pm.viewheight == 22 && pm.numtouch == 0);
  CHECK(pm.mins[2] == -24 && pm.maxs[2] == 32 && pm.waterlevel == 0);
  CHECK(!pm.has_groundentity);
  qa_q2_pmove_classic(&pm, n, 0, false, false, 1);
  CHECK(pm.s.origin[2] == 794);
  CHECK(pm.s.velocity[0] == 50 && pm.s.velocity[2] == -204);
}

static void test_rerelease_pmove(void) {
  const qa_numeric_operations *n = binary32_ops();
  qa_q2_rerelease_movement_context context;
  qa_q2_rerelease_movement_context_init(&context);
  qa_q2_kex_pmove pm;
  memset(&pm, 0, sizeof pm);
  pm.s.pm_type = QA_Q2_KEX_PM_NORMAL;
  pm.s.origin[2] = 100;
  pm.s.gravity = 800;
  pm.s.viewheight = 22;
  pm.cmd.msec = 16;
  pm.cmd.forwardmove = 200;
  pm.cmd.server_frame = 1;
  pm.mins[0] = -16;
  pm.mins[1] = -16;
  pm.mins[2] = -24;
  pm.maxs[0] = 16;
  pm.maxs[1] = 16;
  pm.maxs[2] = 32;
  pm.groundplane = qa_q2_plane();
  pm.trace = kex_free_trace;
  pm.clip = kex_free_clip;
  pm.pointcontents = zero_contents;
  pm.character_bounds = qa_q2_player_bounds;
  qa_q2_pmove_rerelease(&pm, n, qa_q2_pm_config_default, &context, false, 1);
  CHECK(fbits(pm.s.origin[0]) == 0x3d51b718U && pm.s.origin[1] == 0);
  CHECK(fbits(pm.s.origin[2]) == 0x42c79724U);
  CHECK(fbits(pm.s.velocity[0]) == 0x404ccccdU && pm.s.velocity[1] == 0);
  CHECK(fbits(pm.s.velocity[2]) == 0xc14ccccdU);
  CHECK(pm.s.pm_flags == 0 && pm.s.viewheight == 22 && pm.touch.num == 1);
  CHECK(pm.waterlevel == 0 && pm.rdflags == 0 && !pm.jump_sound);
  const qa_vec3 captured = qa_q2_rerelease_movement_context_capture(&context);
  CHECK(fbits(captured.x) == 0x3d51b718U && fbits(captured.z) == 0x42c79724U);
  qa_q2_pmove_rerelease(&pm, n, qa_q2_pm_config_default, &context, false, 1);
  CHECK(fbits(pm.s.origin[0]) == 0x3e1d4952U && fbits(pm.s.origin[2]) == 0x42c6c56dU);
  CHECK(fbits(pm.s.velocity[0]) == 0x40cccccdU && fbits(pm.s.velocity[2]) == 0xc1cccccdU);
  const qa_vec3 restored = {1, 2, 3};
  qa_q2_rerelease_movement_context_restore(&context, restored);
  const qa_vec3 again = qa_q2_rerelease_movement_context_capture(&context);
  CHECK(again.x == 1 && again.y == 2 && again.z == 3);
}

/* --- movement-provider integration (stub scene) --- */

typedef struct stub_services {
  int weapon_steps;
  int animation_steps;
  int touches;
  bool remove_on_touch;
  bool seen_source_trace;
  bool seen_inverted;
  bool seen_plane;
  bool seen_surface;
  double plane_normal_z;
  double surface_flags;
} stub_services;

static qa_trace_result scene_trace(void *data, qa_trace_query query) {
  (void)data;
  qa_trace_result result;
  memset(&result, 0, sizeof result);
  result.kind = "q2";
  result.fraction = 1;
  result.end = query.end;
  result.hit.kind = QA_TRACE_HIT_NONE;
  return result;
}

static qa_point_contents_result scene_contents(void *data, qa_point_contents_query query) {
  (void)data;
  (void)query;
  qa_point_contents_result result;
  memset(&result, 0, sizeof result);
  result.kind = "q2";
  return result;
}

static qa_movement_continuation stub_touch(void *data, qa_movement_touch_contact contact,
                                           qa_movement_state state) {
  stub_services *stub = data;
  stub->touches++;
  stub->seen_source_trace = contact.has_source_trace;
  stub->seen_inverted = contact.source_trace_inverted;
  stub->seen_plane = contact.plane != NULL;
  stub->seen_surface = contact.surface != NULL;
  if (contact.plane != NULL) stub->plane_normal_z = contact.plane->normal.z;
  if (contact.surface != NULL) stub->surface_flags = contact.surface->native_flags;
  qa_movement_continuation continuation;
  memset(&continuation, 0, sizeof continuation);
  if (stub->remove_on_touch) return continuation;
  continuation.is_continue = true;
  continuation.state = state;
  if (state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE)
    continuation.state.as.q2_rerelease.velocity.x = 123;
  return continuation;
}

static qa_weapon_step_result stub_weapon(void *data, qa_weapon_step_input input,
                                         qa_movement_state state) {
  stub_services *stub = data;
  (void)state;
  stub->weapon_steps++;
  qa_weapon_step_result result;
  memset(&result, 0, sizeof result);
  result.arsenal = input.arsenal;
  result.animation = input.animation;
  return result;
}

static qa_animation_step_result stub_animation(void *data, qa_animation_step_input input) {
  stub_services *stub = data;
  stub->animation_steps++;
  qa_animation_step_result result;
  memset(&result, 0, sizeof result);
  result.animation = input.animation;
  return result;
}

static qa_movement_services make_services(stub_services *stub) {
  qa_movement_services services;
  memset(&services, 0, sizeof services);
  services.ctx = stub;
  services.scene.ctx = stub;
  services.scene.trace = scene_trace;
  services.scene.point_contents = scene_contents;
  services.numeric = *binary32_ops();
  services.touch = stub_touch;
  services.weapon_step = stub_weapon;
  services.animation_step = stub_animation;
  return services;
}

static qa_owned_actor test_actor(qa_identity_owner *owner) {
  return qa_identity_owner_owned_actor(owner, qa_identity_owner_actor(owner, 1, 1), "q2:game");
}

static qa_movement_input base_input(qa_identity_owner *owner) {
  qa_movement_input input;
  memset(&input, 0, sizeof input);
  input.actor = test_actor(owner);
  input.command_sequence = 0;
  input.shape = qa_q2_player_shape();
  input.frame.frame = 1;
  input.frame.time.kind = QA_SOURCE_TIME_MILLISECONDS;
  input.frame.time.value = 100;
  input.frame.elapsed.kind = QA_SOURCE_TIME_MILLISECONDS;
  input.frame.elapsed.value = 16;
  input.frame.phase = QA_FRAME_PHASE_CLIENT_COMMAND;
  input.environment.health = 100;
  input.environment.gravity_multiplier = 1;
  input.arsenal.provider = "q3:weapons";
  input.arsenal.state.kind = "q3";
  input.arsenal.state.source_weapon = 2;
  input.animation.provider = "q3:animation";
  input.animation.state.kind = "q3";
  input.execution = "authoritative";
  input.profile.numeric = q2_binary32_profile;
  return input;
}

static qa_movement_input classic_input(qa_identity_owner *owner) {
  qa_movement_input input = base_input(owner);
  input.kind = "q2-classic";
  input.command.kind = QA_USER_COMMAND_Q2;
  input.command.as.q2.milliseconds = 16;
  input.command.as.q2.forward_move = 200;
  input.state.kind = QA_MOVEMENT_STATE_Q2;
  input.state.as.q2.origin_eighths.values[2] = 800;
  input.state.as.q2.gravity = 800;
  input.profile.kind = "q2-classic";
  input.profile.id = "q2:classic";
  input.profile.clock.kind = QA_CLOCK_Q2_CLASSIC;
  input.profile.clock.frame_milliseconds = 100;
  return input;
}

static qa_movement_input rerelease_input(qa_identity_owner *owner) {
  qa_movement_input input = base_input(owner);
  input.kind = "q2-rerelease";
  input.command.kind = QA_USER_COMMAND_Q2_RERELEASE;
  input.command.as.q2_rerelease.milliseconds = 16;
  input.command.as.q2_rerelease.forward_move = 200;
  input.command.as.q2_rerelease.server_frame = 1;
  input.state.kind = QA_MOVEMENT_STATE_Q2_RERELEASE;
  input.state.as.q2_rerelease.origin.z = 100;
  input.state.as.q2_rerelease.gravity = 800;
  input.state.as.q2_rerelease.view_height = 22;
  input.profile.kind = "q2-rerelease";
  input.profile.id = "q2:rerelease";
  input.profile.clock.kind = QA_CLOCK_Q2_RERELEASE;
  input.profile.clock.frame_milliseconds = 25;
  return input;
}

static void test_index_moves(qa_identity_owner *owner) {
  stub_services stub;
  memset(&stub, 0, sizeof stub);
  const qa_movement_services services = make_services(&stub);

  qa_movement_input classic = classic_input(owner);
  qa_movement_result result = qa_move_q2_classic(classic, services);
  CHECK(strcmp(result.status, "active") == 0 && strcmp(result.kind, "q2-classic") == 0);
  CHECK(result.state.as.q2.origin_eighths.values[2] == 798);
  CHECK(result.state.as.q2.velocity_eighths.values[0] == 25);
  CHECK(result.state.as.q2.velocity_eighths.values[2] == -102);
  CHECK(result.bounds.min.z == -24 && result.bounds.max.z == 32);
  CHECK(result.view_height == 22 && result.ground.kind == QA_TRACE_HIT_NONE);
  CHECK(result.water_level == 0 && result.contact_count == 0 && result.effect_count == 0);
  CHECK(fbits(result.horizontal_speed) == 0x40480000U);
  CHECK(result.arsenal.provider == classic.arsenal.provider);
  CHECK(result.arsenal.state.source_weapon == 2);
  CHECK(result.animation.provider == classic.animation.provider);
  free((void *)result.contacts);
  free((void *)result.effects);

  qa_movement_provider provider = qa_create_q2_classic_movement_provider("q2:classic");
  CHECK(strcmp(provider.kind, "q2-classic") == 0 && provider.id != NULL);
  qa_movement_result via = provider.move(provider.ctx, classic, services);
  CHECK(via.state.as.q2.velocity_eighths.values[0] == 25);
  free((void *)via.contacts);
  free((void *)via.effects);

  qa_q2_rerelease_movement_context context;
  qa_q2_rerelease_movement_context_init(&context);
  qa_movement_input rr = rerelease_input(owner);
  qa_movement_result modern = qa_move_q2_rerelease(rr, services, &context);
  CHECK(strcmp(modern.status, "active") == 0 && strcmp(modern.kind, "q2-rerelease") == 0);
  CHECK(fbits(modern.state.as.q2_rerelease.origin.x) == 0x3d51b718U);
  CHECK(fbits(modern.state.as.q2_rerelease.origin.z) == 0x42c79724U);
  CHECK(fbits(modern.state.as.q2_rerelease.velocity.x) == 0x404ccccdU);
  CHECK(fbits(modern.state.as.q2_rerelease.velocity.z) == 0xc14ccccdU);
  CHECK(modern.view_height == 22 && !modern.jump_sound && modern.contact_count == 0);
  CHECK(fbits(modern.horizontal_speed) == 0x404ccccdU);
  free((void *)modern.contacts);
  free((void *)modern.effects);

  qa_movement_provider rr_provider =
      qa_create_q2_rerelease_movement_provider("q2:rerelease", &context);
  CHECK(strcmp(rr_provider.kind, "q2-rerelease") == 0);
  qa_movement_result rr_via = rr_provider.move(rr_provider.ctx, rr, services);
  CHECK(fbits(rr_via.state.as.q2_rerelease.velocity.x) == 0x404ccccdU);
  free((void *)rr_via.contacts);
  free((void *)rr_via.effects);

  CHECK(stub.weapon_steps == 0 && stub.animation_steps == 0);

  qa_movement_input bad = classic_input(owner);
  bad.command.as.q2.milliseconds = 256;
  const qa_movement_services bad_services = services;
  CHECK_ABORTS((void)qa_move_q2_classic(bad, bad_services));
}

static qa_movement_contact *one_contact(qa_trace_hit target) {
  static const qa_q2_surface_info surface = {"test", 2, 7, "material"};
  qa_movement_contact *contacts = malloc(sizeof *contacts);
  memset(contacts, 0, sizeof *contacts);
  contacts[0].target = target;
  contacts[0].trace.kind = "q2";
  contacts[0].trace.fraction = 0;
  contacts[0].trace.source_plane.plane.normal.z = 1;
  contacts[0].trace.surface = &surface;
  contacts[0].substep = 0;
  return contacts;
}

static void test_apply_contacts(qa_identity_owner *owner) {
  stub_services stub;
  memset(&stub, 0, sizeof stub);
  const qa_movement_services services = make_services(&stub);
  qa_q2_rerelease_movement_context context;
  qa_q2_rerelease_movement_context_init(&context);

  qa_trace_hit world;
  memset(&world, 0, sizeof world);
  world.kind = QA_TRACE_HIT_WORLD;
  world.model = 0;

  qa_movement_input rr = rerelease_input(owner);
  qa_movement_result moved = qa_move_q2_rerelease(rr, services, &context);
  free((void *)moved.contacts);
  moved.contacts = one_contact(world);
  moved.contact_count = 1;
  qa_movement_result applied = qa_apply_q2_movement_contacts(rr, services, moved);
  CHECK(strcmp(applied.status, "active") == 0);
  CHECK(applied.state.as.q2_rerelease.velocity.x == 123);
  CHECK(applied.effect_count == 1 && stub.touches == 1);
  CHECK(applied.effects[0].effect.kind == QA_MOVEMENT_EFFECT_TOUCH);
  CHECK(applied.effects[0].sequence == 0);
  CHECK(stub.seen_source_trace && stub.seen_inverted && stub.seen_plane && stub.seen_surface);
  CHECK(stub.plane_normal_z == 1 && stub.surface_flags == 2);
  free((void *)applied.contacts);
  free((void *)applied.effects);

  stub.remove_on_touch = true;
  stub.touches = 0;
  qa_movement_result moved2 = qa_move_q2_rerelease(rr, services, &context);
  free((void *)moved2.contacts);
  moved2.contacts = one_contact(world);
  moved2.contact_count = 1;
  qa_movement_result removed = qa_apply_q2_movement_contacts(rr, services, moved2);
  CHECK(strcmp(removed.status, "actor-removed") == 0 && stub.touches == 1);
  CHECK(removed.effect_count == 1);
  free((void *)removed.effects);

  stub.remove_on_touch = false;
  stub.seen_source_trace = true;
  stub.seen_plane = true;
  stub.seen_surface = true;
  qa_movement_input classic = classic_input(owner);
  qa_movement_result classic_moved = qa_move_q2_classic(classic, services);
  free((void *)classic_moved.contacts);
  classic_moved.contacts = one_contact(world);
  classic_moved.contact_count = 1;
  qa_movement_result classic_applied =
      qa_apply_q2_movement_contacts(classic, services, classic_moved);
  CHECK(strcmp(classic_applied.status, "active") == 0);
  CHECK(!stub.seen_source_trace && !stub.seen_plane && !stub.seen_surface);
  free((void *)classic_applied.contacts);
  free((void *)classic_applied.effects);

  qa_movement_result foreign = qa_move_q2_classic(classic, services);
  const qa_movement_input rr_input = rr;
  const qa_movement_services apply_services = services;
  CHECK_ABORTS((void)qa_apply_q2_movement_contacts(rr_input, apply_services, foreign));
  free((void *)foreign.contacts);
  free((void *)foreign.effects);
}

int main(void) {
  test_types();
  test_dimensions();
  test_math();
  test_view();
  test_sweep();
  test_classic_pmove();
  test_rerelease_pmove();
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "q2-movement-smoke"));
  test_index_moves(&owner);
  test_apply_contacts(&owner);
  qa_identity_owner_destroy(&owner);
  if (failures != 0) {
    fprintf(stderr, "%d of %d checks failed\n", failures, checks);
    return 1;
  }
  printf("all %d checks passed\n", checks);
  return 0;
}
