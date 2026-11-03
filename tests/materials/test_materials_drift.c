/*
 * TS drift (D8c) cases for materials: tests/materials/sky.test.ts,
 * fog.test.ts case 2 and the materials.test.ts "shader numeric edge cases"
 * and "shader script recovery" blocks. Expected values are the TS test's,
 * which pass under bun at TS HEAD. GPL-2.0-or-later.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "qa/materials/material.h"
#include "qa/materials/sky.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                \
  do {                                                                             \
    checks++;                                                                      \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

static qa_material_vertex vertex(double x, double y, double z) {
  qa_material_vertex v;
  memset(&v, 0, sizeof v);
  v.position = (qa_vec3){x, y, z};
  v.normal = (qa_vec3){0, 0, 1};
  v.color = (qa_vec4){255, 255, 255, 255};
  return v;
}

/* sky.test.ts: negative cloud heights keep every cloud texture coordinate finite. */
static void test_sky_negative_heights(void) {
  const double heights[4] = {64, -64, -512, -4096};
  for (int h = 0; h < 4; h++) {
    qa_sky_builder builder;
    qa_sky_builder_init(&builder);
    qa_sky_builder_initialize_cloud_coordinates(&builder, heights[h]);
    qa_material_vertex hull[6] = {vertex(-1000, -1000, 100), vertex(1000, -1000, 100),
                                  vertex(1000, 1000, 100),   vertex(-1000, 1000, 100),
                                  vertex(0, 0, 1000),        vertex(0, 0, -1000)};
    const double indices[18] = {0, 1, 2, 0, 2, 3, 0, 1, 4, 1, 2, 4, 2, 3, 4, 3, 0, 4};
    qa_deform_geometry mesh = {hull, 6, indices, 18};
    qa_sky_builder_clip(&builder, &mesh, 1, (qa_vec3){0, 0, 0});
    qa_sky_geometry sky = qa_sky_builder_build(&builder, (qa_vec3){0, 0, 0}, 4096);
    CHECK(sky.clouds.vertex_count > 0);
    for (size_t i = 0; i < sky.clouds.vertex_count; i++)
      CHECK(isfinite(sky.clouds.vertices[i].tex_coord.x) &&
            isfinite(sky.clouds.vertices[i].tex_coord.y));
    qa_sky_geometry_destroy(&sky);
  }
}

/* fog.test.ts case 2: rgbGen const with missing components reads the absent
 * ones as zero instead of failing the shader. */
static void test_rgbgen_const_missing(void) {
  qa_shader_definitions parsed;
  qa_parse_shader_script("t\n{\n{\nmap a.tga\nrgbGen const ( 0.5 )\n}\n}\n", NULL, &parsed);
  CHECK(parsed.count == 1 && parsed.items[0].stage_count == 1);
  if (parsed.count == 1 && parsed.items[0].stage_count == 1) {
    qa_color_generator rgb = parsed.items[0].stages[0].stage.rgb_gen;
    CHECK(strcmp(rgb.kind, "const") == 0);
    CHECK(rgb.color.x == 0.5 && rgb.color.y == 0 && rgb.color.z == 0);
  }
  qa_shader_definitions_free(&parsed);
}

/* materials.test.ts: out-of-range wave phase wraps to table index zero. */
static void test_wave_out_of_range(void) {
  qa_waveform wave = {"sin", 0, 1, 0, 1e30};
  qa_waveform still = wave;
  still.frequency = 0;
  CHECK(qa_evaluate_waveform(wave, 1) == qa_evaluate_waveform(still, 0));
  qa_waveform half = wave;
  half.frequency = 0.5;
  CHECK(isfinite(qa_evaluate_waveform(half, 21600 * 1e4)));
}

/* materials.test.ts: non-finite texture coordinate modifiers produce finite
 * coordinates. */
static void test_nonfinite_tcmod(void) {
  qa_shader_definitions parsed;
  qa_parse_shader_script(
      "t\n{\n{\nmap a.tga\ntcMod stretch sin 0 1 0 1\ntcMod scale 1e39 1e39\n}\n}\n", NULL,
      &parsed);
  CHECK(parsed.count == 1 && parsed.items[0].stage_count == 1);
  if (parsed.count == 1 && parsed.items[0].stage_count == 1) {
    qa_vec2 uv = qa_evaluate_tex_coords(&parsed.items[0].stages[0].stage, (qa_vec2){0, 0},
                                        (qa_vec3){0, 0, 0}, (qa_vec3){0, 0, 1}, 0, NULL);
    CHECK(isfinite(uv.x) && isfinite(uv.y));
  }
  qa_shader_definitions_free(&parsed);
}

/* materials.test.ts: a stray brace between shaders keeps its siblings. */
static void test_stray_brace_recovery(void) {
  const char *scripts[2] = {
      "a\n{\n{\nmap x.tga\n}\n}\n}\nb\n{\n{\nmap y.tga\n}\n}\n",
      "a\n{\n{\nmap x.tga\n}\n}\n{\nmap z.tga\n}\nb\n{\n{\nmap y.tga\n}\n}\n"};
  for (int i = 0; i < 2; i++) {
    qa_shader_script_inspection inspection;
    qa_inspect_shader_script(scripts[i], "test.shader", NULL, &inspection);
    CHECK(inspection.count == 2);
    if (inspection.count == 2) {
      CHECK(strcmp(inspection.entries[0].name, "a") == 0);
      CHECK(strcmp(inspection.entries[1].name, "b") == 0);
    }
    qa_shader_script_inspection_free(&inspection);
  }
}

int main(void) {
  test_sky_negative_heights();
  test_rgbgen_const_missing();
  test_wave_out_of_range();
  test_nonfinite_tcmod();
  test_stray_brace_recovery();
  printf("test_materials_drift: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
