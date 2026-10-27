/*
 * Port of the TypeScript cases covering core/diagnostics, core/math,
 * core/numeric, core/renderer-math, core/commands/text, core/cvars/info
 * and core/random/q2-rerelease: tests/core/math.test.ts,
 * tests/core/q2-rerelease-random.test.ts, and the tokenizeCommand cases
 * of tests/core/commands-text-bytes.test.ts and
 * tests/core/commands-qw-punctuation.test.ts (their CommandBuffer and
 * CvarRegistry scenarios need commands/index and cvars/index, which are
 * persistence-blocked). Bit patterns and strings beyond those files
 * were produced by running the TS code under bun.
 * GPL-2.0-or-later.
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

#include "qa/core/commands/text.h"
#include "qa/core/cvars/info.h"
#include "qa/core/diagnostics.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/core/random/q2_rerelease.h"
#include "qa/core/renderer_math.h"
#include "qa/foundation/binary.h"

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

static uint32_t fbits(double value) { return qa_float32_bits(value); }
static uint64_t dbits(double value) { return qa_float64_bits(value); }
static int text_is(const char *got, const char *want) {
  return got != NULL && strcmp(got, want) == 0;
}
static int vec_bits(qa_vec3 v, uint32_t x, uint32_t y, uint32_t z) {
  return fbits(v.x) == x && fbits(v.y) == y && fbits(v.z) == z;
}

static void test_numeric(void) {
  CHECK(qa_int32(4294967297.0) == 1);
  CHECK(qa_int32(-1.5) == -1);
  CHECK(qa_int32(NAN) == 0);
  CHECK(qa_uint32(-1.0) == 4294967295u);
  CHECK(qa_uint32(4294967297.0) == 1u);
  /* tests/core/math.test.ts QVM conversion cases */
  CHECK(qa_qvm_float_to_int(2147483647.0) == INT32_MIN);
  CHECK(qa_qvm_float_to_int(NAN) == INT32_MIN);
  CHECK(qa_qvm_float_to_int(-3.9) == -3);
  CHECK(qa_checked_float_to_int(2147483647.0) == 2147483647);
  CHECK_ABORTS(qa_checked_float_to_int(INFINITY));
  CHECK(qa_native_atoi(" -123tail") == -123);
  CHECK(qa_native_atoi("  +42x") == 42);
  CHECK(qa_native_atoi("999999999999") == 2147483647);
  CHECK(qa_native_atoi("-999999999999") == INT32_MIN);
  CHECK(qa_native_atoi("abc") == 0);
  CHECK(dbits(qa_native_atof("0x1.8p+2tail")) == dbits(6.0));
  CHECK(signbit(qa_native_atof("-0")) && qa_native_atof("-0") == 0);
  CHECK(dbits(qa_native_atof("1.5e3tail")) == 0x4097700000000000ULL);
  CHECK(dbits(qa_native_atof("0x10")) == 0x4030000000000000ULL);
  CHECK(dbits(qa_native_atof("-inf")) == 0xfff0000000000000ULL);
  CHECK(isnan(qa_native_atof("nan(0x123)")));
  CHECK(dbits(qa_native_atof(".5")) == 0x3fe0000000000000ULL);
  CHECK(dbits(qa_native_atof("1e")) == 0x3ff0000000000000ULL);
  CHECK(dbits(qa_native_atof("  -0x1p-2")) == 0xbfd0000000000000ULL);
  CHECK(qa_native_atof("abc") == 0);
  /* Q_rand / Q3Random (math.test.ts) */
  qa_random_step step = qa_q_random(1);
  CHECK(step.seed == 69070 && step.value == 3534.0 / 65536.0);
  CHECK(qa_q_crandom(1).value == 2 * (3534.0 / 65536.0 - 0.5));
  qa_q3_random random;
  qa_q3_random_init(&random, 1, 0);
  CHECK(qa_q3_random_next_integer(&random) == 69070);
  qa_random_state saved = qa_q3_random_checkpoint(&random);
  int32_t next_integer = qa_q3_random_next_integer(&random);
  double next_unit = qa_q3_random_next_unit(&random);
  qa_q3_random restored;
  qa_q3_random_restore(&restored, saved);
  CHECK(qa_q3_random_next_integer(&restored) == next_integer);
  CHECK(qa_q3_random_next_unit(&restored) == next_unit);
  CHECK(qa_q3_random_checkpoint(&restored).draws == 3);
  CHECK_ABORTS(qa_q3_random_init(&random, 2147483648LL, 0));
  /* Profiles and operations */
  CHECK(text_is(qa_q3_binary32_profile.id, "q3:binary32"));
  CHECK(text_is(qa_q1_donor_profile.arithmetic.source, "q1-ts"));
  const qa_numeric_operations *donor = qa_create_numeric_operations(&qa_q1_donor_profile);
  const qa_numeric_operations *donor_again = qa_create_numeric_operations(&qa_q1_donor_profile);
  CHECK(donor == donor_again); /* TS WeakMap identity */
  CHECK(donor->add(donor->ctx, 16777216, 1) == 16777217);
  const qa_numeric_operations *q3 = qa_create_numeric_operations(&qa_q3_binary32_profile);
  CHECK(fbits(q3->add(q3->ctx, 16777216, 1)) == 0x4b800000u);
  CHECK(q3->to_int32(q3->ctx, NAN) == -2147483648.0);
  qa_numeric_profile x87 = qa_q3_binary32_profile;
  x87.id = "test:x87";
  x87.arithmetic.kind = QA_ARITHMETIC_X87;
  CHECK_ABORTS(qa_create_numeric_operations(&x87));
}

