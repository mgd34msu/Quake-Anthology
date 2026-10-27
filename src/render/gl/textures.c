/* Port of src/render/gl/textures.ts. GPL-2.0-or-later. */
#include "qa/render/gl/textures.h"
#include "qa/formats/images/palette.h"
#include "qa/formats/images/mip.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
void qa_gl_with_pixel_store(qa_gl *gl,const char *direction,qa_gl_pixel_store_fn operation,void *ctx){
  int pack=!strcmp(direction,"pack");
  int32_t state=0; gl->glGetIntegerv(pack?0x88ed:0x88ef,&state);
  if(state!=0) qa_range_error("OpenGL pixel transfers require client memory without a bound pixel buffer");
  const uint32_t pack_settings[5]={0xd05,0xd02,0xd03,0xd04,0xd00};
  const uint32_t unpack_settings[5]={0xcf5,0xcf2,0xcf3,0xcf4,0xcf0};
  const uint32_t *settings=pack?pack_settings:unpack_settings;
  int32_t saved[5];
  for(int i=0;i<5;i++)gl->glGetIntegerv(settings[i],&saved[i]);
  for(int i=0;i<5;i++)gl->glPixelStorei(settings[i],i==0?1:0);
  operation(ctx);
  for(int i=0;i<5;i++)gl->glPixelStorei(settings[i],saved[i]);
}
typedef struct tex { double ordinal; uint32_t name; bool mipmap; double width,height; qa_render_image content; double (*levels)[2]; size_t level_count; } tex;
typedef struct timpl { qa_gl *gl; qa_renderer_resource_owner owner; double maximum; tex *items; size_t count,cap; } timpl;
static timpl *T(qa_gl_textures *t){ return (timpl*)t->impl; }
static void own(qa_gl_textures *t,qa_renderer_image image){
  timpl *p=T(t);
  if(image.owner.identity!=p->owner.identity||image.owner.session.token!=p->owner.session.token||image.owner.generation!=p->owner.generation) qa_range_error("OpenGL image belongs to another renderer owner");
  if(image.ordinal!=(double)(long long)image.ordinal||image.ordinal<0) qa_range_error("OpenGL image ordinal must be a nonnegative safe integer");
}
static tex *find_tex(qa_gl_textures *t,double ord){ timpl *p=T(t); for(size_t i=0;i<p->count;i++)if(p->items[i].ordinal==ord)return &p->items[i]; return NULL; }
qa_gl_texture_info qa_gl_textures_registered(qa_gl_textures *t,qa_renderer_image image){ own(t,image); tex *x=find_tex(t,image.ordinal);
  if(!x)qa_range_error("OpenGL image is not registered");
  if(image.width!=x->width||image.height!=x->height)qa_range_error("OpenGL image handle dimensions differ from its allocation");
  qa_gl_texture_info o;o.name=x->name;o.mipmap=x->mipmap;o.width=x->width;o.height=x->height;o.content_kind=x->content.kind;return o; }
void qa_gl_textures_bind(qa_gl_textures *t,qa_texture_binding binding){
  if(binding.kind==QA_TEXTURE_BINDING_DYNAMIC_IMAGE) qa_range_error("Dynamic texture must resolve before binding");
  if(binding.kind==QA_TEXTURE_BINDING_BIND_IMAGE) T(t)->gl->glBindTexture(0xde1,qa_gl_textures_registered(t,binding.image).name);
}
static void apply_filter(qa_gl *gl,const char *filter){
  uint32_t mn=0x2600,mag=0x2600;
  if(!strcmp(filter,"linear")){mn=0x2601;mag=0x2601;}
  else if(!strcmp(filter,"nearest-mipmap-nearest")){mn=0x2700;mag=0x2600;}
  else if(!strcmp(filter,"linear-mipmap-nearest")){mn=0x2701;mag=0x2601;}
  else if(!strcmp(filter,"nearest-mipmap-linear")){mn=0x2702;mag=0x2600;}
  else if(!strcmp(filter,"linear-mipmap-linear")){mn=0x2703;mag=0x2601;}
  gl->glTexParameteri(0xde1,0x2801,(int32_t)mn); gl->glTexParameteri(0xde1,0x2800,(int32_t)mag);
}
static void check_dims(qa_gl_textures *t,double w,double h,size_t pixel_count,double channels,int is_depth,const float *fpixels){
  timpl *p=T(t);
  if(w!=(double)(long long)w||h!=(double)(long long)h||w<1||h<1||w>p->maximum||h>p->maximum||pixel_count!=(size_t)(w*h*channels)) qa_range_error("OpenGL image dimensions do not match its pixel allocation");
  if(is_depth) for(size_t i=0;i<pixel_count;i++) if(!isfinite(fpixels[i])||fpixels[i]<0||fpixels[i]>1) qa_range_error("OpenGL depth samples must be finite values in 0..1");
}
typedef struct upload_ctx { qa_gl *gl; const qa_render_image *content; const qa_image_level *levels; } upload_ctx;
static void do_upload(void *ctxp){ upload_ctx *c=(upload_ctx*)ctxp;
  if(c->content->kind==QA_RENDER_IMAGE_DEPTH32F){ for(size_t i=0;i<c->content->depth_level_count;i++) c->gl->glTexImage2D(0xde1,(int32_t)i,0x8cac,(int32_t)c->content->depth_levels[i].width,(int32_t)c->content->depth_levels[i].height,0,0x1902,0x1406,c->content->depth_levels[i].pixels); }
  else for(size_t i=0;i<c->content->level_count;i++) c->gl->glTexImage2D(0xde1,(int32_t)i,0x8058,(int32_t)c->levels[i].width,(int32_t)c->levels[i].height,0,0x1908,0x1401,c->levels[i].pixels.bytes);
}
typedef struct update_ctx { qa_gl *gl; double level; double w,h; int is_depth; const void *pixels; } update_ctx;
static void do_update(void *ctxp){ update_ctx *c=(update_ctx*)ctxp; c->gl->glTexSubImage2D(0xde1,(int32_t)c->level,0,0,(int32_t)c->w,(int32_t)c->h,c->is_depth?0x1902:0x1908,c->is_depth?0x1406:0x1401,c->pixels); }
bool qa_gl_textures_init(qa_gl_textures *t,qa_gl *gl,qa_renderer_resource_owner owner,double maximum){ t->impl=calloc(1,sizeof(timpl)); if(!t->impl)return false; T(t)->gl=gl;T(t)->owner=owner;T(t)->maximum=maximum; return true; }
void qa_gl_textures_close(qa_gl_textures *t){ if(!t->impl)return; timpl *p=T(t);
  for(size_t i=0;i<p->count;i++){ p->gl->glDeleteTextures(1,&p->items[i].name); free(p->items[i].levels); }
  free(p->items); free(p); t->impl=NULL; }
