/*
 * Port of src/bots/behavior/library/call-steps.ts.
 *
 * TS CallSteps<T> is Generator<() => Promise<undefined>, T, undefined>. C has
 * neither generators nor promises (PORTING.md: asynchronous flows become
 * synchronous or explicitly pumped), so a generator becomes a resumable
 * next(ctx, thrown) function and a lazy wait becomes a synchronous run(ctx).
 *  - next(ctx, NULL) is steps.next(); next(ctx, message) is steps.throw(error).
 *    A body that does not handle a thrown error reports it through
 *    qa_call_step_result.thrown, which is how an exception leaving the body
 *    (and crossing yield*, via qa_call_steps_forward) is represented.
 *  - qa_call_wait.run returns NULL on success or the failure text, the
 *    rejected wait of continueCalls. The text stays valid until the next
 *    resumption has returned.
 *  - qa_run_calls cannot return a Promise. It pumps each wait to completion
 *    in order and returns the final value (what the TS promise resolves to).
 *    An error that leaves the body is uncaught and aborts (the TS rejection
 *    reaches the top-level crash handler).
 * Ownership: whoever receives a qa_call_steps drives it to completion, which
 * also releases it (qa_finish_calls and qa_run_calls do this).
 * GPL-2.0-or-later.
 */
#ifndef QA_BOTS_BEHAVIOR_LIBRARY_CALL_STEPS_H
#define QA_BOTS_BEHAVIOR_LIBRARY_CALL_STEPS_H
#include <stdbool.h>

typedef struct qa_call_wait {
  const char *(*run)(void *ctx);
  void *ctx;
} qa_call_wait;

typedef struct qa_call_step_result {
  bool done;
  void *value;       /* done: the generator's return value */
  qa_call_wait wait; /* !done: the yielded lazy wait */
  const char *thrown; /* non-NULL: the body threw this instead of stepping */
} qa_call_step_result;

typedef qa_call_step_result (*qa_call_steps_next_fn)(void *ctx, const char *thrown);

/* next == NULL is a body that finished immediately with `value`. */
typedef struct qa_call_steps {
  qa_call_steps_next_fn next;
  void *ctx;
  void (*release)(void *ctx); /* may be NULL */
  void *value;
} qa_call_steps;

/* A body with no waits, e.g. `*clientCommand() { return undefined; }`. */
qa_call_steps qa_call_steps_done(void *value);
/* One resumption. Does not release. */
qa_call_step_result qa_call_steps_next(qa_call_steps *steps, const char *thrown);
void qa_call_steps_release(qa_call_steps *steps);
/* yield* inner: forwards next/throw; releases inner when it completes or throws. */
qa_call_step_result qa_call_steps_forward(qa_call_steps *inner, const char *thrown);

/* TS finishCalls: must complete on the first step, else aborts with the TS error. */
void *qa_finish_calls(qa_call_steps steps);
/* TS runCalls: see the header comment. */
void *qa_run_calls(qa_call_steps steps);

/* TS waitForCall: call is run lazily as the single wait. It returns the awaited
 * value and sets *failure to reject. The returned steps own their state. */
typedef void *(*qa_wait_for_call_fn)(void *ctx, const char **failure);
qa_call_steps qa_wait_for_call(qa_wait_for_call_fn call, void *ctx);
#endif
