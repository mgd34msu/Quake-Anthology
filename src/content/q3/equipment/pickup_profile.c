/* Port of src/content/q3/equipment/pickup-profile.ts. GPL-2.0-or-later. */
#include "qa/content/q3/equipment/pickup_profile.h"
#include <string.h>
#include "qa/content/q3/equipment/threewave_grapple_profile.h"
#include "qa/foundation/support.h"
const char qa_stock_q3_pickup_digest[]="sha256:57c52bf22e4f528c064f8af1553a7103723bab0a02276bb11eed944bf829b219";
static int32_t word(qa_byte_view v,size_t off){
  if(off+4>v.length) qa_range_error("QVM pickup eligibility word is outside its view");
  uint32_t u=(uint32_t)v.bytes[off]|((uint32_t)v.bytes[off+1]<<8)|((uint32_t)v.bytes[off+2]<<16)|((uint32_t)v.bytes[off+3]<<24);
  int32_t r; memcpy(&r,&u,sizeof r); return r;
}
static bool dropped_owner(qa_q3_equipment_qvm_pickup_eligibility c,bool lithium){
  int32_t mode=word(c.call_words,0);
  bool alternate=lithium&&word(c.call_words,12)!=0;
  return (mode!=10&&!alternate)||word(c.item,164)!=2||word(c.item,168)!=word(c.player,140);
}
static bool always_true(void*ctx,qa_q3_equipment_qvm_pickup_eligibility c){ (void)ctx;(void)c; return true; }
static bool no_lithium(void*ctx,qa_q3_equipment_qvm_pickup_eligibility c){ (void)ctx; return dropped_owner(c,false); }
static bool with_lithium(void*ctx,qa_q3_equipment_qvm_pickup_eligibility c){ (void)ctx; return dropped_owner(c,true); }
static const double OBJ[]={8};
static const double S_GATE[]={104007}, S_TARGETS[]={104340}, S_G1[]={104107}, S_G2[]={104091};
static const qa_q3_equipment_qvm_pickup_grant STOCK_GRANTS[]={
  {3,103594,S_G1,1,false,103658,0,0,0,false,0,0,{0,0,NULL,0,false,0},NULL,always_true},
  {2,103220,S_G2,1,false,103265,0,0,0,false,0,0,{0,0,NULL,0,false,0},NULL,always_true},
};
static const double T_GATE[]={168697,168873}, T_TARGETS[]={169404}, T_G1[]={169094}, T_G2[]={169078}, T_G3[]={169062};
static const qa_q3_equipment_qvm_pickup_grant THREEWAVE_GRANTS[]={
  {3,167408,T_G1,1,false,167814,0,0,0,false,0,0,{0,0,NULL,0,false,0},NULL,no_lithium},
  {2,166848,T_G2,1,true,0,166875,166893,20,false,0,0,{0,0,NULL,0,false,0},NULL,with_lithium},
  {1,166897,T_G3,1,true,0,167099,167173,20,true,204,376,{166958,167099,NULL,0,true,20},NULL,with_lithium},
};
bool qa_q3_native_pickup_profile(const qa_q3_equipment_qvm_artifact *artifact,
    qa_q3_equipment_qvm_pickup_profile *out){
  const char *digest=artifact->module.digest?artifact->module.digest:"";
  bool stock=strcmp(digest,qa_stock_q3_pickup_digest)==0;
  bool threewave=strcmp(digest,qa_threewave_grapple_digest)==0;
  if(!stock&&!threewave) return false;
  *out=(qa_q3_equipment_qvm_pickup_profile){0};
  out->module=artifact->module; out->abi_profile="q3-modern";
  out->field_inuse=520; out->field_client=516; out->field_health=732;
  out->field_item=804; out->field_count=760; out->field_flags=536;
  out->dropped_flag=4096; out->objective_types=OBJ; out->objective_type_count=1;
  if(stock){
    out->entity_stride=808; out->client_stride=776; out->touch=103974;
    out->gate_entry=65953; out->gate_calls=S_GATE; out->gate_call_count=1;
    out->gate_item_argument=1; out->gate_player_argument=2;
    out->targets_entry=129114; out->targets_calls=S_TARGETS; out->targets_call_count=1; out->free_entry=129805;
    out->items=(qa_q3_equipment_qvm_item_layout){2552,36,52,0,28,36,40,1,2};
    out->grants=STOCK_GRANTS; out->grant_count=2; return true;
  }
  out->entity_stride=876; out->client_stride=944; out->touch=168574;
  out->gate_entry=20482; out->gate_calls=T_GATE; out->gate_call_count=2;
  out->gate_item_argument=1; out->gate_player_argument=2;
  out->targets_entry=210519; out->targets_calls=T_TARGETS; out->targets_call_count=1; out->free_entry=211210;
  out->items=(qa_q3_equipment_qvm_item_layout){5304,50,52,0,28,36,40,1,2};
  out->grants=THREEWAVE_GRANTS; out->grant_count=3; return true;
}
