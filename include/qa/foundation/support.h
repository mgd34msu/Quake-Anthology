/*
 * C-only support for the port: TypeScript `throw new RangeError(msg)` on
 * invalid arguments becomes qa_range_error(), which prints the same
 * message to stderr and aborts. There is no corresponding .ts file.
 * GPL-2.0-or-later.
 */
#ifndef QA_FOUNDATION_SUPPORT_H
#define QA_FOUNDATION_SUPPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdnoreturn.h>

/** Largest integer a TypeScript `number` holds exactly (2^53 - 1). */
#define QA_MAX_SAFE_INTEGER 9007199254740991LL

noreturn void qa_range_error(const char *format, ...);

/*
 * TS try/catch around a throwing call, for the places where the TS catch is
 * behavior: the render worker turns a throw into a terminal failure instead of
 * dying, and try/finally scopes restore state. Runs fn(ctx). When fn raises
 * qa_range_error on this thread, stores the message and returns false; nothing
 * is printed. Frames between the raise and this call are abandoned, as a throw
 * abandons them. With no catch active, qa_range_error aborts as documented above.
 */
bool qa_range_error_catch(void (*fn)(void *ctx), void *ctx, char *message, size_t capacity);

/*
 * Every error class the port raises (RangeError, BinaryError, PacketError, ...)
 * funnels through qa_raise, so a TS `catch (error)` that sees any class is
 * qa_try. `name` is the TS error.name ("BinaryError"), `message` the TS
 * error.message, `code` and `detail` the numeric payload a few classes carry
 * (ComError.code, PacketError.offset, ...). A TS `instanceof` test is a strcmp on
 * name; TS code that rethrows other classes calls qa_rethrow.
 */
#define QA_CAUGHT_MESSAGE_CAPACITY 2048
typedef struct qa_caught_error {
  const char *name; /* static text */
  int code;
  long long detail;
  char message[QA_CAUGHT_MESSAGE_CAPACITY];
} qa_caught_error;

/* With a catch active: formats, runs cleanups, longjmps. Otherwise prints
 * "<name>: <message>" to stderr and aborts. `name` must be static text. */
noreturn void qa_raise(const char *name, int code, long long detail, const char *format, ...);
bool qa_try(void (*fn)(void *ctx), void *ctx, qa_caught_error *caught);
noreturn void qa_rethrow(const qa_caught_error *caught);

/*
 * Throw-time cleanup for owned memory held across calls that can raise, the role
 * a GC plays in TS. A raise caught by qa_try runs the nodes pushed inside that
 * try, newest first; frames are abandoned afterward. Nodes live on the caller's
 * stack and pop in LIFO order; popping does not run the cleanup.
 */
typedef struct qa_cleanup {
  void (*fn)(void *arg);
  void *arg;
  struct qa_cleanup *previous;
} qa_cleanup;
void qa_cleanup_push(qa_cleanup *node, void (*fn)(void *arg), void *arg);
void qa_cleanup_pop(qa_cleanup *node);

#endif
