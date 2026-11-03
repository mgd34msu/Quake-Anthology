/*
 * C-only support for the Q2 native bindings (no .ts file): the recoverable
 * TypeScript throws these modules rely on (SourceItemRetired, frame.cancelled,
 * RemovedNativeInput, the ammo stop and supply decline sentinels) unwind a
 * guest call back to the host frame that catches them. Fatal TS throws stay
 * qa_range_error; only those sentinels use this channel.
 *
 * qa_compat_q2_native_try is `try { body(ctx) }`: it returns true when the body
 * completed, false with *thrown filled when a throw reached it. `finally` code
 * runs after the call, then qa_compat_q2_native_rethrow(thrown) continues the
 * unwind for tokens this frame does not handle. A throw with no enclosing try
 * reports its message through qa_range_error, as an uncaught TS Error does.
 *
 * A catch that resumes guest execution must restore the processor and call
 * qa_compat_q2_native_unwind so the runner forgets the abandoned calls.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_Q2_NATIVE_THROW_H
#define QA_COMPAT_Q2_NATIVE_THROW_H
#include <stdbool.h>
#include <stdnoreturn.h>
#include "qa/compat/q2/classic/layout.h"

typedef struct qa_compat_q2_native_thrown {
  const void *token;   /* identity compared, as TS `error === token` */
  const char *message; /* reported when uncaught */
} qa_compat_q2_native_thrown;

bool qa_compat_q2_native_try(void (*body)(void *ctx), void *ctx, qa_compat_q2_native_thrown *thrown);
noreturn void qa_compat_q2_native_throw(const void *token, const char *message);
noreturn void qa_compat_q2_native_rethrow(const qa_compat_q2_native_thrown *thrown);
/* Current runner call depth / abandon deeper calls after a caught throw. */
double qa_compat_q2_native_depth(const qa_compat_q2_runner *runner);
void qa_compat_q2_native_unwind(const qa_compat_q2_runner *runner, double depth);
#endif
