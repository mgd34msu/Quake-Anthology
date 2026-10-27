/* Port of src/compat/q2/rerelease/pickup-profile.ts. GPL-2.0-or-later.
 * Grants/capacities are module-owned static storage filled on first
 * use (fieldOffset lookups need the runtime layouts). */
#ifndef QA_COMPAT_Q2_RERELEASE_PICKUP_PROFILE_H
#define QA_COMPAT_Q2_RERELEASE_PICKUP_PROFILE_H
#include "qa/compat/q2/native_pickups.h"
#include "qa/compat/q2/rerelease/layouts.h"

/* NULL is the TS null. */
const qa_compat_q2_native_pickup_profile *qa_compat_q2_rerelease_get_pickup_profile(qa_content_digest digest);
#endif
