/* Port of src/content/q3/foundation/presentation.ts. GPL-2.0-or-later. */
#include "qa/content/q3/foundation/presentation.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/foundation/animation.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include "qa/render/scene/models/transform.h"
#include "qa/foundation/js.h"

static const qa_vec3 ZERO={0,0,0}, UNIT={1,1,1};
static void *xrealloc(void*p,size_t n){ void*q=realloc(p,n?n:1); if(!q) qa_range_error("out of memory"); return q; }
void qa_q3_character_presenter_init(qa_q3_character_presenter *p, const qa_q3_character_assets *assets){
  *p=(qa_q3_character_presenter){0}; p->pose=qa_q3_create_player_pose_state(); p->assets=assets;
  const qa_q3_character_part *parts[3]={&assets->lower,&assets->upper,&assets->head};
  for(int i=0;i<3;i++){
    p->skin_entries[i]=xrealloc(NULL,(parts[i]->surface_count?parts[i]->surface_count:1)*sizeof *p->skin_entries[i]);
    for(size_t j=0;j<parts[i]->surface_count;j++)
      p->skin_entries[i][j]=(qa_model_custom_skin_entry){parts[i]->surfaces[j].name,parts[i]->surfaces[j].shader};
  }
}
void qa_q3_character_presenter_destroy(qa_q3_character_presenter *p){
  if(!p) return;
  for(int i=0;i<3;i++) free(p->skin_entries[i]);
  free(p->weapon_entities); free(p->passes); free(p->option_states);
}
void qa_q3_character_presenter_reset(qa_q3_character_presenter *p, qa_q3_character_view view, double time_ms){
  qa_q3_animation_set set=qa_q3_animation_set_from_config(&p->assets->animation);
  qa_q3_clear_lerp_frame(set,&p->pose.legs.base,view.animation.legs,time_ms,NULL,NULL);
  qa_q3_clear_lerp_frame(set,&p->pose.torso.base,view.animation.torso,time_ms,NULL,NULL);
  p->pose.legs.base=qa_q3_create_lerp_frame(); p->pose.legs.yaw_angle=view.angles.y;
  p->pose.legs.yawing=false; p->pose.legs.pitch_angle=0; p->pose.legs.pitching=false;
  p->pose.torso.base=qa_q3_create_lerp_frame(); p->pose.torso.yaw_angle=view.angles.y;
  p->pose.torso.yawing=false; p->pose.torso.pitch_angle=view.angles.x; p->pose.torso.pitching=false;
}
static qa_model_source_options pass_options(void *ctx, const qa_scene_entity *entity){
  struct qa_q3_character_pass_option_state *st=ctx;
  const qa_q3_character_presenter *p=st->presenter;
  qa_model_source_options o={0};
  const qa_q3_character_part *parts[3]={&p->assets->lower,&p->assets->upper,&p->assets->head};
  for(int i=0;i<3;i++){
    if(parts[i]->resource.id&&entity->resource.id&&strcmp(parts[i]->resource.id,entity->resource.id)==0){
      o.custom_skin=p->skin_entries[i]; o.custom_skin_count=parts[i]->surface_count; break;
    }
  }
  if(st->shader) o.custom_shader=st->shader;
  return o;
}
static void ensure_passes(qa_q3_character_presenter*p,size_t n){
  if(n<=p->pass_cap) return;
  p->passes=xrealloc(p->passes,n*sizeof *p->passes);
  p->option_states=xrealloc(p->option_states,n*sizeof *p->option_states);
  p->pass_cap=n; p->option_state_cap=n;
}
const qa_q3_character_pass *qa_q3_character_presenter_frame(qa_q3_character_presenter *p,
    qa_q3_character_view view, qa_q3_character_render_options options, size_t *out_count){
  *out_count=0;
  if((qa_js_to_int32(view.source_flags)&0x80)!=0) return p->passes;
  qa_q3_calculate_player_pose_input pose_in={0};
  pose_in.entity=(qa_q3_pose_entity_state){view.source_flags,view.velocity,view.movement_direction,view.animation.legs,view.animation.torso};
  pose_in.fixed_legs=p->assets->animation.fixed_legs; pose_in.fixed_torso=p->assets->animation.fixed_torso;
  pose_in.lerp_angles=view.angles; pose_in.time_ms=options.time_milliseconds;
  pose_in.frame_time_ms=options.frame_milliseconds; pose_in.swing_speed=options.swing_speed;
  qa_q3_player_pose axes=qa_q3_calculate_player_pose(&p->pose,pose_in);
  double speed_scale=(qa_js_to_int32(view.powerups)&(1<<QA_Q3_PW_HASTE))?1.5:1;
  double legs_animation=(p->pose.legs.yawing&&(qa_js_to_int32(view.animation.legs)&~QA_Q3_ANIMATION_TOGGLE_BIT)==QA_Q3_ANIM_LEGS_IDLE)
    ?QA_Q3_ANIM_LEGS_TURN:view.animation.legs;
  qa_q3_animation_set set=qa_q3_animation_set_from_config(&p->assets->animation);
  qa_q3_run_lerp_frame(set,&p->pose.legs.base,(qa_q3_run_lerp_frame_input){options.time_milliseconds,legs_animation,speed_scale,options.no_player_animations},NULL,NULL);
  qa_q3_run_lerp_frame(set,&p->pose.torso.base,(qa_q3_run_lerp_frame_input){options.time_milliseconds,view.animation.torso,speed_scale,options.no_player_animations},NULL,NULL);
  double flags=(double)(0x80|(options.personal_model?2:0)|(options.has_shadow_plane?0x40:0));
  p->actor_storage=view.actor;
  const qa_q3_character_part *assets3[3]={&p->assets->lower,&p->assets->upper,&p->assets->head};
  qa_axis axes3[3]={axes.legs,axes.torso,axes.head};
  qa_q3_pose_lerp_frame *frames[3]={&p->pose.legs,&p->pose.torso,NULL};
  /* Build head=2, torso=1, legs=0 like TS part() order. */
  for(int idx=2;idx>=0;idx--){
    const qa_q3_character_part *asset=assets3[idx];
    qa_scene_entity e={0};
    e.has_opacity=true; e.opacity=view.has_opacity?view.opacity:1;
    e.actor=&p->actor_storage; e.resource=asset->resource;
    e.model.kind=QA_DECODED_MODEL_Q3_MESH; e.model.as.q3_mesh=&asset->scene.model;
    qa_vec3 origin=idx==0?view.origin:ZERO;
    double sc=view.has_scale?view.scale:1;
    e.transform.origin=origin; e.transform.axis=axes3[idx];
    e.transform.scale=idx==0?(qa_vec3){sc,sc,sc}:UNIT;
    e.previous_origin=origin;
    e.pose.is_frame=true;
    e.pose.frame=frames[idx]?frames[idx]->base.frame:0;
    e.pose.previous_frame=frames[idx]?frames[idx]->base.old_frame:0;
    e.pose.back_lerp=frames[idx]?frames[idx]->base.back_lerp:0;
    e.skin=0; e.color=view.color; e.shader_time=options.shader_time;
    e.flags_kind="q3"; e.flags_bits=flags; e.lighting_origin=view.origin;
    e.shadow_plane=options.has_shadow_plane?options.shadow_plane:0;
    p->entities[idx]=e;
  }
  p->torso_attachments[0]=(qa_scene_entity_attachment){"tag_head",&p->entities[2]};
  p->entities[1].attachments=p->torso_attachments; p->entities[1].attachment_count=1;
  p->legs_attachments[0]=(qa_scene_entity_attachment){"tag_torso",&p->entities[1]};
  p->entities[0].attachments=p->legs_attachments; p->entities[0].attachment_count=1;
  size_t max_passes=4+options.weapon_count; ensure_passes(p,max_passes);
  size_t count=0;
  const char *wanted[4]; size_t wanted_count=0;
  int32_t pw=qa_js_to_int32(view.powerups);
  if(pw&(1<<QA_Q3_PW_INVIS)) wanted[wanted_count++]="powerups/invisibility";
  else {
    wanted[wanted_count++]=NULL;
    if(pw&(1<<QA_Q3_PW_QUAD)) wanted[wanted_count++]=view.team&&strcmp(view.team,"red")==0?"powerups/blueflag":"powerups/quad";
    if((pw&(1<<QA_Q3_PW_REGEN))&&qa_js_to_int32(trunc(options.time_milliseconds/100))%10==1) wanted[wanted_count++]="powerups/regen";
    if(pw&(1<<QA_Q3_PW_BATTLESUIT)) wanted[wanted_count++]="powerups/battleSuit";
  }
  for(size_t i=0;i<wanted_count;i++){
    p->option_states[count]=(struct qa_q3_character_pass_option_state){p,wanted[i]};
    p->passes[count]=(qa_q3_character_pass){NULL,p->entities[0],wanted[i],&p->option_states[count],pass_options};
    count++;
  }
  qa_model_attachment_tag_result torso_tag;
  if(qa_model_attachment_tag(&p->entities[0],"tag_torso",&torso_tag)){
    qa_scene_entity world_torso=qa_attach_scene_entity(&p->entities[0],&p->entities[1],torso_tag);
    qa_model_attachment_tag_result weapon_tag;
    if(qa_model_attachment_tag(&world_torso,"tag_weapon",&weapon_tag)){
      if(options.weapon_count>p->weapon_entity_cap){
        p->weapon_entities=xrealloc(p->weapon_entities,options.weapon_count*sizeof *p->weapon_entities);
        p->weapon_entity_cap=options.weapon_count;
      }
      for(size_t i=0;i<options.weapon_count;i++){
        p->weapon_entities[i]=qa_attach_scene_entity(&world_torso,&options.weapon[i].entity,weapon_tag);
        qa_q3_character_pass pass=options.weapon[i]; pass.entity=p->weapon_entities[i];
        p->passes[count++]=pass;
      }
    }
  }
  *out_count=count; return p->passes;
}
