/* Port of src/content/q3/foundation/animation-config.ts. GPL-2.0-or-later. */
#include "qa/content/q3/foundation/animation_config.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/game_numeric.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"

static void *xmalloc(size_t n){ void *p=malloc(n?n:1); if(!p) qa_range_error("out of memory"); return p; }
static char *xstrdup(const char *s){ size_t n=strlen(s)+1; char *p=xmalloc(n); memcpy(p,s,n); return p; }
static void lower_into(char *dst,size_t cap,const char *src){ size_t i=0; for(;src[i]&&i+1<cap;i++) dst[i]=(char)tolower((unsigned char)src[i]); dst[i]='\0'; }

const char *qa_q3_player_footsteps_name(qa_q3_player_footsteps v){
  switch(v){case QA_Q3_FOOTSTEPS_BOOT:return "boot";case QA_Q3_FOOTSTEPS_FLESH:return "flesh";
  case QA_Q3_FOOTSTEPS_MECH:return "mech";case QA_Q3_FOOTSTEPS_ENERGY:return "energy";default:return "normal";}}
const char *qa_q3_player_gender_name(qa_q3_player_gender v){
  return v==QA_Q3_GENDER_FEMALE?"female":v==QA_Q3_GENDER_NEUTER?"neuter":"male";}

void qa_q3_player_animation_config_destroy(qa_q3_player_animation_config *c){
  if(!c) return;
  for(size_t i=0;i<c->warning_count;i++) free(c->warnings[i].message);
  free(c->warnings); c->warnings=NULL; c->warning_count=0;
}

typedef struct ctx {
  const qa_q3_player_animation_parse_context *context;
  qa_common_parse_state *parser;
  qa_q3_animation_warning *warnings; size_t count, cap;
} ctx;
/* TS print(): %s substitution, 1024-byte buffer check on message length. */
static char *print_fmt(ctx *c, const char *format, const char *a, const char *b){
  char buf[4096]; const char *args[2]={a?a:"",b?b:""}; size_t ai=0,o=0;
  for(size_t i=0;format[i]&&o+1<sizeof buf;i++){
    if(format[i]=='%'&&format[i+1]=='s'){ const char *s=ai<2?args[ai++]:""; for(size_t j=0;s[j]&&o+1<sizeof buf;j++) buf[o++]=s[j]; i++; }
    else buf[o++]=format[i];
  }
  buf[o]='\0';
  if(o>=1024) qa_range_error("CG_Printf exceeds its 1024-byte source buffer");
  if(c->context&&c->context->print) c->context->print(c->context->print_ctx,buf);
  return xstrdup(buf);
}
static void warn_fmt(ctx *c, const char *format, const char *a, const char *b){
  char *m=print_fmt(c,format,a,b);
  if(c->count==c->cap){ c->cap=c->cap?c->cap*2:8; c->warnings=realloc(c->warnings,c->cap*sizeof *c->warnings); if(!c->warnings) qa_range_error("out of memory"); }
  c->warnings[c->count++]=(qa_q3_animation_warning){(double)((c->parser->line+1)|0),1,m};
}
static qa_q3_animation *cell(qa_q3_player_animation_target *t, int index){
  if(index<0||(size_t)index>=t->animation_count) qa_range_error("Missing client animation cell %d",index);
  return &t->animations[index];
}

