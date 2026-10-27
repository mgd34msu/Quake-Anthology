/* Port of src/compat/q2/native-primary-weapons.ts. Host interception
 * (dispatcher/spawn/delay/damage regions) binds through the mirrored
 * host fn+ctx; see header. GPL-2.0-or-later. */
#include "qa/compat/q2/native_primary_weapons.h"
#include <string.h>
void qa_compat_q2_native_primary_weapons_init(qa_compat_q2_native_primary_weapons *s, qa_compat_q2_native_primary_weapon_host host, const qa_compat_q2_native_primary_weapon_profile *profile, qa_compat_q2_native_primary_weapon_hooks hooks) {
  if (!profile || !host.module_digest || strcmp(host.module_digest, profile->digest) != 0) qa_range_error("Native primary weapon profile does not identify this artifact");
  s->host = host; s->profile = profile; s->hooks = hooks; s->closed = false;
}
void qa_compat_q2_native_primary_weapons_close(qa_compat_q2_native_primary_weapons *s) {
  if (s->closed) return;
  s->closed = true; if (s->host.on_close) s->host.on_close(s->host.ctx);
}