static void test_math(void) {
  qa_vec3 left = qa_math_vec3(16777216, 1, -16777216);
  qa_vec3 right = qa_math_vec3(1, 1, 1);
  /* RC01 donor cancellation (math.test.ts) */
  qa_vec3 left_copy = left, right_copy = right;
  CHECK(qa_mvm_dot_product(qa_q1_donor_math(), qa_vec3_access_plain(&left_copy),
                           qa_vec3_access_plain(&right_copy)) == 1);
  CHECK(qa_mvm_dot_product(qa_q2_donor_math(), qa_vec3_access_plain(&left_copy),
                           qa_vec3_access_plain(&right_copy)) == 1);
  CHECK(qa_dot3(left, right) == 0);
  qa_mutable_vector_math staged;
  qa_create_mutable_vector_math(&staged, qa_create_numeric_operations(&qa_q3_binary32_profile),
                                QA_ZERO_LENGTH_CLEAR);
  CHECK(qa_mvm_dot_product(&staged, qa_vec3_access_plain(&left_copy),
                           qa_vec3_access_plain(&right_copy)) == 0);

  /* Overlapping guest vectors (math.test.ts) */
  float storage[4] = {1, 2, 3, 4};
  qa_float32_vector_view input, output;
  qa_float32_vector_view_init(&input, (uint8_t *)storage, sizeof storage, 0);
  qa_float32_vector_view_init(&output, (uint8_t *)storage, sizeof storage, 4);
  qa_mvm_vector_copy(qa_q1_donor_math(), qa_vec3_access_view(&input), qa_vec3_access_view(&output));
  CHECK(storage[0] == 1 && storage[1] == 1 && storage[2] == 1 && storage[3] == 1);
  qa_mutable_vec3_storage aliased;
  qa_mutable_vec3_storage_init(&aliased, 1, 2, 3);
  qa_vec3 other = qa_math_vec3(4, 5, 6);
  qa_mvm_cross_product(qa_q2_donor_math(), qa_vec3_access_view(&aliased.view),
                       qa_vec3_access_plain(&other), qa_vec3_access_view(&aliased.view));
  qa_vec3 aliased_value = qa_float32_vector_view_value(&aliased.view);
  CHECK(aliased_value.x == -3 && aliased_value.y == 30 && aliased_value.z == -135);
  qa_float32_vector_view_set(&output, 0, 16777217);
  CHECK(qa_float32_vector_view_get(&input, 1) == 16777216);
  CHECK_ABORTS(qa_float32_vector_view_init(&input, (uint8_t *)storage, 8, 0));

  /* Q2 zero normalization retains output while Q3 clears it */
  qa_mutable_vec3_storage retained;
  qa_mutable_vec3_storage_init(&retained, 4, 5, 6);
  qa_vec3 zero = qa_math_vec3(0, 0, 0);
  CHECK(qa_mvm_vector_normalize2(qa_q2_donor_math(), qa_vec3_access_plain(&zero),
                                 qa_vec3_access_view(&retained.view)) == 0);
  qa_vec3 retained_value = qa_float32_vector_view_value(&retained.view);
  CHECK(retained_value.x == 4 && retained_value.y == 5 && retained_value.z == 6);
  qa_mvm_vector_normalize2(&staged, qa_vec3_access_plain(&zero),
                           qa_vec3_access_view(&retained.view));
  retained_value = qa_float32_vector_view_value(&retained.view);
  CHECK(retained_value.x == 0 && retained_value.y == 0 && retained_value.z == 0);
  CHECK(qa_float32_to_bits(qa_normalize3(qa_math_vec3(-0.0, 0, 0)).x) == 0x80000000u);

  /* Q3 geometry and QVM angle entry points (math.test.ts) */
  qa_vec4 transformed = qa_transform_vec4(qa_identity_mat4(), qa_math_vec4(2, 3, 4, 1));
  CHECK(transformed.x == 2 && transformed.y == 3 && transformed.z == 4 && transformed.w == 1);
  qa_plane plane;
  CHECK(qa_plane_from_points(&plane, qa_math_vec3(0, 0, 0), qa_math_vec3(0, 1, 0),
                             qa_math_vec3(1, 0, 0)));
  CHECK(plane.normal.x == 0 && plane.normal.y == 0 && plane.normal.z == 1 && plane.distance == 0);
  CHECK(qa_qvm_angle_mod(-90) == 270);
  qa_vec3 fwd = qa_qvm_angle_vectors(qa_math_vec3(0, 0, 0)).forward;
  CHECK(fwd.x == 1 && fwd.y == 0 && signbit(fwd.z));
  qa_floor_div_mod_result fdm = qa_floor_div_mod(-5, 3);
  CHECK(fdm.quotient == -2 && fdm.remainder == 1);
  CHECK_ABORTS(qa_floor_div_mod(1, 0));
  CHECK(qa_greatest_common_divisor(12, 18) == 6);

  /* Bun oracle bit patterns */
  qa_vec3 a = qa_math_vec3(1.5, -2.25, 3.125), b = qa_math_vec3(-0.5, 4, 0.75);
  CHECK(vec_bits(qa_add3(a, b), 0x3f800000u, 0x3fe00000u, 0x40780000u));
  CHECK(vec_bits(qa_cross3(a, b), 0xc1630000u, 0xc02c0000u, 0x409c0000u));
  CHECK(fbits(qa_dot3(a, b)) == 0xc0ed0000u);
  CHECK(fbits(qa_length3(a)) == 0x40843e01u);
  CHECK(vec_bits(qa_normalize3(a), 0x3eb9d740u, 0xbf0b6170u, 0x3f41958eu));
  CHECK(vec_bits(qa_lerp3(a, b, 0.3), 0x3f666666u, 0xbec00000u, 0x401a6666u));
  CHECK(qa_dot3_double(a, b) == -7.40625);
  CHECK(qa_hypot3(a) == 4.132568813704135);
  qa_angle_vectors av = qa_math_angle_vectors(qa_math_vec3(30, 45, 10));
  CHECK(vec_bits(av.forward, 0x3f1cc470u, 0x3f1cc470u, 0xbf000000u));
  CHECK(vec_bits(av.right, 0x3f228d6au, 0xbf41fc70u, 0xbe19fe31u));
  qa_angle_vectors donor_av = qa_math_angle_vectors_rule(qa_math_vec3(30, 45, 10), QA_ANGLE_RULE_DONOR);
  CHECK(vec_bits(donor_av.forward, 0x3f1cc471u, 0x3f1cc471u, 0xbf000000u));
  CHECK(vec_bits(qa_vector_to_angles(qa_math_vec3(1, 1, 1)), 0xc20d0ebcu, 0x42340000u, 0u));
  CHECK(vec_bits(qa_rotate_point_around_vector(qa_math_vec3(0, 0, 1), qa_math_vec3(1, 0, 0), 90),
                 0xb33bbd2eu, 0x3f800000u, 0u));
  qa_vec3 donor_rot;
  qa_donor_rotate_point_around_vector(&donor_rot, qa_math_vec3(0, 0, 1), qa_math_vec3(1, 0, 0), 90);
  CHECK(vec_bits(donor_rot, 0x248d3132u, 0x3f800000u, 0u));
  CHECK(qa_clamp_short(40000) == 32767 && qa_clamp_short(-40000) == -32768);
  CHECK(qa_q_log2(1024) == 10 && qa_q_log2(0) == 0);
  CHECK_ABORTS(qa_q_log2(-1));
  CHECK_ABORTS(qa_clamp_short(1.5));
  uint8_t color_storage[4] = {0, 0, 0, 0xab};
  CHECK(qa_color_bytes3(1, 0.5, 0, color_storage, 4) == 0xab007fffu);
  qa_vec3 ncolor;
  CHECK(fbits(qa_normalize_color(qa_math_vec3(2, 1, 0.5), &ncolor)) == 0x40000000u);
  CHECK(vec_bits(ncolor, 0x3f800000u, 0x3f000000u, 0x3e800000u));
  CHECK(qa_lerp_angle(350, 10, 0.5) == 360);
  CHECK(qa_angle_mod(-90) == 270 && qa_angle_normalize180(270) == -90);
  qa_bounds bounds = qa_add_point_to_bounds(
      qa_add_point_to_bounds(qa_empty_bounds(), qa_math_vec3(-1, 2, -3)), qa_math_vec3(4, -5, 6));
  CHECK(fbits(qa_radius_from_bounds(bounds)) == 0x410c6641u);
  CHECK(!qa_plane_from_points(&plane, a, a, a));
  qa_mat4 look = qa_look_at_mat4(qa_math_vec3(1, 2, 3), qa_math_vec3(0, 0, 0), qa_math_vec3(0, 0, 1));
  CHECK(fbits(look.values[0]) == 0xbf64f92eu && fbits(look.values[14]) == 0xc06f7750u);
}

