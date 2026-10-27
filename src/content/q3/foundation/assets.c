/* Port of src/content/q3/foundation/assets.ts. GPL-2.0-or-later. */
#include "qa/content/q3/foundation/assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"

static void *xmalloc(size_t n){ void*p=malloc(n?n:1); if(!p) qa_range_error("out of memory"); return p; }
static char *byte_text(const uint8_t *bytes,size_t n){
  char *t=xmalloc(n+1); for(size_t i=0;i<n;i++) t[i]=(char)bytes[i]; t[n]='\0'; return t;
}
static void component(const char *value,const char *name,bool star){
  const char *plain=(star&&value[0]=='*')?value+1:value;
  if(plain[0]=='\0'||strcmp(plain,".")==0||strcmp(plain,"..")==0||strchr(plain,'/')||strchr(plain,'\\'))
    qa_range_error("Invalid Q3 %s: %s",name,value);
}
typedef struct paths { char **values; size_t count, cap; } paths;
static void push_trunc(paths*p,const char*text,size_t limit){
  char *copy=xmalloc(strlen(text)+1); strcpy(copy,text);
  if(strlen(copy)>limit) copy[limit]='\0';
  if(p->count==p->cap){ p->cap=p->cap?p->cap*2:8; p->values=realloc(p->values,p->cap*sizeof *p->values); if(!p->values) qa_range_error("out of memory"); }
  p->values[p->count++]=copy;
}
static void free_paths(paths*p){ for(size_t i=0;i<p->count;i++) free(p->values[i]); free(p->values); }
static bool first_open(qa_q3_character_resources r,paths*p,qa_q3_character_opened_resource*out){
  for(size_t i=0;i<p->count;i++){
    if(r.open(r.ctx,p->values[i],out)){
      if(out->byte_length!=0) return true;
      free(out->bytes); out->bytes=NULL;
    }
  }
  return false;
}
static void required_open(qa_q3_character_resources r,paths*p,qa_q3_character_opened_resource*out){
  if(first_open(r,p,out)) return;
  size_t total=64; for(size_t i=0;i<p->count;i++) total+=strlen(p->values[i])+2;
  char *joined=xmalloc(total); joined[0]='\0';
  for(size_t i=0;i<p->count;i++){ if(i) strcat(joined,", "); strcat(joined,p->values[i]); }
  qa_range_error("Q3 character resource missing: %s",joined);
}
static paths body_files(const qa_q3_character_selection*s,const char*base,const char*team_name){
  paths p={0}; const char *team=s->team?s->team:"default";
  static const char *folders[2]={"","characters/"};
  char buf[1024];
  for(int f=0;f<2;f++) for(int pi=0;pi<(team_name[0]?2:1);pi++){
    const char *prefix=pi==0?team_name:"";
    snprintf(buf,sizeof buf,"models/players/%s%s/%s%s_%s_%s.skin",folders[f],s->model,prefix,base,s->skin,team);
    push_trunc(&p,buf,63);
    snprintf(buf,sizeof buf,"models/players/%s%s/%s%s_%s.skin",folders[f],s->model,prefix,base,s->team?s->team:s->skin);
    push_trunc(&p,buf,63);
  }
  return p;
}
static paths head_files(const qa_q3_character_selection*s,const char*base,const char*ext,const char*team_name){
  const char *model=s->head_model[0]?s->head_model:s->model;
  const char *name=model[0]=='*'?model+1:model;
  paths p={0}; const char *team=s->team?s->team:"default";
  size_t limit=strcmp(base,"head")==0?63:127;
  char buf[1024];
  int folders_n=model[0]=='*'?1:2;
  for(int f=0;f<folders_n;f++){
    const char *folder=model[0]=='*'?"heads/":(f==0?"":"heads/");
    for(int pi=0;pi<(team_name[0]?2:1);pi++){
      const char *prefix=pi==0?team_name:"";
      snprintf(buf,sizeof buf,"models/players/%s%s/%s/%s%s_%s.%s",folder,name,s->head_skin,prefix,base,team,ext);
      push_trunc(&p,buf,limit);
      snprintf(buf,sizeof buf,"models/players/%s%s/%s%s_%s.%s",folder,name,prefix,base,s->team?s->team:s->head_skin,ext);
      push_trunc(&p,buf,limit);
    }
  }
  return p;
}
static paths two_paths(const char*a,const char*b){ paths p={0}; push_trunc(&p,a,4096); if(b) push_trunc(&p,b,4096); return p; }
static qa_q3_character_part make_part(qa_q3_character_opened_resource*mesh,qa_q3_character_opened_resource*skin){
  qa_q3_character_part part={0};
  part.resource=mesh->reference; part.skin_resource=skin->reference;
  qa_parse_md3(mesh->bytes,mesh->byte_length,mesh->reference.requested_path,&part.decoded);
  qa_to_scene_md3(&part.decoded.model,&part.scene);
  part.model=part.scene.model;
  char *text=byte_text(skin->bytes,skin->byte_length);
  part.surfaces=qa_parse_skin(text,&part.surface_count); free(text);
  free(mesh->bytes); free(skin->bytes);
  return part;
}
void qa_q3_load_character(qa_q3_character_resources resources, qa_q3_character_selection selection,
    qa_q3_character_assets *out){
  component(selection.model,"model",false); component(selection.skin,"skin",false);
  if(selection.head_model[0]) component(selection.head_model,"head model",true);
  component(selection.head_skin,"head skin",false);
  if(selection.team_name[0]) component(selection.team_name,"team name",false);
  const char *model=selection.model, *head=selection.head_model[0]?selection.head_model:model;
  const char *head_name=head[0]=='*'?head+1:head;
  char b1[512],b2[512];
  qa_q3_character_opened_resource lower={0},upper={0},face={0},animation={0};
  { snprintf(b1,sizeof b1,"models/players/%s/lower.md3",model); snprintf(b2,sizeof b2,"models/players/characters/%s/lower.md3",model);
    paths p=two_paths(b1,b2); required_open(resources,&p,&lower); free_paths(&p); }
  { snprintf(b1,sizeof b1,"models/players/%s/upper.md3",model); snprintf(b2,sizeof b2,"models/players/characters/%s/upper.md3",model);
    paths p=two_paths(b1,b2); required_open(resources,&p,&upper); free_paths(&p); }
  if(head[0]=='*'){ snprintf(b1,sizeof b1,"models/players/heads/%s/%s.md3",head_name,head_name);
    paths p=two_paths(b1,NULL); required_open(resources,&p,&face); free_paths(&p); }
  else { snprintf(b1,sizeof b1,"models/players/%s/head.md3",head); snprintf(b2,sizeof b2,"models/players/heads/%s/%s.md3",head_name,head_name);
    paths p=two_paths(b1,b2); required_open(resources,&p,&face); free_paths(&p); }
  { snprintf(b1,sizeof b1,"models/players/%s/animation.cfg",model); snprintf(b2,sizeof b2,"models/players/characters/%s/animation.cfg",model);
    paths p=two_paths(b1,b2); required_open(resources,&p,&animation); free_paths(&p); }
  char team_names[2][256]; int team_name_count;
  if(selection.team_name[0]){
    snprintf(team_names[0],sizeof team_names[0],"%s/",selection.team_name);
    snprintf(team_names[1],sizeof team_names[1],"%s",selection.team&&strcmp(selection.team,"blue")==0?"Pagans/":"Stroggs/");
    team_name_count=2;
  } else { team_names[0][0]='\0'; team_name_count=1; }
  qa_q3_character_opened_resource skins[3]={0}; bool have_skins=false;
  for(int t=0;t<team_name_count&&!have_skins;t++){
    paths lp=body_files(&selection,"lower",team_names[t]);
    paths up=body_files(&selection,"upper",team_names[t]);
    paths hp=head_files(&selection,"head","skin",team_names[t]);
    qa_q3_character_opened_resource legs={0},torso={0},hs={0};
    bool a=first_open(resources,&lp,&legs), b=first_open(resources,&up,&torso), c=first_open(resources,&hp,&hs);
    free_paths(&lp); free_paths(&up); free_paths(&hp);
    if(a&&b&&c){ skins[0]=legs; skins[1]=torso; skins[2]=hs; have_skins=true; }
    else { free(legs.bytes); free(torso.bytes); free(hs.bytes); }
  }
  if(!have_skins) qa_range_error("Q3 character skin missing: %s/%s, %s/%s",model,selection.skin,head,selection.head_skin);
  *out=(qa_q3_character_assets){0}; out->selection=selection;
  out->lower=make_part(&lower,&skins[0]); out->upper=make_part(&upper,&skins[1]); out->head=make_part(&face,&skins[2]);
  out->animation_resource=animation.reference;
  char *anim_text=byte_text(animation.bytes,animation.byte_length);
  qa_q3_parse_player_animation_config(anim_text,animation.reference.requested_path,NULL,&out->animation);
  free(anim_text); free(animation.bytes);
  char icon_team[256]; snprintf(icon_team,sizeof icon_team,"%s",selection.team_name[0]?"":"");
  if(selection.team_name[0]) snprintf(icon_team,sizeof icon_team,"%s/",selection.team_name);
  paths ip=head_files(&selection,"icon","skin",icon_team);
  paths ip2=head_files(&selection,"icon","tga",icon_team);
  for(size_t i=0;i<ip2.count;i++) push_trunc(&ip,ip2.values[i],4096);
  free_paths(&ip2);
  qa_q3_character_opened_resource icon={0};
  if(first_open(resources,&ip,&icon)){ out->has_icon=true; out->icon=icon.reference; free(icon.bytes); }
  free_paths(&ip);
}
static void destroy_part(qa_q3_character_part *p){
  qa_decoded_scene_md3_destroy(&p->scene); qa_decoded_md3_model_destroy(&p->decoded);
  qa_skin_surfaces_destroy(p->surfaces,p->surface_count); p->surfaces=NULL; p->surface_count=0;
}
void qa_q3_character_assets_destroy(qa_q3_character_assets *a){
  if(!a) return;
  destroy_part(&a->lower); destroy_part(&a->upper); destroy_part(&a->head);
  qa_q3_player_animation_config_destroy(&a->animation);
}
