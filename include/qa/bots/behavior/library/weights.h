/* Port of src/bots/behavior/library/weights.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_LIBRARY_WEIGHTS_H
#define QA_BOTS_BEHAVIOR_LIBRARY_WEIGHTS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define QA_MAX_FUZZY_WEIGHTS 128
#define QA_MAX_CACHED_WEIGHT_CONFIGS 128
#define QA_MAX_INVENTORY_VALUE_BOTS_A 999999
/* TS BotRandom canonical for bots slice. */
typedef struct qa_bot_random {
  int64_t (*next_int)(void *ctx);
  void *ctx;
} qa_bot_random;
typedef struct qa_fuzzy_separator_bots_a {
  int64_t inventory_index, threshold;
  bool balanced;
  float weight, min_weight, max_weight;
  struct qa_fuzzy_separator_bots_a *child, *next;
} qa_fuzzy_separator_bots_a;
typedef struct qa_weight_config_bots_a {
  char path[256];
  char *names[QA_MAX_FUZZY_WEIGHTS];
  qa_fuzzy_separator_bots_a *roots[QA_MAX_FUZZY_WEIGHTS];
  int64_t weight_count, max_inventory_index;
} qa_weight_config_bots_a;
typedef struct qa_weight_config_store_bots_a {
  qa_weight_config_bots_a *configs[QA_MAX_CACHED_WEIGHT_CONFIGS];
  size_t count;
  int64_t max_cached;
} qa_weight_config_store_bots_a;
void qa_weight_config_store_bots_a_init(qa_weight_config_store_bots_a *s);
void qa_weight_config_store_bots_a_destroy(qa_weight_config_store_bots_a *s);
void qa_weight_config_bots_a_init(qa_weight_config_bots_a *c, const char *path);
int64_t qa_weight_config_find(const qa_weight_config_bots_a *c, const char *name);
double qa_weight_config_evaluate(const qa_weight_config_bots_a *c, int64_t weight_index,
                                 const double *inventory, size_t inv_count);
double qa_weight_config_evaluate_undecided(const qa_weight_config_bots_a *c, int64_t weight_index,
                                           const double *inventory, size_t inv_count,
                                           qa_bot_random random);
void qa_weight_config_scale_weight(qa_weight_config_bots_a *c, const char *name, double scale);
void qa_weight_config_scale_balance_range(qa_weight_config_bots_a *c, double scale);
qa_weight_config_bots_a *qa_weight_config_store_find(qa_weight_config_store_bots_a *s,
                                                     const char *path);
bool qa_weight_config_store_add(qa_weight_config_store_bots_a *s, qa_weight_config_bots_a *c);
#endif
