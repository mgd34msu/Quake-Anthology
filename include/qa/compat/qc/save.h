/* Port of src/compat/qc/save.ts. GPL-2.0-or-later.
 * Pair lists returned are malloc'd (keys/values strdup'd); free with
 * qa_compat_qc_text_pairs_free. */
#ifndef QA_COMPAT_QC_SAVE_H
#define QA_COMPAT_QC_SAVE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/qc/machine.h"
typedef struct qa_compat_qc_text_pair { char *key, *value; } qa_compat_qc_text_pair;
typedef struct qa_compat_qc_text_pairs { qa_compat_qc_text_pair *items; size_t count; } qa_compat_qc_text_pairs;
void qa_compat_qc_text_pairs_free(qa_compat_qc_text_pairs *p);
typedef struct qa_compat_qc_entity_parse_result_compat { bool empty; qa_compat_qc_text_pairs unknown, retained; } qa_compat_qc_entity_parse_result_compat;
void qa_compat_qc_parse_value(qa_compat_qc_machine *m, qa_compat_qc_words *words, const qa_compat_qc_definition *definition, const char *text);
bool qa_compat_qc_apply_entity_pairs(qa_compat_qc_machine *m, int64_t slot, const qa_compat_qc_text_pair *pairs, size_t count, qa_compat_qc_entity_parse_result_compat *out);
bool qa_compat_qc_apply_global_pairs(qa_compat_qc_machine *m, const qa_compat_qc_text_pair *pairs, size_t count, qa_compat_qc_text_pairs *out_unknown);
bool qa_compat_qc_save_global_pairs(qa_compat_qc_machine *m, qa_compat_qc_text_pairs *out);
bool qa_compat_qc_save_entity_pairs(qa_compat_qc_machine *m, int64_t slot, bool is_free, qa_compat_qc_text_pairs *out);
#endif
