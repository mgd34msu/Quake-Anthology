/*
 * Port of src/bots/behavior/library/bsp-entities.ts (botlib/be_aas_bspq3.c).
 * C accommodations: init selects an owned BotMemory and no print (TS constructor
 * arguments go through qa_aas_bsp_entities_configure). A TS AasBspKey is either
 * a plain string (the const char * functions) or a predicate (qa_aas_bsp_key).
 * destroy releases only C bookkeeping and the owned memory. GPL-2.0-or-later.
 */
#ifndef QA_BOTS_BEHAVIOR_LIBRARY_BSP_ENTITIES_H
#define QA_BOTS_BEHAVIOR_LIBRARY_BSP_ENTITIES_H
#include "qa/bots/behavior/library/memory.h"
#include "qa/contracts/math.h"
#include "qa/persistence/value.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define QA_BSP_MAX_ENTITIES 2048
#define QA_BSP_MAX_EPAIRKEY 128
typedef struct qa_aas_bsp_number_result {
  bool found;
  double value;
} qa_aas_bsp_number_result;
typedef struct qa_aas_bsp_vector_result {
  bool found;
  qa_vec3 value;
} qa_aas_bsp_vector_result;
/** TS AasBspPrint: severity 1|2|3. */
typedef void (*qa_aas_bsp_print_fn)(void *ctx, int severity, const char *text);
/** TS AasBspKey predicate form: strcmp consumes a borrowed key only per compared epair. */
typedef struct qa_aas_bsp_key {
  bool (*matches)(void *ctx, const char *candidate);
  void *ctx;
} qa_aas_bsp_key;
/** TS value() `writes` target: borrowed output written through a host view. */
typedef struct qa_aas_bsp_writes {
  void *ctx;
  void (*set_byte)(void *ctx, int64_t index, uint8_t value);
  void (*clear)(void *ctx, int64_t length);
} qa_aas_bsp_writes;
typedef struct qa_aas_bsp_allocation_cell {
  bool live;
  qa_bot_memory_allocation allocation;
} qa_aas_bsp_allocation_cell;
typedef struct qa_aas_bsp_entities {
  uint32_t pairs[QA_BSP_MAX_ENTITIES];
  int64_t entity_count;
  qa_aas_bsp_allocation_cell *allocations; /* indexed by pointer ID */
  size_t allocation_capacity;
  int64_t next_allocation;
  bool has_entity_text;
  qa_bot_memory_allocation entity_text;
  bool is_loaded;
  qa_aas_bsp_print_fn print;
  void *print_ctx;
  qa_bot_memory owned_memory;
  qa_bot_memory *memory;
} qa_aas_bsp_entities;
void qa_aas_bsp_entities_init(qa_aas_bsp_entities *e);
/* TS constructor(print, memory); memory NULL keeps the owned default. */
void qa_aas_bsp_entities_configure(qa_aas_bsp_entities *e, qa_aas_bsp_print_fn print, void *print_ctx,
                                   qa_bot_memory *memory);
void qa_aas_bsp_entities_destroy(qa_aas_bsp_entities *e);
bool qa_aas_bsp_entities_loaded(const qa_aas_bsp_entities *e);
/* TS load: returns 0. */
int qa_aas_bsp_entities_load(qa_aas_bsp_entities *e, const char *text);
void qa_aas_bsp_entities_dump(qa_aas_bsp_entities *e);
int64_t qa_aas_bsp_entities_next_entity(const qa_aas_bsp_entities *e, int64_t after);
/* TS value(entity, key, output) with size = output length. */
bool qa_aas_bsp_entities_value(qa_aas_bsp_entities *e, int64_t entity, const char *key,
                               uint8_t *output, size_t output_length);
/* Full TS value(): key text or predicate (text NULL), explicit size, optional writes. */
bool qa_aas_bsp_entities_value_ex(qa_aas_bsp_entities *e, int64_t entity, const char *key_text,
                                  const qa_aas_bsp_key *key, uint8_t *output, size_t output_length,
                                  int64_t size, const qa_aas_bsp_writes *writes);
qa_aas_bsp_vector_result qa_aas_bsp_entities_vector(qa_aas_bsp_entities *e, int64_t entity,
                                                    const char *key);
qa_aas_bsp_number_result qa_aas_bsp_entities_float(qa_aas_bsp_entities *e, int64_t entity,
                                                   const char *key);
qa_aas_bsp_number_result qa_aas_bsp_entities_int(qa_aas_bsp_entities *e, int64_t entity,
                                                 const char *key);
qa_aas_bsp_vector_result qa_aas_bsp_entities_vector_key(qa_aas_bsp_entities *e, int64_t entity,
                                                        const qa_aas_bsp_key *key);
qa_aas_bsp_number_result qa_aas_bsp_entities_float_key(qa_aas_bsp_entities *e, int64_t entity,
                                                       const qa_aas_bsp_key *key);
qa_aas_bsp_number_result qa_aas_bsp_entities_int_key(qa_aas_bsp_entities *e, int64_t entity,
                                                     const qa_aas_bsp_key *key);
qa_save_value *qa_aas_bsp_entities_checkpoint(qa_aas_bsp_entities *e, qa_bot_memory_capture *memory);
void qa_aas_bsp_entities_restore(qa_aas_bsp_entities *e, const qa_save_value *value,
                                 qa_bot_memory_restored *memory);
#endif
