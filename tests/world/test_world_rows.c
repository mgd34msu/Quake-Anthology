/* Rows read against src/world/collision/q1/index.ts (derivedClipCells,
 * envelopeCoordinate), q1-solid/clipspace.ts (prepareQ1ClipSolids, cellBounds)
 * and polyhedron.ts (boundsForCell, capContainsPoint). Expected values come
 * from the TypeScript oracle on tests/world/collision/q1/collision.test.ts's
 * clip-only cube, printed to 17 significant digits. GPL-2.0-or-later. */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "qa/contracts/scene.h"
#include "qa/core/numeric.h"
#include "qa/world/collision/q1/hull.h"
#include "qa/world/collision/q1/index.h"
#include "qa/world/geometry/q1_solid/polyhedron.h"

static int failures, checks;
#define CHECK(c)                                                 \
  do {                                                           \
    checks++;                                                    \
    if (!(c)) {                                                  \
      failures++;                                                \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
    }                                                            \
  } while (0)

typedef struct oracle_case {
  qa_vec3 start, end;
  const char *kind;
  double size, fraction;
  int start_solid, all_solid;
  qa_vec3 normal;
  double distance, contents;
} oracle_case;
static const oracle_case oracle[] = {
  {{10,0,0},{-10000,0,0},"box",2,0.00029657842157842156,0,0,{1.0000000000000000,0.0000000000000000,0.0000000000000000},5.0000000000000000,-2},
  {{5,0,0},{-10,0,0},"box",2,1.0000000000000000,1,0,{0.0000000000000000,0.0000000000000000,0.0000000000000000},0.0000000000000000,-2},
  {{5,0,0},{0,0,0},"box",2,1.0000000000000000,1,1,{0.0000000000000000,0.0000000000000000,0.0000000000000000},0.0000000000000000,-2},
  {{40,30,20},{-40,-25,-15},"box",2,0.49034090909090911,0,0,{0.0000000000000000,1.0000000000000000,0.0000000000000000},1.0000000000000000,-2},
  {{30,-30,3},{-30,31,-2},"box",2,0.44211065573770492,0,0,{0.0000000000000000,-1.0000000000000000,0.0000000000000000},1.0000000000000000,-2},
  {{0,50,0},{0.5,-50,0.25},"box",2,0.46968749999999998,0,0,{0.0000000000000000,1.0000000000000000,0.0000000000000000},1.0000000000000000,-2},
  {{10,0,0},{-10000,0,0},"box",7,1.0000000000000000,1,0,{0.0000000000000000,0.0000000000000000,0.0000000000000000},0.0000000000000000,-2},
  {{5,0,0},{-10,0,0},"box",7,1.0000000000000000,1,0,{0.0000000000000000,0.0000000000000000,0.0000000000000000},0.0000000000000000,-2},
  {{5,0,0},{0,0,0},"box",7,1.0000000000000000,1,1,{0.0000000000000000,0.0000000000000000,0.0000000000000000},0.0000000000000000,-2},
  {{40,30,20},{-40,-25,-15},"box",7,0.39960937499999999,0,0,{1.0000000000000000,0.0000000000000000,0.0000000000000000},1.0000000000000000,-2},
  {{30,-30,3},{-30,31,-2},"box",7,0.36014344262295084,0,0,{0.0000000000000000,-1.0000000000000000,0.0000000000000000},1.0000000000000000,-2},
  {{0,50,0},{0.5,-50,0.25},"box",7,0.41968749999999999,0,0,{0.0000000000000000,1.0000000000000000,-2.7755575615628914e-16},0.99999999999999711,-2},
  {{10,0,0},{-10000,0,0},"box",13,1.0000000000000000,1,0,{0.0000000000000000,0.0000000000000000,0.0000000000000000},0.0000000000000000,-2},
  {{5,0,0},{-10,0,0},"box",13,1.0000000000000000,1,1,{0.0000000000000000,0.0000000000000000,0.0000000000000000},0.0000000000000000,-2},
  {{5,0,0},{0,0,0},"box",13,1.0000000000000000,1,1,{0.0000000000000000,0.0000000000000000,0.0000000000000000},0.0000000000000000,-2},
  {{40,30,20},{-40,-25,-15},"box",13,0.29034090909090909,0,0,{0.0000000000000000,1.0000000000000000,0.0000000000000000},1.0000000000000000,-2},
  {{30,-30,3},{-30,31,-2},"box",13,0.26178278688524592,0,0,{0.0000000000000000,-1.0000000000000000,0.0000000000000000},1.0000000000000000,-2},
  {{0,50,0},{0.5,-50,0.25},"box",13,0.35968749999999999,0,0,{0.0000000000000000,1.0000000000000000,0.0000000000000000},1.0000000000000000,-2},
  {{10,0,0},{-10000,0,0},"capsule",2,0.00029657842157842156,0,0,{1.0000000000000000,0.0000000000000000,0.0000000000000000},5.0000000000000000,-2},
  {{5,0,0},{-10,0,0},"capsule",2,1.0000000000000000,1,0,{0.0000000000000000,0.0000000000000000,0.0000000000000000},0.0000000000000000,-2},
  {{5,0,0},{0,0,0},"capsule",2,1.0000000000000000,1,1,{0.0000000000000000,0.0000000000000000,0.0000000000000000},0.0000000000000000,-2},
  {{40,30,20},{-40,-25,-15},"capsule",2,0.49293176579976089,0,0,{-6.6613381476917017e-16,0.93094895772152519,0.36514933673389877},1.2960982944554247,-2},
  {{30,-30,3},{-30,31,-2},"capsule",2,0.44211065573770492,0,0,{0.0000000000000000,-1.0000000000000000,0.0000000000000000},0.99999999999999989,-2},
  {{0,50,0},{0.5,-50,0.25},"capsule",2,0.46968750000000009,0,0,{-2.7755575615629012e-17,1.0000000000000000,-5.5511151231258024e-17},0.99999999999999989,-2},
  {{10,0,0},{-10000,0,0},"capsule",7,1.0000000000000000,1,0,{0.0000000000000000,0.0000000000000000,0.0000000000000000},0.0000000000000000,-2},
  {{5,0,0},{-10,0,0},"capsule",7,1.0000000000000000,1,0,{0.0000000000000000,0.0000000000000000,0.0000000000000000},0.0000000000000000,-2},
  {{5,0,0},{0,0,0},"capsule",7,1.0000000000000000,1,1,{0.0000000000000000,0.0000000000000000,0.0000000000000000},0.0000000000000000,-2},
  {{40,30,20},{-40,-25,-15},"capsule",7,0.40855410546805498,0,0,{0.32652279057966815,0.92984156137979146,0.16963943516428601},2.7320949494424180,-2},
  {{30,-30,3},{-30,31,-2},"capsule",7,0.36977401888830191,0,0,{0.39858400186806542,-0.91713182992132503,-6.3441315624789940e-17},2.9100518392616519,-2},
  {{0,50,0},{0.5,-50,0.25},"capsule",7,0.41968750000000005,0,0,{2.2997476938663969e-16,1.0000000000000000,-6.3441315692866121e-17},0.99999999999999634,-2},
  {{10,0,0},{-10000,0,0},"capsule",13,1.0000000000000000,1,0,{0.0000000000000000,0.0000000000000000,0.0000000000000000},0.0000000000000000,-2},
  {{5,0,0},{-10,0,0},"capsule",13,1.0000000000000000,1,1,{0.0000000000000000,0.0000000000000000,0.0000000000000000},0.0000000000000000,-2},
  {{5,0,0},{0,0,0},"capsule",13,1.0000000000000000,1,1,{0.0000000000000000,0.0000000000000000,0.0000000000000000},0.0000000000000000,-2},
  {{40,30,20},{-40,-25,-15},"capsule",13,0.33896325594555343,0,0,{0.60432960707142080,0.79528429564330849,0.048048050116825010},3.8649803811172374,-2},
  {{30,-30,3},{-30,31,-2},"capsule",13,0.29708160556785984,0,0,{0.55021306053280550,-0.83502430384936910,0.0000000000000000},3.5860896065133967,-2},
  {{0,50,0},{0.5,-50,0.25},"capsule",13,0.35968750000000005,0,0,{6.4051328343759066e-18,1.0000000000000000,-6.8321416900009674e-17},0.99999999999999989,-2},
};

