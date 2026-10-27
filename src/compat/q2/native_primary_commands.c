/* Port of src/compat/q2/native-primary-commands.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/native_primary_commands.h"
#include <string.h>
void qa_compat_q2_native_primary_commands_init(qa_compat_q2_native_primary_commands *s, qa_compat_q2_native_primary_weapon_host host, const qa_compat_q2_native_primary_command_profile *p, qa_compat_q2_native_primary_command_hooks h) {
  if (!p || !host.module_digest || strcmp(host.module_digest, p->digest) != 0) qa_range_error("Native command profile belongs to another artifact");
  s->host = host; s->profile = p; s->hooks = h; s->closed = false;
}
void qa_compat_q2_native_primary_commands_close(qa_compat_q2_native_primary_commands *s) { if (!s->closed) { s->closed = true; if (s->host.on_close) s->host.on_close(s->host.ctx); } }
