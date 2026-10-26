/*
 * Port of src/foundation/cleanup.ts (CleanupStack): one-shot cleanup
 * callbacks retained by an owner until close. Running the stack snapshots
 * its entries first; callbacks registered while running wait for the next
 * run. C accommodation: each entry is a function plus context pointer
 * (TypeScript closures capture theirs), and add/push take one entry per
 * call instead of variadic arguments. GPL-2.0-or-later.
 */
#ifndef QA_FOUNDATION_CLEANUP_H
#define QA_FOUNDATION_CLEANUP_H

#include <stddef.h>

typedef void (*qa_cleanup_fn)(void *ctx);

typedef struct qa_cleanup_entry {
  qa_cleanup_fn fn;
  void *ctx;
} qa_cleanup_entry;

typedef struct qa_cleanup_stack {
  qa_cleanup_entry *entries;
  size_t length;
  size_t capacity;
} qa_cleanup_stack;

void qa_cleanup_stack_init(qa_cleanup_stack *stack);
void qa_cleanup_stack_destroy(qa_cleanup_stack *stack);

/* Returns 0 on success, -1 on allocation failure (no TypeScript counterpart). */
int qa_cleanup_stack_add(qa_cleanup_stack *stack, qa_cleanup_fn fn, void *ctx);
/* Runs callbacks in registration order and empties the stack. */
void qa_cleanup_stack_run_all(qa_cleanup_stack *stack);
/* Runs callbacks in reverse order without emptying the stack. */
void qa_cleanup_stack_invoke_reverse(qa_cleanup_stack *stack);
/* Runs callbacks in reverse registration order and empties the stack. */
void qa_cleanup_stack_run_reverse(qa_cleanup_stack *stack);
void qa_cleanup_stack_clear(qa_cleanup_stack *stack);

#endif
