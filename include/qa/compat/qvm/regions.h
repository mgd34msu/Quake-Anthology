/*
 * Port of src/compat/qvm/regions.ts. Instructions are the landed
 * qa_qvm_instruction (TS owner src/compat/qvm/image.ts, image.h);
 * the instruction list is QvmImage["instructions"] as pointer +
 * count. The TS access union "source" | "read-only" keeps its
 * exact strings.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_REGIONS_H
#define QA_COMPAT_QVM_REGIONS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/image.h"

/* TS QvmRegionEvaluation. has_result false is TS result === null. */
typedef struct qa_qvm_region_evaluation {
  int64_t entry;
  int64_t join;
  const int64_t *inputs;
  size_t input_count;
  bool has_result;
  int64_t result;
} qa_qvm_region_evaluation;

int64_t qa_qvm_qualify_region_evaluation(const qa_qvm_instruction *instructions, size_t count,
                                         int64_t owner, const qa_qvm_region_evaluation *region,
                                         const char *access);
int64_t qa_qvm_qualify_region(const qa_qvm_instruction *instructions, size_t count,
                              int64_t owner, int64_t entry, int64_t join);

#endif
