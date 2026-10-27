/* Port of src/compat/q2/native-pickups.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/native_pickups.h"
#include <string.h>
void qa_compat_q2_native_primary_pickups_init(qa_compat_q2_native_primary_pickups *s, const char *host_digest, bool has_protection, const qa_compat_q2_native_pickup_profile *p) {
  bool any = false; for (size_t i = 0; i < p->grant_count; i++) if (p->grants[i].consumer_count) any = true;
  if (any && !has_protection) qa_range_error("Native pickup consumer has no protection owner binding");
  if (!host_digest || strcmp(host_digest, p->digest) != 0) qa_range_error("Native pickup profile does not identify this artifact");
  s->profile = p; s->host_digest = host_digest; s->has_supply = false; s->closed = false;
}
void qa_compat_q2_native_primary_pickups_bind_supply(qa_compat_q2_native_primary_pickups *s, qa_compat_q2_native_pickup_supply supply) { s->supply = supply; s->has_supply = true; }
void qa_compat_q2_native_primary_pickups_close(qa_compat_q2_native_primary_pickups *s) { s->closed = true; }
