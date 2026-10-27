/*
 * Port of key cases from tests/render/model-attachment.test.ts and
 * tests/render/particles/round-reset.test.ts. GPL-2.0-or-later.
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

#include "qa/core/math.h"
#include "qa/render/scene/models.h"
#include "qa/render/scene/models/attachment.h"
#include "qa/render/scene/models/grip.h"
#include "qa/render/scene/models/image_path.h"
#include "qa/render/scene/models/md3_bounds.h"
#include "qa/render/scene/models/replacements.h"
#include "qa/render/scene/models/shadedots.h"
#include "qa/render/scene/models/shadow_bounds.h"
#include "qa/render/scene/particles.h"

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

static bool same(double a, double b) {
  uint64_t abits, bbits;
  memcpy(&abits, &a, sizeof abits);
  memcpy(&bbits, &b, sizeof bbits);
  return abits == bbits || (isnan(a) && isnan(b));
}
static bool close_to(double a, double b) { return fabs(a - b) < 1e-5; }
static void check_close_point(qa_vec3 actual, qa_vec3 expected) {
  CHECK(close_to(actual.x, expected.x));
  CHECK(close_to(actual.y, expected.y));
  CHECK(close_to(actual.z, expected.z));
}
static qa_model_transform transform_of(qa_vec3 origin, qa_vec3 a, qa_vec3 b,
                                       qa_vec3 c, qa_vec3 scale) {
  qa_model_transform out;
  out.origin = origin;
  out.axis.vectors[0] = a;
  out.axis.vectors[1] = b;
  out.axis.vectors[2] = c;
  out.scale = scale;
  return out;
}

static void test_transform_binary32(void) {
  const qa_model_transform transforms[5] = {
      transform_of((qa_vec3){0, -0.0, 0}, (qa_vec3){1, 0, 0}, (qa_vec3){0, 1, 0},
                   (qa_vec3){0, 0, 1}, (qa_vec3){1, 1, 1}),
      transform_of((qa_vec3){268435456.0, -134217728.0, 0.1}, (qa_vec3){2, 1, 0.1},
                   (qa_vec3){-1, 1, 3}, (qa_vec3){0.3, -2, 1}, (qa_vec3){-2, 0.1, 3}),
      transform_of((qa_vec3){-3, 7, 1}, (qa_vec3){16777216.0, -16777216.0, 1},
                   (qa_vec3){-16777216.0, 16777216.0, -1},
                   (qa_vec3){1, 1, 0x1p-149}, (qa_vec3){1, 1, 1}),
      transform_of((qa_vec3){1e30, -1e30, 0x1p-149}, (qa_vec3){0.1, -0.7, 0.3},
                   (qa_vec3){0.7, 0.1, -0.2}, (qa_vec3){-0.3, 0.2, 0.9},
                   (qa_vec3){1e-20, -1e20, 0}),
      transform_of((qa_vec3){12, -7, 5}, (qa_vec3){0, 1, 0}, (qa_vec3){-1, 0, 0},
                   (qa_vec3){0, 0, 1}, (qa_vec3){1, 1, 1}),
  };
  const double values[12] = {0, -0.0, 1, -1, 0.1, 1 + 0x1p-24, 0x1p-149,
                             -0x1p-149, 0x1p-150, 16777217.0, 1e30, -1e30};
  for (int ti = 0; ti < 5; ti++) {
    qa_model_transform transform = transforms[ti];
    for (int xi = 0; xi < 12; xi++)
      for (int yi = 0; yi < 12; yi++)
        for (int zi = 0; zi < 12; zi++) {
          qa_vec3 value = {values[xi], values[yi], values[zi]};
          qa_vec3 direction = qa_add3(
              qa_add3(qa_scale3(transform.axis.vectors[0], value.x * transform.scale.x),
                      qa_scale3(transform.axis.vectors[1], value.y * transform.scale.y)),
              qa_scale3(transform.axis.vectors[2], value.z * transform.scale.z));
          qa_vec3 point = qa_add3(transform.origin, direction);
          qa_vec3 actual_direction = qa_model_world_direction(transform, value);
          qa_vec3 actual_point = qa_model_world_point(transform, value);
          CHECK(same(actual_direction.x, direction.x));
          CHECK(same(actual_direction.y, direction.y));
          CHECK(same(actual_direction.z, direction.z));
          CHECK(same(actual_point.x, point.x));
          CHECK(same(actual_point.y, point.y));
          CHECK(same(actual_point.z, point.z));
        }
  }
}

static void test_attachment(void) {
  qa_model_transform source =
      transform_of((qa_vec3){12, -7, 5}, (qa_vec3){0, 1, 0}, (qa_vec3){-1, 0, 0},
                   (qa_vec3){0, 0, 1}, (qa_vec3){1, 1, 1});
  qa_model_transform destination =
      transform_of((qa_vec3){-3, 4, -2}, (qa_vec3){1, 0, 0}, (qa_vec3){0, 0, 1},
                   (qa_vec3){0, -1, 0}, (qa_vec3){1, 1, 1});
  qa_model_transform placement = qa_align_model_attachment(source, destination);
  qa_model_transform registered = qa_compose_model_transform(placement, source);
  check_close_point(registered.origin, destination.origin);
  const qa_vec3 points[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  for (int i = 0; i < 3; i++)
    check_close_point(qa_model_world_point(registered, points[i]),
                      qa_model_world_point(destination, points[i]));
  CHECK(placement.scale.x == 1 && placement.scale.y == 1 && placement.scale.z == 1);
  /* Grip registers on the destination origin (held-weapon case shape,
   * with an inline grip in place of the content registrations). */
  qa_model_transform grip =
      transform_of((qa_vec3){1, 2, 3}, (qa_vec3){0, 0, 1}, (qa_vec3){1, 0, 0},
                   (qa_vec3){0, 1, 0}, (qa_vec3){1, 1, 1});
  qa_model_transform hand =
      transform_of((qa_vec3){4, 6, 5}, (qa_vec3){1, 0, 0}, (qa_vec3){0, 1, 0},
                   (qa_vec3){0, 0, 1}, (qa_vec3){1, 1, 1});
  check_close_point(
      qa_model_world_point(qa_align_model_attachment(grip, hand), grip.origin),
      hand.origin);
}

