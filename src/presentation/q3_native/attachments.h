#ifndef QA_Q3_NATIVE_ATTACHMENTS_H
#define QA_Q3_NATIVE_ATTACHMENTS_H

#include "qa/q3_presentation.h"

typedef enum q3n_attachment_rule {
    Q3N_ATTACH_INHERIT,
    Q3N_ATTACH_ROTATED,
    Q3N_ATTACH_CHILD_POSE
} q3n_attachment_rule;
typedef enum q3n_attachment_missing {
    Q3N_ATTACH_MISSING_IDENTITY,
    Q3N_ATTACH_MISSING_SKIP,
    Q3N_ATTACH_MISSING_REQUIRED
} q3n_attachment_missing;
/* INHERIT and ROTATED add offsets to parent origin in axis order. INHERIT
 * uses tag * parent and copies back_lerp; ROTATED uses (child * tag) * parent.
 * CHILD_POSE uses offset-first origin, child * (tag * parent), old_origin and
 * parent lighting_origin. Missing SKIP/REQUIRED leaves the child unchanged. */
bool q3n_attach(const qa_q3_presentation_assets *, qa_q3_ref_entity *child,
    const qa_q3_ref_entity *parent, qa_string_id tag, q3n_attachment_rule,
    q3n_attachment_missing, bool *found, qa_error *);

#endif
