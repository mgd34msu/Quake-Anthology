/* Port of src/content/q3/equipment/weapon-profile.ts. GPL-2.0-or-later. */
#include "qa/content/q3/equipment/weapon_profile.h"
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/equipment/threewave_grapple_profile.h"
#include "qa/foundation/support.h"
static const char *const TEAM_RED_ARGS[]={"team","red"};
static const char *const TEAM_BLUE_ARGS[]={"team","blue"};
static const char *const TEAM_FREE_ARGS[]={"team","free"};
static const qa_source_team_command_entry TEAMS[]={
  {{"q3:1","team:red"},{TEAM_RED_ARGS,2}},
  {{"q3:2","team:blue"},{TEAM_BLUE_ARGS,2}},
  {{NULL,NULL},{TEAM_FREE_ARGS,2}},
};
static const double EXCLUDED[]={1,2,4,7,8};
static const qa_source_equipment_context CONTEXTS[]={{"q2:equipment/hand-grenades",NULL}};
static const double DELAY_INPUTS[]={12};
static const qa_qvm_weapon_predicate PREDICATES[]={{34044,false}};
static const qa_qvm_item_test SETTLED[]={
  {{"client",44},false,0,"at-most",0},
  {{"client",148},false,0,"equals",0},
};
static const qa_qvm_item_test ACCEPTED[]={{{"client",148},false,0,"equals",2}};
/* dispatcher.actor.pointer (TS QvmModInputPointer global form) retained in
 * the landed qa_qvm_weapon_actor shape; see contracts/qvm_mod_callbacks. */
bool qa_q3_primary_weapon_profile(const qa_q3_equipment_qvm_artifact *artifact,
    const qa_q3_guest_weapon *weapons, size_t weapon_count,
    qa_q3_equipment_qvm_primary_weapon_profile *out){
  if(!artifact->module.digest||strcmp(artifact->module.digest,qa_threewave_grapple_digest)!=0) return false;
  *out=(qa_q3_equipment_qvm_primary_weapon_profile){0};
  out->module=artifact->module; out->has_match=true;
  out->match=(qa_source_primary_match){248,TEAMS,3};
  out->abi_profile="q3-modern"; out->entity_stride=876; out->client_stride=944; out->client_pointer=516;
  out->max_health=220; out->persistent_max_health=548;
  out->equipment_movement=(qa_q3_equipment_qvm_equipment_movement_profile){35535,34707,32561,1091860,35397,35503,180,192,true,224,28};
  out->availability_movement_type=4; out->availability_excluded=EXCLUDED; out->availability_excluded_count=5;
  out->availability_health=184; out->availability_team=260; out->availability_spectator_team=3;
  out->availability_flags=12; out->availability_respawn_flag=512;
  out->powerup_quad=312; out->powerup_haste=320; out->powerup_flight=332;
  out->torso_animation_entry=27646; out->torso_animation_attack=7; out->torso_animation_melee=8;
  out->water_level_entity_offset=788; out->water_level_movement_offset=208;
  out->drop_entry=158347; out->drop_argument=0; out->drop_weapon=192; out->drop_ammo=376;
  out->drop_region_entry=158571; out->drop_region_join=158674;
  out->give_entry=139391; out->give_argument=0; out->give_weapons=139524; out->give_ammo=139574;
  out->give_named_entry=139936; out->give_named_join=139947; out->give_named_name=24; out->give_named_item=36;
  out->damage_factor_entry=217003; out->damage_factor_result=1616724;
  out->damage_factor_stop_entry=217081; out->damage_factor_stop_join=217113;
  out->equipment_contexts=CONTEXTS; out->equipment_context_count=1;
  out->delay=(qa_q3_equipment_qvm_region_evaluation){34318,34350,DELAY_INPUTS,1,true,12};
  out->delay_player_movement_global=1091860; out->delay_player_player_offset=0;
  out->teleport_entry=118339; out->teleport_spawn=127849; out->teleport_view=128463;
  out->teleport_region=(qa_q3_equipment_qvm_region_evaluation){118552,118726,NULL,0,false,0};
  out->teleport_objectives=(qa_q3_equipment_qvm_region_evaluation){118552,118701,NULL,0,false,0};
  qa_qvm_weapon_selection_value *values=malloc((weapon_count?weapon_count:1)*sizeof *values);
  if(!values) qa_range_error("out of memory");
  for(size_t i=0;i<weapon_count;i++) values[i]=(qa_qvm_weapon_selection_value){weapons[i].weapon,weapons[i].item};
  out->owned_selection_values=values;
  out->stage.dispatcher.entry=33648;
  out->stage.dispatcher.actor.record="client";
  static const double indirections[]={0};
  out->stage.dispatcher.actor.pointer=(qa_qvm_mod_input_pointer){true,0,1091860,{indirections,1},0};
  out->stage.predicates=PREDICATES; out->stage.predicate_count=1;
  out->stage.settled=SETTLED; out->stage.settled_count=2;
  out->stage.selection.field=(qa_qvm_item_field){"client",144};
  out->stage.selection.values=values; out->stage.selection.value_count=weapon_count;
  out->stage.request.entry=33226; out->stage.request.argument=0;
  out->stage.request.accepted=ACCEPTED; out->stage.request.accepted_count=1;
  return true;
}
void qa_q3_equipment_qvm_primary_weapon_profile_destroy(qa_q3_equipment_qvm_primary_weapon_profile *p){
  if(!p) return;
  free(p->owned_selection_values); p->owned_selection_values=NULL;
}
