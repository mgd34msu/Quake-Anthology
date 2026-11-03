/* Rows read against src/render/scene/visibility.ts: ancestorIndex, the
 * ancestor mark LRU, mergedPvs and leafVisible. Reproduces
 * tests/render/visibility.test.ts on the C cache, and checks cached and
 * uncached traversals agree. GPL-2.0-or-later. */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/contracts/scene.h"
#include "qa/core/math.h"
#include "qa/render/scene/visibility.h"

static int failures, checks;
#define CHECK(c)                                                 \
  do {                                                           \
    checks++;                                                    \
    if (!(c)) {                                                  \
      failures++;                                                \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
    }                                                            \
  } while (0)

static const qa_bounds box = {{-16, -16, -16}, {16, 16, 16}};

typedef struct q3_map {
  qa_bsp_plane plane;
  qa_bsp_node *nodes;
  qa_q3_bsp_leaf *leaves;
  double *leaf_surfaces;
  qa_q3_bsp_surface *surfaces;
  uint8_t bits[8];
  qa_q3_visibility vis;
  qa_q3_world_geometry geometry;
} q3_map;

/* visibility.test.ts balancedMap: heap-ordered nodes on plane x=0; the camera
 * descends into the last leaf (cluster 0); leaf 0 is cluster 1, area 1. */
static void balanced_map(q3_map *m, int depth, uint8_t row) {
  size_t leaf_count = (size_t)1 << depth, node_count = leaf_count - 1;
  memset(m, 0, sizeof *m);
  m->plane.plane.normal = (qa_vec3){1, 0, 0};
  m->nodes = calloc(node_count, sizeof *m->nodes);
  m->leaves = calloc(leaf_count, sizeof *m->leaves);
  m->leaf_surfaces = calloc(leaf_count, sizeof *m->leaf_surfaces);
  m->surfaces = calloc(leaf_count, sizeof *m->surfaces);
  for (size_t i = 0; i < node_count; i++) {
    m->nodes[i].bounds = box;
    for (int side = 0; side < 2; side++) {
      size_t heap = 2 * i + 1 + (size_t)side;
      m->nodes[i].children[side] = heap < node_count ? (qa_bsp_child){true, (double)heap}
                                                    : (qa_bsp_child){false, (double)(heap - node_count)};
    }
  }
  for (size_t i = 0; i < leaf_count; i++) {
    m->leaves[i].cluster = i == leaf_count - 1 ? 0 : i == 0 ? 1 : 2;
    m->leaves[i].area = i == 0 ? 1 : 0;
    m->leaves[i].bounds = box;
    m->leaves[i].surfaces = (qa_index_range){(double)i, 1};
    m->leaf_surfaces[i] = (double)i;
  }
  m->bits[0] = row;
  for (int i = 1; i < 8; i++) m->bits[i] = 0xff;
  m->vis = (qa_q3_visibility){8, 1, {m->bits, 8}};
  m->geometry.entities = "";
  m->geometry.planes = &m->plane; m->geometry.plane_count = 1;
  m->geometry.nodes = m->nodes; m->geometry.node_count = node_count;
  m->geometry.leaves = m->leaves; m->geometry.leaf_count = leaf_count;
  m->geometry.leaf_surfaces = (qa_double_view){m->leaf_surfaces, leaf_count};
  m->geometry.surfaces = m->surfaces; m->geometry.surface_count = leaf_count;
  m->geometry.visibility = &m->vis;
}
static void free_map(q3_map *m) { free(m->nodes); free(m->leaves); free(m->leaf_surfaces); free(m->surfaces); }

static qa_scene_camera camera(void) {
  qa_scene_camera c;
  memset(&c, 0, sizeof c);
  c.axis = qa_angles_to_axis((qa_vec3){0, 0, 0});
  c.viewport = (qa_rect){0, 0, 32, 32};
  return c;
}

static bool same_leaves(const qa_visible_world *v, const double *expected, size_t count) {
  if (v->leaf_count != count) return false;
  for (size_t i = 0; i < count; i++) if (v->leaves[i] != expected[i]) return false;
  return true;
}

static void visible(q3_map *m, qa_world_visibility_cache *cache, const double *areas, size_t area_count, bool has_areas,
                    qa_visible_world *out) {
  qa_world_visibility_options options;
  memset(&options, 0, sizeof options);
  options.no_cull = true;
  options.has_visible_areas = has_areas;
  options.visible_areas = areas;
  options.visible_area_count = area_count;
  qa_decoded_world world = {QA_DECODED_WORLD_Q3, {.q3 = &m->geometry}};
  CHECK(qa_visible_world_new(world, camera(), &options, cache, out));
}

