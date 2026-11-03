/*
 * JS number semantics regressions in src/world: each expected value is what
 * the TypeScript expression returns under bun (noted per check). The C before
 * these fixes cast doubles straight to integers (undefined for NaN and out of
 * range) or truncated fractional keys onto integral ones.
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/contracts/identity.h"
#include "qa/world/actors.h"
#include "qa/world/actors/source_slots.h"
#include "qa/world/collision/contents.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)

/* convertContents(-2.5, "q1", "q2"): the TS switch uses ===, so -2.5 and NaN
 * match no case (bun: 0); C truncated -2.5 onto case -2 (solid, 1). */
static void contents_switch(void) {
  CHECK(qa_convert_contents(-2.5, "q1", "q2") == 0);
  CHECK(qa_convert_contents(NAN, "q1", "q2") == 0);
  CHECK(qa_convert_contents(-2, "q1", "q2") == 1);
  CHECK(qa_convert_contents(-9, "q1", "q2") == (32 | 0x40000));
}

/* Q3 entity reuse: !(freed > ((mapStart + 2000) | 0) && ((now - freed) | 0) < 1000).
 * bun: (0, 3000, 3000 + 2^32 + 5000) -> true; (2^32 + 1000, 2500, 2600) -> true;
 * (1000, 5000, 5500) -> false. A C int32 cast of 2^32 + 5000 is undefined (x86
 * gives INT32_MIN, which reads as "freed just now"). */
static bool reusable(double map_start, double freed, double now) {
  qa_q3_lifetime_state state;
  qa_actor_lifetime_policy policy = qa_q3_entity_lifetime(&state, 64, map_start);
  qa_source_time f = {QA_SOURCE_TIME_MILLISECONDS, freed}, n = {QA_SOURCE_TIME_MILLISECONDS, now};
  return policy.reusable_after(policy.ctx, f, n);
}
static void q3_reuse_wraps(void) {
  CHECK(reusable(0, 3000, 3000 + 4294967296.0 + 5000));
  CHECK(reusable(4294967296.0 + 1000, 2500, 2600));
  CHECK(!reusable(1000, 5000, 5500));
}

/* resolveSaved: restoredActors.get(slot + 0.5) and slots[slot + 0.5] read
 * undefined (bun: new Map([[7, "a"]]).get(7.5) and [0, 1, 2][1.5] are
 * undefined), so a fractional saved slot resolves to null. C truncated it. */
static void fractional_saved_slot(void) {
  qa_identity_owner identities, restore_identities;
  CHECK(qa_identity_owner_create(&identities, "first"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identities, 65536);
  qa_owned_actor actor = qa_session_actor_registry_allocate_at_source(actors, "q2:game", 7, "q2:soldier");
  size_t checkpoint_count = 0, source_count = 0;
  const qa_actor_slot_checkpoint *checkpoints = qa_session_actor_registry_checkpoint(actors, &checkpoint_count);
  const qa_source_actor_checkpoint *sources = qa_session_actor_registry_source_checkpoint(actors, &source_count);
  CHECK(qa_identity_owner_create(&restore_identities, "restore"));
  qa_session_actor_registry *restored = qa_session_actor_registry_restore(
      &restore_identities, checkpoints, checkpoint_count, sources, source_count, 65536);
  qa_owned_actor resolved;
  qa_saved_actor_id exact = {(double)actor.id.slot, (double)actor.id.generation};
  CHECK(qa_session_actor_registry_resolve_saved(restored, exact, &resolved));
  qa_saved_actor_id fraction = {(double)actor.id.slot + 0.5, (double)actor.id.generation};
  CHECK(!qa_session_actor_registry_resolve_saved(restored, fraction, &resolved));
  CHECK(!qa_session_actor_registry_resolve_saved(actors, fraction, &resolved));
  qa_saved_actor_id nan_slot = {NAN, (double)actor.id.generation};
  CHECK(!qa_session_actor_registry_resolve_saved(restored, nan_slot, &resolved));
  qa_session_actor_registry_close(restored);
  qa_session_actor_registry_close(actors);
  qa_session_actor_registry_destroy(restored);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&restore_identities);
  qa_identity_owner_destroy(&identities);
}

int main(void) {
  contents_switch();
  q3_reuse_wraps();
  fractional_saved_slot();
  printf("test_js_semantics_world: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
