/*
 * Port of tests/materials/fog.test.ts case 1: fog volume colors are scaled by
 * the identity light factor before quantizing to bytes. Expected values from
 * bun. (Case 2, rgbGen const with missing components, is in
 * test_materials_drift.c.) GPL-2.0-or-later.
 */
#include <stdio.h>

#include "qa/materials/fog.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                \
  do {                                                                             \
    checks++;                                                                      \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

int main(void) {
  qa_fog_brush_map_entry fogs[1] = {{0, -1}};
  qa_fog_brush_entry brushes[1] = {{0, 6}};
  qa_fog_brush_side_entry sides[6] = {{0}, {1}, {2}, {3}, {4}, {5}};
  qa_plane planes[6];
  for (int i = 0; i < 6; i++) {
    const double sign = i < 3 ? -1 : 1;
    const int axis = i % 3;
    planes[i].normal = (qa_vec3){axis == 0 ? sign : 0, axis == 1 ? sign : 0, axis == 2 ? sign : 0};
    planes[i].distance = 64;
  }
  qa_fog_brush_map map = {fogs, 1, brushes, 1, sides, 6, planes, 6};
  qa_q3_material_fog parameters = {{1, 0.5, 0.25}, 128};
  qa_vec4 color = qa_prepare_fog_volume(map, 0, parameters, 1).color;
  CHECK(color.x == 1 && color.y == 127.0 / 255 && color.z == 63.0 / 255 && color.w == 1);
  qa_vec4 dim = qa_prepare_fog_volume(map, 0, parameters, 0.5).color;
  CHECK(dim.x == 127.0 / 255);
  CHECK(dim.y == 63.0 / 255);
  CHECK(dim.z == 31.0 / 255);
  printf("test_fog_identity_light: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
