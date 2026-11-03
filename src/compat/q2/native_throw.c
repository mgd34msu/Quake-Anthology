/* Recoverable throw channel for the Q2 native bindings. See the header.
 * GPL-2.0-or-later. */
#include "qa/compat/q2/native_throw.h"
#include <setjmp.h>
#include <stddef.h>

typedef struct try_frame {
  struct try_frame *previous;
  jmp_buf jump;
  qa_compat_q2_native_thrown thrown;
} try_frame;
static try_frame *top = NULL;

bool qa_compat_q2_native_try(void (*body)(void *ctx), void *ctx, qa_compat_q2_native_thrown *thrown) {
  try_frame frame;
  frame.previous = top;
  frame.thrown.token = NULL; frame.thrown.message = NULL;
  top = &frame;
  if (setjmp(frame.jump) == 0) {
    body(ctx);
    top = frame.previous;
    return true;
  }
  top = frame.previous;
  if (thrown != NULL) *thrown = frame.thrown;
  return false;
}
noreturn void qa_compat_q2_native_throw(const void *token, const char *message) {
  if (top == NULL) qa_range_error("%s", message != NULL ? message : "Uncaught native control throw");
  top->thrown.token = token; top->thrown.message = message;
  longjmp(top->jump, 1);
}
noreturn void qa_compat_q2_native_rethrow(const qa_compat_q2_native_thrown *thrown) {
  qa_compat_q2_native_throw(thrown->token, thrown->message);
}
double qa_compat_q2_native_depth(const qa_compat_q2_runner *runner) {
  return runner->depth != NULL ? runner->depth(runner->ctx) : 0;
}
void qa_compat_q2_native_unwind(const qa_compat_q2_runner *runner, double depth) {
  if (runner->unwind != NULL) runner->unwind(runner->ctx, depth);
}
