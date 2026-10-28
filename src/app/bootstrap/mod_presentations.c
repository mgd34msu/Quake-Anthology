/* Port of src/app/bootstrap/mod-presentations.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/mod_presentations.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
typedef struct entry {
  qa_app_application_mod_presentation *consumer;
  qa_app_component_client_command_target target;
} entry;
struct qa_app_application_mod_presentations {
  qa_app_mod_presentations_factory factory;
  entry *entries; size_t count, capacity;
  bool closed;
};
qa_app_application_mod_presentations *qa_app_application_mod_presentations_create(
    qa_app_mod_presentations_factory factory) {
  qa_app_application_mod_presentations *presentations = calloc(1, sizeof *presentations);
  if (presentations == NULL) qa_range_error("Out of memory");
  presentations->factory = factory;
  return presentations;
}
static entry *find_entry(qa_app_application_mod_presentations *presentations,
    qa_provider_id provider, qa_seat_id seat) {
  for (size_t i = 0; i < presentations->count; i++) {
    entry *e = &presentations->entries[i];
    if (strcmp(e->target.owner.provider, provider) == 0 && qa_same_seat(e->target.seat, seat)) return e;
  }
  return NULL;
}
const char *qa_app_application_mod_presentations_add(qa_app_application_mod_presentations *presentations,
    qa_provider_id provider, qa_seat_id seat, qa_actor_id viewer, const void *instance) {
  if (presentations->closed) return "Mod presentations are closed";
  if (find_entry(presentations, provider, seat) != NULL)
    return "Mod presentation is already registered for this seat";
  if (presentations->count == presentations->capacity) {
    size_t cap = presentations->capacity == 0 ? 4 : presentations->capacity * 2;
    entry *next = realloc(presentations->entries, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    presentations->entries = next; presentations->capacity = cap;
  }
  entry *e = &presentations->entries[presentations->count++];
  memset(e, 0, sizeof *e);
  e->consumer = presentations->factory.create_presentation(presentations->factory.ctx, provider, seat);
  e->target.owner.provider = provider;
  e->target.owner.generation = 0;
  e->target.instance = instance;
  e->target.seat = seat;
  e->target.viewer = viewer;
  return qa_app_application_mod_presentation_initialize(e->consumer);
}
qa_app_application_mod_presentation *qa_app_application_mod_presentations_find(
    qa_app_application_mod_presentations *presentations, qa_provider_id provider, qa_seat_id seat) {
  entry *e = find_entry(presentations, provider, seat);
  return e != NULL ? e->consumer : NULL;
}
const char *qa_app_application_mod_presentations_frame(qa_app_application_mod_presentations *presentations, double sequence) {
  for (size_t i = 0; i < presentations->count; i++) {
    const char *error = qa_app_application_mod_presentation_frame(presentations->entries[i].consumer, sequence);
    if (error != NULL) return error;
  }
  return NULL;
}
const char *qa_app_application_mod_presentations_receive(qa_app_application_mod_presentations *presentations,
    const void *events, size_t count) {
  for (size_t i = 0; i < presentations->count; i++) {
    const char *error = qa_app_application_mod_presentation_receive(presentations->entries[i].consumer, events, count);
    if (error != NULL) return error;
  }
  return NULL;
}
void qa_app_application_mod_presentations_retire(qa_app_application_mod_presentations *presentations, qa_provider_id provider) {
  size_t kept = 0;
  for (size_t i = 0; i < presentations->count; i++) {
    if (strcmp(presentations->entries[i].target.owner.provider, provider) == 0)
      qa_app_application_mod_presentation_close(presentations->entries[i].consumer);
    else presentations->entries[kept++] = presentations->entries[i];
  }
  presentations->count = kept;
}
void qa_app_application_mod_presentations_close(qa_app_application_mod_presentations *presentations) {
  if (presentations == NULL || presentations->closed) return;
  presentations->closed = true;
  for (size_t i = 0; i < presentations->count; i++)
    qa_app_application_mod_presentation_close(presentations->entries[i].consumer);
  free(presentations->entries);
  free(presentations);
}
