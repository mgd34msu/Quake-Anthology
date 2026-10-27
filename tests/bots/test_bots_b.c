/*
 * Port of key cases from tests/bots/behavior/rerelease (chat-text) and
 * tests/bots/navigation (entity-binding, estimates generic path), plus
 * direct TS-behavior checks for the bots-b ports (helpers, train, nav,
 * aas-reachability-types). GPL-2.0-or-later.
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

#include "qa/bots/behavior/rerelease/chat_text.h"
#include "qa/bots/behavior/rerelease/math.h"
#include "qa/bots/behavior/rerelease/rng.h"
#include "qa/bots/navigation/aas_reachability_types.h"
#include "qa/bots/navigation/entity_binding.h"
#include "qa/bots/navigation/estimates.h"
#include "qa/bots/navigation/helpers.h"
#include "qa/bots/navigation/nav.h"
#include "qa/bots/navigation/train.h"

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

static qa_vec3 v3(double x, double y, double z) { return (qa_vec3){x, y, z}; }

static const char *chat_lookup(void *ctx, const char *key) {
  (void)ctx;
  if (!strcmp(key, "hello_0")) return "Hello";
  if (!strcmp(key, "hello_1")) return "Ready";
  if (!strcmp(key, "hello_3")) return "Not contiguous";
  if (!strcmp(key, "plain")) return "Plain text";
  return NULL;
}

static void test_chat_text(void) {
  qa_xorshift32 random;
  qa_xorshift32_init(&random, 12);
  int32_t saved = qa_xorshift32_peek(&random);
  char *lines[8];
  bool saw_hello = false, saw_ready = false;
  for (int i = 0; i < 8; i++) {
    lines[i] = qa_q1_bot_chat_text("$hello", chat_lookup, NULL, qa_xorshift32_as_random(&random));
    CHECK(lines[i] && (!strcmp(lines[i], "Hello") || !strcmp(lines[i], "Ready")));
    if (lines[i] && !strcmp(lines[i], "Hello")) saw_hello = true;
    if (lines[i] && !strcmp(lines[i], "Ready")) saw_ready = true;
  }
  CHECK(saw_hello && saw_ready);
  int32_t after = qa_xorshift32_peek(&random);
  qa_xorshift32_restore(&random, saved);
  for (int i = 0; i < 8; i++) {
    char *again = qa_q1_bot_chat_text("hello", chat_lookup, NULL, qa_xorshift32_as_random(&random));
    CHECK(again && lines[i] && !strcmp(again, lines[i]));
    free(again);
    free(lines[i]);
  }
  CHECK(qa_xorshift32_peek(&random) == after);
  char *plain = qa_q1_bot_chat_text("$plain", chat_lookup, NULL, qa_xorshift32_as_random(&random));
  CHECK(plain && !strcmp(plain, "Plain text"));
  free(plain);
  char *missing = qa_q1_bot_chat_text("$missing", chat_lookup, NULL, qa_xorshift32_as_random(&random));
  CHECK(missing && !strcmp(missing, "missing"));
  free(missing);
  CHECK(qa_xorshift32_peek(&random) == after);
  CHECK_ABORTS(qa_xorshift32_restore(&random, 0));
}

static void test_rerelease_math(void) {
  CHECK(qa_bot_angle_mod(-90) == 270);
  CHECK(qa_bot_angle_mod(360) == 0);
  CHECK(qa_bot_angle_delta(350, 10) == 20);
  CHECK(qa_bot_angle_delta(10, 350) == -20);
  CHECK(qa_bot_clamp(5, 0, 3) == 3);
  CHECK(qa_bvec_length2d(v3(3, 4, 99)) == 5);
  CHECK(qa_bvec_distance2d(v3(0, 0, 9), v3(3, 4, -9)) == 5);
  qa_vec3 zero = qa_bvec_normalized(v3(0, 0, 0));
  CHECK(zero.x == 0 && zero.y == 0 && zero.z == 0);
}

static void test_entity_binding(void) {
  qa_bounds authored = {{-65, 511, -241}, {65, 641, 1}};
  qa_bounds linked = {{-65, 511, -291}, {65, 641, -49}};
  CHECK(qa_source_mover_bounds_match(authored, linked, v3(0, 0, -50), v3(0, 0, 0)));
  CHECK(!qa_source_mover_bounds_match(authored, linked, v3(0, 0, -50), v3(0, 0, -50)));
  CHECK(!qa_source_mover_bounds_match(authored, linked, v3(0, 0, -50), v3(0.01, 0, 0)));
}

static qa_navigation_profile profile(void) {
  qa_navigation_profile p;
  memset(&p, 0, sizeof p);
  p.movement.kind = "q3";
  p.policy.kind = "q3";
  p.shape.bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
  p.crouched_shape.bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 4}};
  p.maximum_step = 18;
  p.maximum_drop = 100;
  p.minimum_floor_normal = 0.7;
  p.team = -1;
  return p;
}

static qa_point_contents_result fake_contents(void *ctx, qa_point_contents_query q) {
  (void)ctx; (void)q;
  qa_point_contents_result r;
  memset(&r, 0, sizeof r);
  r.kind = "q2";
  r.merged = 32 | 8 | 0x20000000; /* water | lava | ladder */
  return r;
}
static qa_trace_result fake_trace(void *ctx, qa_trace_query q) {
  (void)ctx;
  qa_trace_result r;
  memset(&r, 0, sizeof r);
  r.kind = "q3";
  r.fraction = 1;
  r.end = q.end;
  return r;
}

