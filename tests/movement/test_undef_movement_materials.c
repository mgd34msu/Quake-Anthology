/*
 * Behavior cases for the w8b movement/materials helpers: qa_q1_* and
 * qa_q2_* JS-number shims (TS src/movement/q1/common.ts and
 * src/movement/q2/{math,types}.ts call-site semantics) and qa_mat_*
 * (TS src/materials/material.ts evaluateWaveform/shaderTableIndex and
 * JS ToInt32/ToUint32/Math.fround). TS throw cases run in a child
 * process and must abort. GPL-2.0-or-later.
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

#include "qa/core/numeric.h"
#include "qa/materials/material.h"
#include "qa/materials/shader_types.h"
#include "qa/movement/q1/common.h"
#include "qa/movement/q2/math.h"
#include "qa/movement/q2/types.h"

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

static bool same_double(double a, double b) {
  if (isnan(a) || isnan(b)) return isnan(a) && isnan(b);
  return a == b && signbit(a) == signbit(b);
}

static void test_js_max_min(void) {
  const double pairs[][2] = {{1, 2}, {2, 1}, {-0.0, 0.0}, {0.0, -0.0}, {-0.0, -0.0},
                             {INFINITY, 1}, {1, -INFINITY}, {NAN, 1}, {1, NAN}, {-5, -5}};
  for (size_t i = 0; i < sizeof pairs / sizeof pairs[0]; i++) {
    CHECK(same_double(qa_q1_js_max(pairs[i][0], pairs[i][1]),
                      qa_q2_js_max(pairs[i][0], pairs[i][1])));
    CHECK(same_double(qa_q1_js_min(pairs[i][0], pairs[i][1]),
                      qa_q2_js_min(pairs[i][0], pairs[i][1])));
  }
  /* JS Math.max(+0, -0) is +0; Math.min(+0, -0) is -0. */
  CHECK(!signbit(qa_q1_js_max(-0.0, 0.0)));
  CHECK(!signbit(qa_q2_js_max(0.0, -0.0)));
  CHECK(signbit(qa_q1_js_min(-0.0, 0.0)));
  CHECK(signbit(qa_q2_js_min(0.0, -0.0)));
  CHECK(isnan(qa_q1_js_max(NAN, 1)));
  CHECK(isnan(qa_q2_js_min(1, NAN)));
  CHECK(qa_q1_js_max(1, 2) == 2 && qa_q1_js_min(1, 2) == 1);
}

static void test_js_truthy_and_flags(void) {
  CHECK(!qa_q2_js_truthy(0.0));
  CHECK(!qa_q2_js_truthy(-0.0));
  CHECK(!qa_q2_js_truthy(NAN));
  CHECK(qa_q2_js_truthy(1e-300));
  CHECK(qa_q2_js_truthy(-1));
  CHECK(qa_q2_js_truthy(INFINITY));

  /* Flag ops go through JS ToInt32 of the number field first. */
  CHECK(qa_q1_flag_test(4294967296.0 + 8, 8));
  CHECK(qa_q2_flag_test(4294967296.0 + 8, 8));
  CHECK(!qa_q1_flag_test(NAN, 1));
  CHECK(!qa_q2_flag_test(INFINITY, 1));
  CHECK(qa_q1_flag_test(2147483648.0, 1 << 30) == false);
  CHECK(qa_q1_flag_set(0, 5) == 5);
  CHECK(qa_q2_flag_set(4294967295.0, 1) == -1);
  CHECK(qa_q1_flag_clear(-1, 1) == -2);
  CHECK(qa_q2_flag_clear(7, 2) == 5);
  CHECK(qa_q1_flag_set(-0.5, 2) == 2);
  CHECK(qa_q2_flag_set(0, 0) == 0 && !signbit(qa_q2_flag_set(-0.0, 0)));
}

