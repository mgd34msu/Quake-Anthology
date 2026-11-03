/* See include/qa/foundation/support.h. GPL-2.0-or-later. */
#include "qa/foundation/support.h"

#include <stdarg.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct catch_frame {
  jmp_buf jump;
  qa_caught_error *caught;
  qa_cleanup *cleanups;
  struct catch_frame *previous;
} catch_frame;
static _Thread_local catch_frame *catch_top;
static _Thread_local qa_cleanup *cleanup_top;

bool qa_try(void (*fn)(void *ctx), void *ctx, qa_caught_error *caught) {
  catch_frame frame;
  frame.caught = caught;
  frame.cleanups = cleanup_top;
  frame.previous = catch_top;
  catch_top = &frame;
  if (setjmp(frame.jump) != 0) return false; /* the raise restored catch_top and cleanup_top */
  fn(ctx);
  catch_top = frame.previous;
  return true;
}

bool qa_range_error_catch(void (*fn)(void *ctx), void *ctx, char *message, size_t capacity) {
  qa_caught_error caught;
  if (qa_try(fn, ctx, &caught)) return true;
  if (capacity != 0) snprintf(message, capacity, "%s", caught.message);
  return false;
}

static noreturn void raise_formatted(const char *name, int code, long long detail,
    const char *format, va_list args) {
  if (catch_top != NULL) {
    catch_frame *frame = catch_top;
    qa_caught_error *caught = frame->caught;
    /* Format first: the arguments may be owned by a pending cleanup. */
    char *message = caught->message;
    vsnprintf(message, sizeof caught->message, format, args);
    caught->name = name;
    caught->code = code;
    caught->detail = detail;
    catch_top = frame->previous;
    while (cleanup_top != frame->cleanups) {
      qa_cleanup *node = cleanup_top;
      cleanup_top = node->previous;
      node->fn(node->arg);
    }
    longjmp(frame->jump, 1);
  }
  fprintf(stderr, "%s: ", name);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  abort();
}

noreturn void qa_raise(const char *name, int code, long long detail, const char *format, ...) {
  va_list args;
  va_start(args, format);
  raise_formatted(name, code, detail, format, args);
}

noreturn void qa_rethrow(const qa_caught_error *caught) {
  qa_raise(caught->name, caught->code, caught->detail, "%s", caught->message);
}

noreturn void qa_range_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  raise_formatted("RangeError", 0, 0, format, args);
}

void qa_cleanup_push(qa_cleanup *node, void (*fn)(void *arg), void *arg) {
  node->fn = fn;
  node->arg = arg;
  node->previous = cleanup_top;
  cleanup_top = node;
}

void qa_cleanup_pop(qa_cleanup *node) {
  if (cleanup_top != node) {
    fputs("RangeError: cleanup popped out of order\n", stderr);
    abort();
  }
  cleanup_top = node->previous;
}
