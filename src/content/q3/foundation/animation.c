/* Port of src/content/q3/foundation/animation.ts. GPL-2.0-or-later. */
#include "qa/content/q3/foundation/animation.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include "qa/core/common_error.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"

#define MAX_TOTAL (QA_Q3_ANIM_FLAG_STAND2RUN + 1)
static int32_t i32(double v){ return qa_int32(v); }
static int32_t add32(double a,double b){ return (int32_t)((uint32_t)i32(a)+(uint32_t)i32(b)); }
static int32_t sub32(double a,double b){ return (int32_t)((uint32_t)i32(a)-(uint32_t)i32(b)); }
qa_q3_animation_set qa_q3_animation_set_from_config(const qa_q3_player_animation_config *c){
  return (qa_q3_animation_set){c->animations,c->animation_present,QA_Q3_TOTAL_ANIMATION_COUNT};
}
qa_q3_lerp_frame qa_q3_create_lerp_frame(void){ return (qa_q3_lerp_frame){0}; }
static void clock_check(double v){
  if(v!=trunc(v)||!(v>=-2147483648.0&&v<=2147483647.0)) qa_range_error("animation clock %g is outside int32 milliseconds",v);
}
static void anim_num_check(double v){
  if(v!=trunc(v)||!(v>=-2147483648.0&&v<=2147483647.0)) qa_range_error("animation number %g is outside int32",v);
}
static const qa_q3_animation *animation_at(qa_q3_animation_set config, int32_t index){
  if(index<0||index>=MAX_TOTAL){ char b[96]; snprintf(b,sizeof b,"Bad animation number: %d",index); qa_common_error(QA_COMMON_ERROR_DROP,b); }
  if((size_t)index>=config.count||(config.present&&!config.present[index]))
    qa_range_error("animation slot %d is not playable",index);
  return &config.animations[index];
}
void qa_q3_set_lerp_frame_animation(qa_q3_animation_set config, qa_q3_lerp_frame *state,
    double new_animation, qa_q3_lerp_print_fn print, void *print_ctx){
  anim_num_check(new_animation); state->animation_number=new_animation;
  int32_t idx=i32(new_animation)&~QA_Q3_ANIMATION_TOGGLE_BIT;
  const qa_q3_animation *a=animation_at(config,idx); state->current_animation=a;
  state->animation_time=(double)add32(state->frame_time,a->initial_lerp);
  if(print){ char b[64]; snprintf(b,sizeof b,"Anim: %d\n",idx); print(print_ctx,b); }
}
static const qa_q3_animation *current_animation(const qa_q3_lerp_frame *s){
  if(!s->current_animation) qa_range_error("lerp frame has no current animation");
  return s->current_animation;
}
void qa_q3_run_lerp_frame(qa_q3_animation_set config, qa_q3_lerp_frame *state,
    qa_q3_run_lerp_frame_input input, qa_q3_lerp_print_fn print, void *print_ctx){
  if(input.no_player_animations){ state->old_frame=0; state->frame=0; state->back_lerp=0; return; }
  clock_check(input.time_ms); anim_num_check(input.new_animation);
  if(!isfinite(input.speed_scale)||input.speed_scale<0)
    qa_range_error("animation speed scale %g must be finite and non-negative",input.speed_scale);
  if(input.new_animation!=state->animation_number||!state->current_animation)
    qa_q3_set_lerp_frame_animation(config,state,input.new_animation,print,print_ctx);
  if(input.time_ms>=state->frame_time){
    state->old_frame=state->frame; state->old_frame_time=state->frame_time;
    const qa_q3_animation *a=current_animation(state);
    if(a->frame_lerp==0) return;
    if(input.time_ms<state->animation_time) state->frame_time=state->animation_time;
    else state->frame_time=(double)add32(state->old_frame_time,a->frame_lerp);
    int32_t diff=sub32(state->frame_time,state->animation_time);
    int32_t frame_offset=(int32_t)trunc((double)diff/a->frame_lerp);
    frame_offset=qa_qvm_float_to_int((float)((float)frame_offset*(float)input.speed_scale));
    int32_t frame_count=(int32_t)a->num_frames;
    if(a->flipflop) frame_count=(int32_t)((uint32_t)frame_count*2u);
    if(frame_offset>=frame_count){
      frame_offset=(int32_t)(frame_offset-frame_count);
      if(a->loop_frames!=0){ frame_offset%=(int32_t)a->loop_frames; frame_offset=add32(frame_offset,(double)sub32(a->num_frames,a->loop_frames)); }
      else { frame_offset=frame_count-1; state->frame_time=input.time_ms; }
    }
    if(a->reversed) state->frame=(double)sub32((double)add32(a->first_frame,(double)sub32(a->num_frames,1)),(double)frame_offset);
    else if(a->flipflop&&frame_offset>=(int32_t)a->num_frames)
      state->frame=(double)sub32((double)add32(a->first_frame,(double)sub32(a->num_frames,1)),(double)(frame_offset%(int32_t)a->num_frames));
    else state->frame=(double)add32(a->first_frame,(double)frame_offset);
    if(input.time_ms>state->frame_time){ state->frame_time=input.time_ms; if(print) print(print_ctx,"Clamp lf->frameTime\n"); }
  }
  if(state->frame_time>(double)add32(input.time_ms,200)) state->frame_time=input.time_ms;
  if(state->old_frame_time>input.time_ms) state->old_frame_time=input.time_ms;
  if(state->frame_time==state->old_frame_time) state->back_lerp=0;
  else {
    float elapsed=(float)sub32(input.time_ms,state->old_frame_time);
    float duration=(float)sub32(state->frame_time,state->old_frame_time);
    float fraction=(float)(elapsed/duration);
    state->back_lerp=(float)(1.0f-fraction);
  }
}
void qa_q3_clear_lerp_frame(qa_q3_animation_set config, qa_q3_lerp_frame *state,
    double animation, double time_ms, qa_q3_lerp_print_fn print, void *print_ctx){
  clock_check(time_ms); state->frame_time=time_ms; state->old_frame_time=time_ms;
  qa_q3_set_lerp_frame_animation(config,state,animation,print,print_ctx);
  const qa_q3_animation *s=current_animation(state);
  state->old_frame=s->first_frame; state->frame=s->first_frame;
}
