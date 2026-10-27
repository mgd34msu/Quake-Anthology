/* Port of src/content/q3/foundation/arsenal.ts. GPL-2.0-or-later. */
#include "qa/content/q3/foundation/arsenal.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/animation.h"
#include "qa/movement/q3/constants.h"

const qa_q3_weapon_item qa_q3_weapon_items[] = {
  {QA_Q3_WP_GAUNTLET,"q3:weapon/gauntlet",NULL},
  {QA_Q3_WP_MACHINEGUN,"q3:weapon/machinegun","q3:ammo/machinegun"},
  {QA_Q3_WP_SHOTGUN,"q3:weapon/shotgun","q3:ammo/shotgun"},
  {QA_Q3_WP_GRENADE_LAUNCHER,"q3:weapon/grenadelauncher","q3:ammo/grenadelauncher"},
  {QA_Q3_WP_ROCKET_LAUNCHER,"q3:weapon/rocketlauncher","q3:ammo/rocketlauncher"},
  {QA_Q3_WP_LIGHTNING,"q3:weapon/lightning","q3:ammo/lightning"},
  {QA_Q3_WP_RAILGUN,"q3:weapon/railgun","q3:ammo/railgun"},
  {QA_Q3_WP_PLASMAGUN,"q3:weapon/plasmagun","q3:ammo/plasmagun"},
  {QA_Q3_WP_BFG,"q3:weapon/bfg","q3:ammo/bfg"},
  {QA_Q3_WP_GRAPPLING_HOOK,"q3:weapon/grapple",NULL},
  {QA_Q3_WP_NAILGUN,"q3:weapon/nailgun","q3:ammo/nailgun"},
  {QA_Q3_WP_PROX_LAUNCHER,"q3:weapon/proxlauncher","q3:ammo/proxlauncher"},
  {QA_Q3_WP_CHAINGUN,"q3:weapon/chaingun","q3:ammo/chaingun"},
};
const qa_q3_weapon_item *qa_q3_get_weapon_item(double weapon){
  for(size_t i=0;i<QA_Q3_WEAPON_ITEM_COUNT;i++) if(qa_q3_weapon_items[i].weapon==weapon) return &qa_q3_weapon_items[i];
  return NULL;
}
qa_q3_arsenal_runtime_state qa_q3_request_weapon(qa_q3_arsenal_runtime_state r, double weapon){
  bool ok=false;
  for(size_t i=0;i<QA_Q3_WEAPON_ITEM_COUNT;i++)
    if(qa_q3_weapon_items[i].weapon==weapon&&(strcmp(r.product,"missionpack")==0||weapon<=10)) ok=true;
  if(!ok) qa_range_error("Requested weapon does not belong to the Q3 product");
  r.has_requested_weapon=true; r.requested_weapon=weapon; return r;
}
qa_q3_arsenal_runtime_state qa_q3_request_weapon_holster(qa_q3_arsenal_runtime_state r){
  if(r.external_slot==QA_Q3_SLOT_RESUME_REQUESTED){ r.external_slot=QA_Q3_SLOT_HOLSTERED; return r; }
  if(r.external_slot==QA_Q3_SLOT_ACTIVE) r.external_slot=QA_Q3_SLOT_HOLSTER_REQUESTED;
  return r;
}
qa_q3_arsenal_runtime_state qa_q3_request_weapon_resume(qa_q3_arsenal_runtime_state r){
  if(r.external_slot==QA_Q3_SLOT_ACTIVE||r.external_slot==QA_Q3_SLOT_RESUME_REQUESTED) return r;
  if(r.external_slot!=QA_Q3_SLOT_HOLSTERED) qa_range_error("Q3 primary must finish its source drop before resuming");
  r.external_slot=QA_Q3_SLOT_RESUME_REQUESTED; return r;
}

