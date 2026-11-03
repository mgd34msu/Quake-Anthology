/* TS drift D7 checks for src/render: tests/render/particles/rail-geometry.test.ts and the pure
 * cases of tests/render/stencil-shadow.test.ts (volume construction and the finish quad).
 * Expected values are the TS test literals. GPL-2.0-or-later. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/render/scene/models/stencil_shadow.h"
#include "qa/render/scene/particles/primitives.h"
#include "qa/render/scene/shadow_geometry.h"
#include "qa/render/scene/view.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); } } while (0)

static size_t rail_vertices(const char *kind, double x) {
  qa_rail_pose pose; memset(&pose, 0, sizeof pose);
  pose.kind = kind; pose.origin = (qa_vec3){x, 0, 0}; pose.old_origin = (qa_vec3){0, 0, 0};
  pose.shader_rgba = (qa_vec4){255, 255, 255, 255};
  qa_entity_geometry geometry = qa_rail_geometry(pose, (qa_vec3){0, 100, 0}, NULL);
  size_t count = geometry.vertex_count;
  qa_entity_geometry_destroy(&geometry);
  return count;
}
/* "rails with source-undefined lengths draw nothing instead of throwing" */
static void test_rails(void) {
  const char *kinds[3] = {"rail-core", "rail-rings", "lightning"};
  for (int k = 0; k < 3; k++) {
    const double xs[3] = {NAN, 3e9, 1e10};
    for (int i = 0; i < 3; i++) CHECK(rail_vertices(kinds[k], xs[i]) == 0);
    CHECK(rail_vertices(kinds[k], 640) > 0);
  }
  CHECK(rail_vertices("rail-rings", 3.3e7 * 32) == 0);
}

static const qa_vec3 up = {0, 0, 1};
/* "a lone upward triangle lit from above casts a three-edge silhouette extruded 512 units" */
static void test_lone_triangle(void) {
  qa_vec3 positions[3] = {{0, 0, 0}, {10, 0, 0}, {0, 10, 0}};
  double indices[3] = {0, 1, 2};
  qa_stencil_shadow_volume volume;
  CHECK(qa_make_stencil_shadow_volume(positions, 3, indices, 3, up, &volume));
  CHECK(volume.position_count == 6);
  CHECK(volume.positions[3].x == 0 && volume.positions[3].y == 0 && volume.positions[3].z == -512);
  CHECK(volume.index_count == 18);
  const double first[6] = {0, 3, 1, 1, 3, 4};
  for (int i = 0; i < 6 && volume.index_count >= 6; i++) CHECK(volume.indices[i] == first[i]);
  qa_stencil_shadow_volume_destroy(&volume);
}
/* "an edge shared by two lit triangles is interior and a light behind the surface casts nothing" */
static void test_shared_edge(void) {
  qa_vec3 quad[4] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
  double indices[6] = {0, 1, 2, 0, 2, 3};
  qa_stencil_shadow_volume volume;
  CHECK(qa_make_stencil_shadow_volume(quad, 4, indices, 6, up, &volume) && volume.index_count == 24);
  qa_stencil_shadow_volume_destroy(&volume);
  CHECK(qa_make_stencil_shadow_volume(quad, 4, indices, 6, (qa_vec3){0, 0, -1}, &volume) && volume.index_count == 0);
  qa_stencil_shadow_volume_destroy(&volume);
  qa_vec3 *many = calloc(500, sizeof *many);
  CHECK(!qa_make_stencil_shadow_volume(many, 500, NULL, 0, up, &volume));
  free(many);
}
/* "the finish quad covers the view": every corner sits at w = 10 outside the clip square. */
static void test_finish_quad(void) {
  qa_scene_camera camera; memset(&camera, 0, sizeof camera);
  camera.axis.vectors[0] = (qa_vec3){1, 0, 0}; camera.axis.vectors[1] = (qa_vec3){0, 1, 0}; camera.axis.vectors[2] = (qa_vec3){0, 0, 1};
  camera.projection = qa_scene_perspective_projection(90, 90, 1000, 4);
  camera.viewport = (qa_rect){0, 0, 8, 8};
  qa_vec4 corners[4];
  qa_shadow_finish_quad(&camera, corners);
  for (int i = 0; i < 4; i++) CHECK(corners[i].w == 10 && fabs(corners[i].x) > corners[i].w && fabs(corners[i].y) > corners[i].w);
}

int main(void) {
  test_rails();
  test_lone_triangle();
  test_shared_edge();
  test_finish_quad();
  printf("test_d7_drift: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