static void test_renderer_math(void) {
  CHECK(qa_renderer_sine(1024) == 0); /* math.test.ts */
  CHECK(fabs((double)qa_inverse_sqrt32(4) - 0.5) < 0.005);
  CHECK(fbits((double)qa_renderer_sine(256)) == 0x3f7fffecu);
  CHECK(fbits((double)qa_renderer_sine(-1)) == 0xa58d3132u);
  CHECK(fbits((double)qa_inverse_sqrt32(4)) == 0x3eff910fu);
  CHECK(fbits((double)qa_inverse_sqrt32(2)) == 0x3f34f95eu);
  CHECK(vec_bits(qa_normalize_fast3(qa_math_vec3(3, 0, 4)), 0x3f195c9bu, 0u, 0x3f4c7b79u));
  CHECK_ABORTS(qa_renderer_sine(2147483648.0));
  CHECK_ABORTS(qa_renderer_sine(0.5));
}

static void test_q2_random(void) {
  static const uint32_t reference[10] = {3499211612u, 581869302u, 3890346734u, 3586334585u,
                                         545404204u, 4161255391u, 3922919429u, 949333985u,
                                         2715962298u, 1323567403u};
  qa_q2_rerelease_random random;
  qa_q2_rerelease_random_init_default(&random);
  for (int i = 0; i < 10; i++) CHECK(qa_q2_rerelease_random_next_uint32(&random) == reference[i]);
  for (int i = 10; i < 620; i++) (void)qa_q2_rerelease_random_next_uint32(&random);
  qa_q2_rerelease_random_state checkpoint;
  qa_q2_rerelease_random_capture(&random, &checkpoint);
  float expected_float[8];
  int64_t expected_ms[8];
  int32_t expected_int[8];
  for (int i = 0; i < 8; i++) {
    expected_float[i] = qa_q2_rerelease_random_float_range(&random, -1, 1);
    expected_ms[i] = qa_q2_rerelease_random_time_milliseconds(&random, 3000, 10000);
    expected_int[i] = qa_q2_rerelease_random_integer_range(&random, -20, 50);
  }
  qa_q2_rerelease_random_restore(&random, &checkpoint);
  for (int i = 0; i < 8; i++) {
    CHECK(qa_q2_rerelease_random_float_range(&random, -1, 1) == expected_float[i]);
    CHECK(qa_q2_rerelease_random_time_milliseconds(&random, 3000, 10000) == expected_ms[i]);
    CHECK(qa_q2_rerelease_random_integer_range(&random, -20, 50) == expected_int[i]);
  }
  CHECK(checkpoint.index == 620 && checkpoint.draws == 620);
  qa_q2_rerelease_random scalar;
  qa_q2_rerelease_random_init_default(&scalar);
  CHECK((double)qa_q2_rerelease_random_float_unit(&scalar) == 0.8147236704826355);
  CHECK(qa_q2_rerelease_random_time_milliseconds(&scalar, 3000, 10000) == 3948);
  CHECK((double)qa_q2_rerelease_random_float_range(&scalar, -1, 1) == 0.8115838766098022);
  CHECK(qa_q2_rerelease_random_integer_max(&scalar, 100) == 83);
  qa_q2_rerelease_random_state state;
  qa_q2_rerelease_random_capture(&scalar, &state);
  int64_t before = state.draws;
  CHECK(qa_q2_rerelease_random_integer_range(&scalar, 7, 8) == 7);
  qa_q2_rerelease_random_capture(&scalar, &state);
  CHECK(state.draws == before);
  CHECK(qa_q2_rerelease_random_time_milliseconds(&scalar, 7, 7) == 7);
  qa_q2_rerelease_random_capture(&scalar, &state);
  CHECK(state.draws == before + 1);
  qa_q2_rerelease_random wide;
  qa_q2_rerelease_random_init_default(&wide);
  CHECK(qa_q2_rerelease_random_integer64(&wide, INT64_MIN, INT64_MAX) == 5805627399050534646LL);
  qa_q2_rerelease_random_capture(&wide, &state);
  CHECK(state.draws == 2);
  CHECK_ABORTS(qa_q2_rerelease_random_float_range(&scalar, 2, 1));
  CHECK_ABORTS(qa_q2_rerelease_random_integer_range(&scalar, 5, 5));
  state.index = 625;
  CHECK_ABORTS(qa_q2_rerelease_random_restore(&scalar, &state));
  /* The TS MaximumWord subclass case has no C counterpart (no virtual
   * nextUint32); its arithmetic, float(fround(0xffffffff)/2^32) == 1,
   * is covered by the unit conversion above. */
}

