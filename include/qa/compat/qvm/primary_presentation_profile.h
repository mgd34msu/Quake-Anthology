/*
 * Port of src/compat/qvm/primary-presentation-profile.ts. The
 * compatibility declaration selection is the landed
 * qa_qvm_read_compatibility_declaration (compatibility.h). The
 * returned profile is the landed
 * content struct; when *out_owned is true its status arrays are
 * malloc'd and released by qa_qvm_equipment_presentation_free.
 * False is TS null. Async flattened to synchronous.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_PRIMARY_PRESENTATION_PROFILE_H
#define QA_COMPAT_QVM_PRIMARY_PRESENTATION_PROFILE_H

#include <stdbool.h>

#include "qa/compat/qvm/module.h"
#include "qa/content/mounts.h"
#include "qa/content/q3/equipment/cgame_weapon_hud.h"

bool qa_qvm_read_equipment_presentation(const qa_qvmy_artifact *artifact, qa_content_mounted *mounts,
                                        qa_q3_equipment_qvm_presentation_profile *out, bool *out_owned);
void qa_qvm_equipment_presentation_free(qa_q3_equipment_qvm_presentation_profile *profile);

#endif
