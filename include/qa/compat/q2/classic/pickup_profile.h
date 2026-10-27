/* Port of src/compat/q2/classic/pickup-profile.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_CLASSIC_PICKUP_PROFILE_H
#define QA_COMPAT_Q2_CLASSIC_PICKUP_PROFILE_H
#include "qa/compat/q2/classic/layout.h"
#include "qa/compat/q2/native_pickups.h"

/* TS classicPickupProfile(digest); NULL is the TS null. */
const qa_compat_q2_native_pickup_profile *qa_compat_q2_classic_get_pickup_profile(qa_content_digest digest);
#endif
