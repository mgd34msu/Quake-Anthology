/*
 * Port of the material.ts / material-finish.ts cases in
 * tests/materials/materials.test.ts (shader script fixture, waveform
 * and texcoord evaluation, lightmap/base finish, implicit picture),
 * plus name-hash and registration-failure coverage. GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/materials/material.h"
#include "qa/materials/material_finish.h"
#include "qa/materials/source_state.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    checks++;                                                             \
    if (!(cond)) {                                                        \
      failures++;                                                         \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
    }                                                                     \
  } while (0)

#define CHECK_ABORTS(...)                                                 \
  do {                                                                    \
    checks++;                                                             \
    pid_t pid = fork();                                                   \
    if (pid == 0) {                                                       \
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */ \
      (void)freopen("/dev/null", "w", stderr);                            \
      __VA_ARGS__;                                                        \
      _exit(0);                                                           \
    }                                                                     \
    int status = 0;                                                       \
    waitpid(pid, &status, 0);                                             \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {          \
      failures++;                                                         \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,       \
              __LINE__, #__VA_ARGS__);                                    \
    }                                                                     \
  } while (0)

static const char *script =
    "textures/example/stone\n"
    "{\n"
    "  surfaceparm metalsteps\n"
    "  {\n"
    "    map $lightmap\n"
    "    rgbGen identity\n"
    "  }\n"
    "  {\n"
    "    map textures/example/stone.tga\n"
    "    blendFunc filter\n"
    "    rgbGen identity\n"
    "  }\n"
    "}\n"
    "textures/example/liquid\n"
    "{\n"
    "  surfaceparm water\n"
    "  cull none\n"
    "  deformVertexes wave 100 sin 0 2 0 0.5\n"
    "  {\n"
    "    animMap 2 textures/example/a.tga textures/example/b.tga\n"
    "    blendFunc blend\n"
    "    rgbGen wave triangle 0.5 0.5 0 1\n"
    "    tcMod scale 2 3\n"
    "    tcMod scroll 0.25 -0.5\n"
    "  }\n"
    "}";

static qa_mat_registered_image registered_image(double ordinal, double tmu) {
  qa_mat_registered_image image;
  memset(&image, 0, sizeof image);
  image.frame.image.ordinal = ordinal;
  image.frame.image.width = 4;
  image.frame.image.height = 4;
  image.tmu = tmu;
  return image;
}
typedef struct host_state {
  qa_mat_registered_image base;
  bool missing;
  int warnings;
  int finds;
} host_state;
static bool host_find(void *ctx, qa_mat_source_image_request request,
                      qa_mat_registered_image *out) {
  (void)request;
  host_state *state = (host_state *)ctx;
  state->finds++;
  if (state->missing) return false;
  *out = state->base;
  return true;
}
static bool host_play(void *ctx, const char *name, qa_mat_registered_video *out) {
  (void)ctx;
  (void)name;
  (void)out;
  return false;
}
static void host_sun(void *ctx, qa_mat_registered_sun sun) { (void)ctx; (void)sun; }
static void host_sky(void *ctx, double height) { (void)ctx; (void)height; }
static void host_warn_fn(void *ctx, const char *message) {
  (void)message;
  ((host_state *)ctx)->warnings++;
}
static qa_shader_registration_host make_host(host_state *state) {
  qa_shader_registration_host host;
  memset(&host, 0, sizeof host);
  host.white_image = registered_image(0, 0);
  host.default_image = registered_image(1, 0);
  host.lightmap_image = registered_image(2, 1);
  host.ctx = state;
  host.find_image = host_find;
  host.play_shader_cinematic = host_play;
  host.apply_sun = host_sun;
  host.initialize_sky_tex_coords = host_sky;
  host.print_warning = host_warn_fn;
  return host;
}
static qa_finish_shader_profile default_profile(void) {
  qa_finish_shader_profile profile;
  memset(&profile, 0, sizeof profile);
  profile.detail_textures = true;
  profile.hardware = "generic";
  profile.iterator.multitexture = true;
  profile.iterator.texture_env_add = true;
  profile.iterator.driver = "generic";
  return profile;
}