typedef struct fake_ops_ctx { int calls; } fake_ops_ctx;
static double fake_store(void *ctx, double v) { ((fake_ops_ctx *)ctx)->calls++; return (float)v; }
static double fake_add(void *ctx, double a, double b) { ((fake_ops_ctx *)ctx)->calls++; return (float)(a + b); }
static double fake_subtract(void *ctx, double a, double b) { ((fake_ops_ctx *)ctx)->calls++; return (float)(a - b); }
static double fake_multiply(void *ctx, double a, double b) { ((fake_ops_ctx *)ctx)->calls++; return (float)(a * b); }
static double fake_divide(void *ctx, double a, double b) { ((fake_ops_ctx *)ctx)->calls++; return (float)(a / b); }
static double fake_square_root(void *ctx, double v) { ((fake_ops_ctx *)ctx)->calls++; return (float)sqrt(v); }
static double fake_to_int32(void *ctx, double v) { ((fake_ops_ctx *)ctx)->calls++; return trunc(v) + 1000; }

static void test_n_dispatch(void) {
  fake_ops_ctx ctx = {0};
  qa_numeric_operations ops;
  memset(&ops, 0, sizeof ops);
  ops.ctx = &ctx;
  ops.store = fake_store;
  ops.add = fake_add;
  ops.subtract = fake_subtract;
  ops.multiply = fake_multiply;
  ops.divide = fake_divide;
  ops.square_root = fake_square_root;
  ops.to_int32 = fake_to_int32;

  /* fround staging must come from the supplied operations, with ctx. */
  CHECK(qa_q1_n_store(&ops, 0.1) == (double)(float)0.1);
  CHECK(qa_q1_n_add(&ops, 0.1, 0.2) == (double)(float)(0.1 + 0.2));
  CHECK(qa_q1_n_add(&ops, 0.1, 0.2) != 0.1 + 0.2);
  CHECK(qa_q1_n_subtract(&ops, 1, 0.1) == (double)(float)(1 - 0.1));
  CHECK(qa_q1_n_multiply(&ops, 3, 0.1) == (double)(float)(3 * 0.1));
  CHECK(qa_q1_n_divide(&ops, 1, 3) == (double)(float)(1.0 / 3));
  CHECK(qa_q1_n_square_root(&ops, 2) == (double)(float)sqrt(2));
  CHECK(qa_q1_n_to_int32(&ops, 42.9) == 1042);
  CHECK(ctx.calls == 8);

  ctx.calls = 0;
  CHECK(qa_q2_n_store(&ops, 0.1) == (double)(float)0.1);
  CHECK(qa_q2_n_add(&ops, 0.1, 0.2) == (double)(float)(0.1 + 0.2));
  CHECK(qa_q2_n_subtract(&ops, 1, 0.1) == (double)(float)(1 - 0.1));
  CHECK(qa_q2_n_multiply(&ops, 3, 0.1) == (double)(float)(3 * 0.1));
  CHECK(qa_q2_n_divide(&ops, 1, 0) == INFINITY);
  CHECK(isnan(qa_q2_n_square_root(&ops, -1)));
  CHECK(ctx.calls == 6);
}

static void test_same_hit(void) {
  qa_trace_hit none_a, none_b, world_a, world_b, actor_a, actor_b;
  memset(&none_a, 0, sizeof none_a);
  memset(&none_b, 0, sizeof none_b);
  none_a.kind = none_b.kind = QA_TRACE_HIT_NONE;
  memset(&world_a, 0, sizeof world_a);
  memset(&world_b, 0, sizeof world_b);
  world_a.kind = world_b.kind = QA_TRACE_HIT_WORLD;
  world_a.model = world_b.model = 3;
  memset(&actor_a, 0, sizeof actor_a);
  memset(&actor_b, 0, sizeof actor_b);
  actor_a.kind = actor_b.kind = QA_TRACE_HIT_ACTOR;
  actor_a.actor.slot = actor_b.actor.slot = 7;
  actor_a.actor.generation = actor_b.actor.generation = 2;

  CHECK(qa_q2_same_hit(none_a, none_b));
  CHECK(qa_q2_same_hit(world_a, world_b));
  world_b.model = 4;
  CHECK(!qa_q2_same_hit(world_a, world_b));
  CHECK(qa_q2_same_hit(actor_a, actor_b));
  actor_b.actor.slot = 8;
  CHECK(!qa_q2_same_hit(actor_a, actor_b));
  actor_b.actor.slot = 7;
  actor_b.actor.generation = 3;
  CHECK(!qa_q2_same_hit(actor_a, actor_b));
  CHECK(!qa_q2_same_hit(none_a, world_a));
  CHECK(!qa_q2_same_hit(world_a, actor_a));
  /* JS === on the world model number: NaN models are never equal. */
  world_a.model = world_b.model = NAN;
  CHECK(!qa_q2_same_hit(world_a, world_b));
}

