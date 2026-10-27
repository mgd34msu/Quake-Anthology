/* Port of src/compat/q2/native-primary-drop.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/native_primary_drop.h"
#include <string.h>
void qa_compat_q2_native_primary_drop_init(qa_compat_q2_native_primary_drop *s, qa_compat_q2_native_primary_weapon_host h, const qa_compat_q2_native_primary_drop_profile *p, qa_compat_q2_native_primary_drop_hooks hk) {
  if (!p || !h.module_digest || strcmp(h.module_digest, p->digest) != 0) qa_range_error("Native drop profile belongs to another artifact");
  s->host = h; s->profile = p; s->hooks = hk; s->closed = false;
}
void qa_compat_q2_native_primary_drop_close(qa_compat_q2_native_primary_drop *s) { if (!s->closed) { s->closed = true; if (s->host.on_close) s->host.on_close(s->host.ctx); } }
