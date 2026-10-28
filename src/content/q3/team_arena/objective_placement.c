/* Port of src/content/q3/team-arena/objective-placement.ts. GPL-2.0-or-later. */
#include "qa/content/q3/team_arena/objective_placement.h"
#include <string.h>

qa_q3_ta_objective_placement_result qa_q3_ta_check_objective_placements(
    qa_q3_base_product product, double game_type,
    const char *const *classnames, size_t count,
    const char **missing_buffer) {
  qa_q3_ta_objective_placement_result out = {QA_Q3_TA_OBJECTIVE_READY, NULL, 0, product, game_type};
  const char *required[3];
  size_t required_count = 0;
  if (game_type == QA_Q3_BASE_GT_FFA || game_type == QA_Q3_BASE_GT_TOURNAMENT
      || game_type == QA_Q3_BASE_GT_SINGLE_PLAYER || game_type == QA_Q3_BASE_GT_TEAM) {
    required_count = 0;
  } else if (game_type == QA_Q3_BASE_GT_CTF) {
    required[0] = "team_CTF_redflag"; required[1] = "team_CTF_blueflag"; required_count = 2;
  } else if (game_type == QA_Q3_BASE_GT_1FCTF) {
    if (product != QA_Q3_BASE_PRODUCT_MISSIONPACK) { out.kind = QA_Q3_TA_OBJECTIVE_UNSUPPORTED; return out; }
    required[0] = "team_CTF_redflag"; required[1] = "team_CTF_blueflag"; required[2] = "team_CTF_neutralflag";
    required_count = 3;
  } else if (game_type == QA_Q3_BASE_GT_OBELISK) {
    if (product != QA_Q3_BASE_PRODUCT_MISSIONPACK) { out.kind = QA_Q3_TA_OBJECTIVE_UNSUPPORTED; return out; }
    required[0] = "team_redobelisk"; required[1] = "team_blueobelisk"; required_count = 2;
  } else if (game_type == QA_Q3_BASE_GT_HARVESTER) {
    if (product != QA_Q3_BASE_PRODUCT_MISSIONPACK) { out.kind = QA_Q3_TA_OBJECTIVE_UNSUPPORTED; return out; }
    required[0] = "team_redobelisk"; required[1] = "team_blueobelisk"; required[2] = "team_neutralobelisk";
    required_count = 3;
  } else {
    out.kind = QA_Q3_TA_OBJECTIVE_UNSUPPORTED;
    return out;
  }
  size_t missing = 0;
  for (size_t i = 0; i < required_count; i++) {
    bool present = false;
    for (size_t j = 0; j < count; j++)
      if (classnames[j] && strcmp(classnames[j], required[i]) == 0) present = true;
    if (!present) missing_buffer[missing++] = required[i];
  }
  if (missing == 0) return out;
  out.kind = QA_Q3_TA_OBJECTIVE_MISSING;
  out.classnames = missing_buffer;
  out.classname_count = missing;
  return out;
}
