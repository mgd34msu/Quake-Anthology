/* Port of src/bots/behavior/library/genetic.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_LIBRARY_GENETIC_H
#define QA_BOTS_BEHAVIOR_LIBRARY_GENETIC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* TS BotRandom (owner: library/weights.ts) mirrored as fn+ctx. */
typedef struct qa_bot_random_bots_a {
  int64_t (*next_int)(void *ctx);
  void *ctx;
  double (*next_unit)(void *ctx);
  void *unit_ctx;
} qa_bot_random_bots_a;
typedef enum qa_genetic_result_kind {
  QA_GENETIC_SELECTED,
  QA_GENETIC_ERROR
} qa_genetic_result_kind;
typedef enum qa_genetic_error {
  QA_GENETIC_NONE,
  QA_GENETIC_TOO_MANY_RANKS,
  QA_GENETIC_TOO_FEW_VALID_RANKS,
  QA_GENETIC_UNDEFINED_RANDOM_INDEX
} qa_genetic_error;
typedef struct qa_genetic_selection_result {
  qa_genetic_result_kind kind;
  qa_genetic_error reason;
  int64_t parent1;
  int64_t parent2;
  int64_t child;
} qa_genetic_selection_result;
typedef void (*qa_genetic_write_fn)(void *ctx, const char *target, int64_t value);
typedef void (*qa_genetic_warning_fn)(void *ctx, const char *text);
/* ranks: count values; invalid count aborts with TS RangeError message. */
qa_genetic_selection_result qa_genetic_parents_and_child_selection(
    const double *ranks, size_t count, qa_bot_random_bots_a random, qa_genetic_warning_fn warning,
    void *warning_ctx, qa_genetic_write_fn write, void *write_ctx);
#endif
