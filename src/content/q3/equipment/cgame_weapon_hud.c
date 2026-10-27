/* Port of src/content/q3/equipment/cgame-weapon-hud.ts. GPL-2.0-or-later. */
#include "qa/content/q3/equipment/cgame_weapon_hud.h"
#include <string.h>
static const double FUNCTIONS[]={24534,24736};
static const qa_q3_equipment_hud_ammo AMMO1[]={{10141,10228},{10402,10525}};
static const qa_q3_equipment_hud_ammo AMMO2[]={{14011,14098},{14833,14960}};
static const qa_q3_equipment_hud_region REGIONS[]={
  {10083,10087,true,AMMO1,2},
  {13793,13797,true,AMMO2,2},
};
bool qa_q3_equipment_presentation_profile(const qa_q3_equipment_qvm_artifact *artifact,
    qa_q3_equipment_qvm_presentation_profile *out){
  if(!artifact->role||strcmp(artifact->role,"cgame")!=0) return false;
  const char *digest=artifact->module.digest?artifact->module.digest:"";
  *out=(qa_q3_equipment_qvm_presentation_profile){0};
  if(strcmp(digest,"sha256:a4744482c9b93852cc71f4d7ce03b3e4337e5d89844d27d272c2c16d74df07fa")==0){
    out->warning_entry=22881; out->warning_state=1084512; out->warning_none=0; out->warning_low=1; out->warning_empty=2;
    out->status_is_regions=false; out->status_functions=FUNCTIONS; out->status_function_count=2;
    out->held_entry=106742; out->held_gun=28; out->held_parent_argument=0; out->held_state_argument=1;
    out->held_entity_argument=2; out->held_entity_number_offset=0;
    out->hud=108261; out->view_entry=107738; out->view_decision=107828; out->view_taken=true; return true;
  }
  if(strcmp(digest,"sha256:14858804fb98609ed8b3b3c3b825f0a7cb544063f7e43735c884cd5e4a51157c")==0){
    out->warning_entry=26123; out->warning_state=1012656; out->warning_none=0; out->warning_low=1; out->warning_empty=2;
    out->status_is_regions=true; out->status_regions=REGIONS; out->status_region_count=2;
    out->held_entry=103468; out->held_gun=184; out->held_parent_argument=0; out->held_state_argument=1;
    out->held_entity_argument=2; out->held_entity_number_offset=0;
    out->hud=106815; out->view_entry=105885; out->view_decision=105975; out->view_taken=true; return true;
  }
  return false;
}