static qa_bsp_plane planes[18];
static qa_bsp_node nodes[6];
static qa_q1_leaf leaves[2];
static qa_q1_clip_node clipnodes[12];
static qa_q1_world_model models[1];
static double headnodes[4] = {0, 0, 6, -1};
static qa_q1_world_geometry geometry;

/* collision.test.ts clipOnlyCube(), with the clip hulls pushed 4 units along x
 * and drawing headnode 0, as "drawing hits bound clip reconstruction". */
static void build_geometry(void) {
  static const double ns[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
  qa_bounds b = {{-1,-1,-1},{1,1,1}};
  for (int i = 0; i < 6; i++) {
    planes[i].plane.normal = (qa_vec3){ns[i][0], ns[i][1], ns[i][2]};
    planes[i].plane.distance = 1; planes[i].type = 3;
    nodes[i].plane = i; nodes[i].bounds = b;
    nodes[i].children[0] = (qa_bsp_child){false, 1};
    nodes[i].children[1] = i == 5 ? (qa_bsp_child){false, 0} : (qa_bsp_child){true, i + 1};
  }
  leaves[0] = (qa_q1_leaf){-2, b, {0,0}, false, 0, {0,0,0,0}};
  leaves[1] = (qa_q1_leaf){-1, b, {0,0}, false, 0, {0,0,0,0}};
  static const qa_bounds hull_bounds[3] = {{{0,0,0},{0,0,0}}, {{-16,-16,-24},{16,16,32}}, {{-32,-32,-24},{32,32,64}}};
  size_t plane_count = 6, clip_count = 0;
  for (int hull = 1; hull <= 2; hull++) {
    qa_bounds h = hull_bounds[hull];
    for (int i = 0; i < 6; i++) {
      qa_vec3 n = planes[i].plane.normal;
      double offset = -(n.x * (n.x >= 0 ? h.min.x : h.max.x) + n.y * (n.y >= 0 ? h.min.y : h.max.y) +
                        n.z * (n.z >= 0 ? h.min.z : h.max.z));
      planes[plane_count] = planes[i];
      planes[plane_count].plane.distance = planes[i].plane.distance + offset + n.x * 4;
      clipnodes[clip_count].plane = (double)plane_count;
      clipnodes[clip_count].children[0] = (qa_q1_clip_child){false, -1};
      clipnodes[clip_count].children[1] = i == 5 ? (qa_q1_clip_child){false, -2} : (qa_q1_clip_child){true, (double)(clip_count + 1)};
      plane_count++;
      clip_count++;
    }
  }
  models[0].bounds = b;
  models[0].headnodes = (qa_double_view){headnodes, 4};
  models[0].visible_leaves = 1;
  memset(&geometry, 0, sizeof geometry);
  geometry.format = "bsp29"; geometry.entities = "";
  geometry.planes = planes; geometry.plane_count = plane_count;
  geometry.nodes = nodes; geometry.node_count = 6;
  geometry.leaves = leaves; geometry.leaf_count = 2;
  geometry.clipnodes = clipnodes; geometry.clipnode_count = clip_count;
  geometry.models = models; geometry.model_count = 1;
}

static qa_trace_result trace(qa_q1_collision *c, const oracle_case *k, double shift) {
  qa_trace_query q;
  memset(&q, 0, sizeof q);
  q.start = (qa_vec3){k->start.x, k->start.y + shift, k->start.z};
  q.end = (qa_vec3){k->end.x, k->end.y + shift, k->end.z};
  q.shape.kind = k->kind;
  q.shape.bounds = (qa_bounds){{-k->size, -k->size, -k->size * 1.5}, {k->size, k->size, k->size * 1.5}};
  q.policy.kind = "q1"; q.policy.move = "normal"; q.policy.has_hull = false; q.policy.derived = true;
  q.numeric = qa_q1_donor_profile;
  return qa_q1_collision_trace(c, q);
}

static bool matches(const qa_trace_result *r, const oracle_case *k) {
  qa_vec3 n = r->contact.has_plane ? r->contact.plane.normal : (qa_vec3){0, 0, 0};
  double d = r->contact.has_plane ? r->contact.plane.distance : 0;
  bool same = r->fraction == k->fraction && r->start_solid == (k->start_solid != 0) &&
              r->all_solid == (k->all_solid != 0) && n.x == k->normal.x && n.y == k->normal.y &&
              n.z == k->normal.z && d == k->distance && r->contents == k->contents;
  if (!same)
    fprintf(stderr, "%s %g from (%g,%g,%g): fraction %.17g/%.17g normal (%.17g,%.17g,%.17g) d %.17g\n", k->kind,
            k->size, k->start.x, k->start.y, k->start.z, r->fraction, k->fraction, n.x, n.y, n.z, d);
  return same;
}

int main(void) {
  build_geometry();
  size_t count = sizeof oracle / sizeof oracle[0];
  qa_q1_collision_options options = {NULL, NULL};
  qa_q1_collision cached;
  CHECK(qa_q1_collision_init(&cached, &geometry, options));
  /* Cold, then cached, then after churn has evicted every entry. */
  for (int pass = 0; pass < 3; pass++) {
    for (size_t i = 0; i < count; i++) {
      qa_trace_result r = trace(&cached, &oracle[i], 0);
      CHECK(matches(&r, &oracle[i]));
    }
    CHECK(cached.clip_cell_entries <= 128);
    CHECK(cached.clip_cell_count <= 1024 && cached.clip_face_count <= 4096 && cached.clip_vertex_count <= 16384);
    if (pass == 1)
      for (int churn = 0; churn < 140; churn++) (void)trace(&cached, &oracle[3], 1000 + churn * 0.5);
  }
  CHECK(cached.prepared_clip != NULL && cached.prepared_clip[0] != NULL);
  qa_q1_collision_destroy(&cached);

  /* A fresh collision per trace never reuses an entry. */
  for (size_t i = 0; i < count; i++) {
    qa_q1_collision fresh;
    CHECK(qa_q1_collision_init(&fresh, &geometry, options));
    qa_trace_result r = trace(&fresh, &oracle[i], 0);
    CHECK(matches(&r, &oracle[i]));
    qa_q1_collision_destroy(&fresh);
  }

  /* boundsForCell fast path: a split by a plane clear of the box keeps it whole. */
  qa_convex_cell *box = qa_box_cell((qa_bounds){{-1, -1, -1}, {1, 1, 1}});
  CHECK(box->bounds_state == 1 && box->bmin[0] == -1 && box->bmax[2] == 1);
  qa_split_result away = qa_split_cell(box, (qa_plane){{1, 0, 0}, 5});
  CHECK(away.front == NULL && away.back == box);
  qa_split_result across = qa_split_cell(box, (qa_plane){{1, 0, 0}, 0});
  CHECK(across.front != NULL && across.back != NULL && across.front != box);
  CHECK(across.front->bounds_state == 1 && across.front->bmin[0] == 0 && across.front->bmax[0] == 1);
  CHECK(across.back->bounds_state == 1 && across.back->bmin[0] == -1 && across.back->bmax[0] == 0);
  qa_convex_cell *copy = qa_convex_cell_clone(across.front);
  CHECK(copy->face_count == across.front->face_count && copy->faces != across.front->faces);
  qa_convex_cell_destroy(copy);
  qa_convex_cell_destroy(across.front);
  qa_convex_cell_destroy(across.back);
  qa_convex_cell_destroy(box);

  printf("test_world_rows: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