static void test_q3_view_origin(void) {
  qa_vec3 origin = {674.6016845703125, 2103.394287109375, 25.288625717163086};
  qa_vec3 camera = qa_add3(origin, (qa_vec3){8, 10, 12});
  qa_axis zero_axis = {{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}};
  qa_vec3 result = qa_q3_model_view_origin(origin, zero_axis, camera, true);
  CHECK(result.x == 0 && result.y == 0 && result.z == 0);
  qa_model_transform zero = transform_of(origin, (qa_vec3){0, 0, 0},
                                         (qa_vec3){0, 0, 0}, (qa_vec3){0, 0, 0},
                                         (qa_vec3){1, 1, 1});
  CHECK_ABORTS((void)qa_model_local_delta(zero, camera));
  qa_axis scaled_axis = {{{2, 0, 0}, {0, 3, 0}, {0, 0, 4}}};
  qa_vec3 scaled = qa_q3_model_view_origin((qa_vec3){0, 0, 0}, scaled_axis,
                                           (qa_vec3){8, 10, 12}, false);
  CHECK(scaled.x == 16 && scaled.y == 30 && scaled.z == 48);
  qa_vec3 normalized = qa_q3_model_view_origin((qa_vec3){0, 0, 0}, scaled_axis,
                                               (qa_vec3){8, 10, 12}, true);
  CHECK(normalized.x == 8 && normalized.y == 15 && normalized.z == 24);
}

static void test_lighting_and_tables(void) {
  CHECK(QA_SHADEDOT_QUANT == 16 && QA_ANORM_DOTS_ROW == 256);
  CHECK(qa_r_avertexnormal_dots[0] == 1.23f);
  CHECK(qa_r_avertexnormal_dots[1] == 1.30f);
  CHECK(qa_r_avertexnormal_dots[4095] == 1.0f);
  qa_vec3 color;
  CHECK(!qa_q2_shell_color(0, &color));
  CHECK(qa_q2_shell_color(1024 | 2048 | 4096, &color) && color.x == 1 && color.z == 1);
  CHECK(qa_q2_shell_color(65536, &color) && color.x == 0.9 && color.y == 0.7);
  qa_vec3 light = qa_q2_alias_light(8, (qa_vec3){0, 0, 0}, 0, false, false);
  CHECK(light.x == 1 && light.y == 1 && light.z == 1);
  qa_vec3 shadow = qa_q1_alias_shadow_direction(0);
  CHECK(close_to(shadow.z, (float)(1 / sqrt(2))));
  CHECK(qa_alias_shade_divisor((qa_vec3){0.25, 0, 0}) == 1);
  CHECK(qa_alias_shade_divisor((qa_vec3){2, 0, 0}) == 4);
  char *path = qa_model_image_path("models\\items/../weapons/skin.pcx");
  CHECK(path != NULL);
  free(path);
  CHECK_ABORTS(free(qa_model_image_path("../escape.pcx")));
  CHECK_ABORTS(free(qa_model_image_path("c:/absolute.pcx")));
}

