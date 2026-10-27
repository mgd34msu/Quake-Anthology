/* Port of src/world/gameplay/transitions.ts. GPL-2.0-or-later. */
#include "qa/world/gameplay/transitions.h"

#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static noreturn void qa_error(const char *message) {
  fprintf(stderr, "Error: %s\n", message);
  abort();
}
static bool str_eq(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}
static void *xrealloc(void *ptr, size_t size) {
  void *next = realloc(ptr, size != 0 ? size : 1);
  if (next == NULL) abort();
  return next;
}

typedef struct pending_entry {
  int64_t id;
  qa_transition_decision decision;
} pending_entry;

struct qa_shared_transition_coordinator {
  void (*apply)(void *ctx, qa_transition_decision decision);
  void *ctx;
  pending_entry *pending;
  size_t pending_count;
  size_t pending_capacity;
  int64_t next_id;
  bool committing;
  qa_objective_id *blocked;
  size_t blocked_count;
  size_t blocked_capacity;
};

qa_shared_transition_coordinator *qa_shared_transition_coordinator_create(
    void (*apply)(void *ctx, qa_transition_decision decision), void *ctx) {
  qa_shared_transition_coordinator *coordinator = calloc(1, sizeof *coordinator);
  if (coordinator == NULL) return NULL;
  coordinator->apply = apply;
  coordinator->ctx = ctx;
  return coordinator;
}
void qa_shared_transition_coordinator_destroy(qa_shared_transition_coordinator *coordinator) {
  if (coordinator == NULL) return;
  free(coordinator->pending);
  free((void *)coordinator->blocked);
  free(coordinator);
}

static bool is_campaign_intent(const qa_transition_intent *intent) {
  return str_eq(intent->kind, "campaign-level") || str_eq(intent->kind, "campaign-complete");
}
static bool is_match_intent(const qa_transition_intent *intent) {
  return str_eq(intent->kind, "round-complete") || str_eq(intent->kind, "match-rotation");
}

qa_transition_decision qa_shared_transition_coordinator_resolve(
    qa_shared_transition_coordinator *coordinator, qa_transition_mode mode,
    const qa_transition_intent *intents, size_t intent_count, int64_t *out_id) {
  coordinator->blocked_count = 0;
  /* Candidate order: combined+round-first puts match intents first. */
  bool round_first = str_eq(mode.kind, "combined") && str_eq(mode.simultaneous, "round-first");
  for (int pass = 0; pass < 2; pass++) {
    bool want_match = round_first ? pass == 0 : pass == 1;
    for (size_t i = 0; i < intent_count; i++) {
      const qa_transition_intent *intent = &intents[i];
      bool campaign_candidate = is_campaign_intent(intent) && !str_eq(mode.kind, "competitive") &&
                                str_eq(intent->campaign, mode.campaign);
      bool match_candidate = is_match_intent(intent) &&
          (str_eq(mode.kind, "campaign")
               ? mode.allow_round_restart && str_eq(intent->kind, "round-complete")
               : str_eq(intent->match, mode.match));
      if (want_match ? !match_candidate : !campaign_candidate) continue;
      if (is_campaign_intent(intent)) {
        bool missing = false;
        for (size_t g = 0; g < intent->gate_count; g++) {
          if (!intent->gates[g].satisfied) {
            missing = true;
            if (coordinator->blocked_count == coordinator->blocked_capacity) {
              coordinator->blocked_capacity =
                  coordinator->blocked_capacity != 0 ? coordinator->blocked_capacity * 2 : 8;
              coordinator->blocked = xrealloc((void *)coordinator->blocked,
                                              coordinator->blocked_capacity * sizeof *coordinator->blocked);
            }
            coordinator->blocked[coordinator->blocked_count++] = intent->gates[g].objective;
          }
        }
        if (missing) continue;
      }
      qa_transition_decision decision;
      memset(&decision, 0, sizeof decision);
      if (str_eq(intent->kind, "campaign-level")) {
        decision.kind = "travel";
        decision.map = intent->map;
        decision.spawn_point = intent->spawn_point;
        decision.complete_campaign = false;
      } else if (str_eq(intent->kind, "campaign-complete")) {
        decision.kind = "campaign-complete";
        decision.campaign = intent->campaign;
      } else if (str_eq(intent->kind, "round-complete")) {
        decision.kind = "round";
        decision.winner = intent->winner;
      } else { /* match-rotation */
        if (str_eq(mode.kind, "combined")) continue;
        decision.kind = "travel";
        decision.map = intent->map;
        decision.spawn_point = "";
        decision.complete_campaign = false;
      }
      if (coordinator->pending_count == coordinator->pending_capacity) {
        coordinator->pending_capacity =
            coordinator->pending_capacity != 0 ? coordinator->pending_capacity * 2 : 8;
        coordinator->pending = xrealloc(coordinator->pending,
                                        coordinator->pending_capacity * sizeof *coordinator->pending);
      }
      int64_t id = ++coordinator->next_id;
      coordinator->pending[coordinator->pending_count].id = id;
      coordinator->pending[coordinator->pending_count].decision = decision;
      coordinator->pending_count++;
      if (out_id != NULL) *out_id = id;
      return decision;
    }
  }
  /* Deduplicate blocked objectives (TS Set). */
  size_t unique = 0;
  for (size_t i = 0; i < coordinator->blocked_count; i++) {
    bool seen = false;
    for (size_t j = 0; j < unique; j++) {
      if (str_eq(coordinator->blocked[j], coordinator->blocked[i])) seen = true;
    }
    if (!seen) coordinator->blocked[unique++] = coordinator->blocked[i];
  }
  coordinator->blocked_count = unique;
  qa_transition_decision stay;
  memset(&stay, 0, sizeof stay);
  stay.kind = "stay";
  stay.blocked = coordinator->blocked;
  stay.blocked_count = unique;
  if (out_id != NULL) *out_id = -1;
  return stay;
}

