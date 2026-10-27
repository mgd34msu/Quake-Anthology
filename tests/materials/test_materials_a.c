/*
 * Port of key cases in tests/materials/materials.test.ts for the
 * materials slice-a modules. GPL-2.0-or-later.
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

#include "qa/materials.h"

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

static qa_renderer_image test_image(double ordinal) {
  qa_renderer_image image;
  memset(&image, 0, sizeof image);
  image.ordinal = ordinal;
  image.width = 4;
  image.height = 4;
  return image;
}

static qa_vec4 project_identity(void *ctx, qa_vec3 position) {
  (void)ctx;
  qa_vec4 out = {position.x, position.y, position.z, 1};
  return out;
}

static void test_light_styles_and_lightmaps(void) {
  CHECK(qa_q1_light_style("m", 0, 0) == 264);
  CHECK(qa_q1_light_style("am", 0.05, 1) == 0);
  CHECK(qa_q1_light_style("am", 0.05, 2) == 132);
  qa_q2_light_style style = qa_q2_light_style_value("m", 0);
  CHECK(style.rgb.x == 1 && style.rgb.y == 1 && style.rgb.z == 1);
  CHECK(style.white == 3);

  uint8_t lum[1] = {64};
  qa_bsp_lighting lighting = {false, {lum, 1}, NULL};
  double styles[2] = {0, 255};
  qa_lightmap_face face;
  memset(&face, 0, sizeof face);
  face.width = 1;
  face.height = 1;
  face.lighting = &lighting;
  face.styles = styles;
  face.style_count = 2;
  face.plane.normal = (qa_vec3){0, 0, 1};
  face.projection.kind = "classic";
  face.projection.texture.s = (qa_vec4){1, 0, 0, 0};
  face.projection.texture.t = (qa_vec4){0, 1, 0, 0};
  double scales[1] = {256};
  qa_build_q1_options options;
  memset(&options, 0, sizeof options);
  qa_built_lightmap q1 = qa_build_q1_lightmap(face, scales, 1, options);
  CHECK(q1.image.pixels.bytes[0] == 127 && q1.image.pixels.bytes[1] == 127 &&
        q1.image.pixels.bytes[2] == 127 && q1.image.pixels.bytes[3] == 255);
  qa_image_level direct = qa_direct_lightmap_pixels(q1);
  CHECK(direct.pixels.bytes[0] == 128 && direct.pixels.bytes[3] == 255);
  free((void *)direct.pixels.bytes);
  qa_built_lightmap_destroy(&q1);

  uint8_t rgb[3] = {200, 100, 50};
  qa_bsp_lighting rgb_lighting = {true, {rgb, 3}, "bspx"};
  face.lighting = &rgb_lighting;
  qa_q2_light_style q2_styles[1] = {style};
  qa_build_q2_options q2_options;
  memset(&q2_options, 0, sizeof q2_options);
  q2_options.has_modulate = true;
  q2_options.modulate = 2;
  qa_built_lightmap q2 = qa_build_q2_lightmap(face, q2_styles, 1, q2_options);
  CHECK(q2.image.pixels.bytes[0] == 255 && q2.image.pixels.bytes[1] == 127 &&
        q2.image.pixels.bytes[2] == 63 && q2.image.pixels.bytes[3] == 255);
  qa_built_lightmap_destroy(&q2);

  qa_lightmap_projection decoupled;
  memset(&decoupled, 0, sizeof decoupled);
  decoupled.kind = "decoupled";
  decoupled.mapping.axes[0] = (qa_vec3){0.5, 0, 0};
  decoupled.mapping.axes[1] = (qa_vec3){0, 0.25, 0};
  decoupled.mapping.offset = (qa_vec2){1, 2};
  qa_vec2 uv = qa_lightmap_coordinates((qa_vec3){4, 8, 0}, decoupled);
  CHECK(uv.x == 3 && uv.y == 4);

  qa_lightmap_atlas atlas;
  qa_lightmap_atlas_init(&atlas, 128, 128);
  qa_vec2 at;
  CHECK(qa_lightmap_atlas_allocate(&atlas, 16, 16, &at) && at.x == 0 && at.y == 0);
  CHECK(qa_lightmap_atlas_allocate(&atlas, 16, 16, &at) && at.x == 16 && at.y == 0);
  CHECK(!qa_lightmap_atlas_allocate(&atlas, 200, 16, &at));
  qa_lightmap_atlas_destroy(&atlas);
}

static void test_q2_lightgrid(void) {
  qa_q2_lightgrid_leaf leaves[1] = {{{0, 0, 0}, {2, 2, 2}, 0, 8}};
  qa_q2_lightgrid_sample samples[8];
  for (int i = 0; i < 8; i++) {
    double value = 10.0 * (i + 1);
    samples[i].style = 0;
    samples[i].rgb = (qa_vec3){value, value, value};
  }
  qa_q2_lightgrid grid;
  memset(&grid, 0, sizeof grid);
  grid.spacing = (qa_vec3){1, 1, 1};
  grid.scale = (qa_vec3){1, 1, 1};
  grid.size = (qa_vec3){2, 2, 2};
  grid.style_count = 1;
  grid.root.kind = QA_Q2_LIGHTGRID_LEAF;
  grid.root.index = 0;
  grid.leaves = leaves;
  grid.leaf_count = 1;
  grid.samples = samples;
  grid.sample_count = 8;
  qa_q2_light_style styles[1] = {qa_q2_light_style_value("m", 0)};
  qa_vec3 color;
  CHECK(qa_q2_light_grid_point(grid, (qa_vec3){0.5, 0.5, 0.5}, styles, 1,
                               NULL, &color));
  CHECK(fabs(color.x - 45.0 / 255) < 1e-6);
  CHECK(color.y == color.x);
}

static void test_legacy_materials(void) {
  qa_renderer_image base = test_image(1), lightmap_img = test_image(2);
  qa_renderer_image white = test_image(0);
  qa_legacy_texture_entry textures[3] = {
      {"+0button", base}, {"+1button", lightmap_img}, {"+Abutton", white}};
  qa_q1_texture_animations frames =
      qa_q1_texture_animations_build("+0button", textures, 3);
  CHECK(frames.animation_count == 2 && frames.alternate_animation_count == 1);
  qa_surface_lighting lighting;
  memset(&lighting, 0, sizeof lighting);
  lighting.kind = "unlit";
  qa_create_q1_options options;
  memset(&options, 0, sizeof options);
  options.animation = frames.animation;
  options.animation_count = frames.animation_count;
  options.alternate_animation = frames.alternate_animation;
  options.alternate_animation_count = frames.alternate_animation_count;
  qa_render_material material =
      qa_create_q1_material("+0button", base, lighting, options);
  CHECK(qa_q1_animated_texture(&material, 0.25, false).ordinal == 2);
  CHECK(qa_q1_animated_texture(&material, 0.25, true).ordinal == 0);
  CHECK(strcmp(qa_q1_surface_kind("*lava1"), "lava") == 0);
  CHECK(strcmp(qa_q1_surface_kind("sky4"), "sky") == 0);

  qa_material_vertex vertex;
  memset(&vertex, 0, sizeof vertex);
  vertex.normal = (qa_vec3){0, 0, 1};
  vertex.tex_coord = (qa_vec2){0.25, 0.5};
  vertex.lightmap_coord = (qa_vec2){0.75, 0.25};
  vertex.color = (qa_vec4){255, 128, 64, 255};
  qa_material_vertex vertices[3] = {vertex, vertex, vertex};
  vertices[1].position = (qa_vec3){1, 0, 0};
  vertices[2].position = (qa_vec3){0, 1, 0};
  double indices[3] = {0, 1, 2};
  qa_material_geometry geometry = {vertices, 3, indices, 3};
  qa_legacy_material_draw_context context;
  memset(&context, 0, sizeof context);
  context.fullbright = &white;
  context.q1_lightmap_encoding = "rgb";
  context.cull = "front";
  context.depth_range[1] = 1;
  context.project.project = project_identity;
  size_t count = 0;
  qa_draw_batch *batches =
      qa_prepare_legacy_material_batches(&material, geometry, &context, &count);
  CHECK(count == 2);
  CHECK(strcmp(batches[1].state.depth_test, "equal") == 0);
  CHECK(!batches[1].state.depth_write);
  CHECK(batches[1].vertices[0].color.x == 1 &&
        batches[1].vertices[0].color.w == 1);
  qa_mat_draw_batches_destroy(batches, count);
  qa_q1_texture_animations_destroy(&frames);

  CHECK_ABORTS((void)qa_create_q2_material("x", NULL, 0, lighting, 0, ""));
}

static void test_compile_flags_and_waveform(void) {
  const char *water[1] = {"water"};
  qa_surface_parameter_flags flags = qa_shader_surface_flags(water, 1);
  CHECK(flags.surface == 0 && flags.contents == 32 && flags.clear_solid);
  const char *metal[1] = {"MetalSteps"};
  flags = qa_shader_surface_flags(metal, 1);
  CHECK(flags.surface == 0x1000 && flags.contents == 0 && !flags.clear_solid);
  qa_waveform wave = {"triangle", 0, 1, 0.25, 0};
  CHECK(qa_mat_evaluate_waveform(wave, 0) == 1);
  CHECK(isfinite(qa_cloud_tex_coord(4, 0.5, 0.5, 512).x));
  qa_vec3 sky = qa_sky_vector(0, 1, 1, 2);
  CHECK(sky.x == 2 && sky.y == -2 && sky.z == 2);
}

static void test_fog_and_state(void) {
  CHECK(qa_fog_factor(0, 0.5) == 0);
  CHECK(qa_fog_factor(1, 31.0 / 32) == 1);
  qa_image_level texture = qa_create_fog_texture(NULL, 0);
  CHECK(texture.width == 256 && texture.height == 32);
  CHECK(texture.pixels.bytes[3] == 0);
  free((void *)texture.pixels.bytes);
  qa_source_state_input input = {"less-equal", true, false, NULL, NULL, "none"};
  CHECK(qa_source_state_bits(input, "fill", true) ==
        QA_SOURCE_STATE_DEPTHMASK_TRUE);
  input.depth_test = "equal";
  input.has_blend = true;
  input.blend_source = "one";
  input.blend_destination = "one";
  input.alpha_test = "gt0";
  uint32_t bits = qa_source_state_bits(input, "fill", true);
  CHECK(bits == (QA_SOURCE_STATE_DEPTHFUNC_EQUAL | QA_SOURCE_STATE_SRCBLEND_ONE |
                 QA_SOURCE_STATE_DSTBLEND_ONE | QA_SOURCE_STATE_DEPTHMASK_TRUE |
                 QA_SOURCE_STATE_ATEST_GT_0));
  CHECK(qa_source_state_changes(true, bits, bits, NULL, NULL) == 0);
  qa_q1_fog_state fog_state;
  qa_q1_fog_state_init(&fog_state);
  qa_q1_fog target = {0.5, {1, 0.5, 0}};
  qa_q1_fog_state_update(&fog_state, target, 0, 0);
  qa_q1_fog sampled = qa_q1_fog_state_sample(&fog_state, 10);
  CHECK(sampled.density == 0.5 && sampled.color.x == 1);
  qa_q1_fog_transition world = qa_q1_world_fog(
      "{\n\"classname\" \"worldspawn\"\n\"fog\" \"0.1 0.2 0.3 0.4\"\n}\n");
  CHECK(fabs(world.target.density - 0.1) < 1e-12);
  CHECK(fabs(world.target.color.x - 0.2) < 1e-12);
  CHECK(qa_height_fog_fraction(150, 50, 150) == 0.5);
}

static void test_noise_turbulence_and_color(void) {
  CHECK(qa_source_turbulence(0, QA_TURBULENCE_Q1) == 0);
  double full = qa_source_turbulence(1.5, QA_TURBULENCE_Q1);
  CHECK(qa_source_turbulence(1.5, QA_TURBULENCE_Q2) == (float)(full * 0.5));
  qa_renderer_noise noise;
  qa_renderer_noise_init(&noise);
  double sample = qa_renderer_noise_sample(&noise, 0.25, 0.5, 0.75, 1);
  /* Exact float32 value from the TypeScript RendererNoise. */
  CHECK(sample == -0.019027791917324066);
  CHECK(isfinite(sample) && sample >= -1 && sample <= 1);
  CHECK(sample == qa_renderer_noise_sample(&noise, 0.25, 0.5, 0.75, 1));
  CHECK(qa_animated_picture_index(0, 2, 4) == 0);
  CHECK(qa_animated_picture_index(0.5, 2, 4) == 1);

  qa_mat_shader_stage stage;
  memset(&stage, 0, sizeof stage);
  stage.rgb_gen.kind = "vertex";
  stage.alpha_gen.kind = "identity";
  qa_material_vertex vertex;
  memset(&vertex, 0, sizeof vertex);
  vertex.color = (qa_vec4){255, 128, 64, 255};
  qa_stage_color_context context;
  memset(&context, 0, sizeof context);
  context.identity_light = 1;
  context.entity_rgba = (qa_vec4){255, 255, 255, 255};
  context.noise = &noise;
  qa_vec4 color = qa_evaluate_stage_color(stage, vertex, context, false,
                                          false, 0);
  CHECK(color.x == 1 && color.y == 128.0 / 255 && color.z == 64.0 / 255);
}

