/* Port of src/compat/q2/native-mod-deferred.ts (reader; live state
 * needs NativeModHost, see unresolved). GPL-2.0-or-later. */
#include "qa/compat/q2/native_mod_deferred.h"
#include <string.h>
#include "qa/persistence/save_image.h"
#include "qa/persistence/shared.h"
typedef qa_save_reader R;
static const R *F(const R *r,const char *n,R *o){qa_save_reader_field(r,n,o);return o;}
qa_compat_q2_saved_native_deferred_damage qa_compat_q2_read_native_deferred_damage(const R *reader){
  qa_compat_q2_saved_native_deferred_damage o; memset(&o,0,sizeof o); R req,mod,x;
  F(reader,"request",&req); F(reader,"mod",&mod); size_t n=qa_save_reader_list_count(&mod);
  double mv[8]={0}; for(size_t i=0;i<n&&i<8;i++){R it;qa_save_reader_item(&mod,i,&it);mv[i]=qa_save_reader_integer_min(&it,0);qa_save_reader_destroy(&it);}
  if(n!=3||mv[0]>255||mv[1]>255||mv[2]>255) qa_save_reader_fail(reader,"native deferred damage requires its three-byte mod_t");
  o.mod[0]=mv[0];o.mod[1]=mv[1];o.mod[2]=mv[2]; qa_save_reader_destroy(&mod);
  o.target=qa_read_saved_actor(F(reader,"target",&x)); qa_save_reader_destroy(&x);
  o.attack=qa_read_q2_attack_checkpoint(F(reader,"attack",&x)); qa_save_reader_destroy(&x);
  o.amount=qa_save_reader_finite(F(&req,"amount",&x));qa_save_reader_destroy(&x);
  o.knockback_req=qa_save_reader_finite(F(&req,"knockback",&x));qa_save_reader_destroy(&x);
  o.point_req=qa_read_vector(F(&req,"point",&x));qa_save_reader_destroy(&x);
  o.direction=qa_read_vector(F(&req,"direction",&x));qa_save_reader_destroy(&x);
  o.normal=qa_read_vector(F(&req,"normal",&x));qa_save_reader_destroy(&x);
  {static const char *const dv[]={"direct","radius"};o.delivery=qa_save_reader_choice_string(F(&req,"delivery",&x),dv,2);qa_save_reader_destroy(&x);}
  qa_save_reader_destroy(&req);
  o.blood=qa_save_reader_finite(F(reader,"blood",&x));qa_save_reader_destroy(&x);
  o.knockback=qa_save_reader_finite(F(reader,"knockback",&x));qa_save_reader_destroy(&x);
  o.point=qa_read_vector(F(reader,"point",&x));qa_save_reader_destroy(&x);
  F(reader,"attackerSlot",&x);o.has_attacker=!qa_save_reader_is_null(&x);if(o.has_attacker)o.attacker_slot=qa_save_reader_integer_min(&x,0);qa_save_reader_destroy(&x);
  F(reader,"inflictorSlot",&x);o.has_inflictor=!qa_save_reader_is_null(&x);if(o.has_inflictor)o.inflictor_slot=qa_save_reader_integer_min(&x,0);qa_save_reader_destroy(&x);
  return o;
}
void qa_compat_q2_native_mod_deferred_damage_state_clear(qa_compat_q2_native_mod_deferred_damage_state *s){s->pending_count=0;}
