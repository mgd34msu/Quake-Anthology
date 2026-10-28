/* Port of src/app/bootstrap/frame-clock.ts (PresentationTime).
 * Supersedes qa_app_presentation_time_mirror in remote_seats.h.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_FRAME_CLOCK_H
#define QA_APP_BOOTSTRAP_FRAME_CLOCK_H
#include <stdbool.h>
typedef struct qa_app_presentation_time {
  bool has_committed;
  double committed_milliseconds;
} qa_app_presentation_time;
void qa_app_presentation_time_init(qa_app_presentation_time *clock);
void qa_app_presentation_time_advance(qa_app_presentation_time *clock, double wall_milliseconds,
                                      double wall_elapsed_milliseconds, double presentation_elapsed_milliseconds);
/* Aborts with the TS Error before the first committed frame. */
double qa_app_presentation_time_milliseconds(const qa_app_presentation_time *clock);
#endif