static int argv_is(const qa_command_tokens *tokens, const char *const *want, size_t count) {
  if (tokens->argc != count) return 0;
  for (size_t i = 0; i < count; i++) {
    if (!text_is(tokens->argv[i], want[i])) return 0;
  }
  return 1;
}

static const char *variable_named(void *ctx, const char *name) {
  (void)ctx;
  return strcmp(name, "name") == 0 ? "bob" : "?";
}

typedef struct print_log {
  char buffer[1024];
} print_log;

static void log_print(void *ctx, const char *text) {
  strcat(((print_log *)ctx)->buffer, text);
}

static void test_command_text(void) {
  qa_command_tokens tokens;
  /* commands-qw-punctuation.test.ts tokenize case */
  const char *text = "record 'a -'a q1:shotgun {word}(tail)";
  static const char *const want_qw[] = {"record", "'a", "-'a", "q1:shotgun", "{word}(tail)"};
  static const char *const want_nq[] = {"record", "'", "a", "-", "'", "a", "q1", ":",
                                        "shotgun", "{", "word", "}", "(", "tail", ")"};
  for (int mode = 0; mode < 2; mode++) {
    qa_command_text_mode m = mode == 0 ? QA_COMMAND_TEXT_SOURCE : QA_COMMAND_TEXT_CONSOLE;
    CHECK(qa_tokenize_command(text, QA_COMMAND_DIALECT_Q1_QUAKEWORLD, m, &tokens));
    CHECK(argv_is(&tokens, want_qw, 5));
    qa_command_tokens_destroy(&tokens);
    CHECK(qa_tokenize_command(text, QA_COMMAND_DIALECT_Q1_NETQUAKE, m, &tokens));
    CHECK(argv_is(&tokens, want_nq, 15));
    qa_command_tokens_destroy(&tokens);
  }
  /* commands-text-bytes.test.ts tokenize cases (byte-string form) */
  static const char *const want_high[] = {"echo", "\x80\xff"};
  CHECK(qa_tokenize_command("echo \x80\xff", QA_COMMAND_DIALECT_Q3, QA_COMMAND_TEXT_CONSOLE, &tokens));
  CHECK(argv_is(&tokens, want_high, 2));
  qa_command_tokens_destroy(&tokens);
  static const char *const want_gyp[] = {"echo", "gyp"};
  CHECK(qa_tokenize_command("echo \xe9 \xdc \xe4 \xf1 gyp", QA_COMMAND_DIALECT_Q2_CLASSIC,
                            QA_COMMAND_TEXT_SOURCE, &tokens));
  CHECK(argv_is(&tokens, want_gyp, 2));
  qa_command_tokens_destroy(&tokens);
  static const char *const want_grant[] = {"grant", "q1:shotgun"};
  CHECK(qa_tokenize_command("grant \"q1:shotgun\"", QA_COMMAND_DIALECT_Q1_QUAKEWORLD,
                            QA_COMMAND_TEXT_CONSOLE, &tokens));
  CHECK(argv_is(&tokens, want_grant, 2));
  qa_command_tokens_destroy(&tokens);
  /* Bun oracle: q3 comments/quotes and argsText joins */
  static const char *const want_q3[] = {"say", "hello world", "foo"};
  CHECK(qa_tokenize_command("say \"hello world\" foo//bar /* c */ baz", QA_COMMAND_DIALECT_Q3,
                            QA_COMMAND_TEXT_SOURCE, &tokens));
  CHECK(argv_is(&tokens, want_q3, 3));
  CHECK(text_is(tokens.args_text, "hello world foo"));
  qa_command_tokens_destroy(&tokens);
  CHECK(qa_tokenize_command("say  spaced  out  ", QA_COMMAND_DIALECT_Q2_CLASSIC,
                            QA_COMMAND_TEXT_SOURCE, &tokens));
  CHECK(text_is(tokens.args_text, "spaced  out"));
  qa_command_tokens_destroy(&tokens);
  char *tail = qa_command_text_tail("cmd \"a b\" rest here", QA_COMMAND_DIALECT_Q3, 2);
  CHECK(text_is(tail, "rest here"));
  free(tail);
  tail = qa_command_text_tail("only", QA_COMMAND_DIALECT_Q3, 5);
  CHECK(text_is(tail, ""));
  free(tail);
  CHECK_ABORTS(free(qa_command_text_tail("x", QA_COMMAND_DIALECT_Q3, -1)));
  CHECK(qa_command_separator_offset("a \"b;c\" ; d", QA_COMMAND_DIALECT_Q3) == 8);
  CHECK(qa_command_separator_offset("a\rb", QA_COMMAND_DIALECT_Q3) == 1);
  CHECK(qa_command_separator_offset("a\rb", QA_COMMAND_DIALECT_Q2_CLASSIC) == 3);
  char *folded = qa_ascii_fold("AbC\xc9Z");
  CHECK(text_is(folded, "abc\xc9z"));
  free(folded);
  print_log log = {{0}};
  char *expanded = NULL;
  CHECK(qa_expand_command_macros("echo $name and $name", variable_named, NULL, log_print,
                                 &log, QA_COMMAND_TEXT_SOURCE, &expanded));
  CHECK(text_is(expanded, "echo bob and bob"));
  free(expanded);
  CHECK(!qa_expand_command_macros("echo \"x", variable_named, NULL, log_print, &log,
                                  QA_COMMAND_TEXT_SOURCE, &expanded));
  CHECK(text_is(log.buffer, "Line has unmatched quote, discarded.\n"));
  CHECK(qa_command_dialect_is_q1(QA_COMMAND_DIALECT_Q1_QUAKEWORLD));
  CHECK(qa_command_dialect_is_q2(QA_COMMAND_DIALECT_Q2_RERELEASE));
  CHECK(!qa_command_dialect_is_q2(QA_COMMAND_DIALECT_Q3));
}