/* Round-reset fixtures. */
static qa_particle_shader shaders[64];
static char shader_names[64][32];
static int registrations = 0;
static const qa_particle_shader *register_shader(void *ctx, const char *name) {
  (void)ctx;
  int index = registrations++;
  (void)snprintf(shader_names[index], sizeof shader_names[index], "%s", name);
  shaders[index].name = shader_names[index];
  return &shaders[index];
}
static qa_particle_trace trace_stub(void *ctx, qa_vec3 start, qa_vec3 end,
                                    qa_bounds bounds, double pass, double contents) {
  (void)ctx; (void)start; (void)bounds; (void)pass; (void)contents;
  return (qa_particle_trace){end, 1022, "clear", 1};
}
static const char *config_stub(void *ctx, double index) {
  (void)ctx; (void)index;
  return "";
}
static void print_stub(void *ctx, const char *message) {
  (void)ctx; (void)message;
}

static void test_particle_round_reset(void) {
  registrations = 0;
  double time = 0;
  qa_particle_resources resources = {NULL, register_shader};
  qa_particle_animations animations;
  CHECK(qa_load_particle_animations(&resources, NULL, &animations));
  qa_game_random random;
  qa_game_random_init(&random, 7);
  qa_axis axis = qa_angles_to_axis((qa_vec3){0, 0, 0});
  qa_vec3 snap_origin = {-100, 0, 0};
  qa_particle_client_state state = {&time, &axis, &snap_origin};
  qa_particle_host host = {0};
  host.animations = animations;
  host.media = (qa_particle_media){NULL, NULL, NULL};
  host.prediction = (qa_particle_tracer){NULL, trace_stub};
  host.random = &random;
  host.hardware_type = "generic";
  host.config_string = config_stub;
  host.print = print_stub;
  qa_particle_system system;
  qa_particle_system_init(&system, state, host, NULL);
  qa_particle_explosion_request request = {"explode1", {0, 0, 0}, {0, 0, 0},
                                           1000, 4, 16};
  qa_particle_system_explosion(&system, request);
  size_t poly_count = 0;
  qa_ref_poly *polys = qa_particle_system_add_particles(&system, NULL, &poly_count);
  CHECK(poly_count == 1);
  const qa_particle_shader *shader = poly_count > 0 ? polys[0].shader : NULL;
  CHECK(shader != NULL);
  qa_ref_polys_destroy(polys, poly_count);
  int count = registrations;
  CHECK(qa_particle_system_active_count(&system) == 1);
  for (int round = 0; round < 3; round++) {
    qa_particle_system_reset_round(&system);
    CHECK(qa_particle_system_active_count(&system) == 0);
    size_t empty_count = 0;
    qa_ref_poly *empty =
        qa_particle_system_add_particles(&system, NULL, &empty_count);
    CHECK(empty_count == 0);
    qa_ref_polys_destroy(empty, empty_count);
    time += 400;
    qa_particle_system_explosion(&system, request);
    size_t round_count = 0;
    qa_ref_poly *round_polys =
        qa_particle_system_add_particles(&system, NULL, &round_count);
    CHECK(round_count == 1);
    CHECK(round_count > 0 && round_polys[0].shader == shader);
    qa_ref_polys_destroy(round_polys, round_count);
    CHECK(registrations == count);
  }
  qa_particle_system_destroy(&system);
  qa_particle_animations_destroy(&animations);
}

static void test_particles_misc(void) {
  qa_q2_particle_state state = {1000, {0, 0, 0}, {10, 0, 0}, {0, 0, -5}, 3, 1, -0.5};
  qa_scene_particle sample;
  CHECK(qa_sample_q2_particle(state, 2000, &sample));
  CHECK(sample.palette_index == 3 && sample.alpha == 0.5);
  CHECK(!qa_sample_q2_particle(state, 4000, &sample));
  qa_model_owned_geometry beam =
      qa_q2_beam_geometry((qa_vec3){0, 0, 0}, (qa_vec3){10, 0, 0}, 4,
                          (qa_vec4){255, 0, 0, 255});
  CHECK(beam.vertex_count == 12 && beam.index_count == 36);
  qa_model_owned_geometry_destroy(&beam);
  qa_entity_geometry sprite = qa_sprite_geometry(
      (qa_sprite_pose){{0, 0, 0}, 10, 0, {255, 255, 255, 255}},
      (qa_axis){{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}, false);
  CHECK(sprite.vertex_count == 4 && sprite.index_count == 6);
  qa_entity_geometry_destroy(&sprite);
}

int main(void) {
  test_transform_binary32();
  test_attachment();
  test_q3_view_origin();
  test_lighting_and_tables();
  test_particle_round_reset();
  test_particles_misc();
  printf("render models: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
