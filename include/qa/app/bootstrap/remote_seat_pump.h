/* Port of src/app/bootstrap/remote-seat-pump.ts. Promises become a
 * pumped synchronous state machine: poll/drain execute inline (the TS
 * defers to a microtask; ordering vs other seats is preserved by the
 * caller pumping each seat). Enqueued operations run from drain_ready;
 * their result is delivered via resolve/reject fn+ctx. The 4096-entry
 * EventRing becomes a bounded FIFO with the same capacity.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_REMOTE_SEAT_PUMP_H
#define QA_APP_BOOTSTRAP_REMOTE_SEAT_PUMP_H
#include <stdbool.h>
typedef struct qa_app_remote_seat_pump_source {
  void *ctx;
  /* Returns error message (borrowed) or NULL. The retirement error
   * pointer compares equal to qa_app_remote_seat_pump_retirement_error. */
  const char *(*network_poll)(void *ctx, double now_milliseconds);
  void (*close)(void *ctx);
} qa_app_remote_seat_pump_source;
typedef struct qa_app_remote_seat_publication {
  void *ctx;
  bool (*ready)(void *ctx);
  const char *(*execute)(void *ctx); /* error or NULL; delivery via ctx */
  void (*reject)(void *ctx, const char *error);
} qa_app_remote_seat_publication;
typedef struct qa_app_remote_seat_pump qa_app_remote_seat_pump;
extern const char qa_app_remote_seat_pump_retirement_error_text[];
qa_app_remote_seat_pump *qa_app_remote_seat_pump_create(qa_app_remote_seat_pump_source source);
void qa_app_remote_seat_pump_destroy(qa_app_remote_seat_pump *p);
void qa_app_remote_seat_pump_poll(qa_app_remote_seat_pump *p, double now_milliseconds);
void qa_app_remote_seat_pump_throw_failure(qa_app_remote_seat_pump *p); /* aborts with stored failure */
/* Returns false when closed (TS rejected promise with retirementError;
 * reject is invoked immediately). */
bool qa_app_remote_seat_pump_enqueue(qa_app_remote_seat_pump *p, qa_app_remote_seat_publication publication);
void qa_app_remote_seat_pump_drain_ready(qa_app_remote_seat_pump *p);
void qa_app_remote_seat_pump_close(qa_app_remote_seat_pump *p);
#endif