static void test_info(void) {
  print_log log = {{0}};
  qa_info_options q3 = {QA_COMMAND_DIALECT_Q3, 8192, QA_CVAR_INFO_CLIENT_USERINFO,
                        false, log_print, &log};
  char *result = qa_set_info_value("\\name\\old\\team\\red", "name", "new", &q3);
  CHECK(text_is(result, "\\team\\red\\name\\new"));
  free(result);
  result = qa_set_info_value("\\name\\old\\team\\red", "name", "", &q3);
  CHECK(text_is(result, "\\team\\red"));
  free(result);
  qa_info_options q3_small = {QA_COMMAND_DIALECT_Q3, 64, QA_CVAR_INFO_CLIENT_USERINFO,
                              false, log_print, &log};
  result = qa_set_info_value("", "k", "v", &q3_small);
  CHECK(text_is(result, "\\k\\v"));
  free(result);
  qa_info_options q2 = {QA_COMMAND_DIALECT_Q2_CLASSIC, 1024, QA_CVAR_INFO_CLIENT_USERINFO,
                        false, log_print, &log};
  result = qa_set_info_value("", "name", "Ab\x80""C", &q2);
  CHECK(text_is(result, "\\name\\AbC"));
  free(result);
  qa_info_options qw = {QA_COMMAND_DIALECT_Q1_QUAKEWORLD, 1024, QA_CVAR_INFO_CLIENT_USERINFO,
                        false, log_print, &log};
  result = qa_set_info_value("", "team", "RED", &qw);
  CHECK(text_is(result, "\\team\\red"));
  free(result);
  result = qa_set_info_value("", "name", "a\x80""b", &qw);
  CHECK(text_is(result, "\\name\\a\x80""b"));
  free(result);
  CHECK(log.buffer[0] == '\0');
  result = qa_set_info_value("", "ke;y", "v", &q3);
  CHECK(text_is(result, ""));
  free(result);
  CHECK(text_is(log.buffer, "Can't use keys or values with a semicolon\n"));
  qa_info_options tiny = {QA_COMMAND_DIALECT_Q3, 8, QA_CVAR_INFO_CLIENT_USERINFO,
                          false, log_print, &log};
  CHECK_ABORTS(free(qa_set_info_value("0123456789", "k", "v", &tiny)));
}

