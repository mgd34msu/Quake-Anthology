/* Port of src/content/q3/foundation/events.ts. GPL-2.0-or-later. */
#include "qa/content/q3/foundation/events.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "qa/core/numeric.h"
#include "qa/movement/q3/constants.h"
#include "qa/foundation/js.h"

const char *qa_q3_footstep_material_name(qa_q3_footstep_material m){
  switch(m){case QA_Q3_FOOTSTEP_MATERIAL_BOOT:return "boot";case QA_Q3_FOOTSTEP_MATERIAL_FLESH:return "flesh";
  case QA_Q3_FOOTSTEP_MATERIAL_MECH:return "mech";case QA_Q3_FOOTSTEP_MATERIAL_ENERGY:return "energy";
  case QA_Q3_FOOTSTEP_MATERIAL_METAL:return "metal";case QA_Q3_FOOTSTEP_MATERIAL_SPLASH:return "splash";
  default:return "normal";}
}
void qa_q3_character_event_presenter_init(qa_q3_character_event_presenter *p,
    qa_q3_player_pose_state *pose, qa_q3_player_footsteps footsteps, double (*rand_fn)(void*), void *random_ctx){
  *p=(qa_q3_character_event_presenter){0}; p->pose=pose; p->footsteps=footsteps;
  p->rand_fn=rand_fn; p->random_ctx=random_ctx; p->muzzle_flash_time=-99999;
}
typedef struct sink { qa_q3_character_presentation_effect *out; size_t cap, count; } sink;
static void push(sink*s,qa_q3_character_presentation_effect e){ if(s->count<s->cap) s->out[s->count]=e; s->count++; }
static qa_q3_character_presentation_effect sound_eff(const char*path,const char*channel){
  qa_q3_character_presentation_effect e={0}; e.kind=QA_Q3_EFFECT_SOUND; e.channel=channel;
  snprintf(e.text,sizeof e.text,"%s",path); return e;
}
static qa_q3_character_presentation_effect custom_eff(const char*name,const char*channel){
  qa_q3_character_presentation_effect e={0}; e.kind=QA_Q3_EFFECT_CUSTOM_SOUND; e.channel=channel;
  snprintf(e.text,sizeof e.text,"%s",name); return e;
}
static qa_q3_character_presentation_effect kind_eff(qa_q3_character_presentation_effect_kind k){
  qa_q3_character_presentation_effect e={0}; e.kind=k; return e;
}
size_t qa_q3_character_event_presenter_pain(qa_q3_character_event_presenter *p,
    double time_ms, double health, qa_q3_character_presentation_effect *out, size_t capacity){
  sink s={out,capacity,0};
  if(((int32_t)((uint32_t)qa_js_to_int32(time_ms)-(uint32_t)qa_js_to_int32(p->pose->pain_time)))<500) return 0;
  int level=health<25?25:health<50?50:health<75?75:100;
  p->pose->pain_time=time_ms; p->pose->pain_direction=!p->pose->pain_direction;
  char name[64]; snprintf(name,sizeof name,"*pain%d_1.wav",level);
  push(&s,custom_eff(name,"voice")); return s.count;
}
size_t qa_q3_character_event_presenter_event(qa_q3_character_event_presenter *p,
    qa_q3_character_event source, qa_q3_character_event_options options,
    qa_q3_character_presentation_effect *out, size_t capacity){
  sink s={out,capacity,0};
  int32_t event=qa_int32(source.event)&~0x300; double time=source.time_milliseconds;
  switch(event){
    case QA_Q3_EV_NONE: return 0;
    case QA_Q3_EV_FOOTSTEP: case QA_Q3_EV_FOOTSTEP_METAL: case QA_Q3_EV_FOOTSPLASH:
    case QA_Q3_EV_FOOTWADE: case QA_Q3_EV_SWIM:
      if(options.footsteps){
        qa_q3_character_presentation_effect e=kind_eff(QA_Q3_EFFECT_FOOTSTEP);
        e.material=event==QA_Q3_EV_FOOTSTEP?(qa_q3_footstep_material)p->footsteps
          :event==QA_Q3_EV_FOOTSTEP_METAL?QA_Q3_FOOTSTEP_MATERIAL_METAL:QA_Q3_FOOTSTEP_MATERIAL_SPLASH;
        e.variant=(double)(qa_int32(p->rand_fn(p->random_ctx))&3); push(&s,e);
      }
      return s.count;
    case QA_Q3_EV_FALL_SHORT: case QA_Q3_EV_FALL_MEDIUM: case QA_Q3_EV_FALL_FAR:
      if(event==QA_Q3_EV_FALL_FAR) p->pose->pain_time=time;
      if(options.local){ p->land_change=-8*(event-QA_Q3_EV_FALL_SHORT+1); p->land_time=time; }
      push(&s,event==QA_Q3_EV_FALL_SHORT?sound_eff("sound/player/land1.wav","auto")
        :custom_eff(event==QA_Q3_EV_FALL_MEDIUM?"*pain100_1.wav":"*fall1.wav",event==QA_Q3_EV_FALL_MEDIUM?"voice":"auto"));
      return s.count;
    case QA_Q3_EV_STEP_4: case QA_Q3_EV_STEP_8: case QA_Q3_EV_STEP_12: case QA_Q3_EV_STEP_16: {
      if(!options.local||!options.predict_steps) return 0;
      int32_t elapsed=(int32_t)((uint32_t)qa_js_to_int32(time)-(uint32_t)qa_js_to_int32(p->step_time));
      double previous=elapsed<200?(float)((float)(p->step_change*(float)(int32_t)(200-elapsed))/200.0f):0;
      double v=(float)(previous+4*(event-QA_Q3_EV_STEP_4+1));
      p->step_change=v<32?v:32; p->step_time=time; return 0;
    }
    case QA_Q3_EV_JUMP_PAD: {
      qa_q3_character_presentation_effect e=kind_eff(QA_Q3_EFFECT_JUMP_PAD_SMOKE);
      e.radius=32; e.duration_milliseconds=1000; push(&s,e);
      push(&s,sound_eff("sound/world/jumppad.wav","voice")); push(&s,custom_eff("*jump1.wav","voice")); return s.count;
    }
    case QA_Q3_EV_JUMP: push(&s,custom_eff("*jump1.wav","voice")); return s.count;
    case QA_Q3_EV_TAUNT: push(&s,custom_eff("*taunt.wav","voice")); return s.count;
    case QA_Q3_EV_WATER_TOUCH: push(&s,sound_eff("sound/player/watr_in.wav","auto")); return s.count;
    case QA_Q3_EV_WATER_LEAVE: push(&s,sound_eff("sound/player/watr_out.wav","auto")); return s.count;
    case QA_Q3_EV_WATER_UNDER: push(&s,sound_eff("sound/player/watr_un.wav","auto")); return s.count;
    case QA_Q3_EV_WATER_CLEAR: push(&s,custom_eff("*gasp.wav","auto")); return s.count;
    case QA_Q3_EV_NOAMMO: if(options.local) push(&s,kind_eff(QA_Q3_EFFECT_OUT_OF_AMMO)); return s.count;
    case QA_Q3_EV_CHANGE_WEAPON: push(&s,sound_eff("sound/weapons/change.wav","auto")); return s.count;
    case QA_Q3_EV_FIRE_WEAPON: p->muzzle_flash_time=time; push(&s,kind_eff(QA_Q3_EFFECT_WEAPON_FIRE)); return s.count;
    case QA_Q3_EV_PLAYER_TELEPORT_IN: { push(&s,sound_eff("sound/world/telein.wav","auto"));
      qa_q3_character_presentation_effect e=kind_eff(QA_Q3_EFFECT_TELEPORT); e.direction="in"; push(&s,e); return s.count; }
    case QA_Q3_EV_PLAYER_TELEPORT_OUT: { push(&s,sound_eff("sound/world/teleout.wav","auto"));
      qa_q3_character_presentation_effect e=kind_eff(QA_Q3_EFFECT_TELEPORT); e.direction="out"; push(&s,e); return s.count; }
    case QA_Q3_EV_PAIN: return options.local?0:qa_q3_character_event_presenter_pain(p,time,source.parameter,out,capacity);
    case QA_Q3_EV_DEATH1: case QA_Q3_EV_DEATH2: case QA_Q3_EV_DEATH3: {
      char name[64]; snprintf(name,sizeof name,"*death%d.wav",event-QA_Q3_EV_DEATH1+1);
      push(&s,custom_eff(name,"voice")); return s.count;
    }
    case QA_Q3_EV_GIB_PLAYER:
      if((qa_js_to_int32(options.source_flags)&0x200)!=0) push(&s,kind_eff(QA_Q3_EFFECT_GIB_PLAYER));
      else { push(&s,sound_eff("sound/player/gibsplt1.wav","body")); push(&s,kind_eff(QA_Q3_EFFECT_GIB_PLAYER)); }
      return s.count;
    case QA_Q3_EV_STOPLOOPINGSOUND: push(&s,kind_eff(QA_Q3_EFFECT_STOP_LOOPING_SOUND)); return s.count;
    default: {
      qa_q3_character_presentation_effect e=kind_eff(QA_Q3_EFFECT_SOURCE_EVENT); e.event=source; push(&s,e); return s.count;
    }
  }
}