static void test_q3_light_grid(void) {
  qa_bounds bounds[1] = {{{0, 0, 0}, {64, 64, 128}}};
  qa_lighting_map map;
  memset(&map, 0, sizeof map);
  map.model_bounds = bounds;
  map.model_count = 1;
  qa_prepare_light_grid_result result = qa_prepare_light_grid(map, 0, 0);
  CHECK(!result.has_grid);
  CHECK(strstr(result.diagnostic, "Light grid mismatch") != NULL);
  qa_vec3 falloff_at = {0, 0, 0};
  (void)falloff_at;
  CHECK(qa_point_light_falloff(0, 64) == 0.5);
  CHECK(qa_point_light_falloff(128, 64) == 0);
  qa_light_grid_destroy(&result.grid);
}

static void test_liquid_and_masks(void) {
  qa_vec2 uv = qa_liquid_tex_coords((qa_vec2){64, 0}, 0, QA_TURBULENCE_Q1,
                                    false);
  CHECK(uv.x == 1);
  qa_q3_dynamic_light lights[2];
  memset(lights, 0, sizeof lights);
  lights[0].origin = (qa_vec3){0, 0, 0};
  lights[0].radius = 10;
  lights[1].origin = (qa_vec3){100, 0, 0};
  lights[1].radius = 10;
  qa_bounds bounds = {{-5, -5, -5}, {5, 5, 5}};
  CHECK(qa_bmodel_dlight_mask(lights, 2, bounds) == 1);
  qa_q3_dynamic_light *moved = qa_transform_dlights(lights, 2,
      (qa_vec3){1, 0, 0},
      (qa_axis){{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}});
  CHECK(moved[0].origin.x == -1 && moved[1].origin.x == 99);
  free(moved);
}

int main(void) {
  test_light_styles_and_lightmaps();
  test_q2_lightgrid();
  test_legacy_materials();
  test_compile_flags_and_waveform();
  test_fog_and_state();
  test_noise_turbulence_and_color();
  test_q3_light_grid();
  test_liquid_and_masks();
  printf("materials-a: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
