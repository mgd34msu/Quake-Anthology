/* Port of src/content/q3/presentation/cgame-body-profile.ts. GPL-2.0-or-later.
 * TS is async (mount open + JSON.parse); per PORTING.md the flow is
 * synchronous here. TextDecoder/JSON.parse become qa_parse_json_value
 * (non-quiet: parse failure aborts like the TS throw). */
#ifndef QA_CONTENT_Q3_PRESENTATION_CGAME_BODY_PROFILE_H
#define QA_CONTENT_Q3_PRESENTATION_CGAME_BODY_PROFILE_H
#include <stddef.h>
#include "qa/compat/qvm/cgame_body.h"
#include "qa/compat/qvm/module.h"
#include "qa/content/mounts.h"
typedef enum qa_q3_pres_body_profile_result {
  QA_Q3_PRES_BODY_PROFILE_NONE, /* TS null */
  QA_Q3_PRES_BODY_PROFILE_OK,
  QA_Q3_PRES_BODY_PROFILE_OOM
} qa_q3_pres_body_profile_result;
/** Entries verified from each artifact's CG_AddCEntity dispatch and local
 * refEntity allocations. On OK, *out is a malloc'd array freed with
 * qa_q3_pres_body_profile_free (mesh part rows are included). */
qa_q3_pres_body_profile_result qa_q3_pres_read_cgame_body_profile(const qa_resolved_qvm_artifact *artifact,
                                                                  qa_content_mounted *mounts,
                                                                  qa_qvm_body_submission **out, size_t *out_count);
void qa_q3_pres_body_profile_free(qa_qvm_body_submission *submissions, size_t count);
#endif
