/* Port of src/compat/q2/rerelease/client-profile.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_RERELEASE_CLIENT_PROFILE_H
#define QA_COMPAT_Q2_RERELEASE_CLIENT_PROFILE_H
#include <stdbool.h>
#include "qa/compat/q2/rerelease/layouts.h"
#include "qa/contracts/content.h"

typedef struct qa_compat_q2_rerelease_client_profile {
  bool authority_is_artifact; /* false: kind "source" */
  qa_content_digest header_digest; /* source only */
  qa_content_digest digest; /* artifact only */
  qa_guest_layout layout;
  double inventory_count;
  double ammo_count;
} qa_compat_q2_rerelease_client_profile;
/* Built on first use (layouts are runtime-built). */
const qa_compat_q2_rerelease_client_profile *qa_compat_q2_rerelease_get_source_client_profile(void);
const qa_compat_q2_rerelease_client_profile *qa_compat_q2_rerelease_get_retail_client_profile(void);
#endif
