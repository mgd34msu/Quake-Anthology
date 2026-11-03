/* Port of src/content/q3/foundation/character.ts. GPL-2.0-or-later. */
#include "qa/content/q3/foundation/character.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/foundation/arsenal.h"
#include "qa/core/game_numeric.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/animation.h"
#include "qa/movement/q3/constants.h"
#include "qa/foundation/js.h"

const qa_bounds qa_q3_character_bounds = {{-15,-15,-24},{15,15,32}};
void qa_q3_death_animation_sequence_init(qa_q3_death_animation_sequence *s){ s->index=0; }
qa_q3_death_animation_checkpoint qa_q3_death_animation_sequence_capture(const qa_q3_death_animation_sequence *s){
  return (qa_q3_death_animation_checkpoint){1,s->index};
}
void qa_q3_death_animation_sequence_restore(qa_q3_death_animation_sequence *s, qa_q3_death_animation_checkpoint c){
  if(c.version!=1||c.index!=trunc(c.index)||c.index<0||c.index>2) qa_range_error("Invalid Q3 death animation checkpoint");
  s->index=c.index;
}
qa_q3_death_animation_next qa_q3_death_animation_sequence_next(qa_q3_death_animation_sequence *s){
  qa_q3_death_animation_next r=s->index==0?(qa_q3_death_animation_next){QA_Q3_ANIM_BOTH_DEATH1,QA_Q3_EV_DEATH1}
    :s->index==1?(qa_q3_death_animation_next){QA_Q3_ANIM_BOTH_DEATH2,QA_Q3_EV_DEATH2}
    :(qa_q3_death_animation_next){QA_Q3_ANIM_BOTH_DEATH3,QA_Q3_EV_DEATH3};
  s->index=(double)(qa_js_to_int32((s->index+1))%3); return r;
}
double qa_q3_maximum_health(const char *handicap){
  int32_t m=qa_game_atoi(handicap); return m<1||m>100?100:m;
}
qa_combat_state qa_q3_initial_combat(const char *handicap, const char *team){
  qa_combat_state s={0};
  s.health=(double)(int32_t)(qa_js_to_int32(qa_q3_maximum_health(handicap))+25);
  s.armor.regular.kind=QA_REGULAR_ARMOR_Q3; s.armor.regular.points=0; s.armor.regular.protection=(float)0.66;
  s.armor.powered.kind="none"; s.mass=200; s.can_take_damage=true; s.invulnerable=false; s.team=team;
  return s;
}
void qa_q3_character_actor_init(qa_q3_character_actor *a, qa_owned_actor owned, qa_provider_id provider,
    const char *product, qa_q3_character_services services, qa_q3_death_animation_sequence *death){
  *a=(qa_q3_character_actor){0}; a->actor=owned; a->provider=provider; a->product=product;
  a->services=services; a->death_animations=death; a->current_animation=qa_q3_spawn_animation();
}
qa_actor_animation_state qa_q3_character_actor_get_animation(const qa_q3_character_actor *a){
  return (qa_actor_animation_state){a->provider,a->current_animation};
}
double qa_q3_character_actor_get_source_flags(const qa_q3_character_actor *a){ return a->flags; }
double qa_q3_character_actor_get_event_sequence(const qa_q3_character_actor *a){ return a->sequence; }
double qa_q3_character_actor_get_respawn_eligible_after(const qa_q3_character_actor *a){ return a->respawn_time; }
double qa_q3_character_actor_get_spawns(const qa_q3_character_actor *a){ return a->spawn_count; }
static void emit_event(qa_q3_character_actor *a,double event,double parameter){
  a->services.emit(a->services.ctx,(qa_q3_character_event){a->actor,a->sequence++,a->services.time_milliseconds(a->services.ctx),event,parameter});
}
void qa_q3_character_actor_jump(qa_q3_character_actor *a){ emit_event(a,QA_Q3_EV_JUMP,0); }
qa_q3_character_checkpoint qa_q3_character_actor_capture(const qa_q3_character_actor *a){
  return (qa_q3_character_checkpoint){1,a->product,a->current_animation,a->flags,a->sequence,
    a->respawn_time,a->spawn_count,a->dead,a->gibbed,a->initialized};
}
static void pain_cb(void *ctx, qa_pain_reaction reaction){
  qa_q3_character_actor *a=ctx;
  qa_combat_state_result r=qa_gameplay_authority_read(a->services.combat,reaction.self.id);
  emit_event(a,QA_Q3_EV_PAIN,r.has_value?r.value.health:0);
}
static void die_cb(void *ctx, qa_death_reaction reaction){ (void)reaction; qa_q3_character_actor_die(ctx); }
static void bind_callbacks(qa_q3_character_actor *a){
  if(a->initialized) return;
  qa_actor_callbacks cb={0}; cb.ctx=a; cb.pain=pain_cb; cb.die=die_cb;
  qa_actor_callback_table_bind(a->services.callbacks,a->actor,cb);
  a->initialized=true;
}
void qa_q3_character_actor_restore(qa_q3_character_actor *a, qa_q3_character_checkpoint c){
  if(c.version!=1||!c.product||strcmp(c.product,a->product)!=0||!c.animation.kind||strcmp(c.animation.kind,"q3")!=0)
    qa_range_error("Q3 character checkpoint belongs to another source product");
  if(!c.initialized&&a->initialized) qa_range_error("Cannot restore an unadmitted Q3 character over an admitted binding");
  if(c.initialized&&!a->initialized){
    if(!qa_shared_body_table_read(a->services.bodies,a->actor.id).has_value
      ||!qa_gameplay_authority_read(a->services.combat,a->actor.id).has_value
      ||!qa_shared_inventory_table_has(a->services.inventory,a->actor.id))
      qa_range_error("Restore shared Q3 character stores before private state");
    bind_callbacks(a);
  }
  a->current_animation=c.animation; a->flags=c.flags; a->sequence=c.event_sequence;
  a->respawn_time=c.respawn_time; a->spawn_count=c.spawn_count; a->dead=c.dead; a->gibbed=c.gibbed;
}
void qa_q3_character_actor_spawn(qa_q3_character_actor *a, qa_q3_character_spawn input){
  qa_q3_character_services *s=&a->services; qa_owned_actor actor=a->actor;
  if(!qa_gameplay_authority_read(s->combat,actor.id).has_value)
    qa_gameplay_authority_create_state(s->combat,actor,input.combat,NULL,NULL);
  else {
    qa_gameplay_authority_set_health(s->combat,actor,input.combat.health);
    qa_gameplay_authority_set_armor(s->combat,actor,input.combat.armor);
    qa_combat_trait_changes ch={0};
    ch.has_can_take_damage=true; ch.can_take_damage=input.combat.can_take_damage;
    ch.has_mass=true; ch.mass=input.combat.mass;
    ch.has_invulnerable=true; ch.invulnerable=input.combat.invulnerable;
    ch.has_team=true; ch.team=input.combat.team;
    ch.has_no_knockback=input.combat.has_no_knockback; ch.no_knockback=input.combat.no_knockback;
    qa_gameplay_authority_set_traits(s->combat,actor,ch);
  }
  if(!qa_shared_inventory_table_has(s->inventory,actor.id))
    qa_shared_inventory_table_create_entries(s->inventory,actor,input.inventory,input.inventory_count);
  else {
    size_t n=0; const qa_inventory_entry *entries=qa_shared_inventory_table_entries(s->inventory,actor.id,&n);
    /* entries pointer is table scratch; copy before configuring. */
    qa_inventory_entry *copy=malloc((n?n:1)*sizeof *copy); if(!copy) qa_range_error("out of memory");
    if(n) memcpy(copy,entries,n*sizeof *copy);
    for(size_t i=0;i<n;i++){ qa_inventory_entry e=copy[i]; e.count=0; qa_shared_inventory_table_configure(s->inventory,actor,e,NULL,NULL); }
    free(copy);
    for(size_t i=0;i<input.inventory_count;i++) qa_shared_inventory_table_configure(s->inventory,actor,input.inventory[i],NULL,NULL);
  }
  a->current_animation=qa_q3_spawn_animation();
  a->flags=(double)((qa_js_to_int32(a->flags)&(4|0x4000|0x80000))^4);
  a->dead=false; a->gibbed=false; a->respawn_time=s->time_milliseconds(s->ctx); a->spawn_count++;
  qa_shared_body_table_write(s->bodies,actor,input.body);
  bind_callbacks(a);
  if(!s->placement||strcmp(s->placement,"source-game")!=0){
    s->kill_box(s->ctx,actor);
    qa_shared_body_table_link(s->bodies,actor,NULL);
    s->spawn_targets(s->ctx,actor);
    if(a->spawn_count>1) emit_event(a,QA_Q3_EV_PLAYER_TELEPORT_IN,0);
  }
}
static void gib_actor(qa_q3_character_actor *a,double killer){
  a->gibbed=true; a->flags=(double)(qa_js_to_int32(a->flags)|0x80);
  qa_combat_trait_changes ch={0}; ch.has_can_take_damage=true; ch.can_take_damage=false;
  qa_gameplay_authority_set_traits(a->services.combat,a->actor,ch);
  qa_shared_body_table_unlink(a->services.bodies,a->actor);
  emit_event(a,QA_Q3_EV_GIB_PLAYER,killer);
}
void qa_q3_character_actor_die(qa_q3_character_actor *a){
  if(a->gibbed) return;
  qa_combat_state_result hr=qa_gameplay_authority_read(a->services.combat,a->actor.id);
  if(!hr.has_value) qa_range_error("Q3 character has no health binding");
  double health=hr.value.health;
  qa_q3_character_death_context context=a->services.death_context(a->services.ctx,a->actor);
  if(a->dead){
    if(health<=-40&&context.blood) gib_actor(a,context.killer_source_slot);
    else if(!context.blood&&health<=-40) qa_gameplay_authority_set_health(a->services.combat,a->actor,-39);
    return;
  }
  a->dead=true; a->flags=(double)(qa_js_to_int32(a->flags)|1);
  a->respawn_time=(double)(int32_t)((uint32_t)qa_js_to_int32(a->services.time_milliseconds(a->services.ctx))+1700u);
  qa_body_state_result br=qa_shared_body_table_read(a->services.bodies,a->actor.id);
  if(br.has_value){
    qa_body_state body=br.value; body.angles=(qa_vec3){0,body.angles.y,0}; body.bounds.max.z=-8;
    qa_shared_body_table_write(a->services.bodies,a->actor,body);
    qa_shared_body_table_link(a->services.bodies,a->actor,NULL);
  }
  if((health<=-40&&!context.no_drop&&context.blood)||context.suicide) gib_actor(a,context.killer_source_slot);
  else {
    if(health<=-40) qa_gameplay_authority_set_health(a->services.combat,a->actor,-39);
    qa_q3_death_animation_next next=qa_q3_death_animation_sequence_next(a->death_animations);
    a->current_animation.legs=(double)(((qa_js_to_int32(a->current_animation.legs)&128)^128)|qa_js_to_int32(next.animation));
    a->current_animation.torso=(double)(((qa_js_to_int32(a->current_animation.torso)&128)^128)|qa_js_to_int32(next.animation));
    emit_event(a,next.event,context.killer_source_slot);
  }
}
bool qa_q3_character_actor_wants_respawn(const qa_q3_character_actor *a, double now, bool attack, bool use_holdable, double force_seconds){
  if(!a->dead||now<=a->respawn_time) return false;
  return attack||use_holdable||(force_seconds>0&&((int32_t)((uint32_t)qa_js_to_int32(now)-(uint32_t)qa_js_to_int32(a->respawn_time))>(int32_t)((uint32_t)qa_js_to_int32(force_seconds)*1000u)));
}
void qa_q3_character_actor_commit_animation(qa_q3_character_actor *a, qa_actor_animation_state animation){
  if(!animation.provider||!a->provider||strcmp(animation.provider,a->provider)!=0
    ||!animation.state.kind||strcmp(animation.state.kind,"q3")!=0)
    qa_range_error("Q3 character animation belongs to another provider");
  a->current_animation=animation.state;
}
qa_animation_step_result qa_q3_step_character_animation(qa_animation_step_input input,
    const char *product, bool dead, double event_sequence){
  if(!input.animation.state.kind||strcmp(input.animation.state.kind,"q3")!=0)
    qa_range_error("Q3 character requires a Q3 animation state");
  double elapsed=input.frame.elapsed.kind==QA_SOURCE_TIME_MILLISECONDS?input.frame.elapsed.value:trunc(input.frame.elapsed.value*1000);
  qa_q3_animation_context context={input.animation,dead,elapsed,0,product,event_sequence};
  qa_q3_animation_request drop={QA_Q3_ANIM_DROP_TIMERS,0,false,0};
  qa_animation_step_result dropped=qa_q3_run_animation_operation(drop,context);
  double animation;
  const char *loco=input.locomotion?input.locomotion:"";
  if(!strcmp(loco,"idle")) animation=QA_Q3_ANIM_LEGS_IDLE;
  else if(!strcmp(loco,"walk")) animation=input.backwards?QA_Q3_ANIM_LEGS_BACKWALK:QA_Q3_ANIM_LEGS_WALK;
  else if(!strcmp(loco,"run")) animation=input.backwards?QA_Q3_ANIM_LEGS_BACK:QA_Q3_ANIM_LEGS_RUN;
  else if(!strcmp(loco,"backward")) animation=QA_Q3_ANIM_LEGS_BACK;
  else if(!strcmp(loco,"crouch")) animation=input.backwards?QA_Q3_ANIM_LEGS_BACKCR:QA_Q3_ANIM_LEGS_WALKCR;
  else if(!strcmp(loco,"jump")) animation=input.backwards?QA_Q3_ANIM_LEGS_JUMPB:QA_Q3_ANIM_LEGS_JUMP;
  else if(!strcmp(loco,"land")) animation=input.backwards?QA_Q3_ANIM_LEGS_LANDB:QA_Q3_ANIM_LEGS_LAND;
  else animation=QA_Q3_ANIM_LEGS_SWIM; /* TS switch is exhaustive over locomotion union */
  qa_q3_animation_context c2=context; c2.animation=dropped.animation;
  qa_q3_animation_request legs={QA_Q3_ANIM_LEGS,animation,input.force,0};
  qa_animation_step_result selected=qa_q3_run_animation_operation(legs,c2);
  qa_animation_step_result landed={0}; bool is_land=strcmp(loco,"land")==0;
  if(is_land){
    qa_q3_animation_context c3=context; c3.animation=selected.animation;
    qa_q3_animation_request timer={QA_Q3_ANIM_LEGS_TIMER,0,false,130};
    landed=qa_q3_run_animation_operation(timer,c3);
  }
  size_t total=dropped.effect_count+selected.effect_count+(is_land?landed.effect_count:0);
  qa_movement_effect *effects=malloc((total?total:1)*sizeof *effects); if(!effects) qa_range_error("out of memory");
  size_t o=0;
  if(dropped.effect_count){ memcpy(effects,dropped.effects,dropped.effect_count*sizeof *effects); o+=dropped.effect_count; }
  if(selected.effect_count){ memcpy(effects+o,selected.effects,selected.effect_count*sizeof *effects); o+=selected.effect_count; }
  if(is_land&&landed.effect_count) memcpy(effects+o,landed.effects,landed.effect_count*sizeof *effects);
  free((void*)dropped.effects); free((void*)selected.effects); if(is_land) free((void*)landed.effects);
  return (qa_animation_step_result){is_land?landed.animation:selected.animation,effects,total};
}
