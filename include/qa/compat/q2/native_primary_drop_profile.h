/* Port of src/compat/q2/native-primary-drop-profile.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_PRIMARY_DROP_PROFILE_H
#define QA_COMPAT_Q2_NATIVE_PRIMARY_DROP_PROFILE_H
#include "qa/compat/q2/native_primary_drop.h"
#include "qa/contracts/content.h"

const qa_compat_q2_native_primary_drop_profile *qa_compat_q2_get_native_primary_drop_profile(qa_content_digest digest);
#endif