typedef struct diag_log {
  qa_diagnostic last;
  int count;
} diag_log;

static void diag_sink(void *ctx, const qa_diagnostic *diagnostic) {
  diag_log *log = (diag_log *)ctx;
  log->last = *diagnostic;
  log->count++;
}

static void test_diagnostics(void) {
  diag_log log = {{0}, 0};
  qa_session_id session = {"session-name", &log};
  qa_session_diagnostics diagnostics;
  qa_diagnostic_sink sink = {&log, diag_sink};
  qa_session_diagnostics_init(&diagnostics, session, sink);
  qa_diagnostic_input input = {QA_DIAGNOSTIC_WARNING, "x", "m", false, NULL, false, {0, 0}};
  qa_session_diagnostics_emit(&diagnostics, input);
  CHECK(log.count == 1);
  CHECK(log.last.severity == QA_DIAGNOSTIC_WARNING);
  CHECK(text_is(log.last.code, "x") && text_is(log.last.message, "m"));
  CHECK(log.last.session.token == session.token);
  CHECK(!log.last.has_provider && !log.last.has_time);
  qa_engine_error_kind kind;
  CHECK(qa_engine_error_kind_parse("drop", &kind) && kind == QA_ENGINE_ERROR_DROP);
  CHECK(text_is(qa_engine_error_kind_name(QA_ENGINE_ERROR_FATAL), "fatal"));
  CHECK_ABORTS(qa_engine_error(QA_ENGINE_ERROR_FATAL, "boom"));
}

int main(void) {
  test_numeric();
  test_math();
  test_renderer_math();
  test_q2_random();
  test_command_text();
  test_info();
  test_diagnostics();
  if (failures != 0) {
    fprintf(stderr, "%d of %d checks failed\n", failures, checks);
    return 1;
  }
  printf("all %d checks passed\n", checks);
  return 0;
}
