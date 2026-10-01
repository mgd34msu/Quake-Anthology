#ifndef QA_Q3_ASSET_SHADER_H
#define QA_Q3_ASSET_SHADER_H

#include "qa/q3_presentation.h"

/* Borrow the actual registered material. Handle zero has no override. The
 * registry and its selected material owners outlive the returned borrow. */
bool qa_q3_assets_shader_read(const qa_q3_presentation_assets *, int32_t handle,
    const qa_material **, qa_error *);

#endif
