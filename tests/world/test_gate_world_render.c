/*
 * Confirmation-gate full-read checks for src/world (gate-render pass): q3
 * patch/polylib, q1 solid sweeps, scheduler, inventory, mod composition,
 * transitions, actor registry release and resource scopes. Expected values
 * come from tests/world/oracles/gate_world_render.ts run under bun (output in
 * gate_world_render.txt). GPL-2.0-or-later.
 */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/time.h"
#include "qa/foundation/support.h"
#include "qa/world/actors/registry.h"
#include "qa/world/collision/q3/patch.h"
#include "qa/world/collision/q3/polylib.h"
#include "qa/world/gameplay/inventory.h"
#include "qa/world/gameplay/mod_composition.h"
#include "qa/world/gameplay/transitions.h"
#include "qa/world/geometry/q1_solid/polyhedron.h"
#include "qa/world/geometry/q1_solid/sweep.h"
#include "qa/world/scheduler.h"
#include "qa/world/session/resources.h"

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

static bool caught_is(const qa_caught_error *caught, const char *name, const char *message) {
  return strcmp(caught->name, name) == 0 && strcmp(caught->message, message) == 0;
}
static qa_vec3 V(double x, double y, double z) {
  qa_vec3 v = {x, y, z};
  return v;
}
static void flat_grid(qa_vec3 out[9]) {
  for (int y = 0; y < 3; y++)
    for (int x = 0; x < 3; x++) out[y * 3 + x] = V(64.0 * x, 64.0 * y, 0);
}

/* 1-2. CM_GeneratePatchFacets errors. */
typedef struct patch_args { double width, height; size_t count; } patch_args;
static void generate_patch(void *ctx) {
  patch_args *args = ctx;
  qa_vec3 grid[9];
  flat_grid(grid);
  qa_patch_collide_destroy(qa_generate_patch_collide(args->width, args->height, grid, args->count, NULL));
}
static void test_patch_errors(void) {
  qa_caught_error caught;
  patch_args bad = {1, 3, 3};
  CHECK(!qa_try(generate_patch, &bad, &caught));
  /* bun: patch_bad_params CommonError|CM_GeneratePatchFacets: bad parameters: (1, 3, managed points) */
  CHECK(caught_is(&caught, "CommonError", "CM_GeneratePatchFacets: bad parameters: (1, 3, managed points)"));
  patch_args infinite = {INFINITY, 3, 9};
  CHECK(!qa_try(generate_patch, &infinite, &caught));
  /* bun: patch_infinite RangeError|collision patch requires integer dimensions */
  CHECK(caught_is(&caught, "RangeError", "collision patch requires integer dimensions"));
}

/* 3. positionInPatch with a NaN start. */
static void test_patch_position_nan(void) {
  qa_vec3 grid[9];
  flat_grid(grid);
  qa_patch_collide *patch = qa_generate_patch_collide(3, 3, grid, 9, NULL);
  qa_patch_shape shape;
  memset(&shape, 0, sizeof shape);
  shape.kind = "box";
  shape.mins = V(-16, -16, -16);
  shape.extents = V(16, 16, 16);
  /* bun: patch_position_nan false true facets 1 planes 5 */
  CHECK(!qa_position_in_patch(patch, V(NAN, 64, 0), shape));
  CHECK(qa_position_in_patch(patch, V(64, 64, 0), shape));
  CHECK(patch->facet_count == 1);
  CHECK(patch->plane_count == 5);
  qa_patch_collide_destroy(patch);
}

/* 4. CheckWinding names the point count. */
typedef struct owned_winding { qa_collision_winding_library *lib; qa_collision_winding *w; } owned_winding;
static void release_winding(void *ctx) {
  owned_winding *owned = ctx;
  qa_winding_free(owned->lib, owned->w);
}
static void check_two_points(void *ctx) {
  qa_collision_winding_library *lib = ctx;
  owned_winding owned = {lib, qa_winding_alloc(lib, 2)};
  qa_cleanup node;
  qa_cleanup_push(&node, release_winding, &owned);
  qa_collision_winding_append(owned.w, V(0, 0, 0));
  qa_collision_winding_append(owned.w, V(1, 0, 0));
  qa_winding_check(lib, owned.w);
  qa_cleanup_pop(&node);
  release_winding(&owned);
}
static void test_check_winding(void) {
  qa_collision_winding_library lib;
  qa_collision_winding_library_init(&lib, NULL);
  qa_caught_error caught;
  CHECK(!qa_try(check_two_points, &lib, &caught));
  /* bun: check_winding CommonError|CheckWinding: 2 points */
  CHECK(caught_is(&caught, "CommonError", "CheckWinding: 2 points"));
}