static void test_names(void) {
  char *value = qa_strip_shader_extension("textures/a.b/c");
  CHECK(strcmp(value, "textures/a") == 0);
  free(value);
  value = qa_normalize_shader_name("Textures/Stone.TGA");
  CHECK(strcmp(value, "textures/stone") == 0);
  free(value);
  CHECK(qa_shader_name_hash("textures/example/stone", 1024) == 586);
  CHECK(qa_shader_name_hash("textures/example/stone", 2048) == 1610);
  CHECK(qa_shader_name_hash("a", 1024) == 284);
  CHECK(qa_shader_name_hash("x\\y", 1024) == qa_shader_name_hash("x/y", 1024));
  CHECK(qa_same_shader_name("Stone", "sToNe"));
  CHECK(!qa_same_shader_name("stone", "stones"));
}

static void test_parse_and_evaluate(void) {
  qa_shader_definitions defs;
  qa_parse_shader_script(script, NULL, &defs);
  CHECK(defs.count == 2);
  const qa_shader_definition *stone = &defs.items[0];
  const qa_shader_definition *liquid = &defs.items[1];
  CHECK(strcmp(stone->name, "textures/example/stone") == 0);
  CHECK(stone->stage_count == 2);
  CHECK(stone->surface_parms.count == 1);
  CHECK(strcmp(stone->surface_parms.values[0], "metalsteps") == 0);
  CHECK(strcmp(stone->stages[0].stage.map.kind, "lightmap") == 0);
  CHECK(stone->stages[0].source_state.is_lightmap);
  CHECK(strcmp(stone->stages[1].stage.map.kind, "image") == 0);
  CHECK(strcmp(stone->stages[1].stage.map.name, "textures/example/stone.tga") == 0);
  CHECK(strcmp(stone->stages[1].stage.blend.source, "dst-color") == 0);
  CHECK(strcmp(liquid->cull, "none") == 0);
  CHECK(liquid->deform_count == 1);
  CHECK(strcmp(liquid->deforms[0].kind, "wave") == 0);
  CHECK(liquid->deforms[0].spread == (double)(float)(1.0 / 100.0));
  const qa_shader_stage_def *stage = &liquid->stages[0].stage;
  CHECK(strcmp(stage->map.kind, "animation") == 0);
  CHECK(stage->map.frequency == 2);
  CHECK(stage->map.frames.count == 2);
  CHECK(strcmp(stage->rgb_gen.kind, "wave") == 0);
  CHECK(stage->tc_mod_count == 2);
  qa_vec2 uv = {0.25, 0.5};
  qa_vec3 position = {0, 0, 0}, normal = {0, 0, 1};
  qa_vec2 result = qa_evaluate_tex_coords(stage, uv, position, normal, 1, NULL);
  CHECK(result.x == 0.75 && result.y == 2);
  qa_render_state state = qa_stage_state(stage, NULL);
  CHECK(strcmp(state.cull, "front") == 0);
  CHECK(state.depth_range[0] == 0 && state.depth_range[1] == 1);
  qa_shader_definitions_free(&defs);

  qa_waveform triangle = {"triangle", 0, 1, 0.25, 0};
  CHECK(qa_evaluate_waveform(triangle, 0) == 1);
  qa_waveform square = {"square", 0, 1, 0, 0};
  CHECK(qa_evaluate_waveform(square, 0) == 1);
  qa_waveform saw = {"sawtooth", 0, 1, 0.5, 0};
  CHECK(qa_evaluate_waveform(saw, 0) == 0.5);
  qa_waveform none = {"none", 0, 1, 0, 0};
  CHECK_ABORTS((void)qa_evaluate_waveform(none, 0));
}

