/* Port of src/compat/qc/mod-objectives.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/mod_objectives.h"
#include <string.h>
#include "qa/foundation/support.h"
void qa_compat_qc_validate_objective_storage(const qa_compat_qc_program *p, qa_qc_mod_objective_storage s, const char *type) {
  if (!s.is_entity_field) {
    const qa_compat_qc_definition *g = qa_compat_qc_program_global_named(p, s.name);
    if (!g || strcmp(g->type, type)) qa_range_error("Objective global %s requires original %s storage", s.name, type);
    return;
  }
  const qa_compat_qc_definition *g = qa_compat_qc_program_global_named(p, s.global);
  if (!g || strcmp(g->type, "entity")) qa_range_error("Objective field requires an original global entity reference");
  for (size_t i = 0; i < s.indirections.count; i++) { const qa_compat_qc_definition *f = qa_compat_qc_program_field_named(p, s.indirections.values[i]);
    if (!f || strcmp(f->type, "entity")) qa_range_error("Objective selector %s requires an original entity field", s.indirections.values[i]); }
  const qa_compat_qc_definition *f = qa_compat_qc_program_field_named(p, s.field);
  if (!f || strcmp(f->type, type)) qa_range_error("Objective field %s requires original %s storage", s.field, type);
}
qa_compat_qc_objective_resolved qa_compat_qc_resolve_objective_storage(qa_compat_qc_machine *m, qa_qc_mod_objective_storage s, qa_compat_qc_objective_current cur) {
  if (cur.fn) cur.fn(cur.ctx, false, 0);
  if (!s.is_entity_field) return (qa_compat_qc_objective_resolved){&m->globals, qa_compat_qc_machine_global_offset(m, s.name), false, 0};
  int64_t ref = qa_compat_qc_words_int(&m->globals, qa_compat_qc_machine_global_offset(m, s.global));
  for (size_t i = 0; i < s.indirections.count; i++) {
    if (cur.fn) cur.fn(cur.ctx, true, ref);
    const qa_compat_qc_words * w = qa_compat_qc_entity_memory_from_reference(m->entities, ref);
    int64_t word = qa_compat_qc_machine_field_offset(m, s.indirections.values[i]);
    ref = qa_compat_qc_words_int(w, word);
  }
  if (cur.fn) cur.fn(cur.ctx, true, ref);
  qa_compat_qc_words * words = qa_compat_qc_entity_memory_from_reference(m->entities, ref);
  int64_t offset = qa_compat_qc_machine_field_offset(m, s.field);
  return (qa_compat_qc_objective_resolved){words, offset, true, ref};
}
