/* Port of src/content/mods/selection.ts. GPL-2.0-or-later. */
#include "qa/content/mods/selection.h"

#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* TS throws plain Error in this module. */
static noreturn void qa_error(const char *message) {
  fprintf(stderr, "Error: %s\n", message);
  abort();
}
static noreturn void fail_format(const char *format, const char *a) {
  char message[4096];
  (void)snprintf(message, sizeof message, format, a);
  qa_error(message);
}
static noreturn void fail_format2(const char *format, const char *a, const char *b) {
  char message[4096];
  (void)snprintf(message, sizeof message, format, a, b);
  qa_error(message);
}

static char *key_of(qa_mod_selection selection) {
  char *key = qa_mod_selection_key(selection);
  if (key == NULL) qa_error("out of memory");
  return key;
}
static bool same_selection(qa_mod_selection a, qa_mod_selection b) {
  char *ka = key_of(a), *kb = key_of(b);
  bool same = strcmp(ka, kb) == 0;
  free(ka);
  free(kb);
  return same;
}
static const qa_mod_description *find_description(const qa_mod_selection_set *set,
                                                  qa_mod_selection selection) {
  for (size_t i = 0; i < set->description_count; i++)
    if (same_selection(set->descriptions[i].selection, selection))
      return &set->descriptions[i];
  return NULL;
}

struct resolve_state {
  const qa_mod_selection_set *set;
  const qa_mod_description **active;
  size_t active_count;
  size_t active_capacity;
  qa_mod_selection *visiting;
  size_t visiting_count;
  size_t visiting_capacity;
};

static bool list_has(const qa_mod_selection *list, size_t count,
                     qa_mod_selection selection) {
  for (size_t i = 0; i < count; i++)
    if (same_selection(list[i], selection)) return true;
  return false;
}
static bool active_has(const struct resolve_state *state, qa_mod_selection selection) {
  for (size_t i = 0; i < state->active_count; i++)
    if (same_selection(state->active[i]->selection, selection)) return true;
  return false;
}

static void visit(struct resolve_state *state, qa_mod_selection selection) {
  char *key = key_of(selection);
  if (active_has(state, selection)) {
    free(key);
    return;
  }
  if (list_has(state->visiting, state->visiting_count, selection)) {
    /* TS: `Mod dependency cycle: ${[...visiting, key].join(" -> ")}` */
    char message[8192];
    size_t used = (size_t)snprintf(message, sizeof message, "Mod dependency cycle: ");
    for (size_t i = 0; i < state->visiting_count; i++) {
      char *part = key_of(state->visiting[i]);
      used += (size_t)snprintf(message + used, sizeof message - used, "%s -> ", part);
      free(part);
    }
    (void)snprintf(message + used, sizeof message - used, "%s", key);
    free(key);
    qa_error(message);
  }
  const qa_mod_description *description = find_description(state->set, selection);
  if (description == NULL) {
    char *copy = key;
    fail_format("Required mod is not installed: %s", copy);
  }
  free(key);
  if (strcmp(description->purpose, "addition") != 0)
    fail_format("%s defines a game type; select it as the game's rules",
                description->title);
  if (!description->is_available)
    fail_format2("%s: %s", description->title,
                 description->unavailable_reason != NULL
                     ? description->unavailable_reason
                     : "");
  if (state->visiting_count == state->visiting_capacity) {
    size_t capacity = state->visiting_capacity == 0 ? 4 : state->visiting_capacity * 2;
    qa_mod_selection *grown = (qa_mod_selection *)realloc(
        state->visiting, capacity * sizeof *grown);
    if (grown == NULL) qa_error("out of memory");
    state->visiting = grown;
    state->visiting_capacity = capacity;
  }
  state->visiting[state->visiting_count++] = selection;
  for (size_t i = 0; i < description->requires_count; i++)
    visit(state, description->requires[i]);
  state->visiting_count--;
  if (state->active_count == state->active_capacity) {
    size_t capacity = state->active_capacity == 0 ? 4 : state->active_capacity * 2;
    const qa_mod_description **grown = (const qa_mod_description **)realloc(
        state->active, capacity * sizeof *grown);
    if (grown == NULL) qa_error("out of memory");
    state->active = grown;
    state->active_capacity = capacity;
  }
  state->active[state->active_count++] = description;
}

/* TS private resolve(): returns a malloc'd dependency-ordered list. */
static qa_mod_selection *resolve(const qa_mod_selection_set *set,
                                 const qa_mod_selection *selections,
                                 size_t selection_count, size_t *out_count) {
  struct resolve_state state;
  memset(&state, 0, sizeof state);
  state.set = set;
  for (size_t i = 0; i < selection_count; i++) visit(&state, selections[i]);
  for (size_t i = 0; i < state.active_count; i++) {
    const qa_mod_description *description = state.active[i];
    for (size_t j = 0; j < description->conflicts_count; j++) {
      for (size_t k = 0; k < state.active_count; k++)
        if (same_selection(state.active[k]->selection, description->conflicts[j]))
          fail_format2("%s conflicts with %s", description->title,
                       state.active[k]->title);
    }
  }
  qa_mod_selection *result = state.active_count > 0
      ? (qa_mod_selection *)malloc(state.active_count * sizeof *result) : NULL;
  if (state.active_count > 0 && result == NULL) qa_error("out of memory");
  for (size_t i = 0; i < state.active_count; i++)
    result[i] = state.active[i]->selection;
  *out_count = state.active_count;
  free(state.active);
  free(state.visiting);
  return result;
}

