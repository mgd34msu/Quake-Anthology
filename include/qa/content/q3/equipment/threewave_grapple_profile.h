/* Port of src/content/q3/equipment/threewave-grapple-profile.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_EQUIPMENT_THREEWAVE_GRAPPLE_PROFILE_H
#define QA_CONTENT_Q3_EQUIPMENT_THREEWAVE_GRAPPLE_PROFILE_H
#include <stdbool.h>
#include "qa/content/q3/equipment/lrctf_grapple_profile.h"
extern const char qa_threewave_grapple_digest[];
bool qa_threewave_grapple_profile(const qa_q3_equipment_qvm_artifact *artifact,
    struct qa_qvm_grapple_definition *out);
#endif