/* 5. sweepBoxCell with a NaN start. */
static void test_sweep_nan(void) {
  qa_bounds bounds = {{0, 0, 0}, {32, 32, 32}};
  qa_convex_cell *cell = qa_box_cell(bounds);
  qa_cell_shape shape;
  memset(&shape, 0, sizeof shape);
  shape.kind = "box";
  shape.axes[0] = V(1, 0, 0);
  shape.axes[1] = V(0, 1, 0);
  shape.axes[2] = V(0, 0, 1);
  shape.extents = V(8, 8, 8);
  qa_sweep_interval out;
  /* bun: sweep_nan null clean 0.4375 0.8125 0.437255859375 */
  CHECK(!qa_sweep_box_cell(cell, V(NAN, 16, 16), V(64, 16, 16), shape, 0.03125, &out));
  CHECK(qa_sweep_box_cell(cell, V(-64, 16, 16), V(64, 16, 16), shape, 0.03125, &out));
  CHECK(out.enter == 0.4375 && out.exit == 0.8125 && out.contact == 0.437255859375);
  qa_convex_cell_destroy(cell);
}

/* 6. compareInvocationOrder: indexOf is the first match. */
static void test_invocation_order(void) {
  qa_provider_id providers[3] = {"p:a", "p:b", "p:a"};
  qa_frame_ordering ordering;
  memset(&ordering, 0, sizeof ordering);
  ordering.kind = QA_FRAME_ORDERING_MIXED;
  ordering.providers = providers;
  ordering.provider_count = 3;
  qa_invocation_order left, right;
  memset(&left, 0, sizeof left);
  memset(&right, 0, sizeof right);
  left.provider = "p:a";
  left.actor.slot = 1;
  right.provider = "p:b";
  right.actor.slot = 2;
  /* bun: invocation_order -1 */
  CHECK(qa_compare_invocation_order(ordering, left, right, NULL, NULL) == -1);
}

/* 7. inventoryGive with a NaN capacity. */
static void give_nan(void *ctx) {
  (void)ctx;
  qa_inventory_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.item = "x";
  entry.count = 1;
  entry.capacity = NAN;
  (void)qa_inventory_give(entry, 2);
}
static void test_give_nan_capacity(void) {
  qa_caught_error caught;
  CHECK(!qa_try(give_nan, NULL, &caught));
  /* bun: give_nan_capacity RangeError|Inventory counter must be finite */
  CHECK(caught_is(&caught, "RangeError", "Inventory counter must be finite"));
}

/* 8. A replacement that swallows the canonical throw still fails the dispatch. */
typedef struct swallow_next { qa_mod_next next; const void *request; void *out; } swallow_next;
static void call_next(void *ctx) {
  swallow_next *call = ctx;
  call->next.call(call->next.ctx, call->request, call->out);
}
static void swallowing_replace(void *ctx, const void *request, qa_mod_next next, void *out) {
  (void)ctx;
  swallow_next call = {next, request, out};
  qa_caught_error ignored;
  if (!qa_try(call_next, &call, &ignored)) *(double *)out = -1;
}
static void throwing_canonical(void *ctx, const void *request, void *out) {
  (void)ctx;
  (void)request;
  (void)out;
  qa_raise("Error", 0, 0, "canonical boom");
}
static void dispatch_swallowed(void *ctx) {
  qa_mod_operation *op = ctx;
  double input = 5, result = 0;
  qa_mod_canonical canonical = {NULL, throwing_canonical};
  qa_mod_operation_dispatch(op, &input, canonical, NULL, &result);
}
static void test_mod_swallowed(void) {
  qa_mod_operation op;
  CHECK(qa_mod_operation_init(&op, "gate", sizeof(double), sizeof(double)));
  qa_mod_operation_registration registration;
  memset(&registration, 0, sizeof registration);
  registration.identity.provider = "p";
  registration.identity.id = "m:r";
  registration.kind = QA_MOD_REPLACE;
  registration.replace = swallowing_replace;
  CHECK(qa_mod_operation_register(&op, registration) >= 0);
  qa_caught_error caught;
  CHECK(!qa_try(dispatch_swallowed, &op, &caught));
  /* bun: mod_swallowed Error|canonical boom */
  CHECK(caught_is(&caught, "Error", "canonical boom"));
  qa_mod_operation_destroy(&op);
}

