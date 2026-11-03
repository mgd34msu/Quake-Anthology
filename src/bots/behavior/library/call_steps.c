/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/library/call_steps.h"
#include "qa/foundation/support.h"
#include <stddef.h>
#include <stdlib.h>

qa_call_steps qa_call_steps_done(void *value) {
  qa_call_steps steps = {NULL, NULL, NULL, value};
  return steps;
}

qa_call_step_result qa_call_steps_next(qa_call_steps *steps, const char *thrown) {
  if (steps->next != NULL)
    return steps->next(steps->ctx, thrown);
  qa_call_step_result result = {true, steps->value, {NULL, NULL}, thrown};
  return result;
}

void qa_call_steps_release(qa_call_steps *steps) {
  if (steps->release != NULL)
    steps->release(steps->ctx);
  steps->release = NULL;
}

qa_call_step_result qa_call_steps_forward(qa_call_steps *inner, const char *thrown) {
  qa_call_step_result step = qa_call_steps_next(inner, thrown);
  if (step.done || step.thrown != NULL)
    qa_call_steps_release(inner);
  return step;
}

void *qa_finish_calls(qa_call_steps steps) {
  qa_call_step_result step = qa_call_steps_next(&steps, NULL);
  if (step.thrown != NULL)
    qa_range_error("%s", step.thrown);
  if (!step.done)
    qa_range_error("Cannot synchronously finish an asynchronous call");
  qa_call_steps_release(&steps);
  return step.value;
}

/* TS continueCalls: only the wait's failure enters throw; a resumed body that
 * throws independently reaches the caller of the promise. */
void *qa_run_calls(qa_call_steps steps) {
  qa_call_step_result step = qa_call_steps_next(&steps, NULL);
  while (true) {
    if (step.thrown != NULL)
      qa_range_error("%s", step.thrown);
    if (step.done)
      break;
    const char *failure = step.wait.run(step.wait.ctx);
    step = qa_call_steps_next(&steps, failure);
  }
  qa_call_steps_release(&steps);
  return step.value;
}

typedef struct wait_for_call {
  qa_wait_for_call_fn call;
  void *ctx;
  bool yielded;
  bool completed;
  void *value;
} wait_for_call;

static const char *wait_for_call_run(void *ctx) {
  wait_for_call *state = ctx;
  const char *failure = NULL;
  void *value = state->call(state->ctx, &failure);
  if (failure != NULL)
    return failure;
  state->value = value;
  state->completed = true;
  return NULL;
}

static qa_call_step_result wait_for_call_next(void *ctx, const char *thrown) {
  wait_for_call *state = ctx;
  qa_call_step_result step = {false, NULL, {NULL, NULL}, NULL};
  /* A generator not yet started leaves through throw without running its body;
   * a suspended one receives the error at its yield. Neither catches it. */
  if (thrown != NULL) {
    step.thrown = thrown;
    return step;
  }
  if (!state->yielded) {
    state->yielded = true;
    step.wait.run = wait_for_call_run;
    step.wait.ctx = state;
    return step;
  }
  if (!state->completed)
    qa_range_error("Call resumed before its wait completed");
  step.done = true;
  step.value = state->value;
  return step;
}

qa_call_steps qa_wait_for_call(qa_wait_for_call_fn call, void *ctx) {
  wait_for_call *state = calloc(1, sizeof *state);
  if (state == NULL)
    qa_range_error("out of memory");
  state->call = call;
  state->ctx = ctx;
  qa_call_steps steps = {wait_for_call_next, state, free, NULL};
  return steps;
}