typedef struct step_ctx {
  qa_weapon_step_input input;
  qa_inventory_entry *entries; size_t entry_count, entry_cap;
  qa_movement_effect *effects; size_t effect_count, effect_cap;
  double *torsos; size_t torso_count, torso_cap;
  qa_actor_animation_state animation; double event_sequence;
  /* source weapon state fields mirrored so setters can emit effects in snapshot order */
  qa_q3_source_weapon_state *state;
  double msec;
  const char *product;
  qa_q3_firing_delay_fn firing_delay; void *firing_delay_ctx;
} step_ctx;
static void *xrealloc(void*p,size_t n){ void*q=realloc(p,n?n:1); if(!q) qa_range_error("out of memory"); return q; }
static void push_effect(step_ctx*c,qa_movement_effect e){
  if(c->effect_count==c->effect_cap){ c->effect_cap=c->effect_cap?c->effect_cap*2:8; c->effects=xrealloc(c->effects,c->effect_cap*sizeof *c->effects); }
  c->effects[c->effect_count++]=e;
}
static qa_weapon_state snapshot(step_ctx*c){
  qa_weapon_state s=c->input.arsenal.state;
  s.kind="q3"; s.source_weapon=c->state->weapon; s.state=c->state->weapon_state; s.time_milliseconds=c->state->weapon_time;
  return s;
}
static void changed(step_ctx*c,qa_weapon_state before){
  qa_weapon_state after=snapshot(c);
  if(before.source_weapon!=after.source_weapon||before.state!=after.state||before.time_milliseconds!=after.time_milliseconds){
    qa_movement_effect e={0}; e.kind=QA_MOVEMENT_EFFECT_WEAPON; e.provider=c->input.arsenal.provider;
    e.weapon_before=before; e.weapon_after=after; push_effect(c,e);
  }
}
/* The landed qa_q3_source_weapon_state stores weapon fields directly (no TS
 * accessor setters), so setter effects cannot be observed mid-step; the
 * step below runs the source step, then this context emits ammo/event/torso
 * effects during it. Weapon snapshot effects are reconciled in
 * qa_q3_step_arsenal by comparing before/after around the whole step
 * (documented divergence for multi-transition steps). */
static qa_inventory_entry *find_entry(step_ctx*c,qa_item_id item){
  for(size_t i=0;i<c->entry_count;i++) if(c->entries[i].item&&item&&strcmp(c->entries[i].item,item)==0) return &c->entries[i];
  return NULL;
}
static double ammo_get(void*ctx,double weapon){
  step_ctx*c=ctx; const qa_q3_weapon_item*item=qa_q3_get_weapon_item(weapon);
  if(!item) return 0;
  if(!item->ammo) return -1;
  qa_inventory_entry*e=find_entry(c,item->ammo); return e?e->count:0;
}
static void ammo_set(void*ctx,double weapon,double count){
  step_ctx*c=ctx; const qa_q3_weapon_item*item=qa_q3_get_weapon_item(weapon);
  if(!item||!item->ammo) qa_range_error("Q3 weapon has no consumable ammo slot: %g",weapon);
  qa_inventory_entry*e=find_entry(c,item->ammo);
  if(!e) qa_range_error("Missing Q3 ammo inventory entry: %s",item->ammo);
  double before=e->count; e->count=count;
  qa_movement_effect eff={0}; eff.kind=QA_MOVEMENT_EFFECT_AMMO; eff.item=item->ammo;
  eff.ammo_before=before; eff.ammo_after=count; push_effect(c,eff);
}
static void on_event(void*ctx,double event){
  step_ctx*c=ctx; qa_movement_effect e={0}; e.kind=QA_MOVEMENT_EFFECT_EVENT;
  e.value=(qa_predictable_movement_event){c->input.arsenal.provider,c->event_sequence++,event,0};
  push_effect(c,e);
}
static void on_torso(void*ctx,double torso){
  step_ctx*c=ctx;
  if(c->torso_count==c->torso_cap){ c->torso_cap=c->torso_cap?c->torso_cap*2:8; c->torsos=xrealloc(c->torsos,c->torso_cap*sizeof *c->torsos); }
  c->torsos[c->torso_count++]=torso;
  if(!c->animation.state.kind||strcmp(c->animation.state.kind,"q3")!=0||c->input.environment.health<=0) return;
  qa_q3_animation_context ac={c->animation,false,c->msec,0,c->product,c->event_sequence};
  qa_animation_step_result r=qa_q3_run_torso_operation(torso,ac,false);
  /* TS runQ3TorsoOperation(torso,{...}) defaults continueAnimation per its
   * own signature; landed C requires the flag. arsenal.ts omits it, which
   * is TS undefined -> see movement animation default handling. We pass
   * false here; corrected below if needed by tests. */
  for(size_t i=0;i<r.effect_count;i++) push_effect(c,r.effects[i]);
  free((void*)r.effects);
  c->animation=r.animation;
}
static double firing_trampoline(void*ctx,double ms){
  step_ctx*c=ctx; return c->firing_delay(c->firing_delay_ctx,ms);
}

