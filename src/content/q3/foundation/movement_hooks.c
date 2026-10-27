/* Port of src/content/q3/foundation/movement-hooks.ts. GPL-2.0-or-later. */
#include "qa/content/q3/foundation/movement_hooks.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/animation.h"
#include "qa/movement/q3/constants.h"

qa_q3_arsenal_controls qa_q3_source_arsenal_controls(qa_q3_hook_context context){
  return (qa_q3_arsenal_controls){(qa_int32(context.command.buttons)&QA_Q3_BUTTON_ATTACK)!=0,
    (qa_int32(context.command.buttons)&QA_Q3_BUTTON_USE_HOLDABLE)!=0,context.command.weapon};
}
typedef struct holder { qa_q3_arsenal_runtime_access runtime; } holder;
static bool firing_hook(void *ctx, qa_q3_hook_context context){
  (void)ctx;
  if(!context.arsenal.state.kind||strcmp(context.arsenal.state.kind,"q3")!=0)
    qa_range_error("Q3 source hooks require the selected Q3 arsenal");
  const qa_q3_weapon_item *item=qa_q3_get_weapon_item(context.arsenal.state.source_weapon);
  if(!item) return false;
  if(!item->ammo) return true;
  for(size_t i=0;i<context.arsenal.ammo_count;i++)
    if(context.arsenal.ammo[i].item&&strcmp(context.arsenal.ammo[i].item,item->ammo)==0) return context.arsenal.ammo[i].count!=0;
  return false;
}
static qa_animation_step_result animation_hook(void *ctx, qa_q3_animation_request request, qa_q3_hook_context context){
  (void)ctx; return qa_q3_source_animation(request,&context);
}
static qa_q3_weapon_phase_result weapon_hook(void *ctx, qa_q3_hook_context context){
  holder *h=ctx;
  qa_q3_arsenal_runtime_state previous=h->runtime.read(h->runtime.ctx,context.input.actor,context.input.execution);
  qa_q3_arsenal_runtime_state state=previous;
  state.respawned=(qa_int32(context.motion->pm_flags)&QA_Q3_FLAG_RESPAWNED)!=0;
  state.use_item_held=(qa_int32(context.motion->pm_flags)&QA_Q3_FLAG_USE_ITEM_HELD)!=0;
  state.event_sequence=context.motion->event_sequence;
  qa_weapon_step_input in={context.input.actor,context.input.command,context.frame,context.arsenal,context.animation,
    context.input.environment,h->runtime.gauntlet_hit(h->runtime.ctx,context)};
  qa_q3_arsenal_step result=qa_q3_step_arsenal(in,state,qa_q3_source_arsenal_controls(context),NULL,NULL);
  h->runtime.write(h->runtime.ctx,context.input.actor,context.input.execution,result.runtime);
  double movement_flags=(double)((qa_int32(context.motion->pm_flags)&~(QA_Q3_FLAG_RESPAWNED|QA_Q3_FLAG_USE_ITEM_HELD))
    |(result.runtime.respawned?QA_Q3_FLAG_RESPAWNED:0)|(result.runtime.use_item_held?QA_Q3_FLAG_USE_ITEM_HELD:0));
  qa_q3_weapon_phase_result out={result.base,movement_flags};
  free((void*)result.torso_animations); /* phase result has no torso field */
  return out;
}
static qa_animation_step_result torso_hook(void *ctx, qa_q3_hook_context context){
  (void)ctx;
  if(!context.arsenal.state.kind||strcmp(context.arsenal.state.kind,"q3")!=0)
    qa_range_error("Q3 source torso hooks require a Q3 arsenal");
  if(context.arsenal.state.state!=QA_Q3_WEAPON_READY) return (qa_animation_step_result){context.animation,NULL,0};
  return qa_q3_source_torso(context.arsenal.state.source_weapon==QA_Q3_WP_GAUNTLET?QA_Q3_ANIM_TORSO_STAND2:QA_Q3_ANIM_TORSO_STAND,&context,true);
}
qa_q3_movement_hooks qa_q3_create_source_movement_hooks(qa_q3_arsenal_runtime_access runtime){
  holder *h=malloc(sizeof *h); if(!h) qa_range_error("out of memory"); h->runtime=runtime;
  return (qa_q3_movement_hooks){h,firing_hook,animation_hook,weapon_hook,torso_hook};
}
void qa_q3_source_movement_hooks_destroy(qa_q3_movement_hooks *hooks){
  if(!hooks) return;
  free(hooks->ctx); hooks->ctx=NULL;
}
