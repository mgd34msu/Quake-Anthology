/*
 * Regression checks for TypeScript-vs-C evaluation-order defects and for
 * String(number) spelling. Expected values were produced by running the
 * TypeScript (bun) on the same inputs; see each block. GPL-2.0-or-later.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/app/bootstrap/effects/particles.h"
#include "qa/app/bootstrap/simulation/random.h"
#include "qa/input/support.h"
#include "qa/network/common/js_number.h"

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

#define CHECK_STR(actual, expected)                                                   \
  do {                                                                                \
    const char *a_ = (actual), *e_ = (expected);                                      \
    checks++;                                                                         \
    if (strcmp(a_, e_) != 0) {                                                        \
      failures++;                                                                     \
      fprintf(stderr, "FAIL %s:%d: got \"%s\", want \"%s\"\n", __FILE__, __LINE__, a_, e_); \
    }                                                                                 \
  } while (0)

/* bun -e 'for (const v of [...]) console.log(String(v))' */
static void test_js_number_spelling(void) {
  static const struct { double value; const char *text; } cases[] = {
    {0.00001, "0.00001"},
    {1e-7, "1e-7"},
    {0.000001, "0.000001"},
    {1.5e-5, "0.000015"},
    {123.456, "123.456"},
    {1e21, "1e+21"},
    {123456789012345680000.0, "123456789012345680000"},
    {-0.0, "0"},
    {2.5e-6, "0.0000025"},
    {(double)(float)0.1, "0.10000000149011612"},
    {(double)(float)1234.567, "1234.5670166015625"},
    {128.12345, "128.12345"},
    {1234567.5, "1234567.5"},
  };
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    char buffer[40];
    qa_net_js_number(cases[i].value, buffer, sizeof buffer);
    CHECK_STR(buffer, cases[i].text);
    CHECK_STR(QA_JS_NUMBER(cases[i].value), cases[i].text);
    /* The input module printed 0.00001 as "1e-5" before it shared this code. */
    char *input_text = qa_input_js_number(cases[i].value);
    CHECK_STR(input_text, cases[i].text);
    free(input_text);
  }
}

#define CHECK_NEAR(actual, expected)                                                 \
  do {                                                                              \
    double a_ = (actual), e_ = (expected);                                          \
    checks++;                                                                       \
    if (!(fabs(a_ - e_) <= 1e-9 * (1 + fabs(e_)))) {                                \
      failures++;                                                                   \
      fprintf(stderr, "FAIL %s:%d: got %.17g, want %.17g\n", __FILE__, __LINE__, a_, e_); \
    }                                                                               \
  } while (0)

/* Call arguments run right to left under GCC: add3(scale3(d, unit()), scale3(r,
 * signed())) drew the second random first, so every Q2 berserk-slam and steam
 * particle flew along a different vector than in TypeScript. Expected values:
 *   const p = new SourceParticles(new SourceRandom(12345), 3);
 *   p.q2BerserkSlam({x:0,y:0,z:0}, {x:1,y:0,z:0}, 0);  p.sample(0.5, 0).q2
 *   const p2 = new SourceParticles(new SourceRandom(777), 2);
 *   p2.q2Steam({x:0,y:0,z:0}, {x:0,y:0,z:1}, 8, 2, 100, 0, true);  p2.sample(0.25, 0).q2 */
static void check_particles(qa_app_source_particles *p, double seconds, const double (*want)[4], size_t count) {
  qa_scene_particle *q1 = NULL, *q2 = NULL;
  size_t q1_count = 0, q2_count = 0;
  qa_app_source_particles_sample(p, seconds, 0, &q1, &q1_count, &q2, &q2_count);
  CHECK(q2_count == count);
  for (size_t i = 0; i < count && i < q2_count; i++) {
    CHECK_NEAR(q2[i].origin.x, want[i][0]);
    CHECK_NEAR(q2[i].origin.y, want[i][1]);
    CHECK_NEAR(q2[i].origin.z, want[i][2]);
    CHECK_NEAR(q2[i].alpha, want[i][3]);
  }
  free(q1);
  free(q2);
}

static void test_particle_random_order(void) {
  static const double slam[][4] = {
    {2.4405040740966797, 11.850947380065918, -82.37654113769531, 0.09176834507647358},
    {1.3154698610305786, -61.211830139160156, -51.6080207824707, 0.3442061906839532},
    {25.187292098999023, 54.110050201416016, 22.84933090209961, 0.011028479675483815},
  };
  qa_app_sim_source_random *random = qa_app_sim_source_random_create(12345, "classic");
  qa_app_source_particles *p = qa_app_source_particles_create(random, 3);
  qa_vec3 origin = {0, 0, 0}, east = {1, 0, 0}, up = {0, 0, 1};
  qa_app_source_particles_q2_berserk_slam(p, origin, east, 0);
  check_particles(p, 0.5, slam, 3);
  qa_app_source_particles_destroy(p);
  qa_app_sim_source_random_destroy(random);

  static const double steam[][4] = {
    {0.07441425323486328, -14.397666931152344, 31.84499740600586, 0.5421915343087396},
    {-6.9334492683410645, -11.627603530883789, 24.77996063232422, 0.5767696703228573},
  };
  random = qa_app_sim_source_random_create(777, "classic");
  p = qa_app_source_particles_create(random, 2);
  qa_app_source_particles_q2_steam(p, origin, up, 8, 2, 100, 0, true);
  check_particles(p, 0.25, steam, 2);
  qa_app_source_particles_destroy(p);
  qa_app_sim_source_random_destroy(random);
}

int main(void) {
  test_js_number_spelling();
  test_particle_random_order();
  printf("eval order: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
