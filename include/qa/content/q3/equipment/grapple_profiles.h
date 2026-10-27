/* Port of src/content/q3/equipment/grapple-profiles.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_EQUIPMENT_GRAPPLE_PROFILES_H
#define QA_CONTENT_Q3_EQUIPMENT_GRAPPLE_PROFILES_H
#include <stdbool.h>
#include "qa/content/q3/equipment/lrctf_grapple_profile.h"
bool qa_q3_grapple_profile(const qa_q3_equipment_qvm_artifact *artifact,
    struct qa_qvm_grapple_definition *out);
#endif
