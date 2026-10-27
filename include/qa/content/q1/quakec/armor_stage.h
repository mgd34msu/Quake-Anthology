/* Port of src/content/q1/quakec/armor-stage.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_QUAKEC_ARMOR_STAGE_H
#define QA_CONTENT_Q1_QUAKEC_ARMOR_STAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/q1/quakec/compat_types.h"
#include "qa/contracts/mod_callbacks.h"

/* TS qcStatementAccess result. Buffers cover the widest access
 * (Call8 reads: callee word + 8*3 staging words). */
typedef struct qa_q1qc_statement_access {
  int64_t read[32]; size_t read_count;
  int64_t write[8]; size_t write_count;
} qa_q1qc_statement_access;
qa_q1qc_statement_access qa_q1qc_statement_access_of(qa_q1qc_statement statement);
bool qa_q1qc_access_has(const int64_t *words, size_t count, int64_t word);

/* TS qcRegionPrivateWritesAreDead. */
bool qa_q1qc_region_private_writes_are_dead(const qa_q1qc_program *program,
                                            int64_t start, int64_t end,
                                            const int64_t *writes, size_t write_count);

/* TS QcArmorStage (ModQcArmorStage + region). Heap owned; the
 * embedded source's statement/scale arrays point at owned copies. */
typedef struct qa_q1qc_armor_stage {
  qa_mod_qc_armor_stage source;
  qa_q1qc_inline_region region;
  qa_qc_statement *statements_copy;
  qa_mod_qc_armor_regular_scale *scales_copy;
} qa_q1qc_armor_stage;
/* declared NULL = TS undefined. NULL return = TS null. */
qa_q1qc_armor_stage *qa_q1qc_armor_stage_create(const qa_q1qc_program *program,
                                                const qa_mod_qc_armor_stage *declared);
void qa_q1qc_armor_stage_destroy(qa_q1qc_armor_stage *stage);

#endif