int main(void) {
  q3_map sparse;
  balanced_map(&sparse, 14, 0x02);
  qa_world_visibility_cache cache;
  qa_world_visibility_cache_init(&cache);
  const double zero[1] = {0};
  qa_visible_world warm, again, uncached;
  visible(&sparse, &cache, NULL, 0, false, &warm);
  CHECK(same_leaves(&warm, zero, 1) && warm.surface_count == 1 && warm.surfaces[0] == 0);
  CHECK(cache.ancestor_built && !cache.ancestor_unsafe && cache.ancestor_mark_count == 1);
  visible(&sparse, &cache, NULL, 0, false, &again);
  CHECK(same_leaves(&again, zero, 1) && cache.ancestor_mark_count == 1);
  visible(&sparse, NULL, NULL, 0, false, &uncached);
  CHECK(same_leaves(&uncached, zero, 1));
  qa_visible_world_destroy(&warm); qa_visible_world_destroy(&again); qa_visible_world_destroy(&uncached);
  /* Visible-area changes select different marks. */
  const double area0[1] = {0}, area1[1] = {1};
  qa_visible_world by_area;
  visible(&sparse, &cache, area0, 1, true, &by_area);
  CHECK(by_area.leaf_count == 0);
  qa_visible_world_destroy(&by_area);
  visible(&sparse, &cache, area1, 1, true, &by_area);
  CHECK(same_leaves(&by_area, zero, 1));
  qa_visible_world_destroy(&by_area);
  visible(&sparse, &cache, NULL, 0, false, &by_area);
  CHECK(same_leaves(&by_area, zero, 1) && cache.ancestor_mark_count == 3);
  qa_visible_world_destroy(&by_area);
  /* Five distinct area sets: the LRU keeps four. */
  const double areas[5][1] = {{0}, {1}, {2}, {3}, {4}};
  for (int i = 0; i < 5; i++) {
    visible(&sparse, &cache, areas[i], 1, true, &by_area);
    CHECK(by_area.leaf_count == (i == 1 ? 1u : 0u));
    qa_visible_world_destroy(&by_area);
  }
  CHECK(cache.ancestor_mark_count == 4);
  qa_world_visibility_cache_destroy(&cache);
  free_map(&sparse);

  /* Dense PVS still visits every leaf, cached and uncached alike. */
  q3_map dense;
  balanced_map(&dense, 4, 0x07);
  qa_world_visibility_cache_init(&cache);
  qa_visible_world first, second, plain;
  visible(&dense, &cache, NULL, 0, false, &first);
  visible(&dense, &cache, NULL, 0, false, &second);
  visible(&dense, NULL, NULL, 0, false, &plain);
  CHECK(first.leaf_count == 16 && second.leaf_count == 16 && plain.leaf_count == 16);
  CHECK(memcmp(first.leaves, plain.leaves, 16 * sizeof *first.leaves) == 0);
  CHECK(memcmp(second.surfaces, plain.surfaces, 16 * sizeof *first.surfaces) == 0);
  qa_visible_world_destroy(&first); qa_visible_world_destroy(&second); qa_visible_world_destroy(&plain);
  qa_world_visibility_cache_destroy(&cache);

  /* A node reached twice makes single-parent marks unsafe: traversal still
   * matches the uncached result. */
  dense.nodes[2].children[0] = (qa_bsp_child){true, 3};
  qa_world_visibility_cache_init(&cache);
  visible(&dense, &cache, NULL, 0, false, &first);
  visible(&dense, NULL, NULL, 0, false, &plain);
  CHECK(cache.ancestor_unsafe && cache.ancestor_mark_count == 0);
  CHECK(first.leaf_count == plain.leaf_count &&
        memcmp(first.leaves, plain.leaves, first.leaf_count * sizeof *first.leaves) == 0);
  qa_visible_world_destroy(&first); qa_visible_world_destroy(&plain);
  qa_world_visibility_cache_destroy(&cache);
  free_map(&dense);

  /* Q1 solid children all share leaf zero; that must not mark the index unsafe. */
  {
    enum { DEPTH = 4, SLOTS = 1 << DEPTH, NODES = SLOTS - 1 };
    qa_bsp_node nodes[NODES];
    memset(nodes, 0, sizeof nodes);
    for (int i = 0; i < NODES; i++) {
      nodes[i].bounds = box;
      for (int side = 0; side < 2; side++) {
        int heap = 2 * i + 1 + side;
        int slot = heap - NODES;
        nodes[i].children[side] = heap < NODES ? (qa_bsp_child){true, heap}
                                               : (qa_bsp_child){false, slot == SLOTS - 1 ? 1 : slot == 0 ? 2 : 0};
      }
    }
    qa_q1_leaf leaves[3];
    memset(leaves, 0, sizeof leaves);
    for (int i = 0; i < 3; i++) { leaves[i].contents = i == 0 ? -2 : -1; leaves[i].bounds = box; }
    qa_bsp_plane plane;
    memset(&plane, 0, sizeof plane);
    plane.plane.normal = (qa_vec3){1, 0, 0};
    qa_q1_world_geometry q1;
    memset(&q1, 0, sizeof q1);
    q1.format = "bsp29"; q1.entities = "";
    q1.planes = &plane; q1.plane_count = 1;
    q1.nodes = nodes; q1.node_count = NODES;
    q1.leaves = leaves; q1.leaf_count = 3;
    qa_decoded_world world = {QA_DECODED_WORLD_Q1, {.q1 = &q1}};
    qa_world_visibility_options options;
    memset(&options, 0, sizeof options);
    options.no_cull = true;
    options.no_vis = true;
    qa_world_visibility_cache_init(&cache);
    qa_visible_world cached_q1, plain_q1;
    CHECK(qa_visible_world_new(world, camera(), &options, &cache, &cached_q1));
    CHECK(qa_visible_world_new(world, camera(), &options, NULL, &plain_q1));
    CHECK(cache.ancestor_built && !cache.ancestor_unsafe);
    CHECK(cached_q1.leaf_count == 2 && plain_q1.leaf_count == 2 &&
          memcmp(cached_q1.leaves, plain_q1.leaves, 2 * sizeof *cached_q1.leaves) == 0);
    qa_visible_world_destroy(&cached_q1); qa_visible_world_destroy(&plain_q1);
    qa_world_visibility_cache_destroy(&cache);
  }

  printf("test_render_rows: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
