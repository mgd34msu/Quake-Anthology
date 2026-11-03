/*
 * Port of src/bots/behavior/library/weights.ts (botlib/be_ai_weight.c).
 * C accommodations:
 *  - WeightConfigStore: init selects defaults (owned BotMemory); the TS
 *    constructor arguments go through qa_weight_config_store_bots_a_configure,
 *    which keeps the resolver and options by value. The struct address must stay
 *    stable while a load runs. destroy releases C bookkeeping and the owned
 *    memory only; BotMemory owns the configuration bytes.
 *  - load's TS WeightConfigError/WeightConfigLoadError become a NULL return with
 *    qa_weight_config_error filled. A NULL error pointer means nothing catches
 *    the throw: the message is printed and the process aborts, as TS propagation
 *    to the top-level crash handler does.
 *  - Config structs stay allocated until destroy so a freed config can still
 *    answer requireOpen with the TS error.
 * GPL-2.0-or-later.
 */
#ifndef QA_BOTS_BEHAVIOR_LIBRARY_WEIGHTS_H
#define QA_BOTS_BEHAVIOR_LIBRARY_WEIGHTS_H
#include "qa/bots/behavior/library/memory.h"
#include "qa/bots/behavior/library/script_sources.h"
#include "qa/persistence/value.h"
#include "qa/ui/common/legacy/script/preprocessor.h"
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
/* TS sourceUnitRandom: botlib's random(), the low 15 bits of rand() over 0x7fff. */
float qa_bot_source_unit_random(qa_bot_random random);
/** TS WeightInventory: values[count], or read when values is NULL. */
typedef struct qa_weight_inventory {
  const double *values;
  size_t count;
  double (*read)(void *ctx, int64_t index);
  void *ctx;
} qa_weight_inventory;
/** TS readonly string[] result; release with qa_string_list_destroy. */
typedef struct qa_string_list {
  char **items;
  size_t count;
} qa_string_list;
void qa_string_list_destroy(qa_string_list *list);
typedef struct qa_weight_config_store_bots_a qa_weight_config_store_bots_a;
typedef struct qa_weight_config_bots_a qa_weight_config_bots_a;
typedef enum qa_weight_config_error_kind {
  QA_WEIGHT_CONFIG_ERROR_NONE,
  QA_WEIGHT_CONFIG_ERROR_PARSE, /* TS WeightConfigError */
  QA_WEIGHT_CONFIG_ERROR_LOAD   /* TS WeightConfigLoadError */
} qa_weight_config_error_kind;
typedef struct qa_weight_config_error {
  qa_weight_config_error_kind kind;
  char *message; /* owned; release with qa_weight_config_error_destroy */
} qa_weight_config_error;
void qa_weight_config_error_destroy(qa_weight_config_error *error);
/** TS WeightConfigStoreOptions; zero/NULL is the TS undefined field. */
typedef struct qa_weight_config_store_options {
  double (*debug_milliseconds)(void *ctx);
  bool (*debug_developer)(void *ctx);
  void *debug_ctx;
  qa_bot_memory *memory;
  bool reload_characters;
  bool (*reload_characters_fn)(void *ctx);
  void *reload_ctx;
  void (*print)(void *ctx, int severity, const char *text);
  void *print_ctx;
  bool has_max_cached_configs;
  double max_cached_configs;
  const qa_ui_script_preprocessor_options *preprocessor;
} qa_weight_config_store_options;
typedef struct qa_weight_heap_entry {
  uint8_t kind; /* 0 absent, 1 name, 2 separator */
  qa_bot_memory_allocation allocation;
} qa_weight_heap_entry;
struct qa_weight_config_store_bots_a {
  qa_bot_memory owned_memory;
  qa_bot_memory *memory;
  qa_bot_script_reader resolver;
  bool has_resolver;
  qa_weight_config_store_options options;
  qa_ui_script_preprocessor_options preprocessor;
  int64_t max_cached_configs;
  qa_weight_heap_entry *heap;
  size_t heap_capacity;
  int64_t next_pointer;
  qa_weight_config_bots_a *cached[QA_MAX_CACHED_WEIGHT_CONFIGS];
  size_t cached_length;
  qa_weight_config_bots_a **owned; /* TS Set in insertion order */
  size_t owned_count, owned_capacity;
  qa_weight_config_bots_a **created;
  size_t created_count, created_capacity;
  int64_t generation;
};
void qa_weight_config_store_bots_a_init(qa_weight_config_store_bots_a *s);
void qa_weight_config_store_bots_a_configure(qa_weight_config_store_bots_a *s,
                                             const qa_bot_script_reader *resolver,
                                             const qa_weight_config_store_options *options);
void qa_weight_config_store_bots_a_destroy(qa_weight_config_store_bots_a *s);
qa_weight_config_bots_a *qa_weight_config_store_load(qa_weight_config_store_bots_a *s,
                                                     const char *path,
                                                     qa_weight_config_error *error);
void qa_weight_config_store_free(qa_weight_config_store_bots_a *s, qa_weight_config_bots_a *config);
void qa_weight_config_store_shutdown(qa_weight_config_store_bots_a *s);
qa_save_value *qa_weight_config_store_checkpoint(qa_weight_config_store_bots_a *s,
                                                 qa_bot_memory_capture *memory);
void qa_weight_config_store_restore(qa_weight_config_store_bots_a *s, const qa_save_value *value,
                                    qa_bot_memory_restored *memory);
int64_t qa_weight_config_store_reference(qa_weight_config_store_bots_a *s,
                                         const qa_weight_config_bots_a *config);
qa_weight_config_bots_a *qa_weight_config_store_resolve(qa_weight_config_store_bots_a *s,
                                                        double reference);
/* Borrowed TS configurations(): the store's owned list. */
qa_weight_config_bots_a *const *qa_weight_config_store_configurations(
    const qa_weight_config_store_bots_a *s, size_t *out_count);

const char *qa_weight_config_path(const qa_weight_config_bots_a *c);
int64_t qa_weight_config_weight_count(const qa_weight_config_bots_a *c);
const char *qa_weight_config_name(const qa_weight_config_bots_a *c, int64_t index);
int64_t qa_weight_config_max_inventory_index(const qa_weight_config_bots_a *c);
const qa_ui_script_diagnostic *qa_weight_config_diagnostics(const qa_weight_config_bots_a *c,
                                                            size_t *out_count);
int64_t qa_weight_config_find(const qa_weight_config_bots_a *c, const char *name);
float qa_weight_config_evaluate(const qa_weight_config_bots_a *c, int64_t weight_index,
                                qa_weight_inventory inventory);
float qa_weight_config_evaluate_undecided(const qa_weight_config_bots_a *c, int64_t weight_index,
                                          qa_weight_inventory inventory, qa_bot_random random);
void qa_weight_config_scale_weight(qa_weight_config_bots_a *c, const char *name, double scale);
void qa_weight_config_scale_balance_range(qa_weight_config_bots_a *c, double scale);
void qa_weight_config_evolve(qa_weight_config_bots_a *c, qa_bot_random random);
/** Mutates this output in source order and does not roll back earlier changes on an error.
 * report may be NULL; the returned list holds every error message. */
qa_string_list qa_weight_config_interbreed_from(qa_weight_config_bots_a *c,
                                                const qa_weight_config_bots_a *parent1,
                                                const qa_weight_config_bots_a *parent2,
                                                void (*report)(void *ctx, const char *message),
                                                void *report_ctx);
#endif
