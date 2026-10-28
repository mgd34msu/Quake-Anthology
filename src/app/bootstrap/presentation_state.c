/* Port of src/app/bootstrap/presentation-state.ts (owner lifecycle
 * and event capacity). GPL-2.0-or-later. */
#include "qa/app/bootstrap/presentation_state.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
typedef struct owner_entry {
  qa_presentation_owner token;
  char *content;
  const char *status; /* "active"|"restored" */
} owner_entry;
struct qa_app_presentation_state {
  qa_app_presentation_state_host host;
  owner_entry *owners; size_t count, capacity;
  double next_generation;
  size_t pending;
  bool closed;
};
static char *dup_text(const char *text) {
  size_t len = strlen(text);
  char *out = malloc(len + 1);
  if (out == NULL) qa_range_error("Out of memory");
  memcpy(out, text, len + 1);
  return out;
}
qa_app_presentation_state *qa_app_presentation_state_create(qa_app_presentation_state_host host) {
  qa_app_presentation_state *state = calloc(1, sizeof *state);
  if (state == NULL) qa_range_error("Out of memory");
  state->host = host;
  return state;
}
void qa_app_presentation_state_destroy(qa_app_presentation_state *state) {
  if (state == NULL) return;
  for (size_t i = 0; i < state->count; i++) free(state->owners[i].content);
  free(state->owners);
  free(state);
}
static owner_entry *find_owner(qa_app_presentation_state *state, qa_provider_id provider) {
  for (size_t i = 0; i < state->count; i++)
    if (strcmp(state->owners[i].token.provider, provider) == 0) return &state->owners[i];
  return NULL;
}
void qa_app_presentation_state_activate(qa_app_presentation_state *state,
    qa_provider_id provider, qa_content_id content, bool restoring, bool component_persistent,
    qa_presentation_owner *out_token) {
  owner_entry *prior = find_owner(state, provider);
  if (prior != NULL && strcmp(prior->status, "active") == 0)
    qa_range_error("Presentation owner is already active: %s", provider);
  if (prior != NULL && (!restoring || strcmp(prior->content, content) != 0))
    qa_range_error("Restored presentation owner differs: %s", provider);
  if (prior == NULL && restoring && component_persistent)
    qa_range_error("Legacy save has persistent presentation in component %s's content without recorded ownership; primary and component output cannot be distinguished", provider);
  if (prior == NULL) {
    if (state->next_generation + 1 > 9007199254740991.0)
      qa_range_error("Presentation owner generation exhausted");
    if (state->count == state->capacity) {
      size_t cap = state->capacity == 0 ? 4 : state->capacity * 2;
      owner_entry *next = realloc(state->owners, cap * sizeof *next);
      if (next == NULL) qa_range_error("Out of memory");
      state->owners = next; state->capacity = cap;
    }
    prior = &state->owners[state->count++];
    memset(prior, 0, sizeof *prior);
    prior->token.provider = provider;
    prior->token.generation = state->next_generation + 1;
    state->next_generation = prior->token.generation;
    prior->content = dup_text(content);
  }
  prior->status = "active";
  *out_token = prior->token;
}
void qa_app_presentation_state_retire(qa_app_presentation_state *state, qa_provider_id provider) {
  owner_entry *entry = find_owner(state, provider);
  if (entry == NULL || strcmp(entry->status, "active") != 0)
    qa_range_error("Presentation owner is not active: %s", provider);
  entry->status = "restored"; /* retired entries await re-activation */
  size_t kept = 0;
  for (size_t i = 0; i < state->count; i++) {
    if (&state->owners[i] == entry) { free(state->owners[i].content); continue; }
    state->owners[kept++] = state->owners[i];
  }
  state->count = kept;
}
bool qa_app_presentation_state_is_active(const qa_app_presentation_state *state, qa_provider_id provider) {
  for (size_t i = 0; i < state->count; i++)
    if (strcmp(state->owners[i].token.provider, provider) == 0)
      return strcmp(state->owners[i].status, "active") == 0;
  return false;
}
void qa_app_presentation_state_require_restored(qa_app_presentation_state *state) {
  for (size_t i = 0; i < state->count; i++) {
    if (strcmp(state->owners[i].status, "restored") == 0)
      qa_range_error("Saved presentation owner was not restored: %s", state->owners[i].token.provider);
  }
}
void qa_app_presentation_state_admit_event(qa_app_presentation_state *state) {
  if (state->pending >= QA_APP_PRESENTATION_EVENT_CAPACITY)
    qa_range_error("Presentation event capacity exhausted (%d undrained events); drain presentation output before admitting more",
        QA_APP_PRESENTATION_EVENT_CAPACITY);
  state->pending++;
}
void qa_app_presentation_state_drain_events(qa_app_presentation_state *state, size_t count) {
  state->pending = count >= state->pending ? 0 : state->pending - count;
}
size_t qa_app_presentation_state_pending_events(const qa_app_presentation_state *state) {
  return state->pending;
}
void qa_app_presentation_state_close(qa_app_presentation_state *state) {
  if (state != NULL) state->closed = true;
}
