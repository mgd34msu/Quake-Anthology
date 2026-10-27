/* Port of src/world/gameplay/mod-composition.ts. GPL-2.0-or-later. */
#include "qa/world/gameplay/mod_composition.h"

#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

/* TS throws plain Error here (not RangeError); abort identically. */
static noreturn void qa_error(const char *message) {
  fprintf(stderr, "Error: %s\n", message);
  abort();
}

struct qa_mod_operation_entry {
  qa_mod_operation_registration contribution;
  int64_t sequence;
  bool active;
};

static bool str_eq(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}

bool qa_mod_operation_init(qa_mod_operation *operation, const char *name,
                           size_t request_size, size_t result_size) {
  operation->name = name;
  operation->request_size = request_size;
  operation->result_size = result_size;
  operation->entries = NULL;
  operation->entry_count = 0;
  operation->entry_capacity = 0;
  operation->sequence = 0;
  return true;
}

void qa_mod_operation_destroy(qa_mod_operation *operation) {
  free(operation->entries);
  operation->entries = NULL;
  operation->entry_count = 0;
  operation->entry_capacity = 0;
}

bool qa_mod_operation_active(const qa_mod_operation *operation) {
  return operation->entry_count != 0;
}

int64_t qa_mod_operation_register(qa_mod_operation *operation,
                                  qa_mod_operation_registration contribution) {
  char message[2048];
  /* TS: Number.isSafeInteger(order); int64_t order is checked here. */
  if (contribution.identity.order > QA_MAX_SAFE_INTEGER ||
      contribution.identity.order < -QA_MAX_SAFE_INTEGER) {
    (void)snprintf(message, sizeof message, "Invalid mod order for %s: %s/%s",
                   operation->name, contribution.identity.provider,
                   contribution.identity.id);
    qa_range_error("%s", message);
  }
  for (size_t i = 0; i < operation->entry_count; i++) {
    const qa_mod_operation_registration *previous =
        &operation->entries[i].contribution;
    if (str_eq(previous->identity.provider, contribution.identity.provider) &&
        str_eq(previous->identity.id, contribution.identity.id)) {
      (void)snprintf(message, sizeof message,
                     "Duplicate mod registration for %s: %s/%s", operation->name,
                     contribution.identity.provider, contribution.identity.id);
      qa_error(message);
    }
    if (previous->kind == QA_MOD_REPLACE && contribution.kind == QA_MOD_REPLACE) {
      (void)snprintf(message, sizeof message, "Conflicting replacements for %s: %s/%s and %s/%s",
                     operation->name, previous->identity.provider, previous->identity.id,
                     contribution.identity.provider, contribution.identity.id);
      qa_error(message);
    }
  }
  if (operation->entry_count == operation->entry_capacity) {
    size_t capacity = operation->entry_capacity != 0 ? operation->entry_capacity * 2 : 4;
    qa_mod_operation_entry *entries =
        realloc(operation->entries, capacity * sizeof *entries);
    if (entries == NULL) return -1;
    operation->entries = entries;
    operation->entry_capacity = capacity;
  }
  qa_mod_operation_entry entry;
  entry.contribution = contribution;
  entry.sequence = operation->sequence++;
  entry.active = true;
  /* Insert sorted by (order, sequence), as TS sort of the appended list. */
  size_t at = operation->entry_count;
  while (at > 0) {
    const qa_mod_operation_entry *other = &operation->entries[at - 1];
    if (other->contribution.identity.order < entry.contribution.identity.order ||
        (other->contribution.identity.order == entry.contribution.identity.order &&
         other->sequence < entry.sequence)) break;
    at--;
  }
  memmove(&operation->entries[at + 1], &operation->entries[at],
          (operation->entry_count - at) * sizeof *operation->entries);
  operation->entries[at] = entry;
  operation->entry_count++;
  return entry.sequence;
}

void qa_mod_operation_unregister(qa_mod_operation *operation, int64_t sequence) {
  for (size_t i = 0; i < operation->entry_count; i++) {
    if (operation->entries[i].sequence == sequence && operation->entries[i].active) {
      operation->entries[i].active = false;
      memmove(&operation->entries[i], &operation->entries[i + 1],
              (operation->entry_count - i - 1) * sizeof *operation->entries);
      operation->entry_count--;
      return;
    }
  }
}