void qa_mod_selection_set_refresh(qa_mod_selection_set *set,
                                  const qa_mod_description *descriptions,
                                  size_t description_count) {
  size_t capacity = description_count + set->selected_count;
  qa_mod_description *next = capacity > 0
      ? (qa_mod_description *)malloc(capacity * sizeof *next) : NULL;
  if (capacity > 0 && next == NULL) qa_error("out of memory");
  size_t count = 0;
  for (size_t i = 0; i < description_count; i++) {
    for (size_t j = 0; j < count; j++)
      if (same_selection(next[j].selection, descriptions[i].selection)) {
        char *key = key_of(descriptions[i].selection);
        fail_format("Duplicate mod declaration: %s", key);
      }
    next[count++] = descriptions[i];
  }
  /* Missing enabled packages stay visible so users can disable them. */
  for (size_t i = 0; i < set->selected_count; i++) {
    bool present = false;
    for (size_t j = 0; j < count; j++)
      if (same_selection(next[j].selection, set->selected[i])) present = true;
    if (present) continue;
    const qa_mod_description *previous = NULL;
    for (size_t j = 0; j < set->description_count; j++)
      if (same_selection(set->descriptions[j].selection, set->selected[i]))
        previous = &set->descriptions[j];
    if (previous != NULL) {
      next[count] = *previous;
      next[count].is_available = false;
      next[count].unavailable_reason = "This enabled mod is no longer installed";
      count++;
    }
  }
  free(set->descriptions);
  set->descriptions = next;
  set->description_count = count;
}

void qa_mod_selection_set_restore(qa_mod_selection_set *set,
                                  const qa_mod_selection *selections,
                                  size_t selection_count) {
  size_t count = 0;
  qa_mod_selection *resolved = resolve(set, selections, selection_count, &count);
  free(set->selected);
  set->selected = resolved;
  set->selected_count = count;
}

bool qa_mod_selection_set_init(qa_mod_selection_set *set,
                               const qa_mod_description *descriptions,
                               size_t description_count,
                               const qa_mod_selection *initial,
                               size_t initial_count) {
  memset(set, 0, sizeof *set);
  qa_mod_selection_set_refresh(set, descriptions, description_count);
  qa_mod_selection_set_restore(set, initial, initial_count);
  return true;
}

void qa_mod_selection_set_destroy(qa_mod_selection_set *set) {
  free(set->descriptions);
  free(set->selected);
  memset(set, 0, sizeof *set);
}

const qa_mod_description *qa_mod_selection_set_entries(
    const qa_mod_selection_set *set, size_t *out_count) {
  *out_count = set->description_count;
  return set->descriptions;
}
const qa_mod_selection *qa_mod_selection_set_enabled(
    const qa_mod_selection_set *set, size_t *out_count) {
  *out_count = set->selected_count;
  return set->selected;
}
bool qa_mod_selection_set_has(const qa_mod_selection_set *set,
                              qa_mod_selection selection) {
  return list_has(set->selected, set->selected_count, selection);
}

void qa_mod_selection_set_set_enabled(qa_mod_selection_set *set,
                                      qa_mod_selection selection, bool enabled) {
  if (enabled) {
    size_t count = set->selected_count + 1;
    qa_mod_selection *combined =
        (qa_mod_selection *)malloc(count * sizeof *combined);
    if (combined == NULL) qa_error("out of memory");
    if (set->selected_count > 0)
      memcpy(combined, set->selected, set->selected_count * sizeof *combined);
    combined[set->selected_count] = selection;
    size_t resolved_count = 0;
    qa_mod_selection *resolved = resolve(set, combined, count, &resolved_count);
    free(combined);
    free(set->selected);
    set->selected = resolved;
    set->selected_count = resolved_count;
    return;
  }
  /* Remove the selection plus everything that (transitively)
   * requires it. */
  bool *removed = set->selected_count > 0
      ? (bool *)calloc(set->selected_count, sizeof *removed) : NULL;
  if (set->selected_count > 0 && removed == NULL) qa_error("out of memory");
  for (size_t i = 0; i < set->selected_count; i++)
    removed[i] = same_selection(set->selected[i], selection);
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t i = 0; i < set->selected_count; i++) {
      if (removed[i]) continue;
      const qa_mod_description *description =
          find_description(set, set->selected[i]);
      if (description == NULL) continue;
      for (size_t j = 0; j < description->requires_count && !removed[i]; j++)
        for (size_t k = 0; k < set->selected_count; k++)
          if (removed[k] &&
              same_selection(description->requires[j], set->selected[k])) {
            removed[i] = true;
            changed = true;
            break;
          }
    }
  }
  size_t count = 0;
  for (size_t i = 0; i < set->selected_count; i++)
    if (!removed[i]) set->selected[count++] = set->selected[i];
  set->selected_count = count;
  free(removed);
}

void qa_mod_selection_set_validate(const qa_mod_selection_set *set) {
  size_t count = 0;
  qa_mod_selection *resolved = resolve(set, set->selected, set->selected_count, &count);
  free(resolved);
}
