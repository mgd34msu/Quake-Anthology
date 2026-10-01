#ifndef QA_Q3_NATIVE_ATTACHMENTS_H
#define QA_Q3_NATIVE_ATTACHMENTS_H

#include "qa/q3_presentation.h"

/* Parent and child retain actual registered handles. Missing tags use the
 * renderer's genuine cleared orientation; attachment never registers media. */
bool q3n_attach(qa_q3_presentation_assets *, qa_q3_ref_entity *child,
    const qa_q3_ref_entity *parent, const char *tag, bool rotated, qa_error *);

#endif