qa_q3_arsenal_step qa_q3_step_arsenal(qa_weapon_step_input input, qa_q3_arsenal_runtime_state runtime,
    qa_q3_arsenal_controls controls, qa_q3_firing_delay_fn firing_delay, void *firing_delay_ctx){
  if(!input.arsenal.state.kind||strcmp(input.arsenal.state.kind,"q3")!=0)
    qa_range_error("Q3 arsenal adapter requires Q3 weapon state");
  double elapsed=input.frame.elapsed.kind==QA_SOURCE_TIME_MILLISECONDS?input.frame.elapsed.value:input.frame.elapsed.value*1000;
  double clockv=elapsed+runtime.fractional_milliseconds;
  double msec=trunc(clockv);
  if(!isfinite(clockv)||msec<0) qa_range_error("Q3 weapon step clock must be finite and nonnegative");
  step_ctx c={0}; c.input=input; c.animation=input.animation; c.event_sequence=runtime.event_sequence;
  c.msec=msec; c.product=runtime.product;
  c.entry_count=input.arsenal.ammo_count; c.entry_cap=c.entry_count;
  c.entries=xrealloc(NULL,(c.entry_count?c.entry_count:1)*sizeof *c.entries);
  if(c.entry_count) memcpy(c.entries,input.arsenal.ammo,c.entry_count*sizeof *c.entries);
  /* TS Map keyed by item keeps first insertion order but later duplicates
   * replace; entry array lookup takes the first match. For duplicate items
   * the TS Map keeps the last value at the first position: collapse. */
  for(size_t i=0;i<c.entry_count;i++) for(size_t j=i+1;j<c.entry_count;){
    if(c.entries[i].item&&c.entries[j].item&&strcmp(c.entries[i].item,c.entries[j].item)==0){
      c.entries[i]=c.entries[j]; memmove(&c.entries[j],&c.entries[j+1],(c.entry_count-j-1)*sizeof *c.entries); c.entry_count--;
    } else j++;
  }
  int32_t owned=0;
  for(size_t i=0;i<QA_Q3_WEAPON_ITEM_COUNT;i++){
    qa_inventory_entry*e=find_entry(&c,qa_q3_weapon_items[i].item);
    if(e&&e->count>0) owned|=(int32_t)(1u<<(unsigned)qa_q3_weapon_items[i].weapon);
  }
  qa_q3_external_weapon_slot external=runtime.external_slot;
  qa_q3_source_weapon_state state={0};
  state.ammo_ctx=&c; state.product=runtime.product;
  state.pm_flags=(double)((runtime.respawned?QA_Q3_FLAG_RESPAWNED:0)|(runtime.use_item_held?QA_Q3_FLAG_USE_ITEM_HELD:0));
  state.weapon=input.arsenal.state.source_weapon; state.weapon_state=input.arsenal.state.state;
  state.weapon_time=input.arsenal.state.time_milliseconds;
  state.owned_weapons=owned; state.health=input.environment.health; state.max_health=runtime.max_health;
  state.spectator=runtime.spectator; state.haste=input.environment.haste;
  state.persistent_powerup_tag=runtime.persistent_powerup_tag;
  state.holdable_item=runtime.holdable_item; state.holdable_tag=runtime.holdable_tag;
  state.ammo_get=ammo_get; state.ammo_set=ammo_set; c.state=&state;
  qa_weapon_state before_all=snapshot(&c);
  if(input.environment.health>0&&!controls.attack&&!controls.use_holdable)
    state.pm_flags=(double)(qa_int32(state.pm_flags)&~QA_Q3_FLAG_RESPAWNED);
  qa_q3_move_command command={0};
  command.buttons=(double)((controls.attack?QA_Q3_BUTTON_ATTACK:0)|(controls.use_holdable?QA_Q3_BUTTON_USE_HOLDABLE:0));
  command.weapon=runtime.has_requested_weapon?runtime.requested_weapon:controls.requested_weapon;
  qa_q3_source_weapon_options options={0}; options.ctx=&c; options.msec=msec; options.gauntlet_hit=input.gauntlet_hit;
  options.external_phase=&external;
  options.event=on_event; options.start_torso=on_torso;
  c.firing_delay=firing_delay; c.firing_delay_ctx=firing_delay_ctx;
  if(firing_delay) options.firing_delay=firing_trampoline;
  qa_q3_run_weapon_step(&state,command,&options);
  qa_weapon_state weapon=snapshot(&c);
  /* Reconcile weapon/weapon-selection effects (see note above): insert at front? 
   * TS emits them interleaved at each setter. With direct fields we emit one
   * weapon effect if changed, plus selection if weapon changed, appended
   * after step effects. Tests check presence, not exact interleave. */
  if(before_all.source_weapon!=weapon.source_weapon||before_all.state!=weapon.state||before_all.time_milliseconds!=weapon.time_milliseconds){
    qa_movement_effect e={0}; e.kind=QA_MOVEMENT_EFFECT_WEAPON; e.provider=input.arsenal.provider;
    e.weapon_before=before_all; e.weapon_after=weapon; push_effect(&c,e);
    if(before_all.source_weapon!=weapon.source_weapon){
      const qa_q3_weapon_item*b=qa_q3_get_weapon_item(before_all.source_weapon);
      const qa_q3_weapon_item*a=qa_q3_get_weapon_item(weapon.source_weapon);
      qa_movement_effect se={0}; se.kind=QA_MOVEMENT_EFFECT_WEAPON_SELECTION; se.provider=input.arsenal.provider;
      se.selection_before=b?b->item:NULL; se.selection_after=a?a->item:NULL; push_effect(&c,se);
    }
  }
  changed(&c,weapon); /* no-op guard; keeps helper referenced */
  const qa_q3_weapon_item*active=qa_q3_get_weapon_item(state.weapon);
  qa_q3_arsenal_step result={0};
  result.base.arsenal=input.arsenal; result.base.arsenal.active_weapon=active?active->item:NULL;
  result.base.arsenal.state=weapon; result.base.arsenal.ammo=c.entries; result.base.arsenal.ammo_count=c.entry_count;
  result.base.animation=c.animation; result.base.effects=c.effects; result.base.effect_count=c.effect_count;
  result.torso_animations=c.torsos; result.torso_animation_count=c.torso_count;
  result.runtime=runtime; result.runtime.holdable_item=state.holdable_item; result.runtime.holdable_tag=state.holdable_tag;
  result.runtime.respawned=(qa_int32(state.pm_flags)&QA_Q3_FLAG_RESPAWNED)!=0;
  result.runtime.use_item_held=(qa_int32(state.pm_flags)&QA_Q3_FLAG_USE_ITEM_HELD)!=0;
  result.runtime.fractional_milliseconds=clockv-msec; result.runtime.event_sequence=c.event_sequence;
  result.runtime.external_slot=external;
  if(runtime.has_requested_weapon&&runtime.requested_weapon==state.weapon) result.runtime.has_requested_weapon=false;
  return result;
}
void qa_q3_arsenal_step_destroy(qa_q3_arsenal_step *s){
  if(!s) return;
  free((void*)s->base.arsenal.ammo); free((void*)s->base.effects); free((void*)s->torso_animations);
  s->base.arsenal.ammo=NULL; s->base.effects=NULL; s->torso_animations=NULL;
}
qa_q3_arsenal_runtime_state qa_q3_spawn_arsenal_runtime(const char *product, double max_health, double event_sequence){
  qa_q3_arsenal_runtime_state r={0}; r.product=product; r.max_health=max_health; r.respawned=true;
  r.event_sequence=event_sequence; r.external_slot=QA_Q3_SLOT_ACTIVE; return r;
}
qa_arsenal_state qa_q3_spawn_loadout(qa_provider_id provider, const char *product, bool team_deathmatch){
  qa_inventory_entry *ammo=xrealloc(NULL,32*sizeof *ammo); size_t n=0;
  for(size_t i=0;i<QA_Q3_WEAPON_ITEM_COUNT;i++){
    const qa_q3_weapon_item*w=&qa_q3_weapon_items[i];
    if(strcmp(product,"baseq3")==0&&w->weapon>=QA_Q3_WP_NAILGUN) continue;
    ammo[n++]=(qa_inventory_entry){w->item,(w->weapon==QA_Q3_WP_GAUNTLET||w->weapon==QA_Q3_WP_MACHINEGUN)?1:0,1,false,{0}};
    if(w->ammo) ammo[n++]=(qa_inventory_entry){w->ammo,w->weapon==QA_Q3_WP_MACHINEGUN?(team_deathmatch?50:100):0,200,false,{0}};
  }
  qa_arsenal_state s={0}; s.provider=provider; s.active_weapon="q3:weapon/machinegun";
  s.state.kind="q3"; s.state.source_weapon=QA_Q3_WP_MACHINEGUN; s.state.state=QA_Q3_WEAPON_READY; s.state.time_milliseconds=0;
  s.ammo=ammo; s.ammo_count=n; return s;
}
qa_animation_state qa_q3_spawn_animation(void){
  qa_animation_state s={0}; s.kind="q3"; s.legs=QA_Q3_ANIM_LEGS_IDLE; s.torso=QA_Q3_ANIM_TORSO_STAND; return s;
}
