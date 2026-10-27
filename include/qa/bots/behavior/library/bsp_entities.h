/* Port of src/bots/behavior/library/bsp-entities.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_LIBRARY_BSP_ENTITIES_H
#define QA_BOTS_BEHAVIOR_LIBRARY_BSP_ENTITIES_H
#include "qa/contracts/math.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define QA_BSP_MAX_ENTITIES 2048
typedef struct qa_aas_bsp_number_result {
  bool found;
  double value;
} qa_aas_bsp_number_result;
typedef struct qa_aas_bsp_vector_result {
  bool found;
  qa_vec3 value;
} qa_aas_bsp_vector_result;
typedef struct qa_aas_bsp_entities {
  char *text;
  size_t length;
  int64_t entity_count;
  void (*print)(void *ctx, int severity, const char *text);
  void *print_ctx;
} qa_aas_bsp_entities;
void qa_aas_bsp_entities_init(qa_aas_bsp_entities *e);
void qa_aas_bsp_entities_destroy(qa_aas_bsp_entities *e);
bool qa_aas_bsp_entities_load(qa_aas_bsp_entities *e, const char *text);
const char *qa_aas_bsp_entities_get_string(qa_aas_bsp_entities *e, int64_t entity, const char *key,
                                           char *buf, size_t buf_len);
qa_aas_bsp_number_result qa_aas_bsp_entities_get_float(qa_aas_bsp_entities *e, int64_t entity,
                                                       const char *key);
qa_aas_bsp_number_result qa_aas_bsp_entities_get_int(qa_aas_bsp_entities *e, int64_t entity,
                                                     const char *key);
qa_aas_bsp_vector_result qa_aas_bsp_entities_get_vector(qa_aas_bsp_entities *e, int64_t entity,
                                                        const char *key);
int64_t qa_aas_bsp_entities_get_count(const qa_aas_bsp_entities *e);
#endif