static void test_helpers(void) {
  CHECK(qa_nav_distance(v3(0, 0, 0), v3(3, 4, 0)) == 5);
  qa_vec3 mid = qa_nav_midpoint(v3(0, 0, 0), v3(4, 6, 8));
  CHECK(mid.x == 2 && mid.y == 3 && mid.z == 4);
  qa_navigation_profile p = profile();
  qa_navigation_profile out;
  CHECK(!qa_nav_crouched_profile(&p, &out)); /* no crouched shape, no crouch cap */
  p.has_crouched_shape = true;
  CHECK(!qa_nav_crouched_profile(&p, &out));
  p.capabilities = 1u << QA_TM_CROUCH;
  CHECK(qa_nav_crouched_profile(&p, &out));
  CHECK(out.shape.bounds.max.z == 4);
  qa_navigation_node node;
  memset(&node, 0, sizeof node);
  node.source.kind = QA_NS_NAV3;
  node.flags = 512;
  CHECK(qa_nav_node_profile(&p, &node, &out) && out.shape.bounds.max.z == 4);
  node.flags = 0;
  CHECK(qa_nav_node_profile(&p, &node, &out) && out.shape.bounds.max.z == 32);
  qa_nav_validate_profile(&p);
  qa_navigation_profile bad = p;
  bad.minimum_floor_normal = 0;
  CHECK_ABORTS(qa_nav_validate_profile(&bad));
  qa_navigation_profile bad2 = p;
  bad2.policy.kind = "q1";
  CHECK_ABORTS(qa_nav_validate_profile(&bad2));
  qa_navigation_node nodes[1] = {node};
  CHECK(qa_nav_at_node(nodes, 1, 0) == &nodes[0]);
  CHECK_ABORTS((void)qa_nav_at_node(nodes, 1, 1));
  qa_navigation_world world;
  memset(&world, 0, sizeof world);
  world.scene.point_contents = fake_contents;
  world.scene.trace = fake_trace;
  CHECK(qa_nav_contents_world(&world, &p, v3(0, 0, 0)) == (1 | 4 | 8));
  CHECK(qa_nav_clear_world(&world, &p, v3(0, 0, 0), v3(10, 0, 0)));
}

static void test_train(void) {
  qa_navigation_profile p = profile();
  p.capabilities = 1u << QA_TM_MOVER;
  p.shape.bounds = (qa_bounds){{-8, -8, 0}, {8, 8, 16}};
  qa_navigation_train_stop stops[2];
  memset(stops, 0, sizeof stops);
  stops[0].id = 1; stops[0].origin = v3(0, 0, 0); stops[0].has_next = true; stops[0].next = 2; stops[0].wait = 1;
  stops[1].id = 2; stops[1].origin = v3(100, 0, 0); stops[1].has_next = true; stops[1].next = 1; stops[1].wait = 1;
  qa_navigation_entity_state state;
  memset(&state, 0, sizeof state);
  state.enabled = true;
  state.bounds = (qa_bounds){{-16, -16, -8}, {16, 16, 0}};
  state.has_train = true;
  state.train_origin = v3(0, 0, 0);
  state.train_stops = stops;
  state.train_stop_count = 2;
  qa_navigation_edge edge;
  memset(&edge, 0, sizeof edge);
  edge.start = v3(0, 0, 0);
  edge.end = v3(100, 0, 0);
  /* TS profile shape bounds interact with supports() via point+bounds;
   * the default standing shape (minz -24) also works with step 18. */
  qa_navigation_train_ride ride;
  CHECK(qa_navigation_find_train_ride(&state, &edge, &p, &ride));
  CHECK(ride.boarding.id == 1 && ride.arrival.id == 2);
  state.locked = true;
  CHECK(!qa_navigation_find_train_ride(&state, &edge, &p, &ride));
  state.locked = false;
  stops[0].next = 1; /* self-loop: never reaches arrival */
  CHECK(!qa_navigation_find_train_ride(&state, &edge, &p, &ride));
}

