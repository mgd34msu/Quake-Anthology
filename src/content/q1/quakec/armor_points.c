/* Port of src/content/q1/quakec/armor-points.ts. GPL-2.0-or-later. */
#include "qa/content/q1/quakec/armor_points.h"

#include <math.h>
#include <string.h>

#include "qa/content/q1/quakec/id1_program.h"
#include "qa/foundation/support.h"

qa_q1qc_empty_armor qa_q1qc_empty_armor_for(const qa_q1qc_program *program,
                                            const qa_mod_qc_empty_armor *declared) {
  qa_mod_qc_empty_armor pinned = {"q1:item_armorInv", 0.8};
  const qa_mod_qc_empty_armor *source = declared;
  if (source == NULL &&
      strcmp(qa_q1_id1_program_binding_get(program, NULL)->attribution, "pinned") == 0)
    source = &pinned;
  qa_q1qc_empty_armor none = {false, NULL, 0};
  if (source == NULL) return none;
  bool authored = source->item != NULL &&
                  (strcmp(source->item, "q1:item_armor1") == 0 ||
                   strcmp(source->item, "q1:item_armor2") == 0 ||
                   strcmp(source->item, "q1:item_armorInv") == 0);
  if (!authored || !isfinite((double)(float)source->absorption) || source->absorption < 0)
    qa_range_error("QC points-only armor requires an authored item and finite nonnegative absorption");
  qa_q1qc_empty_armor result = {true, source->item, (double)(float)source->absorption};
  return result;
}
qa_regular_armor_state qa_q1qc_empty_armor_apply(qa_q1qc_empty_armor armor, double points) {
  qa_regular_armor_state result = {0};
  result.kind = QA_REGULAR_ARMOR_Q1;
  result.item = armor.item;
  result.absorption = armor.absorption;
  result.points = points;
  return result;
}