/* 9. A throwing transition apply does not leave the coordinator committing. */
static int apply_calls = 0;
static void throwing_apply(void *ctx, qa_transition_decision decision) {
  (void)ctx;
  (void)decision;
  if (++apply_calls == 1) qa_raise("Error", 0, 0, "apply boom");
}
typedef struct commit_args {
  qa_shared_transition_coordinator *coordinator;
  int64_t id;
  qa_transition_decision decision;
} commit_args;
static void commit_decision(void *ctx) {
  commit_args *args = ctx;
  qa_shared_transition_coordinator_commit(args->coordinator, args->id, args->decision);
}
static void test_transition_commit(void) {
  qa_shared_transition_coordinator *coordinator =
      qa_shared_transition_coordinator_create(throwing_apply, NULL);
  qa_transition_mode mode;
  memset(&mode, 0, sizeof mode);
  mode.kind = "campaign";
  mode.campaign = "c";
  qa_transition_intent intent;
  memset(&intent, 0, sizeof intent);
  intent.kind = "campaign-complete";
  intent.campaign = "c";
  commit_args first = {coordinator, 0, {0}}, second = {coordinator, 0, {0}};
  first.decision = qa_shared_transition_coordinator_resolve(coordinator, mode, &intent, 1, &first.id);
  second.decision = qa_shared_transition_coordinator_resolve(coordinator, mode, &intent, 1, &second.id);
  qa_caught_error caught;
  /* bun: transition_first Error|apply boom second none calls 2 */
  CHECK(!qa_try(commit_decision, &first, &caught));
  CHECK(caught_is(&caught, "Error", "apply boom"));
  CHECK(qa_try(commit_decision, &second, &caught));
  CHECK(apply_calls == 2);
  qa_shared_transition_coordinator_destroy(coordinator);
}

/* 10. Registry release: every callback runs; failures aggregate. */
static int release_ran = 0;
static void throwing_release(void *ctx, qa_owned_actor actor) {
  (void)ctx;
  (void)actor;
  release_ran++;
  qa_raise("Error", 0, 0, "first");
}
static void counting_release(void *ctx, qa_owned_actor actor) {
  (void)ctx;
  (void)actor;
  release_ran++;
}
typedef struct release_args { qa_session_actor_registry *registry; qa_owned_actor actor; } release_args;
static void release_actor(void *ctx) {
  release_args *args = ctx;
  qa_session_actor_registry_release(args->registry, args->actor);
}
static void test_release_aggregate(void) {
  qa_identity_owner identities;
  CHECK(qa_identity_owner_create(&identities, "release"));
  qa_session_actor_registry *registry = qa_session_actor_registry_create(&identities, 65536);
  qa_owned_actor actor = qa_session_actor_registry_allocate(registry, "p:a", "test:actor");
  (void)qa_session_actor_registry_on_release(registry, throwing_release, NULL);
  (void)qa_session_actor_registry_on_release(registry, counting_release, NULL);
  release_args args = {registry, actor};
  qa_caught_error caught;
  /* bun: release_aggregate AggregateError|Actor release callbacks failed ran 2 live false */
  CHECK(!qa_try(release_actor, &args, &caught));
  CHECK(caught_is(&caught, "AggregateError", "Actor release callbacks failed"));
  CHECK(release_ran == 2);
  CHECK(!qa_session_actor_registry_is_live(registry, actor.id));
  qa_session_actor_registry_destroy(registry);
  qa_identity_owner_destroy(&identities);
}

/* 11. ResourceScope.close aggregates cleanup failures under its name. */
static int scope_ran = 0;
static void counting_cleanup(void *ctx) {
  (void)ctx;
  scope_ran++;
}
static void throwing_cleanup(void *ctx) {
  (void)ctx;
  scope_ran++;
  qa_raise("Error", 0, 0, "cleanup boom");
}
static void close_scope(void *ctx) { qa_resource_scope_close(ctx); }
static void test_scope_close(void) {
  qa_resource_scope *scope = qa_resource_scope_create("Gate scope");
  qa_resource_scope_defer(scope, counting_cleanup, NULL);
  qa_resource_scope_defer(scope, throwing_cleanup, NULL);
  qa_caught_error caught;
  /* bun: scope_close AggregateError|Failed to close Gate scope ran 2 */
  CHECK(!qa_try(close_scope, scope, &caught));
  CHECK(caught_is(&caught, "AggregateError", "Failed to close Gate scope"));
  CHECK(scope_ran == 2);
  qa_resource_scope_destroy(scope);
}

int main(void) {
  test_patch_errors();
  test_patch_position_nan();
  test_check_winding();
  test_sweep_nan();
  test_invocation_order();
  test_give_nan_capacity();
  test_mod_swallowed();
  test_transition_commit();
  test_release_aggregate();
  test_scope_close();
  printf("test_gate_world_render: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
