/* Port of src/compat/q2/native-mod-protection.ts. Live guest binding needs NativeModHost (app/bootstrap, other slice); declaration/state retained here. GPL-2.0-or-later. */
#include "qa/compat/q2/native_mod_protection.h"
void qa_compat_q2_native_mod_protection_init(qa_compat_q2_native_mod_protection *s, void *host_ctx, const qa_native_mod_declaration *d){s->host_ctx=host_ctx;s->declaration=d;s->closed=false;s->active=false;}
void qa_compat_q2_native_mod_protection_close(qa_compat_q2_native_mod_protection *s){s->closed=true;s->active=false;}