void qa_gl_textures_apply(qa_gl_textures *t,qa_image_resource_operation op){
  timpl *p=T(t); qa_gl *gl=p->gl;
  if(op.kind==QA_IMAGE_OP_TEXTURE_MODE){
    for(size_t i=0;i<p->count;i++){ if(!p->items[i].mipmap)continue; gl->glBindTexture(0xde1,p->items[i].name); apply_filter(gl,op.filter); }
    return;
  }
  own(t,op.image);
  switch(op.kind){
  case QA_IMAGE_OP_RELEASE: {
    tex *x=find_tex(t,op.image.ordinal); if(!x)qa_range_error("OpenGL image is not registered");
    gl->glDeleteTextures(1,&x->name); free(x->levels);
    size_t idx=(size_t)(x-p->items); memmove(&p->items[idx],&p->items[idx+1],(p->count-idx-1)*sizeof *p->items); p->count--; return; }
  case QA_IMAGE_OP_CREATE: {
    if(find_tex(t,op.image.ordinal)) qa_range_error("OpenGL image is already registered");
    const qa_render_image *content=&op.content;
    double fw,fh; size_t lc;
    if(content->kind==QA_RENDER_IMAGE_DEPTH32F){ if(content->depth_level_count==0)qa_range_error("OpenGL image dimensions do not match its pixel allocation"); fw=content->depth_levels[0].width;fh=content->depth_levels[0].height;lc=content->depth_level_count; }
    else { if(content->level_count==0)qa_range_error("OpenGL image dimensions do not match its pixel allocation"); fw=content->levels[0].width;fh=content->levels[0].height;lc=content->level_count; }
    if(op.image.width!=fw||op.image.height!=fh) qa_range_error("OpenGL image descriptor dimensions differ from its base mip level");
    for(size_t i=0;i<lc;i++){
      double w,h; size_t pc;
      if(content->kind==QA_RENDER_IMAGE_DEPTH32F){ w=content->depth_levels[i].width;h=content->depth_levels[i].height;pc=content->depth_levels[i].pixel_count; check_dims(t,w,h,pc,1,1,content->depth_levels[i].pixels); }
      else { w=content->levels[i].width;h=content->levels[i].height;pc=content->levels[i].pixels.length; check_dims(t,w,h,pc,content->kind==QA_RENDER_IMAGE_RGBA8?4:1,0,NULL); }
      double ew=fmax(1,floor(fw/pow(2,(double)i))),eh=fmax(1,floor(fh/pow(2,(double)i)));
      if(w!=ew||h!=eh||(double)i>floor(log2(fmax(fw,fh)))) qa_range_error("OpenGL mip levels must halve their preceding dimensions");
    }
    qa_image_level *expanded=NULL;
    if(content->kind==QA_RENDER_IMAGE_INDEXED8){
      expanded=(qa_image_level*)calloc(lc,sizeof *expanded); if(!expanded)qa_range_error("OpenGL texture allocation failed");
      for(size_t i=0;i<lc;i++) if(!qa_expand_indexed_image(content,(int64_t)i,QA_PALETTE_LAYER_COMBINED,&expanded[i]))qa_range_error("OpenGL texture allocation failed");
    }
    uint32_t name=0; gl->glGenTextures(1,&name);
    if(name==0){ if(expanded){for(size_t i=0;i<lc;i++)qa_image_level_destroy(&expanded[i]);free(expanded);} qa_range_error("OpenGL could not allocate a texture"); }
    gl->glBindTexture(0xde1,name);
    upload_ctx uc; uc.gl=gl;uc.content=content;uc.levels=expanded?expanded:content->levels;
    qa_gl_with_pixel_store(gl,"unpack",do_upload,&uc);
    gl->glTexParameteri(0xde1,0x813d,(int32_t)lc-1);
    apply_filter(gl,op.sampling.filter);
    uint32_t wrap=!strcmp(op.sampling.wrap,"repeat")?0x2901:content->kind==QA_RENDER_IMAGE_DEPTH32F?0x812f:0x2900;
    gl->glTexParameteri(0xde1,0x2802,(int32_t)wrap); gl->glTexParameteri(0xde1,0x2803,(int32_t)wrap);
    if(content->kind==QA_RENDER_IMAGE_RGBA8){ float col[4]={(float)content->border_color.x,(float)content->border_color.y,(float)content->border_color.z,(float)content->border_color.w}; gl->glTexParameterfv(0xde1,0x1004,col); }
    uint32_t err=gl->glGetError();
    if(expanded){ for(size_t i=0;i<lc;i++)qa_image_level_destroy(&expanded[i]); }
    if(err!=0){ gl->glDeleteTextures(1,&name); free(expanded); qa_range_error("OpenGL texture upload failed: 0x%x",err); }
    if(p->count==p->cap){ size_t nc=p->cap?p->cap*2:8; tex *n=(tex*)realloc(p->items,nc*sizeof *n); if(!n)qa_range_error("OpenGL texture allocation failed"); p->items=n;p->cap=nc; }
    tex *x=&p->items[p->count++]; memset(x,0,sizeof *x);
    x->ordinal=op.image.ordinal;x->name=name;x->width=fw;x->height=fh;x->content=*content;
    x->mipmap=strcmp(op.sampling.filter,"nearest")&&strcmp(op.sampling.filter,"linear");
    x->levels=(double(*)[2])calloc(lc,sizeof(*x->levels)); if(!x->levels)qa_range_error("OpenGL texture allocation failed");
    x->level_count=lc;
    for(size_t i=0;i<lc;i++){ if(content->kind==QA_RENDER_IMAGE_DEPTH32F){x->levels[i][0]=content->depth_levels[i].width;x->levels[i][1]=content->depth_levels[i].height;} else if(expanded){x->levels[i][0]=expanded[i].width;x->levels[i][1]=expanded[i].height;} else {x->levels[i][0]=content->levels[i].width;x->levels[i][1]=content->levels[i].height;} }
    free(expanded); return; }
  case QA_IMAGE_OP_UPDATE: {
    tex *x=find_tex(t,op.image.ordinal); if(!x)qa_range_error("OpenGL image is not registered");
    long li=(long)op.level;
    double uw=op.update_depth_level?op.update_depth_level->width:op.update_level?op.update_level->width:0;
    double uh=op.update_depth_level?op.update_depth_level->height:op.update_level?op.update_level->height:0;
    if(op.level!=(double)li||li<0||(size_t)li>=x->level_count||x->levels[li][0]!=uw||x->levels[li][1]!=uh) qa_range_error("OpenGL update dimensions differ from the registered mip level");
    update_ctx uc; uc.gl=gl;uc.level=op.level;uc.w=uw;uc.h=uh;
    qa_image_level ex; int have_ex=0;
    if(x->content.kind==QA_RENDER_IMAGE_DEPTH32F){
      if(!op.update_depth_level) qa_range_error("OpenGL depth updates require float32 samples");
      check_dims(t,uw,uh,op.update_depth_level->pixel_count,1,1,op.update_depth_level->pixels);
      uc.is_depth=1;uc.pixels=op.update_depth_level->pixels;
    } else {
      if(!op.update_level) qa_range_error("OpenGL color updates require byte samples");
      check_dims(t,uw,uh,op.update_level->pixels.length,x->content.kind==QA_RENDER_IMAGE_RGBA8?4:1,0,NULL);
      uc.is_depth=0;
      if(x->content.kind==QA_RENDER_IMAGE_INDEXED8){ qa_render_image tmp=x->content; tmp.levels=op.update_level;tmp.level_count=1;
        if(!qa_expand_indexed_image(&tmp,0,QA_PALETTE_LAYER_COMBINED,&ex))qa_range_error("OpenGL texture allocation failed");
        have_ex = 1; uc.pixels = ex.pixels.bytes; }
      else uc.pixels=op.update_level->pixels.bytes;
    }
    gl->glBindTexture(0xde1,x->name);
    qa_gl_with_pixel_store(gl,"unpack",do_update,&uc);
    if(have_ex)qa_image_level_destroy(&ex);
    uint32_t err=gl->glGetError();
    if(err!=0) qa_range_error("OpenGL texture update failed: 0x%x",err);
    return; }
  default: qa_range_error("Invalid OpenGL image operation"); }
}
