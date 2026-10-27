/* Port of src/content/q2/missionpacks/monsters/xatrix-variants.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_XATRIX_VARIANTS_H
#define QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_XATRIX_VARIANTS_H

#include <stddef.h>

#include "qa/content/q2/foundation/monsters/index.h"

extern const qa_vec3 qa_q2mpm_xatrix_brain_right_eye[11];
extern const qa_vec3 qa_q2mpm_xatrix_brain_left_eye[11];
const qa_q2m_definition *qa_q2mpm_get_xatrix_brain_definition(void);
const qa_q2m_definition *qa_q2mpm_get_xatrix_infantry_definition(void);
/* Writes 3 definitions (brain, infantry, supertank); returns 3. */
size_t qa_q2mpm_create_xatrix_base_variants(qa_q2m_definition *out, qa_q2m_monsters *monsters);

#endif
