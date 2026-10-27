/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/library/call_steps.h"
#include "qa/foundation/support.h"
#include <stddef.h>
void *qa_finish_calls(qa_call_steps s) {
  if (s.next == NULL)
    qa_range_error("Cannot synchronously finish an asynchronous call");
  qa_call_step_result r = s.next(s.ctx);
  if (!r.done)
    qa_range_error("Cannot synchronously finish an asynchronous call");
  return r.value;
}
void *qa_run_calls(qa_call_steps s) {
  if (s.next == NULL)
    qa_range_error("Cannot synchronously finish an asynchronous call");
  for (;;) {
    qa_call_step_result r = s.next(s.ctx);
    if (r.done)
      return r.value;
  }
}