void qa_q3_parse_player_animation_config(const char *text, const char *source,
    const qa_q3_player_animation_parse_context *context, qa_q3_player_animation_config *out){
  if(!source) source="<animation.cfg>";
  qa_q3_animation owned_cells[QA_Q3_TOTAL_ANIMATION_COUNT];
  qa_q3_player_animation_target owned={QA_Q3_FOOTSTEPS_NORMAL,{0,0,0},QA_Q3_GENDER_MALE,false,false,owned_cells,QA_Q3_TOTAL_ANIMATION_COUNT};
  for(int i=0;i<QA_Q3_TOTAL_ANIMATION_COUNT;i++) owned_cells[i]=(qa_q3_animation){0};
  qa_q3_player_animation_target *target=(context&&context->target)?context->target:&owned;
  qa_common_parse_state owned_parser; qa_common_parse_state_init(&owned_parser);
  qa_common_parse_state *parser=(context&&context->parser)?context->parser:&owned_parser;
  ctx c={context,parser,NULL,0,0};
  size_t length=strlen(text);
  if(length==0) qa_text_parse_error(source,1,1,"empty animation file");
  if(length>19998){ char *m=print_fmt(&c,"File %s too long\n",source,NULL); const char *mm=m; qa_text_parse_error(source,1,1,"%s",mm); }
  qa_common_parse_cursor cursor;
  if(!qa_common_parse_cursor_init(&cursor,text,length,false)) qa_range_error("out of memory");
  target->footsteps=QA_Q3_FOOTSTEPS_NORMAL; target->head_offset=(qa_vec3){0,0,0};
  target->gender=QA_Q3_GENDER_MALE; target->fixed_legs=false; target->fixed_torso=false;
  for(;;){
    bool prev_has=cursor.has_offset; size_t previous=cursor.offset;
    char *token=xstrdup(qa_common_parse_state_parse(parser,&cursor,true));
    char directive[1100]; lower_into(directive,sizeof directive,token);
    if(strcmp(directive,"footsteps")==0){
      char *value=xstrdup(qa_common_parse_state_parse(parser,&cursor,true));
      char low[1100]; lower_into(low,sizeof low,value);
      if(!strcmp(low,"default")||!strcmp(low,"normal")) target->footsteps=QA_Q3_FOOTSTEPS_NORMAL;
      else if(!strcmp(low,"boot")) target->footsteps=QA_Q3_FOOTSTEPS_BOOT;
      else if(!strcmp(low,"flesh")) target->footsteps=QA_Q3_FOOTSTEPS_FLESH;
      else if(!strcmp(low,"mech")) target->footsteps=QA_Q3_FOOTSTEPS_MECH;
      else if(!strcmp(low,"energy")) target->footsteps=QA_Q3_FOOTSTEPS_ENERGY;
      else warn_fmt(&c,"Bad footsteps parm in %s: %s\n",source,value);
      free(value);
    } else if(strcmp(directive,"headoffset")==0){
      double vals[3];
      for(int k=0;k<3;k++){ char *v=xstrdup(qa_common_parse_state_parse(parser,&cursor,true)); vals[k]=qa_game_atof(v); free(v); }
      target->head_offset=(qa_vec3){vals[0],vals[1],vals[2]};
    } else if(strcmp(directive,"sex")==0){
      char *v=xstrdup(qa_common_parse_state_parse(parser,&cursor,true));
      char first=v[0]?(char)tolower((unsigned char)v[0]):'\0';
      target->gender=first=='f'?QA_Q3_GENDER_FEMALE:first=='n'?QA_Q3_GENDER_NEUTER:QA_Q3_GENDER_MALE;
      free(v);
    } else if(strcmp(directive,"fixedlegs")==0){ target->fixed_legs=true; }
    else if(strcmp(directive,"fixedtorso")==0){ target->fixed_torso=true; }
    else {
      char first=token[0];
      if(first>='0'&&first<='9'){ if(prev_has) qa_common_parse_cursor_set_offset(&cursor,previous); else qa_common_parse_cursor_set_null(&cursor); free(token); break; }
      warn_fmt(&c,"unknown token '%s' is %s\n",token,source);
      if(prev_has==cursor.has_offset&&previous==cursor.offset){ free(token); qa_common_parse_cursor_destroy(&cursor); qa_range_error("CG animation prelude reached the source nonprogress cycle"); }
    }
    free(token);
  }
  int32_t skip=0; int index=0;
  for(;index<QA_Q3_SOURCE_ANIMATION_COUNT;index++){
    char *first_token=xstrdup(qa_common_parse_state_parse(parser,&cursor,true));
    qa_q3_animation *animation=cell(target,index);
    if(first_token[0]=='\0'){
      free(first_token);
      if(index>=QA_Q3_ANIM_TORSO_GETFLAG&&index<=QA_Q3_ANIM_TORSO_NEGATIVE){
        qa_q3_animation *g=cell(target,QA_Q3_ANIM_TORSO_GESTURE);
        animation->first_frame=g->first_frame; animation->frame_lerp=g->frame_lerp;
        animation->initial_lerp=g->initial_lerp; animation->loop_frames=g->loop_frames;
        animation->num_frames=g->num_frames; animation->reversed=false; animation->flipflop=false;
        continue;
      }
      break;
    }
    animation->first_frame=qa_game_atoi(first_token); free(first_token);
    if(index==QA_Q3_ANIM_LEGS_WALKCR) skip=(int32_t)((int32_t)animation->first_frame-(int32_t)cell(target,QA_Q3_ANIM_TORSO_GESTURE)->first_frame);
    if(index>=QA_Q3_ANIM_LEGS_WALKCR&&index<QA_Q3_ANIM_TORSO_GETFLAG) animation->first_frame=(double)(int32_t)((int32_t)animation->first_frame-skip);
    char *count_token=xstrdup(qa_common_parse_state_parse(parser,&cursor,true));
    if(count_token[0]=='\0'){ free(count_token); break; }
    animation->num_frames=qa_game_atoi(count_token); free(count_token);
    animation->reversed=false; animation->flipflop=false;
    if(animation->num_frames<0){ animation->num_frames=(double)(int32_t)(-(int32_t)animation->num_frames); animation->reversed=true; }
    char *loop_token=xstrdup(qa_common_parse_state_parse(parser,&cursor,true));
    if(loop_token[0]=='\0'){ free(loop_token); break; }
    animation->loop_frames=qa_game_atoi(loop_token); free(loop_token);
    char *fps_token=xstrdup(qa_common_parse_state_parse(parser,&cursor,true));
    if(fps_token[0]=='\0'){ free(fps_token); break; }
    double fps=qa_game_atof(fps_token); free(fps_token);
    if(fps==0) fps=1;
    animation->frame_lerp=qa_qvm_float_to_int((float)(1000.0/fps));
    animation->initial_lerp=qa_qvm_float_to_int((float)(1000.0/fps));
  }
  qa_common_parse_cursor_destroy(&cursor);
  if(index!=QA_Q3_SOURCE_ANIMATION_COUNT){
    char *m=print_fmt(&c,"Error parsing animation file: %s",source,NULL);
    qa_text_parse_error(source,(parser->line+1)|0,1,"%s",m);
  }
  /* Object.assign copies (flipflop retained from source cell for back*, as TS). */
  qa_q3_animation *backcr=cell(target,QA_Q3_ANIM_LEGS_BACKCR); *backcr=*cell(target,QA_Q3_ANIM_LEGS_WALKCR); backcr->reversed=true;
  qa_q3_animation *backwalk=cell(target,QA_Q3_ANIM_LEGS_BACKWALK); *backwalk=*cell(target,QA_Q3_ANIM_LEGS_WALK); backwalk->reversed=true;
  qa_q3_animation *fr=cell(target,QA_Q3_ANIM_FLAG_RUN); *fr=(qa_q3_animation){0,16,16,66,66,false,fr->flipflop};
  qa_q3_animation *fs=cell(target,QA_Q3_ANIM_FLAG_STAND); *fs=(qa_q3_animation){16,5,0,50,50,false,fs->flipflop};
  qa_q3_animation *fs2=cell(target,QA_Q3_ANIM_FLAG_STAND2RUN); *fs2=(qa_q3_animation){16,5,1,66,66,true,fs2->flipflop};
  *out=(qa_q3_player_animation_config){0};
  out->footsteps=target->footsteps; out->head_offset=target->head_offset; out->gender=target->gender;
  out->fixed_legs=target->fixed_legs; out->fixed_torso=target->fixed_torso;
  for(size_t i=0;i<target->animation_count&&i<QA_Q3_TOTAL_ANIMATION_COUNT;i++){
    out->animations[i]=target->animations[i]; out->animation_present[i]=i!=(size_t)QA_Q3_MAX_ANIMATIONS_SENTINEL;
  }
  out->warnings=c.warnings; out->warning_count=c.count;
}
