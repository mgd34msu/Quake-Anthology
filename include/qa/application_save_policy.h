#ifndef QA_APPLICATION_SAVE_POLICY_H
#define QA_APPLICATION_SAVE_POLICY_H
#include "qa/application.h"
#include "qa/recovery.h"

/* Authority and dedicated selection come from the actual frontend/network
 * owner. Loading an offline image does not require a live world or player. */
bool qa_application_save_policy(qa_application *,qa_save_authority,bool dedicated,
    bool loading,qa_save_purpose,qa_error *);
/* The real stock Source product, or NULL for a composed/modded session.
 * Eligibility and original-format representability remain separate checks. */
const qa_product *qa_application_save_original_product(const qa_application *);
#endif
