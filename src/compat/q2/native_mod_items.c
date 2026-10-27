/* Port of src/compat/q2/native-mod-items.ts (declaration validation
 * subset on landed shapes; live binding needs NativeModHost). */
#include "qa/compat/q2/native_mod_items.h"
#include <string.h>
void qa_compat_q2_validate_native_mod_items(const qa_native_mod_declaration *d){
  if(!d->items) return;
  for(size_t i=0;i<d->items->definition_count;i++) for(size_t j=0;j<i;j++)
    if(d->items->definitions[i].item&&d->items->definitions[j].item&&!strcmp(d->items->definitions[i].item,d->items->definitions[j].item)) qa_range_error("Native item definitions are ambiguous");
}
void qa_compat_q2_native_mod_items_state_init(qa_compat_q2_native_mod_items_state *s, void *ctx, const qa_native_mod_declaration *d){s->host_ctx=ctx;s->declaration=d;s->closed=false;qa_compat_q2_validate_native_mod_items(d);}
void qa_compat_q2_native_mod_items_state_close(qa_compat_q2_native_mod_items_state *s){s->closed=true;}