static void test_register_and_finish(void) {
  qa_shader_script_inspection inspection;
  qa_inspect_shader_script(script, NULL, NULL, &inspection);
  CHECK(inspection.count == 2);
  CHECK(inspection.entries[0].accepted && inspection.entries[1].accepted);
  host_state state = {registered_image(1, 0), false, 0, 0};
  qa_shader_registration_host host = make_host(&state);
  qa_shader_registration_result registered;
  qa_shader_program_register(&inspection.entries[0].program, &host, &registered);
  CHECK(strcmp(registered.shader.kind, "defined") == 0);
  CHECK(registered.shader.stage_count == 2);
  CHECK(strcmp(registered.shader.stages[0].kind, "loaded") == 0);
  CHECK(registered.shader.stages[0].binding.playback.image.image.ordinal == 2);

  qa_finish_shader_input finish;
  finish.definition = registered.shader.definition;
  finish.lightmap_index = 0;
  finish.images = registered.shader.stages;
  finish.image_count = registered.shader.stage_count;
  finish.profile = default_profile();
  qa_finished_shader_result finished;
  qa_finish_shader(finish, &finished);
  CHECK(finished.shader.num_unfogged_passes == 1);
  CHECK(strcmp(finished.shader.iterator.multitexture_env, "modulate") == 0);
  CHECK(finished.shader.has_lightmap_stage);
  CHECK(finished.shader.lightmap_index == 0);
  CHECK(finished.shader.sort == 3);
  qa_finished_shader_result_free(&finished);
  qa_shader_registration_result_free(&registered);

  host_state missing = {registered_image(1, 0), true, 0, 0};
  qa_shader_registration_host missing_host = make_host(&missing);
  qa_shader_registration_result defaulted;
  qa_shader_program_register(&inspection.entries[0].program, &missing_host, &defaulted);
  CHECK(strcmp(defaulted.shader.kind, "defaulted") == 0);
  CHECK(strcmp(defaulted.shader.failure.kind, "missing-image") == 0);
  CHECK(strcmp(defaulted.shader.failure.request.name,
               "textures/example/stone.tga") == 0);
  qa_shader_registration_result_free(&defaulted);
  qa_shader_script_inspection_free(&inspection);
}

static void test_inspect_rejected_and_implicit(void) {
  qa_shader_script_inspection inspection;
  qa_inspect_shader_script("bad/one { bogusparm 1 { map x } } good/two { { map y } }",
                           "test.shader", NULL, &inspection);
  CHECK(inspection.count == 2);
  CHECK(!inspection.entries[0].accepted);
  CHECK(strcmp(inspection.entries[0].error.reason, "source-rejection") == 0);
  CHECK(inspection.entries[1].accepted);
  qa_shader_script_inspection_free(&inspection);

  qa_finish_implicit_shader_input implicit;
  memset(&implicit, 0, sizeof implicit);
  implicit.kind = "picture";
  implicit.name = "ui/icon";
  implicit.base_image.kind = "loaded";
  implicit.base_image.tmu = 0;
  implicit.base_image.binding.kind = "images";
  implicit.base_image.binding.playback.kind = "single";
  implicit.profile = default_profile();
  qa_finished_shader_result finished;
  qa_finish_implicit_shader(implicit, &finished);
  CHECK(finished.shader.source_stage_count == 1);
  CHECK(strcmp(finished.shader.source_stages[0].stage.depth_func, "always") == 0);
  CHECK(!finished.shader.source_stages[0].stage.depth_write);
  CHECK(strcmp(finished.shader.source_stages[0].stage.rgb_gen.kind, "vertex") == 0);
  CHECK(strcmp(finished.shader.source_stages[0].alpha_gen, "vertex") == 0);
  CHECK((finished.shader.source_stages[0].state_bits &
         QA_SOURCE_STATE_DEPTHTEST_DISABLE) != 0);
  qa_finished_shader_result_free(&finished);

  qa_finish_failed_shader_input failed = {"missing/shader", -1, default_profile()};
  qa_finished_shader_result failed_result;
  qa_finish_failed_shader(failed, &failed_result);
  CHECK(failed_result.shader.num_unfogged_passes == 0);
  CHECK(failed_result.shader.sort == 7);
  qa_finished_shader_result_free(&failed_result);

  qa_finish_shader_input bad;
  memset(&bad, 0, sizeof bad);
  bad.definition.name = "x";
  bad.definition.cull = "front";
  bad.lightmap_index = -5;
  bad.profile = default_profile();
  CHECK_ABORTS(qa_finished_shader_result r; qa_finish_shader(bad, &r));
}

int main(void) {
  test_names();
  test_parse_and_evaluate();
  test_register_and_finish();
  test_inspect_rejected_and_implicit();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
