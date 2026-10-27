/* Port of src/content/q3/equipment/combat-profile.ts. GPL-2.0-or-later. */
#include "qa/content/q3/equipment/combat_profile.h"
#include <string.h>
#include "qa/content/q3/equipment/grapple_profiles.h"
#include "qa/content/q3/equipment/threewave_grapple_profile.h"
static const qa_qvm_combat_role_index ARMOR_ROLES[]={{"target",0},{"amount",1},{"flags",2}};
static const qa_qvm_combat_role_index DAMAGE_ROLES[]={{"target",0},{"inflictor",1},{"attacker",2},{"direction",3},{"point",4},{"amount",5},{"flags",6},{"method",7}};
static const qa_qvm_combat_team TEAMS[]={{1,"q3:1"},{2,"q3:2"}};
static const qa_q3_equipment_armor_when WHEN[]={{107944,"equal",10},{1089728,"not-equal",0}};
static qa_q3_equipment_qvm_game_armor_definition lrctf_armor(void){
  qa_q3_equipment_qvm_game_armor_definition a={0}; a.check_armor=139839;
  a.call=(qa_qvm_combat_call){ARMOR_ROLES,3,NULL,0}; a.points_stat=3; a.protection=(float)0.66; return a;
}
static qa_q3_equipment_qvm_game_armor_definition threewave_armor(void){
  qa_q3_equipment_qvm_game_armor_definition a={0}; a.check_armor=161690;
  a.call=(qa_qvm_combat_call){ARMOR_ROLES,3,NULL,0}; a.points_stat=6; a.protection=(float)0.66;
  a.has_tiers=true; a.tiers_stat=3; a.when_any=WHEN; a.when_any_count=2;
  static const qa_q3_equipment_armor_tier values[3]={{0,0.3f},{1,0.6f},{2,0.8f}};
  /* fround literals: 0.3f etc. are the binary32 values Math.fround produces. */
  a.values=values; a.value_count=3; a.fallback=(float)0.3; return a;
}
bool qa_q3_native_combat_profile(const qa_q3_equipment_qvm_artifact *artifact,
    qa_q3_equipment_qvm_primary_combat_profile *out){
  struct qa_qvm_grapple_definition source;
  if(!qa_q3_grapple_profile(artifact,&source)) return false;
  *out=(qa_q3_equipment_qvm_primary_combat_profile){0};
  out->grapple=source;
  out->damage_call=(qa_qvm_combat_call){DAMAGE_ROLES,8,NULL,0};
  out->state.health_stat=0; out->state.team_persistent_stat=3;
  out->state.team_values=TEAMS; out->state.team_value_count=2;
  out->state.flag_notarget=32; out->state.flag_invulnerable=16; out->state.flag_no_knockback=2048;
  out->state.mass=(qa_qvm_combat_mass){true,200,0,NULL};
  out->damage_flags=(qa_qvm_damage_flags){1,2,4,8,16};
  out->reactions.pain_call=(qa_qvm_reaction_call){3,0,2};
  out->reactions.die_call=(qa_qvm_reaction_call){5,0,3};
  if(artifact->module.digest&&strcmp(artifact->module.digest,qa_lrctf_grapple_digest)==0){
    out->armor=lrctf_armor(); out->reactions.flags=536; out->reactions.pain=728; out->reactions.die=732; return true;
  }
  if(artifact->module.digest&&strcmp(artifact->module.digest,qa_threewave_grapple_digest)==0){
    out->armor=threewave_armor(); out->reactions.flags=536; out->reactions.pain=712; out->reactions.die=716; return true;
  }
  return false;
}
