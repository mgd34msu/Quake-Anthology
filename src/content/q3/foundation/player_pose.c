/* Port of src/content/q3/foundation/player-pose.ts. GPL-2.0-or-later. */
#include "qa/content/q3/foundation/player_pose.h"
#include <math.h>
#include <stdint.h>
#include "qa/core/common_error.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include "qa/foundation/js.h"

static float f32(double v){ return (float)v; }
static qa_q3_pose_lerp_frame create_pose_frame(void){ qa_q3_pose_lerp_frame r={0}; r.base=qa_q3_create_lerp_frame(); return r; }
qa_q3_player_pose_state qa_q3_create_player_pose_state(void){
  qa_q3_player_pose_state s={0}; s.legs=create_pose_frame(); s.torso=create_pose_frame(); return s;
}
static double float32_checked(double v,const char*name){
  float r=f32(v); if(!isfinite(r)) qa_range_error("%s must be a finite float32 value",name); return r;
}
static double ms_checked(double v,const char*name,bool allow_negative){
  if(v!=trunc(v)||v<-2147483648.0||v>2147483647.0||(!allow_negative&&v<0))
    qa_range_error("%s must be %s millisecond value",name,allow_negative?"an int32":"a non-negative int32");
  return v;
}
static double angle_subtract(double a,double b){
  float angle=f32(a-b);
  /* At large magnitudes angle - 360 rounds back to angle in binary32 and the source loop never ends. JS % is fmod. */
  if(fabs(angle)>0x1000000) angle=f32(fmod(angle,360));
  while(angle>180) angle=f32(angle-360);
  while(angle<-180) angle=f32(angle+360);
  return angle;
}
qa_q3_swing_angles_result qa_q3_swing_angles(qa_q3_swing_angles_input input){
  double destination=float32_checked(input.destination,"swing destination");
  double swing_tol=float32_checked(input.swing_tolerance,"swing tolerance");
  double clamp_tol=float32_checked(input.clamp_tolerance,"clamp tolerance");
  double speed=float32_checked(input.speed,"swing speed");
  double frame_time=ms_checked(input.frame_time_ms,"frame time",false);
  double angle=float32_checked(input.angle,"swing angle");
  bool swinging=input.swinging;
  if(swing_tol<0||clamp_tol<1||speed<0) qa_range_error("swing tolerances and speed are outside source ranges");
  if(!swinging){ double s=angle_subtract(angle,destination); if(s>swing_tol||s<-swing_tol) swinging=true; }
  if(!swinging) return (qa_q3_swing_angles_result){angle,false};
  double swing=angle_subtract(destination,angle);
  double distance=fabs(swing);
  double scale=distance<f32(swing_tol*0.5)?0.5:distance<swing_tol?1:2;
  if(swing>=0){
    double move=f32(f32(frame_time*scale)*speed);
    if(move>=swing){ move=swing; swinging=false; }
    angle=qa_qvm_angle_mod(f32(angle+move));
  } else {
    double move=f32(f32(frame_time*scale)*f32(-speed));
    if(move<=swing){ move=swing; swinging=false; }
    angle=qa_qvm_angle_mod(f32(angle+move));
  }
  swing=angle_subtract(destination,angle);
  if(swing>clamp_tol) angle=qa_qvm_angle_mod(f32(destination-f32(clamp_tol-1)));
  else if(swing<-clamp_tol) angle=qa_qvm_angle_mod(f32(destination+f32(clamp_tol-1)));
  return (qa_q3_swing_angles_result){angle,swinging};
}
qa_vec3 qa_q3_add_pain_twitch(qa_vec3 torso, qa_q3_pain_twitch_input input){
  double time_ms=ms_checked(input.time_ms,"pose time",true);
  double pain_time=ms_checked(input.pain_time,"pain time",true);
  int32_t elapsed=(int32_t)((uint32_t)qa_js_to_int32(time_ms)-(uint32_t)qa_js_to_int32(pain_time));
  if(elapsed>=200) return qa_math_vec3(torso.x,torso.y,torso.z);
  float fraction=f32(1.0-(double)f32(f32((double)elapsed)/200.0f));
  float roll=f32(20.0f*fraction);
  return qa_math_vec3(torso.x,torso.y,input.pain_direction?torso.z+roll:torso.z-roll);
}
static qa_vec3 subtract_angles(qa_vec3 a,qa_vec3 b){
  return qa_math_vec3(angle_subtract(a.x,b.x),angle_subtract(a.y,b.y),angle_subtract(a.z,b.z));
}
static const double OFFSETS[8]={0,22,45,-22,0,22,-45,-22};
static double movement_offset(qa_q3_pose_entity_state e){
  if((qa_js_to_int32(e.e_flags)&1)!=0) return 0;
  int32_t dir=qa_qvm_float_to_int(e.movement_direction);
  if(dir<0||dir>=8) qa_common_error(QA_COMMON_ERROR_DROP,"Bad player movement angle");
  return OFFSETS[dir];
}
static double update_yaw(qa_q3_pose_lerp_frame*s,double dest,double tol,qa_q3_calculate_player_pose_input in){
  qa_q3_swing_angles_result r=qa_q3_swing_angles((qa_q3_swing_angles_input){dest,tol,90,in.swing_speed,in.frame_time_ms,s->yaw_angle,s->yawing});
  s->yaw_angle=r.angle; s->yawing=r.swinging; return r.angle;
}
qa_q3_player_pose qa_q3_calculate_player_pose(qa_q3_player_pose_state *state, qa_q3_calculate_player_pose_input input){
  ms_checked(input.time_ms,"pose time",true); ms_checked(input.frame_time_ms,"frame time",false);
  double swing_speed=float32_checked(input.swing_speed,"swing speed");
  if(swing_speed<0) qa_range_error("swing speed must be non-negative");
  qa_vec3 head=qa_math_vec3(input.lerp_angles.x,qa_qvm_angle_mod(input.lerp_angles.y),input.lerp_angles.z);
  qa_vec3 legs_angles=qa_math_vec3(0,0,0), torso_angles=qa_math_vec3(0,0,0);
  if((qa_js_to_int32(input.entity.legs_anim)&~QA_Q3_ANIMATION_TOGGLE_BIT)!=QA_Q3_ANIM_LEGS_IDLE
    ||(qa_js_to_int32(input.entity.torso_anim)&~QA_Q3_ANIMATION_TOGGLE_BIT)!=QA_Q3_ANIM_TORSO_STAND){
    state->torso.yawing=true; state->torso.pitching=true; state->legs.yawing=true;
  }
  double offset=movement_offset(input.entity);
  double legs_dest=f32(head.y+offset);
  double torso_dest=f32(head.y+f32(0.25*offset));
  input.swing_speed=swing_speed;
  double torso_yaw=update_yaw(&state->torso,torso_dest,25,input);
  double legs_yaw=update_yaw(&state->legs,legs_dest,40,input);
  torso_angles=qa_math_vec3(0,torso_yaw,0); legs_angles=qa_math_vec3(0,legs_yaw,0);
  double pitch_dest=head.x>180?f32(f32(-360+head.x)*0.75):f32(head.x*0.75);
  qa_q3_swing_angles_result pitch=qa_q3_swing_angles((qa_q3_swing_angles_input){pitch_dest,15,30,0.1,input.frame_time_ms,state->torso.pitch_angle,state->torso.pitching});
  state->torso.pitch_angle=pitch.angle; state->torso.pitching=pitch.swinging;
  torso_angles=qa_math_vec3(pitch.angle,torso_angles.y,torso_angles.z);
  if(input.fixed_torso) torso_angles=qa_math_vec3(0,torso_angles.y,torso_angles.z);
  double speed=qa_length3(input.entity.velocity);
  if(speed!=0){
    qa_vec3 velocity=qa_normalize3(input.entity.velocity);
    double lean=f32(speed*f32(0.05));
    qa_axis legs_axis=qa_qvm_angles_to_axis(legs_angles);
    double side=f32(lean*qa_dot3(velocity,legs_axis.vectors[1]));
    double forward=f32(lean*qa_dot3(velocity,legs_axis.vectors[0]));
    legs_angles=qa_math_vec3(legs_angles.x+forward,legs_angles.y,legs_angles.z-side);
  }
  if(input.fixed_legs) legs_angles=qa_math_vec3(0,torso_angles.y,0);
  torso_angles=qa_q3_add_pain_twitch(torso_angles,(qa_q3_pain_twitch_input){input.time_ms,state->pain_time,state->pain_direction});
  qa_vec3 head_local=subtract_angles(head,torso_angles);
  qa_vec3 torso_local=subtract_angles(torso_angles,legs_angles);
  return (qa_q3_player_pose){qa_qvm_angles_to_axis(legs_angles),qa_qvm_angles_to_axis(torso_local),qa_qvm_angles_to_axis(head_local)};
}
