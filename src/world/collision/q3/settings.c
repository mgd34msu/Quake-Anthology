/* Port of src/world/collision/q3/settings.ts. GPL-2.0-or-later. */
#include "qa/world/collision/q3/settings.h"
#include "qa/foundation/support.h"
const qa_collision_cvar_definition qa_collision_map_cvar_definitions[3]={
  {"cm_noAreas","0",QA_CVAR_FLAG_CHEAT},
  {"cm_noCurves","0",QA_CVAR_FLAG_CHEAT},
  {"cm_playerCurveClip","1",QA_CVAR_FLAG_ARCHIVE|QA_CVAR_FLAG_CHEAT},
};
void qa_collision_map_settings_init(qa_collision_map_settings*s,qa_cvar_registry*cvars){s->cvars=cvars;}
void qa_collision_map_settings_register_map(qa_collision_map_settings*s){
  for(int i=0;i<3;i++){qa_cvar_snapshot out={0};qa_cvar_registry_register(s->cvars,qa_collision_map_cvar_definitions[i].name,qa_collision_map_cvar_definitions[i].value,qa_collision_map_cvar_definitions[i].flags,&out);qa_cvar_read_free(&out);}
}
static bool enabled(qa_collision_map_settings*s,const char*name){
  qa_cvar_snapshot v={0};
  if(!qa_cvar_registry_get(s->cvars,name,&v))qa_range_error("Collision map cvar %s is not registered",name);
  bool r=v.integer_value!=0; qa_cvar_read_free(&v); return r;
}
bool qa_collision_map_settings_no_areas(qa_collision_map_settings*s){return enabled(s,"cm_noAreas");}
bool qa_collision_map_settings_no_curves(qa_collision_map_settings*s){return enabled(s,"cm_noCurves");}
bool qa_collision_map_settings_player_curve_clip(qa_collision_map_settings*s){return enabled(s,"cm_playerCurveClip");}