static bool allow_walk(void *ctx, const qa_navigation_edge *edge, bool has_flags, int64_t flags) {
  (void)ctx; (void)has_flags; (void)flags;
  return edge->mode == QA_TM_WALK;
}

static void test_estimates_generic(void) {
  qa_navigation_node nodes[5];
  qa_navigation_edge edges[4];
  memset(nodes, 0, sizeof nodes);
  memset(edges, 0, sizeof edges);
  for (int i = 0; i < 5; i++) nodes[i].id = i + 1;
  for (int i = 0; i < 4; i++) {
    edges[i].id = i + 1;
    edges[i].from = i + 1;
    edges[i].to = i + 2;
    edges[i].mode = QA_TM_WALK;
    edges[i].travel_seconds = i == 0 ? 2 : 0.25;
  }
  qa_navigation_graph graph;
  memset(&graph, 0, sizeof graph);
  graph.nodes = nodes; graph.node_count = 5;
  graph.edges = edges; graph.edge_count = 4;
  qa_navigation_estimates est;
  CHECK(qa_navigation_estimates_init(&est, &graph, NULL, allow_walk));
  qa_navigation_estimate_query q;
  memset(&q, 0, sizeof q);
  q.start_node = 1; q.goal_node = 5; q.has_travel_flags = true; q.travel_flags = 0x011c0fbe;
  qa_navigation_estimate_result r = qa_navigation_estimates_estimate(&est, q);
  CHECK(r.is_estimate && r.travel_time == 275 && !r.has_first_edge);
  q.start_node = 5; q.goal_node = 1;
  r = qa_navigation_estimates_estimate(&est, q);
  CHECK(!r.is_estimate);
  q.start_node = 1; q.goal_node = 5; q.has_origin = true; q.origin = v3(0, 0, 0);
  r = qa_navigation_estimates_estimate(&est, q);
  CHECK(r.is_estimate && r.has_first_edge && r.first_edge.id == 1 && r.travel_time == 275);
  q.has_origin = false; q.start_node = 1; q.goal_node = 1;
  r = qa_navigation_estimates_estimate(&est, q);
  CHECK(r.is_estimate && r.travel_time == 1);
  q.start_node = 0; q.goal_node = 0;
  r = qa_navigation_estimates_estimate(&est, q);
  CHECK(!r.is_estimate);
  qa_navigation_estimates_destroy(&est);
}

static void test_settings_and_nav(void) {
  qa_aas_movement_settings s;
  qa_aas_default_movement_settings(&s);
  CHECK(s.gravity == 800.0f && s.max_step == 19.0f);
  CHECK(s.max_steepness == 0.7f); /* fround staging */
  CHECK(s.gravity_direction.z == -1);
  CHECK(s.max_jump_fall_height == 450.0f);
  /* NAV3 v3: no nodes/links/traversals/entities, heuristic 2. */
  uint8_t bytes[28];
  memset(bytes, 0, sizeof bytes);
  memcpy(bytes, "NAV3", 4);
  int32_t v3i = 3;
  memcpy(bytes + 4, &v3i, 4);
  float h = 2.0f;
  memcpy(bytes + 20, &h, 4);
  qa_kex_navigation_asset asset;
  CHECK(qa_parse_kex_navigation(&asset, bytes, sizeof bytes, "<test>"));
  CHECK(asset.is_nav3 && asset.version == 3 && asset.heuristic == 2.0);
  CHECK(asset.node_count == 0 && asset.entity_count == 0);
  qa_kex_navigation_asset_destroy(&asset);
  uint8_t bad[4] = {'B', 'A', 'D', '!'};
  CHECK_ABORTS({ qa_kex_navigation_asset a2; (void)qa_parse_kex_navigation(&a2, bad, sizeof bad, "<test>"); });
}

int main(void) {
  test_chat_text();
  test_rerelease_math();
  test_entity_binding();
  test_helpers();
  /* train supports() uses the standing profile shape from profile(). */
  test_train();
  test_estimates_generic();
  test_settings_and_nav();
  if (failures) {
    fprintf(stderr, "%d of %d checks failed\n", failures, checks);
    return 1;
  }
  printf("all %d bots-b checks passed\n", checks);
  return 0;
}
