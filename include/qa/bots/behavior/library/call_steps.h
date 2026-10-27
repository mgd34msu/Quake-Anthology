/* Port of src/bots/behavior/library/call-steps.ts. Async generators become
 * synchronous step callbacks (fn+ctx); finish aborts if a step yields a wait.
 * GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_LIBRARY_CALL_STEPS_H
#define QA_BOTS_BEHAVIOR_LIBRARY_CALL_STEPS_H
#include <stdbool.h>
typedef struct qa_call_step_result {
  bool done;
  void *value;
  bool has_wait;
} qa_call_step_result;
typedef qa_call_step_result (*qa_call_steps_next_fn)(void *ctx);
typedef struct qa_call_steps {
  qa_call_steps_next_fn next;
  void *ctx;
} qa_call_steps;
/* TS finishCalls: next() must be done, else Error -> qa_range_error. */
void *qa_finish_calls(qa_call_steps steps);
/* TS runCalls: synchronous C has no Promise; drives waits immediately via next. */
void *qa_run_calls(qa_call_steps steps);
#endif
