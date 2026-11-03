/* Port of src/render/cpu/textures.ts. GPL-2.0-or-later. */
#include "qa/render/cpu/textures.h"
#include "qa/formats/images/palette.h"
#include "qa/formats/images/mip.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
typedef struct tex_obj { double ordinal; qa_render_image content; qa_texture_storage *levels; size_t level_count; bool mipmap; qa_texture_sampling sampling; qa_vec4 border_color; } tex_obj;
typedef struct depth_obj { double ordinal; qa_depth_image_level *levels; size_t level_count; float **owned; } depth_obj;
typedef struct impl { tex_obj *tex; size_t tex_count, tex_cap; depth_obj *dep; size_t dep_count, dep_cap; bool has_unit[2]; double units[2]; } impl;
static impl *I(qa_cpu_images *im){ return (impl*)im->impl; }
static bool same_session(qa_session_id a,qa_session_id b){ return a.token==b.token; }
/* Number.isSafeInteger, without a C cast of NaN or a huge double. */
static bool safe_integer(double v){ return isfinite(v)&&v==floor(v)&&fabs(v)<=9007199254740991.0; }
static void require_owner(qa_cpu_images *im,qa_renderer_image image){
  if(image.owner.identity!=im->owner.identity||!same_session(image.owner.session,im->owner.session)||image.owner.generation!=im->owner.generation) qa_range_error("CPU image belongs to another renderer owner");
  if(!safe_integer(image.ordinal)||image.ordinal<0) qa_range_error("Invalid CPU image ordinal");
}
static tex_obj *find_tex(qa_cpu_images *im,double ord){ impl *p=I(im); for(size_t i=0;i<p->tex_count;i++)if(p->tex[i].ordinal==ord)return &p->tex[i]; return NULL; }
static depth_obj *find_dep(qa_cpu_images *im,double ord){ impl *p=I(im); for(size_t i=0;i<p->dep_count;i++)if(p->dep[i].ordinal==ord)return &p->dep[i]; return NULL; }
static tex_obj *require_tex(qa_cpu_images *im,qa_renderer_image image){ require_owner(im,image); tex_obj *o=find_tex(im,image.ordinal); if(!o)qa_range_error("CPU image has not been uploaded"); return o; }
static qa_texture_storage make_storage(const qa_image_level *level, bool *oom){
  qa_texture_storage s; memset(&s,0,sizeof s); *oom=false;
  if(!safe_integer(level->width)||!safe_integer(level->height)||level->width<=0||level->height<=0||(double)level->pixels.length!=level->width*level->height*4) qa_range_error("Invalid CPU RGBA image level");
  s.pixels=(uint8_t*)malloc(level->pixels.length?level->pixels.length:1); if(!s.pixels){*oom=true;return s;}
  memcpy(s.pixels,level->pixels.bytes,level->pixels.length);
  s.width=level->width;s.height=level->height;s.pixel_length=level->pixels.length;
  s.internal_format="rgba8";s.has_alpha=true;s.component_maximum=255;
  s.has_uniform=true; s.uniform.r=s.pixels[0]/255.0;s.uniform.g=s.pixels[1]/255.0;s.uniform.b=s.pixels[2]/255.0;s.uniform.a=s.pixels[3]/255.0;
  uint32_t first; memcpy(&first,s.pixels,4);
  for(size_t off=4;off+4<=s.pixel_length;off+=4){ uint32_t w; memcpy(&w,s.pixels+off,4); if(w!=first){s.has_uniform=false;break;} }
  return s;
}
static bool mip_filter(const char *f){ return strcmp(f,"nearest")&&strcmp(f,"linear"); }
static bool linear_filter(const char *f){ return !strcmp(f,"linear")||!strcmp(f,"linear-mipmap-nearest")||!strcmp(f,"linear-mipmap-linear"); }
bool qa_cpu_images_init(qa_cpu_images *im,qa_renderer_resource_owner owner){ im->owner=owner; im->impl=calloc(1,sizeof(impl)); return im->impl!=NULL; }
static void free_tex(tex_obj *o){ for(size_t i=0;i<o->level_count;i++)free(o->levels[i].pixels); free(o->levels); }
void qa_cpu_images_destroy(qa_cpu_images *im){ if(!im->impl)return; impl *p=I(im);
  for (size_t i = 0; i < p->tex_count; i++) free_tex(&p->tex[i]);
  free(p->tex);
  for(size_t i=0;i<p->dep_count;i++){ for(size_t j=0;j<p->dep[i].level_count;j++)free(p->dep[i].owned[j]); free(p->dep[i].owned); free(p->dep[i].levels);} free(p->dep); free(p); im->impl=NULL; }
