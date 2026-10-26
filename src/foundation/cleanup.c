/* Port of src/foundation/cleanup.ts. GPL-2.0-or-later. */
#include "qa/foundation/cleanup.h"

#include <stdlib.h>

void qa_cleanup_stack_init(qa_cleanup_stack *stack) {
  stack->entries = NULL;
  stack->length = 0;
  stack->capacity = 0;
}

void qa_cleanup_stack_destroy(qa_cleanup_stack *stack) {
  free(stack->entries);
  stack->entries = NULL;
  stack->length = 0;
  stack->capacity = 0;
}

int qa_cleanup_stack_add(qa_cleanup_stack *stack, qa_cleanup_fn fn, void *ctx) {
  if (stack->length == stack->capacity) {
    size_t grown = stack->capacity == 0 ? 8 : stack->capacity * 2;
    qa_cleanup_entry *entries =
        (qa_cleanup_entry *)realloc(stack->entries, grown * sizeof *entries);
    if (entries == NULL) return -1;
    stack->entries = entries;
    stack->capacity = grown;
  }
  stack->entries[stack->length].fn = fn;
  stack->entries[stack->length].ctx = ctx;
  stack->length++;
  return 0;
}

void qa_cleanup_stack_run_all(qa_cleanup_stack *stack) {
  qa_cleanup_entry *entries = stack->entries;
  size_t length = stack->length;
  stack->entries = NULL;
  stack->length = 0;
  stack->capacity = 0;
  for (size_t index = 0; index < length; index++) {
    if (entries[index].fn != NULL) entries[index].fn(entries[index].ctx);
  }
  free(entries);
}

void qa_cleanup_stack_invoke_reverse(qa_cleanup_stack *stack) {
  for (size_t index = stack->length; index-- > 0;) {
    if (stack->entries[index].fn != NULL) {
      stack->entries[index].fn(stack->entries[index].ctx);
    }
  }
}

void qa_cleanup_stack_run_reverse(qa_cleanup_stack *stack) {
  qa_cleanup_entry *entries = stack->entries;
  size_t length = stack->length;
  stack->entries = NULL;
  stack->length = 0;
  stack->capacity = 0;
  for (size_t index = length; index-- > 0;) {
    if (entries[index].fn != NULL) entries[index].fn(entries[index].ctx);
  }
  free(entries);
}

void qa_cleanup_stack_clear(qa_cleanup_stack *stack) { stack->length = 0; }
