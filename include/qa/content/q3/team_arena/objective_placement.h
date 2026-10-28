/* Port of src/content/q3/team-arena/objective-placement.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_TEAM_ARENA_OBJECTIVE_PLACEMENT_H
#define QA_CONTENT_Q3_TEAM_ARENA_OBJECTIVE_PLACEMENT_H
#include <stddef.h>
#include "qa/content/q3/base/shared/definitions.h"

/* TS ObjectiveClassname strings stay const char* rows. */
typedef enum qa_q3_ta_objective_result_kind {
  QA_Q3_TA_OBJECTIVE_READY, QA_Q3_TA_OBJECTIVE_MISSING, QA_Q3_TA_OBJECTIVE_UNSUPPORTED
} qa_q3_ta_objective_result_kind;
typedef struct qa_q3_ta_objective_placement_result {
  qa_q3_ta_objective_result_kind kind;
  const char *const *classnames; /* missing only; static rows */
  size_t classname_count;
  qa_q3_base_product product; /* unsupported only */
  double game_type;            /* unsupported only */
} qa_q3_ta_objective_placement_result;
/* placements are classname strings; NULL entry is TS null classname.
 * missing rows point into a caller buffer of 3 pointers. */
qa_q3_ta_objective_placement_result qa_q3_ta_check_objective_placements(
    qa_q3_base_product product, double game_type,
    const char *const *classnames, size_t count,
    const char **missing_buffer);
#endif
