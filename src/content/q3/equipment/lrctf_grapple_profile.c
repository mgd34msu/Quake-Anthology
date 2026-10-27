/* Port of src/content/q3/equipment/lrctf-grapple-profile.ts. GPL-2.0-or-later. */
#include "qa/content/q3/equipment/lrctf_grapple_profile.h"
#include <math.h>
#include <string.h>
#include "qa/foundation/support.h"

const char qa_lrctf_grapple_digest[] = "sha256:b9e396cf5ed2b913548cd92e2b0886ad5992653c8903fa3f9ed0b1f4167ca43e";
static double shared_entity_bytes(const char *abi){ return strcmp(abi,"q3-modern")==0?516:504; }
static double player_state_bytes(const char *abi){ return strcmp(abi,"q3-modern")==0?468:444; }
static void check_offset(double v,double minimum,double maximum){
  if(v!=trunc(v)||v<minimum||(int64_t)v%4!=0||v>maximum-4) qa_range_error("field is outside its source record");
}
static void check_global(double v,double bytes,const qa_q3_equipment_qvm_artifact *a){
  if(v!=trunc(v)||v<4||(int64_t)v%4!=0||v+bytes>a->image.data_length+a->image.literal_length+a->image.bss_length)
    qa_range_error("global is outside declared source memory");
}
static void check_callback(double v,const qa_q3_equipment_qvm_artifact *a){
  if(v!=trunc(v)||v<1||v>=(double)a->image.instruction_count
    ||a->image.instructions[(size_t)v].opcode!=QA_Q3_EQUIPMENT_OP_ENTER)
    qa_range_error("callback is not a source function entry");
}
struct qa_qvm_grapple_definition qa_q3_equipment_read_grapple_profile(
    struct qa_qvm_grapple_definition d, const qa_q3_equipment_qvm_artifact *artifact){
  const char *abi=artifact->abi_profile?artifact->abi_profile:"q3-modern";
  if(!d.abi_profile||strcmp(d.abi_profile,abi)!=0) qa_range_error("abiProfile does not match artifact");
  if(!artifact->role||strcmp(artifact->role,"qagame")!=0) qa_range_error("grapple callbacks require a qagame module");
  double es=shared_entity_bytes(abi), ps=player_state_bytes(abi);
  if(d.entity_stride<es||d.client_stride<ps||(int64_t)d.entity_stride%4!=0||(int64_t)d.client_stride%4!=0
    ||(d.entity_stride>d.client_stride?d.entity_stride:d.client_stride)>artifact->image.allocated_data_length)
    qa_range_error("unaligned or oversized source records");
  check_offset(d.fields.inuse,es,d.entity_stride); check_offset(d.fields.client,es,d.entity_stride);
  check_offset(d.fields.parent,es,d.entity_stride); check_offset(d.fields.target,es,d.entity_stride);
  if(d.fields.has_mover) check_offset(d.fields.mover,es,d.entity_stride);
  check_offset(d.fields.health,es,d.entity_stride); check_offset(d.fields.takedamage,es,d.entity_stride);
  check_offset(d.fields.event_time,es,d.entity_stride); check_offset(d.fields.free_after_event,es,d.entity_stride);
  check_offset(d.fields.hook,ps,d.client_stride);
  check_global(d.globals.time,4,artifact); check_global(d.globals.frame,4,artifact);
  check_global(d.globals.movement,4,artifact); check_global(d.globals.forward,12,artifact);
  check_global(d.globals.ground_plane,4,artifact);
  check_callback(d.callbacks.allocate,artifact); check_callback(d.callbacks.free,artifact);
  check_callback(d.callbacks.fire,artifact); check_callback(d.callbacks.release,artifact);
  check_callback(d.callbacks.force_release,artifact); check_callback(d.callbacks.missile,artifact);
  if(d.callbacks.has_follow) check_callback(d.callbacks.follow,artifact);
  check_callback(d.callbacks.think,artifact); check_callback(d.callbacks.pull,artifact);
  if(d.callbacks.has_move_mover_hooks) check_callback(d.callbacks.move_mover_hooks,artifact);
  check_callback(d.callbacks.damage,artifact); check_callback(d.callbacks.same_team,artifact);
  check_callback(d.callbacks.player_move,artifact);
  int64_t flag=(int64_t)d.pulling_flag;
  if(flag<1||flag>0x40000000||(flag&(flag-1))!=0) qa_range_error("expected one player movement flag");
  for(size_t i=0;i<d.movement.word_count;i++){
    double off=d.movement.words[i].offset;
    if(off<4||(int64_t)off%4!=0||off+4>d.movement.byte_length)
      qa_range_error("Movement field is outside the declared source record");
  }
  d.module=artifact->module;
  return d;
}
bool qa_lrctf_grapple_profile(const qa_q3_equipment_qvm_artifact *artifact,
    struct qa_qvm_grapple_definition *out){
  if(!artifact->module.digest||strcmp(artifact->module.digest,qa_lrctf_grapple_digest)!=0) return false;
  struct qa_qvm_grapple_definition d={0};
  d.id="lrctf-1.2"; d.title="LRCTF (Quake 3)"; d.module=artifact->module; d.abi_profile="q3-modern";
  d.entity_stride=856; d.client_stride=872;
  d.fields=(qa_qvm_grapple_fields){520,516,600,784,true,836,840,748,752,552,556};
  d.globals=(qa_qvm_grapple_globals){998864,998860,1008980,1008836,1008884};
  d.callbacks=(qa_qvm_grapple_callbacks){178797,179014,183076,183171,183171,151164,true,183577,5362,15874,true,184569,140358,169306,22369};
  d.pulling_flag=2048; d.fire_arguments=(qa_double_view){NULL,0};
  d.movement=(qa_qvm_grapple_movement){16,NULL,0};
  d.initial_cvars=NULL; d.initial_cvar_count=0;
  d.event_lifetime_milliseconds=300; d.grapple_damage_method=23;
  d.presentation.projectile_model="models/weapons3/hook/hook1.md3";
  d.presentation.view_model="models/weapons3/hook/bit1.md3"; d.presentation.weapon_index=10;
  d.presentation.view_anchor=(qa_qvm_grapple_view_anchor){"models/weapons2/shotgun/shotgun_hand.md3","tag_weapon",{0,0,0},90,-0.2};
  d.presentation.view_attachments=NULL; d.presentation.view_attachment_count=0;
  d.presentation.cable=(qa_qvm_grapple_cable){true,"grapplerope",16,NULL,NULL,NULL,0};
  d.presentation.fire_sound="sound/grapple/midevil/grfire.wav";
  d.presentation.attach_sound="sound/grapple/midevil/grhit.wav";
  *out=qa_q3_equipment_read_grapple_profile(d,artifact);
  return true;
}
