/* Port of src/compat/q2/native-combat-call.ts. readNativeCombatField /
 * lowerNativeCombatArguments need a live GuestCpu/MappedGuestMemory
 * (guest/abi, conflicting headers); see unresolved. GPL-2.0-or-later. */
#include "qa/compat/q2/native_combat_call.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
static const qa_guest_field_layout mod_fields[]={{"id",0,"uint8",1},{"friendly_fire",1,"uint8",1},{"no_point_loss",2,"uint8",1}};
const qa_guest_layout qa_compat_q2_native_rerelease_mod_layout={"q2-rerelease:mod_t",3,1,8,mod_fields,3};
static const qa_compat_q2_native_combat_field dmg[]={QA_Q2CF_TARGET,QA_Q2CF_INFLICTOR,QA_Q2CF_ATTACKER,QA_Q2CF_DIRECTION,QA_Q2CF_POINT,QA_Q2CF_NORMAL,QA_Q2CF_AMOUNT,QA_Q2CF_KNOCKBACK,QA_Q2CF_FLAGS,QA_Q2CF_CAUSE};
static const qa_compat_q2_native_combat_field reg[]={QA_Q2CF_TARGET,QA_Q2CF_POINT,QA_Q2CF_NORMAL,QA_Q2CF_AMOUNT,QA_Q2CF_SPARKS,QA_Q2CF_FLAGS};
static const qa_compat_q2_native_combat_field pow_[]={QA_Q2CF_TARGET,QA_Q2CF_POINT,QA_Q2CF_NORMAL,QA_Q2CF_AMOUNT,QA_Q2CF_FLAGS};
static const qa_compat_q2_native_combat_field pain4[]={QA_Q2CF_TARGET,QA_Q2CF_ATTACKER,QA_Q2CF_KICK,QA_Q2CF_AMOUNT};
static const qa_compat_q2_native_combat_field pain8[]={QA_Q2CF_TARGET,QA_Q2CF_ATTACKER,QA_Q2CF_KICK,QA_Q2CF_AMOUNT,QA_Q2CF_CAUSE};
static const qa_compat_q2_native_combat_field death4[]={QA_Q2CF_TARGET,QA_Q2CF_INFLICTOR,QA_Q2CF_ATTACKER,QA_Q2CF_AMOUNT,QA_Q2CF_POINT};
static const qa_compat_q2_native_combat_field death8[]={QA_Q2CF_TARGET,QA_Q2CF_INFLICTOR,QA_Q2CF_ATTACKER,QA_Q2CF_AMOUNT,QA_Q2CF_POINT,QA_Q2CF_CAUSE};
static const qa_compat_q2_native_combat_field def[]={QA_Q2CF_TARGET};
const qa_compat_q2_native_combat_field *qa_compat_q2_native_combat_fields(qa_compat_q2_native_combat_operation op, int64_t pb, size_t *count) {
  switch (op) {
    case QA_Q2CO_DAMAGE: *count=10; return dmg;
    case QA_Q2CO_REGULAR_ARMOR: *count=6; return reg;
    case QA_Q2CO_POWER_ARMOR: *count=5; return pow_;
    case QA_Q2CO_PAIN: if (pb==4) {*count=4; return pain4;} *count=5; return pain8;
    case QA_Q2CO_DEATH: if (pb==4) {*count=5; return death4;} *count=6; return death8;
    default: *count=1; return def;
  }
}
qa_compat_q2_native_combat_call qa_compat_q2_stock_native_combat_call(qa_compat_q2_native_combat_operation op, bool has_abi, qa_native_call_abi abi, qa_compat_q2_native_combat_argument *storage) {
  size_t n; const qa_compat_q2_native_combat_field *f=qa_compat_q2_native_combat_fields(op, has_abi?abi.pointer_bytes:4,&n);
  for (size_t i=0;i<n;i++) { memset(&storage[i],0,sizeof storage[i]); storage[i].kind=QA_Q2CA_FIELD; storage[i].field=f[i]; }
  qa_compat_q2_native_combat_call c={(has_abi&&abi.pointer_bytes==8)?"microsoft-x64":"cdecl",storage,n}; return c;
}
static qa_guest_value_layout lay(const qa_compat_q2_native_combat_argument *a, qa_native_call_abi abi) {
  qa_guest_value_layout v; memset(&v,0,sizeof v);
  if (a->kind==QA_Q2CA_ADDRESS) { v.storage="pointer"; return v; }
  if (a->kind==QA_Q2CA_VALUE) return a->layout;
  switch (a->field) {
    case QA_Q2CF_TARGET: case QA_Q2CF_INFLICTOR: case QA_Q2CF_ATTACKER: case QA_Q2CF_DIRECTION: case QA_Q2CF_POINT: case QA_Q2CF_NORMAL: v.storage="pointer"; return v;
    case QA_Q2CF_AMOUNT: case QA_Q2CF_KNOCKBACK: case QA_Q2CF_FLAGS: case QA_Q2CF_SPARKS: v.storage="int32"; return v;
    case QA_Q2CF_KICK: v.storage="float32"; return v;
    default: if (abi.pointer_bytes==4) {v.storage="int32"; return v;} v.is_aggregate=true; v.layout=qa_compat_q2_native_rerelease_mod_layout; return v;
  }
}
qa_compat_q2_guest_call_signature qa_compat_q2_native_combat_signature(const qa_compat_q2_native_combat_call *call, qa_compat_q2_native_combat_operation op, qa_native_call_abi abi, qa_guest_value_layout *storage) {
  qa_native_call_abi src;
  if (strcmp(abi.kind,"windows-i386")==0 && strcmp(call->convention,"microsoft-x64")!=0) { src=abi; src.call=call->convention; }
  else if (strcmp(abi.kind,"windows-x86-64")==0 && strcmp(call->convention,"microsoft-x64")==0) src=abi;
  else qa_range_error("Native combat convention does not match its source architecture");
  for (size_t i=0;i<call->argument_count;i++) storage[i]=lay(&call->arguments[i],src);
  qa_compat_q2_guest_call_signature s; memset(&s,0,sizeof s); s.abi=src; s.parameters=storage; s.parameter_count=call->argument_count;
  s.result_is_void=!(op==QA_Q2CO_REGULAR_ARMOR||op==QA_Q2CO_POWER_ARMOR); s.result.storage="int32"; s.variadic=false; return s;
}
static int64_t vbytes(const qa_guest_value_layout *l, int64_t pb) {
  if (l->is_aggregate) return (int64_t)l->layout.byte_length;
  const char *s=l->storage;
  if (!strcmp(s,"pointer")) return pb;
  if (!strcmp(s,"int8")||!strcmp(s,"uint8")) return 1;
  if (!strcmp(s,"int16")||!strcmp(s,"uint16")) return 2;
  if (!strcmp(s,"int32")||!strcmp(s,"uint32")||!strcmp(s,"float32")) return 4;
  return 8;
}
void qa_compat_q2_validate_native_combat_call(const qa_compat_q2_native_combat_call *call, qa_compat_q2_native_combat_operation op, qa_native_call_abi abi) {
  qa_guest_value_layout st[32]; (void)qa_compat_q2_native_combat_signature(call,op,abi,st);
  size_t n; const qa_compat_q2_native_combat_field *req=qa_compat_q2_native_combat_fields(op,abi.pointer_bytes,&n);
  bool seen[12]={0}; size_t seen_n=0;
  for (size_t i=0;i<call->argument_count;i++) { const qa_compat_q2_native_combat_argument *a=&call->arguments[i];
    if (a->kind==QA_Q2CA_FIELD) { bool ok=false; for (size_t j=0;j<n;j++) if (req[j]==a->field) ok=true;
      if (!ok||seen[a->field]) qa_range_error("Native combat fields must occur exactly once");
      seen[a->field]=true; seen_n++; }
    else if (a->kind==QA_Q2CA_ADDRESS) { if (a->address) { double vals[64]; size_t m=a->address->indirections.count<63?a->address->indirections.count:63; vals[0]=a->address->rva; for(size_t j=0;j<m;j++) vals[j+1]=a->address->indirections.values[j];
      for (size_t j=0;j<=m;j++) if (vals[j]!=trunc(vals[j])||vals[j]<0||vals[j]>4294967295.0) qa_range_error("Native combat address exceeds its image declaration"); } }
    else { bool ptr = !a->layout.is_aggregate ? (a->layout.storage&&!strcmp(a->layout.storage,"pointer")) : false;
      if (a->layout.is_aggregate) for (size_t j=0;j<a->layout.layout.field_count;j++) if (!strcmp(a->layout.layout.fields[j].storage,"pointer")) ptr=true;
      if (ptr) qa_range_error("Native combat pointer defaults require an image-relative address");
      if ((int64_t)a->byte_count!=vbytes(&a->layout,abi.pointer_bytes)) qa_range_error("Native combat default bytes do not match their source argument"); }
  }
  if (seen_n!=n) qa_range_error("Native combat declaration omits a required source field");
}
static const qa_save_reader *fld(const qa_save_reader *r,const char *nm,qa_save_reader *o){qa_save_reader_field(r,nm,o);return o;}
bool qa_compat_q2_read_native_combat_call(const qa_save_reader *reader, qa_compat_q2_native_combat_operation op, qa_native_call_abi abi, qa_compat_q2_native_combat_call *out) {
  static const char *const conv[]={"cdecl","stdcall","fastcall","thiscall","microsoft-x64"};
  static const char *const kinds[]={"field","value","address"};
  static const char *const fnames[]={"target","inflictor","attacker","direction","point","normal","amount","knockback","flags","cause","sparks","kick"};
  qa_save_reader cr,lr; out->convention=qa_save_reader_choice_string(fld(reader,"convention",&cr),conv,5); qa_save_reader_destroy(&cr);
  fld(reader,"arguments",&lr); size_t n=qa_save_reader_list_count(&lr);
  qa_compat_q2_native_combat_argument *args=calloc(n?n:1,sizeof *args); if(!args){qa_save_reader_destroy(&lr);return false;}
  for (size_t i=0;i<n;i++){ qa_save_reader it,kr; qa_save_reader_item(&lr,i,&it);
    const char *kind=qa_save_reader_choice_string(fld(&it,"kind",&kr),kinds,3); qa_save_reader_destroy(&kr);
    if(!strcmp(kind,"field")){qa_save_reader fr; const char *fn=qa_save_reader_choice_string(fld(&it,"field",&fr),fnames,12); qa_save_reader_destroy(&fr);
      args[i].kind=QA_Q2CA_FIELD; for(int j=0;j<12;j++) if(!strcmp(fn,fnames[j])) args[i].field=(qa_compat_q2_native_combat_field)j;}
    else if(!strcmp(kind,"address")){qa_save_reader ar; fld(&it,"address",&ar); args[i].kind=QA_Q2CA_ADDRESS;
      if(!qa_save_reader_is_null(&ar)){qa_native_mod_address *ad=malloc(sizeof *ad); qa_save_reader rv,iv; ad->rva=qa_compat_q2_native_offset(fld(&ar,"rva",&rv)); qa_save_reader_destroy(&rv);
        fld(&ar,"indirections",&iv); size_t m=qa_save_reader_list_count(&iv); double *arr=m?malloc(m*sizeof *arr):NULL;
        for(size_t j=0;j<m;j++){qa_save_reader x; qa_save_reader_item(&iv,j,&x); arr[j]=qa_compat_q2_native_offset(&x); qa_save_reader_destroy(&x);}
        qa_save_reader_destroy(&iv); ad->indirections.values=arr; ad->indirections.count=m; args[i].address=ad;}
      qa_save_reader_destroy(&ar);}
    else {qa_save_reader sr,kr2,br; fld(&it,"layout",&sr); static const char *const lk[]={"scalar","aggregate"};
      const char *k2=qa_save_reader_choice_string(fld(&sr,"kind",&kr2),lk,2); qa_save_reader_destroy(&kr2); args[i].kind=QA_Q2CA_VALUE;
      if(!strcmp(k2,"scalar")){qa_save_reader st; args[i].layout.storage=qa_compat_q2_native_scalar(fld(&sr,"storage",&st)); qa_save_reader_destroy(&st);}
      else { /* aggregate layouts via persistence/execution readLayout are guest-world; retain empty layout */ args[i].layout.is_aggregate=true; }
      qa_save_reader_destroy(&sr);
      fld(&it,"bytes",&br); size_t m=qa_save_reader_list_count(&br); uint8_t *b=m?malloc(m):NULL;
      for(size_t j=0;j<m;j++){qa_save_reader x; qa_save_reader_item(&br,j,&x); b[j]=qa_js_to_uint8(qa_save_reader_integer_min(&x,0)); qa_save_reader_destroy(&x);}
      qa_save_reader_destroy(&br); args[i].bytes=b; args[i].byte_count=m;}
    qa_save_reader_destroy(&it);}
  qa_save_reader_destroy(&lr); out->arguments=args; out->argument_count=n;
  qa_compat_q2_validate_native_combat_call(out,op,abi); return true;
}
void qa_compat_q2_native_combat_call_free(qa_compat_q2_native_combat_call *call) {
  for (size_t i=0;i<call->argument_count;i++){ free((void*)call->arguments[i].bytes); if(call->arguments[i].address){free((void*)call->arguments[i].address->indirections.values); free((void*)call->arguments[i].address);} }
  free((void*)call->arguments); call->arguments=NULL; call->argument_count=0;
}
size_t qa_compat_q2_read_native_combat_arguments(const qa_compat_q2_native_combat_call *call, qa_compat_q2_native_combat_operation op, const qa_guest_call_value *values, size_t count, int64_t pb, qa_guest_call_value *out) {
  if (count!=call->argument_count) qa_range_error("Native combat invocation differs from its declared signature");
  size_t n; const qa_compat_q2_native_combat_field *f=qa_compat_q2_native_combat_fields(op,pb,&n);
  for (size_t i=0;i<n;i++){ size_t idx=(size_t)-1; for(size_t j=0;j<call->argument_count;j++) if(call->arguments[j].kind==QA_Q2CA_FIELD&&call->arguments[j].field==f[i]) idx=j;
    if(idx==(size_t)-1) qa_range_error("Native combat invocation lacks a declared field");
    out[i]=values[idx]; }
  return n;
}
