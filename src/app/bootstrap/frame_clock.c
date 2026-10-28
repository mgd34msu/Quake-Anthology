/* Port of src/app/bootstrap/frame-clock.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/frame_clock.h"
#include "qa/foundation/support.h"
void qa_app_presentation_time_init(qa_app_presentation_time *clock) {
  clock->has_committed = false;
  clock->committed_milliseconds = 0;
}
void qa_app_presentation_time_advance(qa_app_presentation_time *clock, double wall_milliseconds,
                                      double wall_elapsed_milliseconds, double presentation_elapsed_milliseconds) {
  double base = clock->has_committed ? clock->committed_milliseconds : wall_milliseconds - wall_elapsed_milliseconds;
  clock->committed_milliseconds = base + presentation_elapsed_milliseconds;
  clock->has_committed = true;
}
double qa_app_presentation_time_milliseconds(const qa_app_presentation_time *clock) {
  if (!clock->has_committed) qa_range_error("Presentation clock has no committed frame");
  return clock->committed_milliseconds;
}
