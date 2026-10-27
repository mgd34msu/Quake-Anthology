/* Port of src/compat/q2/native-mod-pickups.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/native_mod_pickups.h"
#include <string.h>
void qa_compat_q2_validate_native_mod_pickups(const qa_native_mod_declaration *d){
  if(d->pickup_count&&!d->clients) qa_range_error("Native pickups require unique rules and source client admission");
  for(size_t i=0;i<d->pickup_count;i++){
    const qa_mod_pickup_rule *r=&d->pickups[i].rule;
    if(!r->id||!r->id[0]||r->offered_count==0) qa_range_error("Native pickups require unique rules and source client admission");
    for(size_t j=0;j<i;j++) if(!strcmp(d->pickups[j].rule.id,r->id)) qa_range_error("Native pickups require unique rules and source client admission");
    for(size_t k=0;k<r->offered_count;k++) for(size_t i2=0;i2<d->pickup_count;i2++) for(size_t k2=0;k2<d->pickups[i2].rule.offered_count;k2++)
      if((i2!=i||k2!=k)&&!strcmp(d->pickups[i2].rule.offered[k2],r->offered[k])) qa_range_error("Ambiguous native original pickup item");
  }
}
void qa_compat_q2_native_mod_pickups_init(qa_compat_q2_native_mod_pickups *s, void *ctx){s->host_ctx=ctx;s->active=false;s->depth=0;s->closed=false;}
void qa_compat_q2_native_mod_pickups_close(qa_compat_q2_native_mod_pickups *s){s->closed=true;}