typedef struct trace_record {
  int calls;
  qa_trace_query query;
} trace_record;

static qa_trace_result fake_trace(void *ctx, qa_trace_query query) {
  trace_record *record = ctx;
  record->calls++;
  record->query = query;
  qa_trace_result result;
  memset(&result, 0, sizeof result);
  result.fraction = 0.25;
  return result;
}

static void test_context_trace(void) {
  trace_record record;
  memset(&record, 0, sizeof record);
  qa_q1_player_input input;
  memset(&input, 0, sizeof input);
  qa_movement_services services;
  memset(&services, 0, sizeof services);
  services.numeric = *qa_create_numeric_operations(&qa_q1_donor_profile);
  services.scene.ctx = &record;
  services.scene.trace = fake_trace;
  qa_q1_movement_options options;
  memset(&options, 0, sizeof options);

  qa_q1_movement_context context;
  qa_q1_movement_context_init(&context, input, services, options);
  const qa_vec3 start = {1, 2, 3}, end = {4, 5, 6};
  const qa_trace_result via_default = qa_q1_context_trace(&context, start, end);
  CHECK(record.calls == 1);
  CHECK(via_default.fraction == 0.25);
  /* TS defaults: shape is the context shape, move is "normal". */
  CHECK(record.query.policy.move != NULL && strcmp(record.query.policy.move, "normal") == 0);
  CHECK(record.query.start.x == 1 && record.query.end.z == 6);

  const qa_trace_result via_ex = qa_q1_context_trace_ex(&context, start, end, NULL, NULL);
  CHECK(record.calls == 2 && via_ex.fraction == via_default.fraction);
  qa_q1_movement_context_destroy(&context);
}

static void test_mat_int_conversions(void) {
  CHECK(qa_mat_to_int32(NAN) == 0);
  CHECK(qa_mat_to_int32(INFINITY) == 0);
  CHECK(qa_mat_to_int32(-INFINITY) == 0);
  CHECK(qa_mat_to_int32(-0.5) == 0);
  CHECK(qa_mat_to_int32(2147483647.9) == INT32_MAX);
  CHECK(qa_mat_to_int32(2147483648.0) == INT32_MIN);
  CHECK(qa_mat_to_int32(4294967296.0 + 5) == 5);
  CHECK(qa_mat_to_int32(-4294967297.0) == -1);
  CHECK(qa_mat_to_int32(1e10) == 1410065408);
  CHECK(qa_mat_to_uint32(-1) == 4294967295u);
  CHECK(qa_mat_to_uint32(2147483648.0) == 2147483648u);
  const double values[] = {0, -0.0, 1.5, -1.5, 65535.9, -2147483648.0, 4294967295.0,
                           1e15, -1e15, 1e300, -1e300, 9007199254740991.0};
  for (size_t i = 0; i < sizeof values / sizeof values[0]; i++) {
    CHECK(qa_mat_to_int32(values[i]) == qa_int32(values[i]));
    CHECK(qa_mat_to_uint32(values[i]) == qa_uint32(values[i]));
  }

  CHECK((double)qa_mat_f(0.1) == (double)(float)0.1);
  CHECK((double)qa_mat_f(0.1) != 0.1);
  CHECK(signbit(qa_mat_f(-0.0)));
  CHECK(isinf(qa_mat_f(1e300)));
  CHECK(isnan(qa_mat_f(NAN)));

  CHECK(qa_mat_kind_is("sin", "sin"));
  CHECK(!qa_mat_kind_is("sin", "Sin"));
  CHECK(!qa_mat_kind_is("sin", "sine"));
  CHECK(!qa_mat_kind_is(NULL, "sin"));
}

