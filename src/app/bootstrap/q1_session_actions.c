/* Port of src/app/bootstrap/q1-session-actions.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q1_session_actions.h"
#include <string.h>
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
const char *qa_app_source_level_completion(qa_app_simulation_event_view source) {
  if ((is(source.kind, "q1") && is(source.event_kind, "intermission"))
      || (is(source.kind, "q1-session") && is(source.event_kind, "level-completed"))) return "q1";
  return is(source.kind, "q2-rerelease") && is(source.event_kind, "end-of-unit") ? "q2" : NULL;
}
size_t qa_app_q1_session_departures(const qa_app_simulation_event_view *events, size_t event_count,
    const qa_app_seat_actor *seats, size_t seat_count, qa_seat_id *out) {
  size_t written = 0;
  for (size_t s = 0; s < seat_count; s++) {
    bool departs = false;
    for (size_t e = 0; e < event_count; e++) {
      if (is(events[e].kind, "q1-session") && is(events[e].event_kind, "back-to-lobby")
          && (!events[e].has_recipient || qa_same_actor(events[e].recipient, seats[s].actor))) departs = true;
    }
    if (departs) out[written++] = seats[s].seat;
  }
  return written;
}
