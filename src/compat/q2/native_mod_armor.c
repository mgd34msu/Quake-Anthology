/* Port of src/compat/q2/native-mod-armor.ts (validation; read/write
 * via mirrored scalar fn+ctx). GPL-2.0-or-later. */
#include "qa/compat/q2/native_mod_armor.h"
#include <math.h>
#include <string.h>
double qa_compat_q2_native_mod_armor_scalar_bytes(qa_native_mod_scalar_field f){
  const char *e=f.encoding; size_t n=strlen(e);
  if(n>=2&&!strcmp(e+n-2,"64")) return 8;
  if(n>=2&&!strcmp(e+n-2,"16")) return 2;
  if(n>=1&&e[n-1]=='8'&&n>=2&&e[n-2]!='1'&&e[n-2]!='6') return 1;
  if(!strcmp(e,"uint8")||!strcmp(e,"int8")) return 1;
  return 4;
}
void qa_compat_q2_native_mod_armor_validate_value(qa_native_mod_scalar_field f, double value){
  double bits=qa_compat_q2_native_mod_armor_scalar_bytes(f)*8; bool uns=strncmp(f.encoding,"uint",4)==0; bool flt=strncmp(f.encoding,"float",5)==0;
  if(!isfinite(value)||(!strcmp(f.encoding,"float32")&&!isfinite((double)(float)value))) qa_range_error("Native armor value exceeds its declared storage");
  if(!flt&&(value!=trunc(value)||value<(uns?0:-pow(2,bits-1))||value>=pow(2,uns?bits:bits-1))) qa_range_error("Native armor value exceeds its declared storage");
}
static void check_field(const qa_native_mod_declaration *d, qa_native_mod_armor_field f, qa_native_mod_armor_field *all, size_t *n){
  const qa_native_mod_actor_record *rec=NULL;
  for(size_t i=0;i<d->actor_record_count;i++) if(d->actor_records[i].id&&f.record&&!strcmp(d->actor_records[i].id,f.record)) rec=&d->actor_records[i];
  double b=qa_compat_q2_native_mod_armor_scalar_bytes(f.field);
  if(!rec||(!(d->entity_record&&f.record&&!strcmp(f.record,d->entity_record))&&strcmp(rec->base.kind,"clients")!=0)||f.field.offset!=trunc(f.field.offset)||f.field.offset<0||f.field.offset+b>rec->stride) qa_range_error("Native armor field requires a declared entity or public client record");
  for(size_t i=0;i<*n;i++){qa_native_mod_armor_field p=all[i]; double pb=qa_compat_q2_native_mod_armor_scalar_bytes(p.field);
    if(f.record&&p.record&&!strcmp(f.record,p.record)&&f.field.offset<p.field.offset+pb&&p.field.offset<f.field.offset+b&&(f.field.offset!=p.field.offset||strcmp(f.field.encoding,p.field.encoding)!=0)) qa_range_error("Overlapping native armor fields have incompatible storage");}
  all[(*n)++]=f;
}
void qa_compat_q2_native_mod_armor_state_init(qa_compat_q2_native_mod_armor_state *s, qa_native_mod_armor def, const qa_native_mod_declaration *decl){
  s->definition=def; s->declaration=decl;
  qa_native_mod_armor_field all[128]; size_t n=0;
  if(strcmp(def.kind,"none")!=0){
    for(size_t i=0;i<def.q2_regular_count;i++){check_field(decl,def.q2_regular[i].selection.field,all,&n);check_field(decl,def.q2_regular[i].points,all,&n);}
    for(size_t i=0;i<def.regular_count;i++){check_field(decl,def.regular[i].selection.field,all,&n);check_field(decl,def.regular[i].points,all,&n);}
    for(size_t i=0;i<def.power_count;i++){check_field(decl,def.power[i].selection.field,all,&n);check_field(decl,def.power[i].cells,all,&n);if(def.power[i].enabled)check_field(decl,def.power[i].enabled->field,all,&n);}
    for(size_t i=0;i<def.q2_regular_count;i++) for(size_t j=0;j<i;j++) if(def.q2_regular[i].item&&def.q2_regular[j].item&&!strcmp(def.q2_regular[i].item,def.q2_regular[j].item)) qa_range_error("Native armor selection is ambiguous");
    for(size_t i=0;i<def.power_count;i++) for(size_t j=0;j<i;j++) if(!strcmp(def.power[i].kind,def.power[j].kind)) qa_range_error("Native armor selection is ambiguous");
    if(!strcmp(def.kind,"q2")) for(size_t i=0;i<def.q2_regular_count;i++) if(!isfinite(def.q2_regular[i].normal_protection)||!isfinite(def.q2_regular[i].energy_protection)) qa_range_error("Native armor protection must be finite");
    for(size_t i=0;i<def.power_count;i++) if(def.power[i].enabled){ if(strncmp(def.power[i].enabled->field.field.encoding,"float",5)==0||def.power[i].enabled->mask!=trunc(def.power[i].enabled->mask)||def.power[i].enabled->mask<=0) qa_range_error("Native power armor requires an integer enabled mask"); qa_compat_q2_native_mod_armor_validate_value(def.power[i].enabled->field.field,def.power[i].enabled->mask); }
  }
}
