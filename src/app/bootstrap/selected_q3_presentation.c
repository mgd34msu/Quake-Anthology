/* Port of src/app/bootstrap/selected-q3-presentation.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/selected_q3_presentation.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
struct qa_app_selected_q3_presentation {
  qa_app_selected_q3_host host;
  qa_app_selected_q3_source_ref source;
  const void *presentation;
  qa_actor_id actor;
  double generation, frame;
  bool initialized, busy, closed;
};
qa_app_selected_q3_presentation *qa_app_selected_q3_presentation_create(qa_app_selected_q3_host host,
    qa_app_selected_q3_source_ref source, const void *presentation) {
  qa_app_selected_q3_presentation *p = calloc(1, sizeof *p);
  if (!p) return NULL;
  p->host = host; p->source = source; p->presentation = presentation;
  p->actor = host.presentation_actor(host.ctx, presentation);
  p->generation = host.presentation_generation(host.ctx, presentation);
  p->frame = -1;
  const char *error = host.initialize(host.ctx, &source, presentation);
  if (error != NULL) {
    const char *cleanup = host.close_one(host.ctx);
    free(p);
    if (cleanup != NULL) qa_range_error("Selected Q3 presentation initialization and cleanup failed");
    qa_range_error("%s", error);
  }
  p->initialized = true;
  host.flush_audio(host.ctx);
  return p;
}
void qa_app_selected_q3_presentation_destroy(qa_app_selected_q3_presentation *p) {
  if (!p) return;
  qa_app_selected_q3_presentation_close(p);
  free(p);
}
const char *qa_app_selected_q3_presentation_hardware(qa_app_selected_q3_presentation *p) {
  return p->host.hardware(p->host.ctx, &p->source);
}
bool qa_app_selected_q3_presentation_owns(qa_app_selected_q3_presentation *p, qa_app_selected_q3_source_ref source, const void *presentation) {
  if (source.identity != p->source.identity) return false;
  return p->host.source_owns_presentation(p->host.ctx, &source, presentation);
}
static void assert_current(qa_app_selected_q3_presentation *p) {
  if (!qa_app_selected_q3_presentation_owns(p, p->source, p->presentation))
    qa_range_error("Selected Q3 presentation belongs to a retired source or viewer");
}
void qa_app_selected_q3_presentation_prepare(qa_app_selected_q3_presentation *p, qa_app_selected_q3_source_ref source,
                                             const void *const *events, size_t count, double frame) {
  if (p->closed) qa_range_error("Selected Q3 presentation belongs to a retired source or viewer");
  if (p->busy || source.identity != p->source.identity)
    qa_range_error("Selected Q3 scene source changed or is already preparing");
  if (!p->initialized) qa_range_error("Selected Q3 presentation has not initialized");
  assert_current(p);
  p->busy = true;
  const char *error = p->host.prepare_frame(p->host.ctx, &source, p->presentation, events, count, frame);
  p->busy = false;
  if (error != NULL) {
    const char *cleanup = p->host.close_one(p->host.ctx);
    if (cleanup != NULL) qa_range_error("Selected Q3 frame and cleanup failed");
    qa_range_error("%s", error);
  }
  p->frame = frame;
  p->host.flush_audio(p->host.ctx);
}
void qa_app_selected_q3_presentation_close(qa_app_selected_q3_presentation *p) {
  if (p->closed) return;
  p->closed = true;
  const char *error = p->host.close_one(p->host.ctx);
  if (error != NULL) qa_range_error("Selected Q3 presentation cleanup failed");
}
typedef struct seat_entry { const void *presentation; qa_app_selected_q3_presentation *value; } seat_entry;
struct qa_app_selected_q3_presentations {
  qa_app_selected_q3_host host;
  seat_entry *seats; size_t seat_count, seat_cap;
  bool closed, busy;
};
qa_app_selected_q3_presentations *qa_app_selected_q3_presentations_create(qa_app_selected_q3_host host) {
  qa_app_selected_q3_presentations *c = calloc(1, sizeof *c);
  if (!c) return NULL;
  c->host = host;
  return c;
}
static void retire_seat(qa_app_selected_q3_presentations *c, size_t index, bool *failed) {
  if (c->seats[index].value) {
    /* close aborts on cleanup failure in C; TS aggregates. */
    qa_app_selected_q3_presentation_destroy(c->seats[index].value);
    c->seats[index].value = NULL;
  }
  (void)failed;
}
void qa_app_selected_q3_presentations_destroy(qa_app_selected_q3_presentations *c) {
  if (!c) return;
  qa_app_selected_q3_presentations_close(c);
  free(c->seats); free(c);
}
void qa_app_selected_q3_presentations_retain(qa_app_selected_q3_presentations *c, const void *const *presentations, size_t count) {
  if (c->closed) qa_range_error("Selected Q3 presentation collection is closed");
  bool failed = false;
  for (size_t i = 0; i < c->seat_count;) {
    bool kept = false;
    for (size_t j = 0; j < count; j++) if (presentations[j] == c->seats[i].presentation) kept = true;
    if (kept) { i++; continue; }
    retire_seat(c, i, &failed);
    memmove(&c->seats[i], &c->seats[i + 1], (c->seat_count - i - 1) * sizeof(seat_entry));
    c->seat_count--;
  }
  if (failed) qa_range_error("Selected Q3 seat retirement failed");
}
void qa_app_selected_q3_presentations_prepare(qa_app_selected_q3_presentations *c, const void *const *presentations, size_t count,
                                              const qa_app_selected_q3_source_ref *source, const void *const *events, size_t event_count, double frame) {
  if (c->closed || c->busy) qa_range_error("Selected Q3 presentation collection is closed or already preparing");
  c->busy = true;
  qa_app_selected_q3_presentations_retain(c, presentations, count);
  if (source != NULL) {
    for (size_t i = 0; i < count; i++) {
      qa_app_selected_q3_presentation *value = NULL;
      for (size_t j = 0; j < c->seat_count; j++) if (c->seats[j].presentation == presentations[i]) value = c->seats[j].value;
      if (value != NULL && !qa_app_selected_q3_presentation_owns(value, *source, presentations[i])) {
        for (size_t j = 0; j < c->seat_count; j++) if (c->seats[j].presentation == presentations[i]) {
          bool failed = false;
          retire_seat(c, j, &failed);
          memmove(&c->seats[j], &c->seats[j + 1], (c->seat_count - j - 1) * sizeof(seat_entry));
          c->seat_count--;
        }
        value = NULL;
      }
      if (value == NULL) {
        value = qa_app_selected_q3_presentation_create(c->host, *source, presentations[i]);
        if (c->seat_count == c->seat_cap) { c->seat_cap = c->seat_cap ? c->seat_cap * 2 : 4; c->seats = realloc(c->seats, c->seat_cap * sizeof *c->seats); if (!c->seats) qa_range_error("Out of memory"); }
        c->seats[c->seat_count++] = (seat_entry){presentations[i], value};
      }
      qa_app_selected_q3_presentation_prepare(value, *source, events, event_count, frame);
    }
  }
  c->busy = false;
}
void qa_app_selected_q3_presentations_close(qa_app_selected_q3_presentations *c) {
  if (c->closed) return;
  c->closed = true;
  bool failed = false;
  for (size_t i = 0; i < c->seat_count; i++) retire_seat(c, i, &failed);
  c->seat_count = 0;
  if (failed) qa_range_error("Selected Q3 seat retirement failed");
}
