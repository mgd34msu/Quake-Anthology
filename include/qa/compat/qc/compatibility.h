/* Port of src/compat/qc/compatibility.ts. GPL-2.0-or-later.
 * The parsed JSON tree is owned by the result: the declaration readers
 * return strings borrowed from it, so destroy frees it last. Lists are
 * malloc'd. */
#ifndef QA_COMPAT_QC_COMPATIBILITY_H
#define QA_COMPAT_QC_COMPATIBILITY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/content/q1/quakec/pickup_callers.h"
#include "qa/content/q1/quakec/weapon_stage_declaration.h"
#include "qa/contracts/mod_callbacks.h"
#include "qa/contracts/source_match.h"
#include "qa/network/q1/profile.h"
typedef struct qa_compat_qc_quakec_compatibility {
  bool has_teams; qa_source_team_alias *teams; size_t team_count;
  bool has_combat; qa_mod_qc_combat *combat;
  bool has_weapon_stage; qa_qc_primary_weapon_stage_declaration weapon_stage;
  const char *message_dialect; /* RereleaseMessages */
  qa_qc_pickup_caller_declaration *pickup_callers; size_t pickup_caller_count;
  qa_save_value *tree; /* owned; NULL for TS null bytes */
} qa_compat_qc_quakec_compatibility;
/* bytes NULL = TS null. False only on OOM. */
bool qa_compat_qc_read_quakec_compatibility(const uint8_t *bytes, size_t length, const char *artifact_digest, qa_compat_qc_quakec_compatibility *out);
void qa_compat_qc_quakec_compatibility_destroy(qa_compat_qc_quakec_compatibility *c);
#endif