static bool decision_equal(qa_transition_decision a, qa_transition_decision b) {
  return str_eq(a.kind, b.kind) && str_eq(a.winner, b.winner) && str_eq(a.map, b.map) &&
         str_eq(a.spawn_point, b.spawn_point) && a.complete_campaign == b.complete_campaign &&
         str_eq(a.campaign, b.campaign);
}

void qa_shared_transition_coordinator_commit(qa_shared_transition_coordinator *coordinator,
                                             int64_t decision_id,
                                             qa_transition_decision decision) {
  if (str_eq(decision.kind, "stay")) return;
  if (coordinator->committing) qa_error("A level transition is already committing");
  size_t at = coordinator->pending_count;
  for (size_t i = 0; i < coordinator->pending_count; i++) {
    if (coordinator->pending[i].id == decision_id) at = i;
  }
  if (at == coordinator->pending_count)
    qa_error("Transition was not resolved here or was already committed");
  qa_transition_decision stored = coordinator->pending[at].decision;
  memmove(&coordinator->pending[at], &coordinator->pending[at + 1],
          (coordinator->pending_count - at - 1) * sizeof *coordinator->pending);
  coordinator->pending_count--;
  coordinator->committing = true;
  coordinator->apply(coordinator->ctx, stored);
  coordinator->committing = false;
}

static qa_transition_decision c_resolve(void *ctx, qa_transition_mode mode,
                                        const qa_transition_intent *intents, size_t count) {
  return qa_shared_transition_coordinator_resolve(ctx, mode, intents, count, NULL);
}
static void c_commit(void *ctx, qa_transition_decision decision) {
  qa_shared_transition_coordinator *coordinator = ctx;
  if (str_eq(decision.kind, "stay")) return;
  for (size_t i = 0; i < coordinator->pending_count; i++) {
    if (decision_equal(coordinator->pending[i].decision, decision)) {
      qa_shared_transition_coordinator_commit(coordinator, coordinator->pending[i].id, decision);
      return;
    }
  }
  qa_error("Transition was not resolved here or was already committed");
}
qa_transition_coordinator qa_shared_transition_coordinator_as_contract(
    qa_shared_transition_coordinator *coordinator) {
  qa_transition_coordinator table;
  table.ctx = coordinator;
  table.resolve = c_resolve;
  table.commit = c_commit;
  return table;
}
