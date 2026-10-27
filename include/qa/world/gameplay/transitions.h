/*
 * Port of src/world/gameplay/transitions.ts. The TS WeakSet of
 * pending decisions becomes an owned registry of decision records:
 * resolve returns a record id alongside the decision value, and
 * commit consumes that id (a foreign or reused id aborts with the
 * TS message). Blocked lists are coordinator-owned scratch.
 * GPL-2.0-or-later.
 */
#ifndef QA_WORLD_GAMEPLAY_TRANSITIONS_H
#define QA_WORLD_GAMEPLAY_TRANSITIONS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/gameplay.h"

typedef struct qa_shared_transition_coordinator qa_shared_transition_coordinator;

/* apply receives a non-"stay" decision. NULL only on OOM. */
qa_shared_transition_coordinator *qa_shared_transition_coordinator_create(
    void (*apply)(void *ctx, qa_transition_decision decision), void *ctx);
void qa_shared_transition_coordinator_destroy(qa_shared_transition_coordinator *coordinator);
/* *out_id identifies the pending decision for commit (-1 for "stay"). */
qa_transition_decision qa_shared_transition_coordinator_resolve(
    qa_shared_transition_coordinator *coordinator, qa_transition_mode mode,
    const qa_transition_intent *intents, size_t intent_count, int64_t *out_id);
void qa_shared_transition_coordinator_commit(qa_shared_transition_coordinator *coordinator,
                                             int64_t decision_id,
                                             qa_transition_decision decision);
qa_transition_coordinator qa_shared_transition_coordinator_as_contract(
    qa_shared_transition_coordinator *coordinator);

#endif
