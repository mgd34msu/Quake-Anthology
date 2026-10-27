/*
 * Port of src/media/transitions.ts: the post-cinematic command
 * transition, snapshotted at movie admission so an unrelated seat
 * cannot consume it. The host is a vtable plus ctx; the binding
 * holds the one-shot completion latch. GPL-2.0-or-later.
 */
#ifndef QA_MEDIA_TRANSITIONS_H
#define QA_MEDIA_TRANSITIONS_H

#include <stdbool.h>

#include "qa/contracts/identity.h"
#include "qa/media/types.h"

typedef enum qa_cinematic_transition_kind {
  QA_CINEMATIC_TRANSITION_Q2_NEXTSERVER,
  QA_CINEMATIC_TRANSITION_Q3_NEXTMAP,
  QA_CINEMATIC_TRANSITION_RETURN
} qa_cinematic_transition_kind;

typedef struct qa_cinematic_transition {
  qa_cinematic_transition_kind kind;
  double server_count;   /* q2-nextserver */
  const char *command;   /* q3-nextmap; borrowed */
} qa_cinematic_transition;

typedef struct qa_cinematic_transition_host {
  void *ctx;
  void (*send_client_command)(void *ctx, qa_seat_id seat,
                              const char *command);
  void (*append_command)(void *ctx, qa_seat_id seat, const char *command);
  void (*leave_cinematic)(void *ctx, qa_seat_id seat);
} qa_cinematic_transition_host;

typedef struct qa_cinematic_transition_binding {
  qa_seat_id seat;
  qa_cinematic_transition transition;
  qa_cinematic_transition_host host;
  bool completed;
} qa_cinematic_transition_binding;

/* TS cinematicTransition: the returned binding is the one-shot
 * (reason) => void closure. */
qa_cinematic_transition_binding qa_cinematic_transition_make(
    qa_seat_id seat, qa_cinematic_transition transition,
    qa_cinematic_transition_host host);
void qa_cinematic_transition_fire(qa_cinematic_transition_binding *binding,
                                  qa_cinematic_end_reason reason);

#endif