typedef struct next_state {
  qa_mod_operation *operation;
  qa_mod_canonical canonical;
  const qa_mod_operation_registration *replacement;
  void *request; /* dispatch-owned request buffer, replaced by next() */
  bool open;
  bool called;
} next_state;

static void next_call(void *ctx, const void *request, void *out_result) {
  next_state *state = ctx;
  char message[2048];
  if (!state->open) {
    (void)snprintf(message, sizeof message, "Mod continuation for %s is closed: %s/%s",
                   state->operation->name, state->replacement->identity.provider,
                   state->replacement->identity.id);
    qa_error(message);
  }
  if (state->called) {
    (void)snprintf(message, sizeof message,
                   "Mod continuation for %s was already called: %s/%s",
                   state->operation->name, state->replacement->identity.provider,
                   state->replacement->identity.id);
    qa_error(message);
  }
  state->called = true;
  memcpy(state->request, request, state->operation->request_size);
  state->canonical.call(state->canonical.ctx, request, out_result);
}

void qa_mod_operation_dispatch(qa_mod_operation *operation, const void *input,
                               qa_mod_canonical canonical,
                               const qa_mod_before_observers *before_observers,
                               void *out_result) {
  /* Snapshot the entry list as TS captures `entries`; removals during the
   * call still take effect through the shared active flags, so snapshot
   * entry pointers into a heap copy of indices is not enough: copy the
   * registrations but consult live active state by sequence. */
  size_t count = operation->entry_count;
  qa_mod_operation_entry *snapshot = NULL;
  if (count != 0) {
    snapshot = malloc(count * sizeof *snapshot);
    if (snapshot == NULL) abort();
    memcpy(snapshot, operation->entries, count * sizeof *snapshot);
  }
  if (count == 0) {
    canonical.call(canonical.ctx, input, out_result);
    if (before_observers != NULL) before_observers->call(before_observers->ctx);
    free(snapshot);
    return;
  }
  void *request = malloc(operation->request_size != 0 ? operation->request_size : 1);
  void *scratch = malloc(operation->request_size != 0 ? operation->request_size : 1);
  if (request == NULL || scratch == NULL) abort();
  memcpy(request, input, operation->request_size);
  for (size_t i = 0; i < count; i++) {
    if (!snapshot[i].active) continue;
    /* Re-check liveness: an earlier transform may have unregistered it. */
    bool live = false;
    for (size_t j = 0; j < operation->entry_count; j++) {
      if (operation->entries[j].sequence == snapshot[i].sequence) live = true;
    }
    if (!live) continue;
    if (snapshot[i].contribution.kind == QA_MOD_TRANSFORM) {
      snapshot[i].contribution.transform(snapshot[i].contribution.ctx, request, scratch);
      memcpy(request, scratch, operation->request_size);
    }
  }
  const qa_mod_operation_entry *replacement = NULL;
  for (size_t i = 0; i < count; i++) {
    if (snapshot[i].active && snapshot[i].contribution.kind == QA_MOD_REPLACE) {
      bool live = false;
      for (size_t j = 0; j < operation->entry_count; j++) {
        if (operation->entries[j].sequence == snapshot[i].sequence) live = true;
      }
      if (live) { replacement = &snapshot[i]; break; }
    }
  }
  if (replacement != NULL) {
    next_state state;
    state.operation = operation;
    state.canonical = canonical;
    state.replacement = &replacement->contribution;
    state.request = request;
    state.open = true;
    state.called = false;
    qa_mod_next next;
    next.ctx = &state;
    next.call = next_call;
    replacement->contribution.replace(replacement->contribution.ctx, request, next, out_result);
    state.open = false;
  } else {
    canonical.call(canonical.ctx, request, out_result);
  }
  if (before_observers != NULL) before_observers->call(before_observers->ctx);
  for (size_t i = 0; i < count; i++) {
    if (!snapshot[i].active || snapshot[i].contribution.kind != QA_MOD_OBSERVE) continue;
    bool live = false;
    for (size_t j = 0; j < operation->entry_count; j++) {
      if (operation->entries[j].sequence == snapshot[i].sequence) live = true;
    }
    if (!live) continue;
    snapshot[i].contribution.observe(snapshot[i].contribution.ctx, request, out_result);
  }
  free(request);
  free(scratch);
  free(snapshot);
}

void qa_mod_operation_clear(qa_mod_operation *operation) {
  for (size_t i = 0; i < operation->entry_count; i++) operation->entries[i].active = false;
  operation->entry_count = 0;
}
