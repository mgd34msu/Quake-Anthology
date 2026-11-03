/*
 * Confirmation-gate full-read checks for src/world (gate-world). Expected
 * values come from tests/world/oracles/gate_world.ts run under bun (output in
 * gate_world.txt). GPL-2.0-or-later.
 */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/contracts/identity.h"
#include "qa/core/numeric.h"
#include "qa/world/collision/body.h"
#include "qa/world/collision/contents.h"
#include "qa/world/collision/media.h"
#include "qa/world/collision/q3/model.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/registry.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)

static qa_trace_result q3_trace(double surface_flags) {
  qa_trace_result r;
  memset(&r, 0, sizeof r);
  r.kind = "q3";
  r.fraction = 0.5;
  r.hit.kind = QA_TRACE_HIT_WORLD;
  r.contents = 1;
  r.has_surface_flags = true;
  r.surface_flags = surface_flags;
  r.source_plane.plane.normal.z = 1;
  r.source_plane.type = 2;
  return r;
}

static void test_adapt_trace(void) {
  qa_trace_policy q1;
  memset(&q1, 0, sizeof q1);
  q1.kind = "q1";
  q1.move = "normal";
  /* bun: adapt_q1_flags -2147483648 */
  CHECK(qa_adapt_trace_result(q3_trace(2147483648.0), q1).surface_flags == -2147483648.0);
  qa_trace_policy q2;
  memset(&q2, 0, sizeof q2);
  q2.kind = "q2";
  q2.contents_mask = -1;
  q2.leaf_contents = "merged";
  const qa_trace_result first = qa_adapt_trace_result(q3_trace(2), q2);
  const qa_trace_result second = qa_adapt_trace_result(q3_trace(4), q2);
  /* bun: adapt_q2_surfaces 2 4 */
  CHECK(first.surface != NULL && first.surface->flags == 2);
  CHECK(second.surface != NULL && second.surface->flags == 4);
}

static double media_distance(void *ctx, qa_bsp_plane plane, int endpoint) {
  (void)ctx;
  (void)plane;
  return endpoint == 0 ? -1 : NAN;
}

static void test_media_nan(void) {
  qa_bsp_plane plane;
  memset(&plane, 0, sizeof plane);
  plane.plane.normal.x = 1;
  const qa_medium_brush brush = {32, &plane, 1};
  const qa_trace_media media = qa_trace_brush_media(&brush, 1, "q2", media_distance, NULL,
                                                    qa_create_numeric_operations(&qa_q1_donor_profile), 1);
  /* bun: media_nan inOpen false inWater false */
  CHECK(!media.in_open);
  CHECK(!media.in_water);
}

static void test_q2_box_nan(void) {
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "gate"));
  qa_spatial_actor actor;
  memset(&actor, 0, sizeof actor);
  actor.body.actor = qa_identity_owner_actor(&owner, 3, 1);
  const qa_bounds bounds = {{-16, -16, -16}, {16, 16, 16}};
  actor.body.state.bounds = bounds;
  actor.collision.family = "q2";
  actor.collision.role = "solid";
  actor.collision.shape_kind = "box";
  actor.collision.contents = 1;
  qa_trace_query query;
  memset(&query, 0, sizeof query);
  query.start.x = NAN;
  query.start.y = -64;
  query.end.x = NAN;
  query.end.y = 64;
  query.shape.kind = "point";
  query.policy.kind = "q2";
  query.policy.contents_mask = -1;
  query.policy.leaf_contents = "merged";
  query.numeric = qa_q2_donor_profile;
  const qa_trace_result result = qa_trace_actor_body(query, actor);
  /* bun: q2box_nan fraction 1 hit none */
  CHECK(result.fraction == 1);
  CHECK(result.hit.kind == QA_TRACE_HIT_NONE);
  qa_identity_owner_destroy(&owner);
}

static void test_box_model_bounds(void) {
  const qa_bounds bounds = {{0.1, 0.1, 0.1}, {1, 1, 1}};
  qa_temporary_collision_model *model = qa_create_box_model(bounds, NULL, NULL);
  CHECK(model != NULL);
  const qa_vec3 point = {0.1, 0.5, 0.5};
  /* bun: box_bounds 0.10000000149011612 contents 0 */
  CHECK(qa_temporary_collision_model_bounds(model).min.x == 0.10000000149011612);
  CHECK(qa_temporary_model_point_contents(model, point) == 0);
  qa_temporary_collision_model_destroy(model);
}


/* ---- 6. transportAttachments follows attachment insertion order. ---- */
typedef struct link_log {
  int64_t slots[8];
  int count;
} link_log;
static qa_bounds bt_bounds(void *ctx, qa_owned_actor actor, qa_body_state state) {
  (void)ctx;
  return qa_translated_body_bounds(actor, state);
}
static void bt_on_link(void *ctx, qa_linked_body body) {
  link_log *log = ctx;
  if (log->count < 8) log->slots[log->count++] = body.actor.slot;
}
static void bt_on_unlink(void *ctx, qa_actor_id actor) {
  (void)ctx;
  (void)actor;
}
static qa_body_state bt_state(double x) {
  qa_body_state s;
  memset(&s, 0, sizeof s);
  s.origin.x = x;
  return s;
}
static void test_transport_order(void) {
  qa_identity_owner identities;
  CHECK(qa_identity_owner_create(&identities, "gate-bodies"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identities, 65536);
  link_log log;
  memset(&log, 0, sizeof log);
  const qa_body_link_hooks hooks = {&log, bt_bounds, bt_on_link, bt_on_unlink};
  qa_shared_body_table *table = qa_shared_body_table_create(actors, hooks);
  const qa_owned_actor anchor = qa_session_actor_registry_allocate(actors, "test", "test:anchor");
  const qa_owned_actor first = qa_session_actor_registry_allocate(actors, "test", "test:a");
  const qa_owned_actor second = qa_session_actor_registry_allocate(actors, "test", "test:b");
  qa_shared_body_table_create_body(table, anchor, bt_state(0));
  qa_shared_body_table_create_body(table, first, bt_state(100));
  qa_shared_body_table_create_body(table, second, bt_state(200));
  const qa_body_attachment to_second = {anchor.id, QA_BODY_FOLLOW_TRANSLATION, {1, 0, 0}};
  const qa_body_attachment to_first = {anchor.id, QA_BODY_FOLLOW_TRANSLATION, {2, 0, 0}};
  qa_shared_body_table_attach(table, second, to_second);
  qa_shared_body_table_attach(table, first, to_first);
  qa_shared_body_table_transport_attachments(table, *qa_create_numeric_operations(&qa_q1_donor_profile));
  /* bun: transport_order 2,1 slots 0,1,2 */
  CHECK(anchor.id.slot == 0 && first.id.slot == 1 && second.id.slot == 2);
  CHECK(log.count == 2 && log.slots[0] == 2 && log.slots[1] == 1);
  qa_shared_body_table_destroy(table);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identities);
}

int main(void) {
  test_adapt_trace();
  test_media_nan();
  test_q2_box_nan();
  test_box_model_bounds();
  test_transport_order();
  printf("test_gate_world: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
