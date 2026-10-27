/*
 * Port of tests/world/collision/contents.test.ts,
 * tests/world/collision/q1/collision.test.ts (synthetic cube cases),
 * and tests/world/q1-cell-split.test.ts (sampling form).
 * Real-BSP corpus cases (retail.test.ts, start.bsp in
 * q1/collision.test.ts) are skipped: pak fixtures are unavailable to
 * this C test. GPL-2.0-or-later.
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

#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
#include "qa/core/numeric.h"
#include "qa/world/collision/contents.h"
#include "qa/world/collision/media.h"
#include "qa/world/collision/q1/hull.h"
#include "qa/world/collision/q1/index.h"
#include "qa/world/collision/q3/checksum.h"
#include "qa/world/collision/q3/counters.h"
#include "qa/world/geometry/q1_solid/polyhedron.h"
#include "qa/world/geometry/q1_solid/sweep.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_ABORTS(...) do { checks++; pid_t pid = fork(); if (pid == 0) { (void)prctl(PR_SET_DUMPABLE, 0); freopen("/dev/null", "w", stderr); __VA_ARGS__; _exit(0); } int status = 0; waitpid(pid, &status, 0); if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) { failures++; fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__, __LINE__, #__VA_ARGS__); } } while (0)
#define CLOSE(a, b, tol) (fabs((a) - (b)) <= (tol))

static qa_actor_collision corpse(const char *family) {
  qa_actor_collision c;
  memset(&c, 0, sizeof c);
  c.family = family; c.shape_kind = "box"; c.contents = 0x04000000;
  c.role = "solid"; c.dead_monster = true;
  return c;
}
static void test_contents(void) {
  const double CORPSE = 0x04000000, BODY = 0x02000000;
  CHECK(qa_convert_contents(CORPSE, "q2", "q1") != -2);
  CHECK(qa_convert_contents(CORPSE, "q3", "q1") != -2);
  CHECK(qa_actor_contents(corpse("q2"), "q1") == -1);
  CHECK(qa_actor_contents(corpse("q3"), "q1") == -1);
  CHECK(qa_actor_contents(corpse("q1"), "q2") == CORPSE);
  CHECK(qa_actor_contents(corpse("q1"), "q3") == CORPSE);
  CHECK(qa_convert_contents(BODY, "q2", "q1") == -2);
  CHECK(qa_convert_contents(BODY, "q3", "q1") == -2);
  CHECK(qa_convert_contents(1, "q2", "q1") == -2);
  CHECK(qa_convert_contents(1, "q3", "q1") == -2);
  qa_actor_collision live = corpse("q2");
  live.contents = BODY; live.dead_monster = false;
  CHECK(qa_actor_contents(live, "q1") == -2);
  CHECK(qa_convert_contents(-2, "q1", "q3") == 1);
  CHECK(qa_convert_contents(-3, "q1", "q2") == 32);
  CHECK(qa_convert_surface_flags(4, "q2", "q3") == (4 | 16));
  CHECK(qa_convert_surface_flags(7, "q1", "q3") == 0);
}

/* Synthetic unit cube, mirroring q1/collision.test.ts cube(). */
static qa_bsp_plane cube_planes[6];
static qa_bsp_node cube_nodes[6];
static qa_q1_leaf cube_leaves[2];
static qa_q1_world_model cube_models[1];
static double cube_headnodes[4];
static qa_q1_world_geometry cube_geo;
static void build_cube(void) {
  static const double ns[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
  qa_bounds b = {{-1,-1,-1},{1,1,1}};
  for (int i = 0; i < 6; i++) {
    cube_planes[i].plane.normal = (qa_vec3){ns[i][0], ns[i][1], ns[i][2]};
    cube_planes[i].plane.distance = 1; cube_planes[i].type = 3; cube_planes[i].signbits = 0;
    cube_nodes[i].plane = i; cube_nodes[i].bounds = b;
    cube_nodes[i].faces = (qa_index_range){0, 0};
    cube_nodes[i].children[0] = (qa_bsp_child){false, 1};
    cube_nodes[i].children[1] = i == 5 ? (qa_bsp_child){false, 0} : (qa_bsp_child){true, i + 1};
  }
  cube_leaves[0] = (qa_q1_leaf){-2, b, {0,0}, false, 0, {0,0,0,0}};
  cube_leaves[1] = (qa_q1_leaf){-1, b, {0,0}, false, 0, {0,0,0,0}};
  cube_headnodes[0] = 0; cube_headnodes[1] = -1; cube_headnodes[2] = -1; cube_headnodes[3] = -1;
  cube_models[0].bounds = b; cube_models[0].origin = (qa_vec3){0,0,0};
  cube_models[0].headnodes = (qa_double_view){cube_headnodes, 4};
  cube_models[0].visible_leaves = 1; cube_models[0].faces = (qa_index_range){0,0};
  memset(&cube_geo, 0, sizeof cube_geo);
  cube_geo.format = "bsp29"; cube_geo.entities = "";
  cube_geo.planes = cube_planes; cube_geo.plane_count = 6;
  cube_geo.nodes = cube_nodes; cube_geo.node_count = 6;
  cube_geo.leaves = cube_leaves; cube_geo.leaf_count = 2;
  cube_geo.models = cube_models; cube_geo.model_count = 1;
}
static qa_trace_query cube_query(qa_vec3 s, qa_vec3 e) {
  qa_trace_query q;
  memset(&q, 0, sizeof q);
  q.start = s; q.end = e; q.shape.kind = "point";
  q.policy.kind = "q1"; q.policy.move = "normal"; q.policy.has_hull = false;
  q.numeric = qa_q1_donor_profile;
  return q;
}
static void test_q1_cube(void) {
  qa_q1_collision col;
  qa_q1_collision_options opts = {NULL, NULL};
  CHECK(qa_q1_collision_init(&col, &cube_geo, opts));
  qa_trace_result hit = qa_q1_collision_trace(&col, cube_query((qa_vec3){5,0,0}, (qa_vec3){0,0,0}));
  CHECK(CLOSE(hit.fraction, (5 - 1 - 1.0 / 32) / 5, 1e-12));
  CHECK(CLOSE(hit.end.x, 1 + 1.0 / 32, 1e-12));
  CHECK(hit.contents == -2);
  qa_trace_result exiting = qa_q1_collision_trace(&col, cube_query((qa_vec3){0,0,0}, (qa_vec3){5,0,0}));
  CHECK(exiting.start_solid && !exiting.all_solid && exiting.fraction == 1);
  /* Foreign box uses actual extents. */
  qa_trace_query bq = cube_query((qa_vec3){5,5,0}, (qa_vec3){0,0,0});
  bq.shape.kind = "box"; bq.shape.bounds = (qa_bounds){{-2,-2,-2},{2,2,2}};
  qa_trace_result box = qa_q1_collision_trace(&col, bq);
  CHECK(!box.start_solid);
  CHECK(CLOSE(box.end.x, 3 + 1.0 / 32, 1e-6));
  qa_trace_query cq = bq; cq.shape.kind = "capsule";
  qa_trace_result cap = qa_q1_collision_trace(&col, cq);
  CHECK(CLOSE(cap.end.x, 1 + (2 + 1.0 / 32) / sqrt(2), 1e-4));
  CHECK(cap.end.x < box.end.x);
  qa_trace_query eq = cube_query((qa_vec3){0,0,0}, (qa_vec3){0,0,0});
  eq.shape.kind = "box"; eq.shape.bounds = (qa_bounds){{-2,-2,-2},{2,2,2}};
  qa_trace_result emb = qa_q1_collision_trace(&col, eq);
  CHECK(emb.start_solid && emb.all_solid);
  qa_q1_geometry_coverage cov = qa_q1_collision_geometry_coverage(&col, 0);
  CHECK(strcmp(cov.arbitrary_shapes, "drawing-bsp-cells") == 0);
  CHECK(strcmp(cov.clip_only, "derived-native-clipspace") == 0);
  qa_q1_collision_destroy(&col);
}
static int inside_cell(const qa_convex_cell *cell, qa_vec3 p) {
  if (!cell) return 0;
  for (size_t i = 0; i < cell->face_count; i++)
    if (qa_cell_dot(p, cell->faces[i].plane.normal) - cell->faces[i].plane.distance > 1e-6) return 0;
  return cell->face_count >= 4;
}
static void test_split(void) {
  /* split children classify sample points identically to independent clips. */
  uint32_t state = 5;
  for (int round = 0; round < 60; round++) {
    qa_bounds b = {{-32,-32,-32},{32,32,32}};
    qa_convex_cell *cell = qa_box_cell(b);
    for (int step = 0; step < 6 && cell; step++) {
      state = state * 1664525u + 1013904223u; double r1 = (state >> 8) / 16777216.0;
      state = state * 1664525u + 1013904223u; double r2 = (state >> 8) / 16777216.0;
      state = state * 1664525u + 1013904223u; double r3 = (state >> 8) / 16777216.0;
      state = state * 1664525u + 1013904223u; double r4 = (state >> 8) / 16777216.0;
      qa_plane plane = {{r1 * 2 - 1, r2 * 2 - 1, r3 * 2 - 1}, r4 * 32 - 16};
      qa_split_result sp = qa_split_cell(cell, plane);
      qa_convex_cell *cf = qa_clip_cell(cell, qa_negate_plane(plane));
      qa_convex_cell *cb = qa_clip_cell(cell, plane);
      for (int s = 0; s < 40; s++) {
        state = state * 1664525u + 1013904223u; double x = ((state >> 8) / 16777216.0) * 64 - 32;
        state = state * 1664525u + 1013904223u; double y = ((state >> 8) / 16777216.0) * 64 - 32;
        state = state * 1664525u + 1013904223u; double z = ((state >> 8) / 16777216.0) * 64 - 32;
        qa_vec3 p = {x, y, z};
        double d = qa_cell_dot(p, plane.normal) - plane.distance;
        if (fabs(d) < 1e-4) continue;
        CHECK(inside_cell(sp.front, p) == inside_cell(cf, p));
        CHECK(inside_cell(sp.back, p) == inside_cell(cb, p));
      }
      if (cf && cf != cell) qa_convex_cell_destroy(cf);
      if (cb && cb != cell) qa_convex_cell_destroy(cb);
      qa_convex_cell *next = (round % 2 == 0) ? sp.front : sp.back;
      qa_convex_cell *other = (next == sp.front) ? sp.back : sp.front;
      if (other && other != cell) qa_convex_cell_destroy(other);
      if (next != cell) qa_convex_cell_destroy(cell);
      cell = next;
    }
    if (cell) qa_convex_cell_destroy(cell);
  }
}
static void test_sweep_checksum_media(void) {
  qa_bounds b = {{-1,-1,-1},{1,1,1}};
  qa_convex_cell *cell = qa_box_cell(b);
  qa_cell_shape shape;
  memset(&shape, 0, sizeof shape);
  shape.kind = "box";
  shape.axes[0] = qa_cell_axes[0]; shape.axes[1] = qa_cell_axes[1]; shape.axes[2] = qa_cell_axes[2];
  shape.extents = (qa_vec3){2,2,2};
  qa_sweep_interval iv;
  CHECK(qa_sweep_box_cell(cell, (qa_vec3){5,0,0}, (qa_vec3){0,0,0}, shape, 1.0 / 32, &iv));
  CHECK(CLOSE(iv.enter, (5 - 3) / 5.0, 1e-9));
  qa_convex_cell_destroy(cell);

  qa_collision_counters counters;
  qa_collision_counters_init(&counters);
  counters.c_traces = 3;
  qa_collision_counters_reset(&counters);
  CHECK(counters.c_traces == 0 && counters.c_brush_traces == 0);

  uint8_t bytes[64];
  for (int i = 0; i < 64; i++) bytes[i] = (uint8_t)i;
  qa_collision_checksum_lump lump = {0, 64};
  double sum = qa_collision_lump_checksum(bytes, sizeof bytes, lump);
  CHECK(sum == (double)(int32_t)sum);
  CHECK_ABORTS((void)qa_collision_lump_checksum(bytes, sizeof bytes, (qa_collision_checksum_lump){60, 8}));
}
static double media_plane_distance(void *ctx, qa_bsp_plane plane, int endpoint) {
  (void)ctx;
  qa_vec3 p = endpoint == 0 ? (qa_vec3){5,0,0} : (qa_vec3){-5,0,0};
  return p.x * plane.plane.normal.x + p.y * plane.plane.normal.y + p.z * plane.plane.normal.z - plane.plane.distance;
}
static void test_media(void) {
  qa_bsp_plane planes[2] = {{{{1,0,0}, 1}, 0, 0}, {{{-1,0,0}, 1}, 3, 1}};
  qa_medium_brush brush = {32, planes, 2}; /* q2 water contents */
  const qa_numeric_operations *n = qa_create_numeric_operations(&qa_q2_donor_profile);
  qa_trace_media media = qa_trace_brush_media(&brush, 1, "q2", media_plane_distance, NULL, n, 1);
  CHECK(media.in_open && media.in_water);
}
int main(void) {
  build_cube();
  test_contents();
  test_q1_cube();
  test_split();
  test_sweep_checksum_media();
  test_media();
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