static qa_waveform wave(const char *kind, double base, double amplitude, double phase,
                        double frequency) {
  const qa_waveform result = {kind, base, amplitude, phase, frequency};
  return result;
}

static void test_mat_waveform(void) {
  CHECK(qa_mat_shader_table_index(0) == 0);
  CHECK(qa_mat_shader_table_index(1024.9) == 0);
  CHECK(qa_mat_shader_table_index(-1) == 1023);
  CHECK(qa_mat_shader_table_index(-256) == 768);
  CHECK(qa_mat_shader_table_index(2147483647.0) == 1023);
  CHECK(qa_mat_shader_table_index(-2147483648.0) == 0);
  CHECK(qa_mat_shader_table_index(-0.5) == 0);
  CHECK_ABORTS((void)qa_mat_shader_table_index(2147483648.0));
  CHECK_ABORTS((void)qa_mat_shader_table_index(-2147483649.0));
  CHECK_ABORTS((void)qa_mat_shader_table_index(NAN));
  CHECK_ABORTS((void)qa_mat_shader_table_index(INFINITY));

  CHECK(qa_mat_evaluate_waveform(wave("square", 0, 1, 0, 0), 99) == 1);
  CHECK(qa_mat_evaluate_waveform(wave("square", 0, 1, 0.5, 0), 0) == -1);
  CHECK(qa_mat_evaluate_waveform(wave("sawtooth", 0, 1, 0.25, 0), 0) == 0.25);
  CHECK(qa_mat_evaluate_waveform(wave("inversesawtooth", 0, 1, 0.25, 0), 0) == 0.75);
  CHECK(qa_mat_evaluate_waveform(wave("triangle", 0, 1, 0.125, 0), 0) == 0.5);
  CHECK(qa_mat_evaluate_waveform(wave("triangle", 0, 1, 0.25, 0), 0) == 1);
  CHECK(qa_mat_evaluate_waveform(wave("triangle", 0, 1, 0.5, 0), 0) == 0);
  CHECK(qa_mat_evaluate_waveform(wave("triangle", 0, 1, 0.75, 0), 0) == -1);
  /* Negative phase wraps through the TS `integer & 1023`. */
  CHECK(qa_mat_evaluate_waveform(wave("sawtooth", 0, 1, -0.25, 0), 0) == 0.75);
  /* fround staging: fround(base + fround(value * amplitude)). */
  const double staged = (float)(0.1 + (double)(float)(0.25 * 0.1));
  CHECK(qa_mat_evaluate_waveform(wave("sawtooth", 0.1, 0.1, 0.25, 0), 0) == staged);
  /* Cross-check every kind against material.c's own port of the TS. */
  const char *kinds[] = {"sin", "square", "sawtooth", "inversesawtooth", "triangle"};
  for (size_t i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
    for (double t = -1.5; t <= 1.5; t += 0.375) {
      const qa_waveform w = wave(kinds[i], 0.3, 0.7, 0.125, 2);
      CHECK(qa_mat_evaluate_waveform(w, t) == qa_evaluate_waveform(w, t));
    }
  }
  CHECK_ABORTS((void)qa_mat_evaluate_waveform(wave("none", 0, 1, 0, 0), 0));
  CHECK_ABORTS((void)qa_mat_evaluate_waveform(wave("noise", 0, 1, 0, 0), 0));
  /* The table index is computed (and range-checked) before the kind. */
  CHECK_ABORTS((void)qa_mat_evaluate_waveform(wave("sin", 0, 1, 1e30, 0), 0));
}

int main(void) {
  test_js_max_min();
  test_js_truthy_and_flags();
  test_n_dispatch();
  test_same_hit();
  test_context_trace();
  test_mat_int_conversions();
  test_mat_waveform();
  printf("test_undef_movement_materials: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