void qa_cpu_images_validate(qa_cpu_images *im,qa_renderer_image image){ (void)require_tex(im,image); }
qa_depth_image_level qa_cpu_images_depth_image(qa_cpu_images *im,qa_renderer_image image){ require_owner(im,image); depth_obj *o=find_dep(im,image.ordinal); if(!o||o->level_count==0)qa_range_error("CPU image is not a registered depth atlas"); return o->levels[0]; }
void qa_cpu_images_apply(qa_cpu_images *im,qa_image_resource_operation op){
  impl *p=I(im);
  switch(op.kind){
  case QA_IMAGE_OP_CREATE: {
    require_owner(im,op.image);
    if(find_tex(im,op.image.ordinal)||find_dep(im,op.image.ordinal)) qa_range_error("CPU image is already uploaded");
    if(op.content.kind==QA_RENDER_IMAGE_DEPTH32F){
      if(p->dep_count==p->dep_cap){ size_t nc=p->dep_cap?p->dep_cap*2:8; depth_obj *nd=(depth_obj*)realloc(p->dep,nc*sizeof *nd); if(!nd)qa_range_error("CPU image allocation failed"); p->dep=nd;p->dep_cap=nc; }
      depth_obj *o=&p->dep[p->dep_count]; memset(o,0,sizeof *o); o->ordinal=op.image.ordinal; o->level_count=op.content.depth_level_count;
      o->levels=(qa_depth_image_level*)calloc(o->level_count?o->level_count:1,sizeof *o->levels); o->owned=(float**)calloc(o->level_count?o->level_count:1,sizeof *o->owned);
      if(!o->levels||!o->owned)qa_range_error("CPU image allocation failed");
      for(size_t i=0;i<o->level_count;i++){ const qa_depth_image_level *l=&op.content.depth_levels[i];
        if((double)l->pixel_count!=l->width*l->height||l->width<1||l->height<1) qa_range_error("Invalid CPU depth image dimensions");
        o->owned[i]=(float*)malloc(l->pixel_count*sizeof(float)); if(!o->owned[i])qa_range_error("CPU image allocation failed");
        memcpy(o->owned[i],l->pixels,l->pixel_count*sizeof(float));
        o->levels[i].width=l->width;o->levels[i].height=l->height;o->levels[i].pixels=o->owned[i];o->levels[i].pixel_count=l->pixel_count; }
      p->dep_count++; return;
    }
    if(op.content.level_count==0) qa_range_error("Invalid CPU RGBA image level");
    if(op.content.levels[0].width!=op.image.width||op.content.levels[0].height!=op.image.height) qa_range_error("CPU image dimensions disagree");
    if(p->tex_count==p->tex_cap){ size_t nc=p->tex_cap?p->tex_cap*2:8; tex_obj *nt=(tex_obj*)realloc(p->tex,nc*sizeof *nt); if(!nt)qa_range_error("CPU image allocation failed"); p->tex=nt;p->tex_cap=nc; }
    tex_obj *o=&p->tex[p->tex_count]; memset(o,0,sizeof *o); o->ordinal=op.image.ordinal; o->content=op.content; o->sampling=op.sampling; o->mipmap=mip_filter(op.sampling.filter);
    o->border_color=op.content.kind==QA_RENDER_IMAGE_RGBA8?op.content.border_color:(qa_vec4){0,0,0,0};
    o->levels=(qa_texture_storage*)calloc(op.content.level_count,sizeof *o->levels); if(!o->levels)qa_range_error("CPU image allocation failed");
    o->level_count=op.content.level_count;
    for(size_t i=0;i<o->level_count;i++){
      bool oom=false;
      if(op.content.kind==QA_RENDER_IMAGE_RGBA8){ o->levels[i]=make_storage(&op.content.levels[i],&oom); }
      else { qa_image_level ex; if(!qa_expand_indexed_image(&op.content,(int64_t)i,QA_PALETTE_LAYER_COMBINED,&ex))qa_range_error("CPU image allocation failed");
        o->levels[i]=make_storage(&ex,&oom); qa_image_level_destroy(&ex); }
      if(oom)qa_range_error("CPU image allocation failed");
    }
    p->tex_count++; return; }
  case QA_IMAGE_OP_UPDATE: {
    if(op.update_depth_level!=NULL){
      require_owner(im,op.image); depth_obj *o=find_dep(im,op.image.ordinal);
      /* levels?.[level]: a fractional, negative or NaN level reads undefined. */
      long li=safe_integer(op.level)&&op.level>=0?(long)op.level:-1;
      if(!o||li<0||(size_t)li>=o->level_count||o->levels[li].width!=op.update_depth_level->width||o->levels[li].height!=op.update_depth_level->height||o->levels[li].pixel_count!=op.update_depth_level->pixel_count) qa_range_error("CPU depth update dimensions disagree");
      memcpy(o->owned[li],op.update_depth_level->pixels,op.update_depth_level->pixel_count*sizeof(float)); return;
    }
    tex_obj *o=require_tex(im,op.image); long li=safe_integer(op.level)&&op.level>=0?(long)op.level:-1;
    if(li<0||(size_t)li>=o->level_count) qa_range_error("CPU update level is outside its image");
    if(op.update_level==NULL) qa_range_error("CPU update dimensions must match the existing image level");
    qa_image_level incoming=*op.update_level; qa_image_level ex; bool expanded=false;
    if(o->content.kind!=QA_RENDER_IMAGE_RGBA8){ qa_render_image tmp=o->content; tmp.levels=&incoming; tmp.level_count=1;
      if(!qa_expand_indexed_image(&tmp,0,QA_PALETTE_LAYER_COMBINED,&ex))qa_range_error("CPU image allocation failed");
        expanded = true; }
    const qa_image_level *upd=expanded?&ex:&incoming;
    if(o->levels[li].width!=upd->width||o->levels[li].height!=upd->height){ if(expanded)qa_image_level_destroy(&ex); qa_range_error("CPU update dimensions must match the existing image level"); }
    bool oom=false; qa_texture_storage ns=make_storage(upd,&oom); if(expanded)qa_image_level_destroy(&ex);
    if(oom)qa_range_error("CPU image allocation failed");
    free(o->levels[li].pixels); o->levels[li]=ns; return; }
  case QA_IMAGE_OP_RELEASE: {
    require_owner(im,op.image);
    for(size_t i=0;i<p->dep_count;i++)if(p->dep[i].ordinal==op.image.ordinal){ for(size_t j=0;j<p->dep[i].level_count;j++)free(p->dep[i].owned[j]); free(p->dep[i].owned);free(p->dep[i].levels); p->dep[i]=p->dep[--p->dep_count]; return; }
    (void)require_tex(im,op.image);
    for(size_t i=0;i<p->tex_count;i++)if(p->tex[i].ordinal==op.image.ordinal){ free_tex(&p->tex[i]); p->tex[i]=p->tex[--p->tex_count]; break; }
    for(int u=0;u<2;u++)if(p->has_unit[u]&&p->units[u]==op.image.ordinal)p->has_unit[u]=false;
    return; }
  case QA_IMAGE_OP_TEXTURE_MODE:
    for(size_t i=0;i<p->tex_count;i++)if(p->tex[i].mipmap)p->tex[i].sampling.filter=op.filter;
    return;
  }
  qa_range_error("Invalid CPU image operation");
}
void qa_cpu_images_bind(qa_cpu_images *im,double unit,qa_texture_binding op){
  if(op.kind==QA_TEXTURE_BINDING_DYNAMIC_IMAGE) qa_range_error("Dynamic texture must resolve before binding");
  if(op.kind==QA_TEXTURE_BINDING_RETAIN_CURRENT) return;
  (void)require_tex(im,op.image); impl *p=I(im); int u=unit==1?1:0; p->units[u]=op.image.ordinal; p->has_unit[u]=true;
}
qa_bound_texture qa_cpu_images_bound(qa_cpu_images *im,double unit,qa_texture_storage *storage,size_t cap){
  qa_bound_texture out; memset(&out,0,sizeof out); out.kind=QA_BOUND_TEXTURE_INCOMPLETE;
  impl *p=I(im); int u=unit==1?1:0; if(!p->has_unit[u])return out;
  tex_obj *o=find_tex(im,p->units[u]); if(!o||o->level_count==0||cap==0)return out;
  const char *filter=o->sampling.filter; bool linear=linear_filter(filter);
  const char *mip=!mip_filter(filter)?"none":(!strcmp(filter,"nearest-mipmap-linear")||!strcmp(filter,"linear-mipmap-linear"))?"linear":"nearest";
  size_t n=0; storage[n++]=o->levels[0];
  if(strcmp(mip,"none")){
    double w=o->levels[0].width,h=o->levels[0].height;
    for(size_t i=1;i<o->level_count&&(w>1||h>1);i++){ w=qa_js_max(1,floor(w/2));h=qa_js_max(1,floor(h/2));
      if(o->levels[i].width!=w||o->levels[i].height!=h)return out;
      if (n >= cap) return out;
      storage[n++] = o->levels[i]; }
  }
  out.kind=QA_BOUND_TEXTURE_IMAGE;out.internal_format="rgba8";out.wrap=o->sampling.wrap;
  out.minify_linear=linear;out.magnify_linear=linear;out.mipmapping=mip;out.magnification_limit=1;
  out.levels=storage;out.level_count=n;out.border_color=o->border_color; return out;
}
void qa_cpu_images_clear(qa_cpu_images *im){ impl *p=I(im);
  for (size_t i = 0; i < p->tex_count; i++) free_tex(&p->tex[i]);
  p->tex_count = 0;
  for(size_t i=0;i<p->dep_count;i++){for(size_t j=0;j<p->dep[i].level_count;j++)free(p->dep[i].owned[j]);free(p->dep[i].owned);free(p->dep[i].levels);} p->dep_count=0;
  p->has_unit[0]=p->has_unit[1]=false; }
