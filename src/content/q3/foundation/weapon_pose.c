/* Port of src/content/q3/foundation/weapon-pose.ts. GPL-2.0-or-later. */
#include "qa/content/q3/foundation/weapon_pose.h"
#include <math.h>
#include <stdint.h>
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
static float f(double v){ return (float)v; }
qa_q3_weapon_view_pose_result qa_q3_weapon_view_pose(qa_q3_weapon_view_motion in){
  /* TS bobCycle is a number; `& 1` is ToInt32. */
  double scale=((int32_t)in.bob_cycle&1)?-in.horizontal_speed:in.horizontal_speed;
  double roll=f(f(scale*in.bob_fraction_sine)*f(0.005));
  double yaw=f(f(scale*in.bob_fraction_sine)*f(0.01));
  double pitch=f(f(in.horizontal_speed*in.bob_fraction_sine)*f(0.005));
  qa_vec3 origin=in.origin;
  qa_vec3 angles=qa_add3(in.angles,qa_math_vec3(pitch,yaw,roll));
  int32_t delta=(int32_t)((uint32_t)(int32_t)in.time_milliseconds-(uint32_t)(int32_t)in.land_time);
  if(delta<150) origin=qa_add3(origin,qa_math_vec3(0,0,f(f(f(in.land_change*f(0.25))*f((double)delta))/150.0)));
  else if(delta<450) origin=qa_add3(origin,qa_math_vec3(0,0,f(f(f(in.land_change*f(0.25))*f((double)(int32_t)(450-delta)))/300.0)));
  double drift=f(f(f(in.horizontal_speed+40)*f(sin((double)f(f(in.time_milliseconds)*f(0.001)))))*f(0.01));
  angles=qa_add3(angles,qa_math_vec3(drift,drift,drift));
  return (qa_q3_weapon_view_pose_result){origin,angles};
}
double qa_q3_torso_weapon_frame(const qa_q3_player_animation_config *config, double frame){
  static const int indices[3]={QA_Q3_ANIM_TORSO_DROP,QA_Q3_ANIM_TORSO_ATTACK,QA_Q3_ANIM_TORSO_ATTACK2};
  for(int k=0;k<3;k++){
    int index=indices[k];
    if(!config->animation_present[index]) qa_range_error("Missing Q3 weapon torso animation %d",index);
    const qa_q3_animation *a=&config->animations[index];
    bool drop=index==QA_Q3_ANIM_TORSO_DROP;
    if(frame>=a->first_frame&&frame<a->first_frame+(drop?9:6)) return frame-a->first_frame+(drop?6:1);
  }
  return 0;
}
void qa_q3_weapon_barrel_init(qa_q3_weapon_barrel *b){ b->time=0; b->angle=0; b->spinning=false; }
qa_q3_weapon_barrel_step_result qa_q3_weapon_barrel_step(qa_q3_weapon_barrel *b, double time_ms, bool firing){
  int32_t delta=(int32_t)((uint32_t)(int32_t)time_ms-(uint32_t)(int32_t)b->time);
  double angle;
  if(b->spinning) angle=f(b->angle+f(f((double)delta)*f(0.9)));
  else {
    if(delta>1000) delta=1000;
    double speed=f(f(0.5)*f(f(0.9)+f(f((double)(int32_t)(1000-delta))/1000.0)));
    angle=f(b->angle+f(f((double)delta)*speed));
  }
  bool stopped=b->spinning&&!firing;
  if(b->spinning!=firing){ b->time=time_ms; b->angle=qa_qvm_angle_mod(angle); b->spinning=firing; }
  return (qa_q3_weapon_barrel_step_result){angle,stopped};
}
