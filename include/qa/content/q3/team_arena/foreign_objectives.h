/* Port of src/content/q3/team-arena/foreign-objectives.ts. GPL-2.0-or-later.
 * Ready result's entities string is caller-owned (free with the destroy
 * helper); non-ready kinds mirror objective-placement results. JS number
 * formatting in explicit origins: integral values as integer text. */
#ifndef QA_CONTENT_Q3_TEAM_ARENA_FOREIGN_OBJECTIVES_H
#define QA_CONTENT_Q3_TEAM_ARENA_FOREIGN_OBJECTIVES_H
#include <stddef.h>
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/team_arena/objective_placement.h"
#include "qa/contracts/math.h"

typedef enum qa_q3_ta_world_kind { QA_Q3_TA_WORLD_Q1_BSP, QA_Q3_TA_WORLD_Q2_BSP, QA_Q3_TA_WORLD_Q3_BSP } qa_q3_ta_world_kind;
typedef struct qa_q3_ta_explicit_objective_placement {
  const char *classname; /* an ObjectiveClassname string */
  qa_vec3 origin;
} qa_q3_ta_explicit_objective_placement;
typedef struct qa_q3_ta_foreign_objective_adaptation {
  qa_q3_ta_objective_result_kind kind;
  char *entities;      /* ready only, owned */
  double translated;   /* ready only */
  const char *missing[3]; size_t missing_count; /* missing only */
  qa_q3_base_product product; double game_type; /* unsupported only */
} qa_q3_ta_foreign_objective_adaptation;
void qa_q3_ta_foreign_objective_adaptation_destroy(qa_q3_ta_foreign_objective_adaptation *result);
/* explicit may be NULL with count 0 (TS default []). */
qa_q3_ta_foreign_objective_adaptation qa_q3_ta_adapt_foreign_q3_objectives(
    qa_q3_ta_world_kind world_kind, const char *entities,
    qa_q3_base_product product, double game_type,
    const qa_q3_ta_explicit_objective_placement *explicit_placements, size_t explicit_count);
#endif
