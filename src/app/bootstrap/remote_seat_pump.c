/* Port of src/app/bootstrap/remote-seat-pump.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/remote_seat_pump.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
const char qa_app_remote_seat_pump_retirement_error_text[] = "Remote seat publication was retired";
enum { CAPACITY = 4096 };
struct qa_app_remote_seat_pump {
  qa_app_remote_seat_pump_source source;
  qa_app_remote_seat_publication queue[CAPACITY];
  size_t head, count;
  const char *failure;
  bool pending_poll, pending_drain, closed, closing;
};
qa_app_remote_seat_pump *qa_app_remote_seat_pump_create(qa_app_remote_seat_pump_source source) {
  qa_app_remote_seat_pump *p = calloc(1, sizeof *p);
  if (!p) return NULL;
  p->source = source;
  return p;
}
void qa_app_remote_seat_pump_destroy(qa_app_remote_seat_pump *p) { free(p); }
void qa_app_remote_seat_pump_poll(qa_app_remote_seat_pump *p, double now_milliseconds) {
  if (p->closed || p->pending_poll || p->failure != NULL) return;
  p->pending_poll = true;
  if (!p->closed) {
    const char *error = p->source.network_poll(p->source.ctx, now_milliseconds);
    if (error != NULL && strcmp(error, qa_app_remote_seat_pump_retirement_error_text) != 0) p->failure = error;
  }
  p->pending_poll = false;
}
void qa_app_remote_seat_pump_throw_failure(qa_app_remote_seat_pump *p) {
  if (p->failure != NULL) qa_range_error("%s", p->failure);
}
bool qa_app_remote_seat_pump_enqueue(qa_app_remote_seat_pump *p, qa_app_remote_seat_publication publication) {
  if (p->closed) { publication.reject(publication.ctx, qa_app_remote_seat_pump_retirement_error_text); return false; }
  if (p->count == CAPACITY) qa_range_error("Event ring capacity exhausted (%zu undrained records); drain before admitting more", (size_t)CAPACITY);
  p->queue[(p->head + p->count) % CAPACITY] = publication;
  p->count++;
  return true;
}
void qa_app_remote_seat_pump_drain_ready(qa_app_remote_seat_pump *p) {
  if (p->pending_drain || p->closed) return;
  p->pending_drain = true;
  while (!p->closed && p->count > 0) {
    qa_app_remote_seat_publication next = p->queue[p->head];
    if (next.ready != NULL && !next.ready(next.ctx)) break;
    p->head = (p->head + 1) % CAPACITY;
    p->count--;
    const char *error = next.execute(next.ctx);
    if (error != NULL) next.reject(next.ctx, error);
  }
  p->pending_drain = false;
}
void qa_app_remote_seat_pump_close(qa_app_remote_seat_pump *p) {
  if (p->closing) return;
  p->closed = true;
  p->closing = true;
  while (p->count > 0) {
    qa_app_remote_seat_publication next = p->queue[p->head];
    p->head = (p->head + 1) % CAPACITY;
    p->count--;
    next.reject(next.ctx, qa_app_remote_seat_pump_retirement_error_text);
  }
  p->source.close(p->source.ctx);
  qa_app_remote_seat_pump_throw_failure(p);
}
