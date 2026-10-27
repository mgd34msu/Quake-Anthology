/* Port of src/compat/q2/native-primary-profiles.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/native_primary_profiles.h"
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/native_primary_reader.h"
#include "qa/content/mods/match.h"
typedef qa_save_reader R;
static const R *F(const R *r,const char *n,R *o){qa_save_reader_field(r,n,o);return o;}
static double OFF(const R *r,const char *n){R x;double v=qa_compat_q2_native_offset(F(r,n,&x));qa_save_reader_destroy(&x);return v;}
static double INTM(const R *r,const char *n,double m){R x;double v=qa_save_reader_integer_min(F(r,n,&x),m);qa_save_reader_destroy(&x);return v;}
static double FIN(const R *r,const char *n){R x;double v=qa_save_reader_finite(F(r,n,&x));qa_save_reader_destroy(&x);return v;}
static qa_native_item_field FLD(const R *r,const char *n){R x;qa_native_item_field v=qa_compat_q2_native_field(F(r,n,&x));qa_save_reader_destroy(&x);return v;}
static qa_compat_q2_native_region_span REG(const R *r,const char *n){R x;qa_compat_q2_native_region_span v=qa_compat_q2_native_region(F(r,n,&x));qa_save_reader_destroy(&x);return v;}
static const char *CH(const R *r,const char *n,const char *const *c,size_t k){R x;const char *v=qa_save_reader_choice_string(F(r,n,&x),c,k);qa_save_reader_destroy(&x);return v;}
static bool tests(const R *r,const char *n,qa_native_item_test **out,size_t *cnt,qa_native_mod_address **store){
  R x; F(r,n,&x); size_t m=qa_save_reader_list_count(&x); *cnt=m;
  *out=m?calloc(m,sizeof **out):NULL; *store=m?calloc(m,sizeof **store):NULL;
  if(m&&(!*out||!*store)){qa_save_reader_destroy(&x);return false;}
  for(size_t i=0;i<m;i++){R it;qa_save_reader_item(&x,i,&it); if(!qa_compat_q2_native_test(&it,&(*out)[i],&(*store)[i])){qa_save_reader_destroy(&it);qa_save_reader_destroy(&x);return false;} qa_save_reader_destroy(&it);}
  qa_save_reader_destroy(&x); return true;
}
static bool rows(const R *r,const char *n,qa_native_item_test_row **out,size_t *cnt){
  R x; F(r,n,&x); size_t m=qa_save_reader_list_count(&x); *cnt=m; *out=m?calloc(m,sizeof **out):NULL; if(m&&!*out){qa_save_reader_destroy(&x);return false;}
  for(size_t i=0;i<m;i++){R it;qa_save_reader_item(&x,i,&it); size_t k=qa_save_reader_list_count(&it);
    qa_native_item_test *t=k?calloc(k,sizeof *t):NULL; qa_native_mod_address *st=k?calloc(k,sizeof *st):NULL; (void)st;
    for(size_t j=0;j<k;j++){R y;qa_save_reader_item(&it,j,&y); qa_compat_q2_native_test(&y,&t[j],&st[j]); qa_save_reader_destroy(&y);}
    (*out)[i].tests=t; (*out)[i].count=k; qa_save_reader_destroy(&it);}
  qa_save_reader_destroy(&x); return true;
}
bool qa_compat_q2_read_native_primary_weapons(const R *r, qa_content_digest d, qa_native_abi abi, qa_compat_q2_native_primary_weapon_profile *o){
  memset(o,0,sizeof *o); o->digest=d; o->abi=abi;
  R disp,spawn,timeR,ent,cli,anim,atk,delay,evalR,dmg;
  F(r,"dispatcher",&disp); F(r,"spawn",&spawn); F(r,"time",&timeR); F(r,"entity",&ent); F(r,"client",&cli); F(r,"animation",&anim); F(r,"attackAnimation",&atk); F(r,"delay",&delay); F(&delay,"evaluate",&evalR); F(r,"damage",&dmg);
  {R e,k; F(&disp,"entry",&e); qa_save_reader_literal_string(F(&e,"kind",&k),"rva"); qa_save_reader_destroy(&k);
   o->dispatcher.entry.kind="rva"; o->dispatcher.entry.rva=OFF(&e,"rva"); qa_save_reader_destroy(&e);}
  {R x; qa_save_reader_literal_string(F(&disp,"record",&x),"entity"); qa_save_reader_destroy(&x); o->dispatcher.record="entity";}
  o->dispatcher.argument=INTM(&disp,"argument",0); o->dispatcher.arguments=INTM(&disp,"arguments",1);
  if(o->dispatcher.argument>=o->dispatcher.arguments) qa_save_reader_fail(&disp,"dispatcher actor argument is outside its call");
  o->time_milliseconds=FIN(&timeR,"milliseconds"); if(o->time_milliseconds<=0) qa_save_reader_fail(&timeR,"clock scale must be positive");
  {R x; F(r,"equipmentContexts",&x); size_t m=qa_save_reader_list_count(&x); qa_source_equipment_context *ec=m?calloc(m,sizeof *ec):NULL;
   for(size_t i=0;i<m;i++){R it,pv,iv;qa_save_reader_item(&x,i,&it); ec[i].provider=qa_namespaced(F(&it,"provider",&pv)); qa_save_reader_destroy(&pv);
     F(&it,"item",&iv); ec[i].item=qa_save_reader_is_null(&iv)?NULL:qa_namespaced(&iv); qa_save_reader_destroy(&iv); qa_save_reader_destroy(&it);}
   for(size_t i=0;i<m;i++) for(size_t j=0;j<i;j++) if(!strcmp(ec[i].provider,ec[j].provider)) qa_save_reader_fail(&x,"duplicate equipment source context");
   qa_save_reader_destroy(&x); o->equipment_contexts=ec; o->equipment_context_count=m;}
  {R x; F(r,"decisions",&x); size_t m=qa_save_reader_list_count(&x); qa_native_weapon_decision *ds=m?calloc(m,sizeof *ds):NULL;
   for(size_t i=0;i<m;i++){R it,fl;qa_save_reader_item(&x,i,&it); qa_compat_q2_native_region_span s=qa_compat_q2_native_region(&it); ds[i].entry=s.entry; ds[i].join=s.join;
     F(&it,"fields",&fl); size_t k=qa_save_reader_list_count(&fl); qa_native_weapon_decision_field *fs=k?calloc(k,sizeof *fs):NULL;
     for(size_t j=0;j<k;j++){R y;qa_save_reader_item(&fl,j,&y); fs[j].field=FLD(&y,"field"); fs[j].clear_mask=INTM(&y,"clearMask",1); qa_save_reader_destroy(&y);}
     qa_save_reader_destroy(&fl); ds[i].fields=fs; ds[i].field_count=k; qa_save_reader_destroy(&it);}
   qa_save_reader_destroy(&x); o->decisions=ds; o->decision_count=m;}
  o->spawn_entry=OFF(&spawn,"entry"); qa_native_mod_address *st1=NULL,*st2=NULL;
  if(!tests(&spawn,"accepted",(qa_native_item_test**)&o->spawn_accepted,&o->spawn_accepted_count,&st1)) return false;
  if(!tests(r,"active",(qa_native_item_test**)&o->active,&o->active_count,&st2)) return false;
  if(!rows(r,"committedInput",(qa_native_item_test_row**)&o->committed_input,&o->committed_input_count)) return false;
  if(!rows(r,"continuations",(qa_native_item_test_row**)&o->continuations,&o->continuation_count)) return false;
  o->time_address=OFF(&timeR,"address"); {R x;o->time_encoding=qa_compat_q2_native_scalar(F(&timeR,"encoding",&x));qa_save_reader_destroy(&x);}
  o->entity_client=OFF(&ent,"client"); o->entity_water_level=FLD(&ent,"waterLevel"); o->entity_view_height=FLD(&ent,"viewHeight"); o->entity_max_health=FLD(&ent,"maxHealth");
  o->client_byte_length=INTM(&cli,"byteLength",1); o->client_view_angles=OFF(&cli,"viewAngles"); o->client_buttons=FLD(&cli,"buttons"); o->client_latched_buttons=FLD(&cli,"latchedButtons");
  o->attack_animation_entry=OFF(&atk,"entry");
  {R x; F(&atk,"skip",&x); size_t m=qa_save_reader_list_count(&x); qa_compat_q2_native_region_span_fwd *sk=m?calloc(m,sizeof *sk):NULL;
   for(size_t i=0;i<m;i++){R it;qa_save_reader_item(&x,i,&it); qa_compat_q2_native_region_span s=qa_compat_q2_native_region(&it); sk[i].entry=s.entry; sk[i].join=s.join; qa_save_reader_destroy(&it);}
   qa_save_reader_destroy(&x); o->attack_skip=sk; o->attack_skip_count=m;}
  o->anim_frame=FLD(&anim,"frame"); o->anim_end=FLD(&anim,"end"); o->anim_priority=FLD(&anim,"priority"); o->anim_duck=FLD(&anim,"duck"); o->anim_run=FLD(&anim,"run");
  o->delay_flag=FLD(&delay,"flag"); {qa_compat_q2_native_region_span s=REG(&delay,"region"); o->delay_entry=s.entry; o->delay_join=s.join;}
  {static const char *const ek[]={"source-flag","source-animation"}; o->delay_evaluate_kind=CH(&evalR,"kind",ek,2);
   if(!strcmp(o->delay_evaluate_kind,"source-flag")){R x;F(&evalR,"factors",&x);size_t m=qa_save_reader_list_count(&x);double *f=m?malloc(m*sizeof *f):NULL;
     for(size_t i=0;i<m;i++){R it;qa_save_reader_item(&x,i,&it);f[i]=qa_save_reader_finite(&it);qa_save_reader_destroy(&it);} qa_save_reader_destroy(&x); o->delay_factors=f;o->delay_factor_count=m;}
   else {o->delay_anim_entry=OFF(&evalR,"entry"); o->delay_baseline_ms=FIN(&evalR,"baselineMilliseconds");
     R x;F(&evalR,"projection",&x);size_t m=qa_save_reader_list_count(&x);qa_compat_q2_weapon_projection *pr=m?calloc(m,sizeof *pr):NULL;
     for(size_t i=0;i<m;i++){R it;qa_save_reader_item(&x,i,&it);pr[i].field=FLD(&it,"field");pr[i].value=FIN(&it,"value");qa_save_reader_destroy(&it);} qa_save_reader_destroy(&x);o->delay_projection=pr;o->delay_projection_count=m;
     R w;F(&evalR,"writes",&w);size_t k=qa_save_reader_list_count(&w);qa_native_item_field *ws=k?calloc(k,sizeof *ws):NULL;
     for(size_t i=0;i<k;i++){R it;qa_save_reader_item(&w,i,&it);ws[i]=qa_compat_q2_native_field(&it);qa_save_reader_destroy(&it);} qa_save_reader_destroy(&w);o->delay_writes=ws;o->delay_write_count=k;}}
  {static const char *const dk[]={"source-result","source-flag"}; o->damage_kind=CH(&dmg,"kind",dk,2);
   if(!strcmp(o->damage_kind,"source-result")){static const char *const rr[]={"uint8","int32"}; o->damage_entry=OFF(&dmg,"entry"); o->damage_result=CH(&dmg,"result",rr,2);}
   else {R x;o->damage_address=OFF(&dmg,"address"); o->damage_encoding=qa_compat_q2_native_scalar(F(&dmg,"encoding",&x));qa_save_reader_destroy(&x);
     R f;F(&dmg,"factors",&f);size_t m=qa_save_reader_list_count(&f);double *fa=m?malloc(m*sizeof *fa):NULL;
     for(size_t i=0;i<m;i++){R it;qa_save_reader_item(&f,i,&it);fa[i]=qa_save_reader_finite(&it);qa_save_reader_destroy(&it);} qa_save_reader_destroy(&f);o->damage_factors=fa;o->damage_factor_count=m;
     qa_compat_q2_native_region_span s=REG(&dmg,"region"); o->damage_region_entry=s.entry;o->damage_region_join=s.join;}}
  qa_save_reader_destroy(&disp);qa_save_reader_destroy(&spawn);qa_save_reader_destroy(&timeR);qa_save_reader_destroy(&ent);qa_save_reader_destroy(&cli);qa_save_reader_destroy(&anim);qa_save_reader_destroy(&atk);qa_save_reader_destroy(&evalR);qa_save_reader_destroy(&delay);qa_save_reader_destroy(&dmg);
  return true;
}
bool qa_compat_q2_read_native_primary_player(const R *r, qa_content_digest d, qa_compat_q2_native_primary_player_profile *o){
  memset(o,0,sizeof *o); o->digest=d; R m,obj,fw; F(r,"match",&m);
  if(!qa_save_reader_is_undefined(&m)){o->has_match=true; o->match=qa_read_source_primary_match(&m,4294967295.0);} qa_save_reader_destroy(&m);
  o->spawn=OFF(r,"spawn"); F(r,"objectives",&obj); static const char *const ok[]={"none","entry"}; o->objectives_kind=CH(&obj,"kind",ok,2);
  if(!strcmp(o->objectives_kind,"entry")) o->objectives_entry=OFF(&obj,"entry");
  qa_save_reader_destroy(&obj);
  o->command_angles=OFF(r,"commandAngles"); o->velocity=OFF(r,"velocity");
  F(r,"forward",&fw); o->has_forward=!qa_save_reader_is_null(&fw); if(o->has_forward) o->forward=qa_compat_q2_native_offset(&fw); qa_save_reader_destroy(&fw); return true;
}
bool qa_compat_q2_read_native_primary_commands(const R *r, qa_content_digest d, qa_native_abi abi, qa_compat_q2_native_primary_command_profile *o){
  memset(o,0,sizeof *o); o->digest=d;o->abi=abi; R give,drop,cli,items,ammo;
  F(r,"give",&give);F(r,"drop",&drop);F(r,"client",&cli);F(r,"items",&items);F(&items,"ammo",&ammo);
  o->give_entry=OFF(&give,"entry");o->give_weapons=OFF(&give,"weapons");o->give_ammo=OFF(&give,"ammo");
  {qa_compat_q2_native_region_span s=REG(&give,"unknown");o->give_unknown_entry=s.entry;o->give_unknown_join=s.join;}
  {R x;F(&give,"ammoGrants",&x);size_t m=qa_save_reader_list_count(&x);qa_compat_q2_ammo_grant *g=m?calloc(m,sizeof *g):NULL; static const char *const kk[]={"set","add"};
   for(size_t i=0;i<m;i++){R it,dr;qa_save_reader_item(&x,i,&it);qa_compat_q2_native_region_span s=qa_compat_q2_native_region(&it);g[i].entry=s.entry;g[i].join=s.join;g[i].descriptor=qa_compat_q2_native_register(F(&it,"descriptor",&dr));qa_save_reader_destroy(&dr);g[i].kind=CH(&it,"kind",kk,2);qa_save_reader_destroy(&it);}
   qa_save_reader_destroy(&x);o->ammo_grants=g;o->ammo_grant_count=m;}
  o->give_argc=OFF(&give,"argc");o->give_argv=OFF(&give,"argv");
  o->drop_entry=OFF(&drop,"entry"); {qa_compat_q2_native_region_span s=REG(&drop,"eligibility");o->drop_elig_entry=s.entry;o->drop_elig_join=s.join;}
  o->client_pointer=OFF(&cli,"pointer");o->client_weapon=OFF(&cli,"weapon");
  {R x;F(&cli,"ammoIndex",&x);o->has_client_ammo_index=!qa_save_reader_is_null(&x);if(o->has_client_ammo_index)o->client_ammo_index=qa_compat_q2_native_offset(&x);qa_save_reader_destroy(&x);}
  o->client_inventory=OFF(&cli,"inventory");
  o->items_table=OFF(&items,"table");o->items_stride=INTM(&items,"stride",1);o->items_count=INTM(&items,"count",1);o->items_classname=OFF(&items,"classname");o->items_flags=OFF(&items,"flags");o->items_weapon_flag=INTM(&items,"weaponFlag",1);o->items_ammunition_flag=INTM(&items,"ammunitionFlag",1);o->items_icon=OFF(&items,"icon");
  {static const char *const ak[]={"name","index"};o->ammo_kind=CH(&ammo,"kind",ak,2);o->ammo_offset=OFF(&ammo,"offset");if(!strcmp(o->ammo_kind,"name"))o->ammo_label=OFF(&ammo,"label");}
  qa_save_reader_destroy(&give);qa_save_reader_destroy(&drop);qa_save_reader_destroy(&cli);qa_save_reader_destroy(&ammo);qa_save_reader_destroy(&items); return true;
}
bool qa_compat_q2_read_native_primary_inventory(const R *r, qa_content_digest d, qa_native_abi abi, qa_compat_q2_native_primary_inventory_profile *o){
  memset(o,0,sizeof *o);o->digest=d;o->abi=abi; R proto,nx,pv,vl,us,nu;
  F(r,"prototypes",&proto);F(r,"next",&nx);F(r,"previous",&pv);F(r,"validate",&vl);F(r,"use",&us);F(r,"namedUse",&nu);
  o->client=OFF(r,"client");o->inventory=OFF(r,"inventory");o->count=INTM(r,"count",1);o->cursor=OFF(r,"cursor");
  {R x;o->empty=qa_save_reader_integer(F(r,"empty",&x));qa_save_reader_destroy(&x);}
  {R a,b,c2,e,f,g;o->proto_weapon=qa_namespaced(F(&proto,"weapon",&a));qa_save_reader_destroy(&a);o->proto_ammunition=qa_namespaced(F(&proto,"ammunition",&b));qa_save_reader_destroy(&b);o->proto_usable=qa_namespaced(F(&proto,"usable",&c2));qa_save_reader_destroy(&c2);o->proto_passive=qa_namespaced(F(&proto,"passive",&e));qa_save_reader_destroy(&e);o->proto_droppable=qa_namespaced(F(&proto,"droppable",&f));qa_save_reader_destroy(&f);o->proto_undroppable=qa_namespaced(F(&proto,"undroppable",&g));qa_save_reader_destroy(&g);}
  {R x;F(r,"selectionWrites",&x);size_t m=qa_save_reader_list_count(&x);qa_compat_q2_selection_write *w=m?calloc(m,sizeof *w):NULL;
   for(size_t i=0;i<m;i++){R it;qa_save_reader_item(&x,i,&it);w[i].offset=OFF(&it,"offset");w[i].bytes=INTM(&it,"bytes",1);qa_save_reader_destroy(&it);} qa_save_reader_destroy(&x);o->selection_writes=w;o->selection_write_count=m;}
  {qa_compat_q2_native_region_span s=qa_compat_q2_native_region(&nx);o->next_entry=s.entry;o->next_join=s.join;o->next_scan=OFF(&nx,"scan");R x;o->next_menu_argument=qa_save_reader_boolean(F(&nx,"menuArgument",&x));qa_save_reader_destroy(&x);}
  {qa_compat_q2_native_region_span s=qa_compat_q2_native_region(&pv);o->prev_entry=s.entry;o->prev_join=s.join;o->prev_scan=OFF(&pv,"scan");}
  o->validate_entry=OFF(&vl,"entry"); {R x;F(&vl,"scan",&x);o->has_validate_scan=!qa_save_reader_is_null(&x);if(o->has_validate_scan){qa_compat_q2_native_region_span s=qa_compat_q2_native_region(&x);o->validate_scan_entry=s.entry;o->validate_scan_join=s.join;}qa_save_reader_destroy(&x);}
  {qa_compat_q2_native_region_span s=qa_compat_q2_native_region(&us);o->use_entry=s.entry;o->use_join=s.join;o->use_call=OFF(&us,"call");}
  {qa_compat_q2_native_region_span s=qa_compat_q2_native_region(&nu);o->named_entry=s.entry;o->named_join=s.join;o->named_call=OFF(&nu,"call");o->named_lookup_call=OFF(&nu,"lookupCall");o->named_lookup_return=OFF(&nu,"lookupReturn");}
  qa_save_reader_destroy(&proto);qa_save_reader_destroy(&nx);qa_save_reader_destroy(&pv);qa_save_reader_destroy(&vl);qa_save_reader_destroy(&us);qa_save_reader_destroy(&nu); return true;
}
static bool spans(const R *r,const char *n,qa_compat_q2_native_region_span_fwd **out,size_t *cnt){R x;F(r,n,&x);size_t m=qa_save_reader_list_count(&x);*cnt=m;qa_compat_q2_native_region_span_fwd *a=m?calloc(m,sizeof *a):NULL;for(size_t i=0;i<m;i++){R it;qa_save_reader_item(&x,i,&it);qa_compat_q2_native_region_span s=qa_compat_q2_native_region(&it);a[i].entry=s.entry;a[i].join=s.join;qa_save_reader_destroy(&it);}qa_save_reader_destroy(&x);*out=a;return true;}
bool qa_compat_q2_read_native_primary_drop(const R *r, qa_content_digest d, qa_native_abi abi, qa_compat_q2_native_primary_drop_profile *o){
  memset(o,0,sizeof *o);o->digest=d;o->abi=abi; R cli,inv; F(r,"client",&cli);F(r,"inventory",&inv);
  o->client_pointer=OFF(&cli,"pointer");o->client_inventory=OFF(&cli,"inventory");o->client_cursor=OFF(&cli,"cursor");o->client_weapon=OFF(&cli,"weapon");o->client_pending=OFF(&cli,"pending");
  o->named=OFF(r,"named");o->inv_entry=OFF(&inv,"entry");o->inv_admitted=OFF(&inv,"admitted");o->find=OFF(r,"find");o->lookup_return=OFF(r,"lookupReturn");o->allocate=OFF(r,"allocate");o->free_entry=OFF(r,"free");
  spans(r,"callbacks",(qa_compat_q2_native_region_span_fwd**)&o->callbacks,&o->callback_count); spans(r,"debits",(qa_compat_q2_native_region_span_fwd**)&o->debits,&o->debit_count);
  {R x;F(r,"consumer",&x);o->has_consumer=!qa_save_reader_is_null(&x);if(o->has_consumer){qa_compat_q2_native_region_span s=qa_compat_q2_native_region(&x);o->consumer_entry=s.entry;o->consumer_join=s.join;}qa_save_reader_destroy(&x);}
  qa_save_reader_destroy(&cli);qa_save_reader_destroy(&inv); return true;
}
bool qa_compat_q2_read_native_primary_pickups(const R *r, qa_content_digest d, qa_native_abi abi, qa_compat_q2_native_pickup_profile *o){
  memset(o,0,sizeof *o);o->digest=d; R items,ent,timeR,supply,ammo;
  F(r,"items",&items);F(r,"entity",&ent);F(r,"time",&timeR);F(r,"supply",&supply);F(&supply,"ammo",&ammo);
  o->touch=OFF(r,"touch");o->grant_return=OFF(r,"grantReturn");o->targets_return=OFF(r,"targetsReturn");
  {R x;qa_compat_q2_native_signature(F(r,"touchSignature",&x),abi,&o->touch_signature);qa_save_reader_destroy(&x);}
  {R x;qa_compat_q2_native_signature(F(r,"grantSignature",&x),abi,&o->grant_signature);qa_save_reader_destroy(&x);}
  {R x;F(r,"grants",&x);size_t m=qa_save_reader_list_count(&x);qa_compat_q2_native_pickup_grant *g=m?calloc(m,sizeof *g):NULL; static const char *const rk[]={"regular","inventory"};
   for(size_t i=0;i<m;i++){R it;qa_save_reader_item(&x,i,&it);g[i].entry=OFF(&it,"entry");qa_compat_q2_native_region_span s=REG(&it,"recipient");g[i].recipient_entry=s.entry;g[i].recipient_join=s.join;g[i].resource=CH(&it,"resource",rk,2);
     R cs;F(&it,"consumers",&cs); if(!qa_save_reader_is_undefined(&cs)){size_t k=qa_save_reader_list_count(&cs);qa_compat_q2_pickup_consumer *cn=k?calloc(k,sizeof *cn):NULL; static const char *const pk[]={"regular","powered"};
       for(size_t j=0;j<k;j++){R y,s2;qa_save_reader_item(&cs,j,&y);cn[j].entry=OFF(&y,"entry");qa_compat_q2_native_signature(F(&y,"signature",&s2),abi,&cn[j].signature);qa_save_reader_destroy(&s2);cn[j].protection=!strcmp(CH(&y,"protection",pk,2),"regular")?QA_PROTECTION_REGULAR:QA_PROTECTION_POWERED;qa_save_reader_destroy(&y);} g[i].consumers=cn;g[i].consumer_count=k;} qa_save_reader_destroy(&cs);
     R sp;F(&it,"supply",&sp); if(!qa_save_reader_is_undefined(&sp)){static const char *const sk[]={"ammo","weapon"};g[i].has_supply=true;g[i].supply_kind=CH(&sp,"kind",sk,2);
       if(!strcmp(g[i].supply_kind,"ammo")){R dr;g[i].supply_entry=OFF(&sp,"entry");g[i].supply_amount=qa_compat_q2_native_register(F(&sp,"amount",&dr));qa_save_reader_destroy(&dr);}
       else {g[i].supply_ammo_return=OFF(&sp,"ammoReturn");g[i].supply_settle=OFF(&sp,"settle");qa_compat_q2_native_region_span a=REG(&sp,"autoswitch");g[i].supply_auto_entry=a.entry;g[i].supply_auto_join=a.join;}} qa_save_reader_destroy(&sp);
     qa_save_reader_destroy(&it);}
   qa_save_reader_destroy(&x);o->grants=g;o->grant_count=m;}
  o->items_table=OFF(&items,"table");o->items_stride=INTM(&items,"stride",1);o->items_count=INTM(&items,"count",1);o->items_classname=OFF(&items,"classname");o->items_pickup=OFF(&items,"pickup");
  o->entity_item=OFF(&ent,"item");o->entity_count=OFF(&ent,"count");o->entity_spawnflags=OFF(&ent,"spawnflags");o->entity_inuse=OFF(&ent,"inuse");
  {static const double ib[]={1,4};R x;o->entity_inuse_bytes=qa_save_reader_choice_number(F(&ent,"inuseBytes",&x),ib,2);qa_save_reader_destroy(&x);}
  {R x;F(&ent,"generation",&x);o->has_entity_generation=!qa_save_reader_is_null(&x);if(o->has_entity_generation)o->entity_generation=qa_compat_q2_native_offset(&x);qa_save_reader_destroy(&x);}
  o->time_address=OFF(&timeR,"address"); {static const char *const tk[]={"float32-seconds","int64-milliseconds"};o->time_storage=CH(&timeR,"storage",tk,2);}
  o->supply_client=OFF(&supply,"client");o->supply_inventory=OFF(&supply,"inventory");o->supply_flags=OFF(&supply,"flags");o->supply_weapon_flag=INTM(&supply,"weaponFlag",1);
  o->ammo_entry=OFF(&ammo,"entry"); {R x;qa_compat_q2_native_signature(F(&ammo,"signature",&x),abi,&o->ammo_signature);qa_save_reader_destroy(&x);}
  {R x;F(&ammo,"stop",&x);o->has_ammo_stop=!qa_save_reader_is_null(&x);if(o->has_ammo_stop)o->ammo_stop=qa_compat_q2_native_offset(&x);qa_save_reader_destroy(&x);}
  o->ammo_tag=OFF(&ammo,"tag");
  {R x;F(&ammo,"capacities",&x);size_t m=qa_save_reader_list_count(&x);double *cap=m?malloc(m*sizeof *cap):NULL;for(size_t i=0;i<m;i++){R it;qa_save_reader_item(&x,i,&it);cap[i]=qa_compat_q2_native_offset(&it);qa_save_reader_destroy(&it);}qa_save_reader_destroy(&x);o->ammo_capacities=cap;o->ammo_capacity_count=m;}
  {static const double cb[]={2,4};R x;o->ammo_capacity_bytes=qa_save_reader_choice_number(F(&ammo,"capacityBytes",&x),cb,2);qa_save_reader_destroy(&x);}
  qa_save_reader_destroy(&items);qa_save_reader_destroy(&ent);qa_save_reader_destroy(&timeR);qa_save_reader_destroy(&ammo);qa_save_reader_destroy(&supply); return true;
}
void qa_compat_q2_native_primary_weapon_profile_free(qa_compat_q2_native_primary_weapon_profile *p){free((void*)p->equipment_contexts);free((void*)p->decisions);free((void*)p->spawn_accepted);free((void*)p->active);free((void*)p->committed_input);free((void*)p->continuations);free((void*)p->attack_skip);free((void*)p->delay_factors);free((void*)p->delay_projection);free((void*)p->delay_writes);free((void*)p->damage_factors);}
void qa_compat_q2_native_primary_command_profile_free(qa_compat_q2_native_primary_command_profile *p){free((void*)p->ammo_grants);}
void qa_compat_q2_native_primary_inventory_profile_free(qa_compat_q2_native_primary_inventory_profile *p){free((void*)p->selection_writes);}
void qa_compat_q2_native_primary_drop_profile_free(qa_compat_q2_native_primary_drop_profile *p){free((void*)p->callbacks);free((void*)p->debits);}
void qa_compat_q2_native_pickup_profile_free(qa_compat_q2_native_pickup_profile *p){free((void*)p->grants);free((void*)p->ammo_capacities);free((void*)p->touch_signature.parameters);free((void*)p->grant_signature.parameters);free((void*)p->ammo_signature.parameters);}
