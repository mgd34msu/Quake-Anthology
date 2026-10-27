/* Port of src/compat/q2/native-mod-provider.ts (declaration
 * validation delegating to items/pickups validators; full provider
 * wiring needs NativeModHost/ModHostServices, see unresolved). */
#include "qa/compat/q2/native_mod_provider.h"
#include <string.h>
#include "qa/compat/q2/native_mod_items.h"
#include "qa/compat/q2/native_mod_pickups.h"
void qa_compat_q2_validate_native_mod_declaration(const qa_native_mod_declaration *d){
  if(d->version!=1||!d->runtime||strcmp(d->runtime,"native")!=0) qa_range_error("Native mod declaration requires its native runtime version");
  qa_compat_q2_validate_native_mod_items(d);
  qa_compat_q2_validate_native_mod_pickups(d);
}
void qa_compat_q2_native_mod_provider_init(qa_compat_q2_native_mod_provider *s, void *ctx, const qa_native_mod_declaration *d){qa_compat_q2_validate_native_mod_declaration(d);s->host_ctx=ctx;s->declaration=d;s->closed=false;}
void qa_compat_q2_native_mod_provider_close(qa_compat_q2_native_mod_provider *s){s->closed=true;}
