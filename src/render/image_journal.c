/* Port of src/render/image-journal.ts. GPL-2.0-or-later. */
#include "qa/render/image_journal.h"
#include "qa/foundation/support.h"
#include <stdlib.h>
#include <string.h>
typedef struct update { double level; qa_image_resource_operation op; uint8_t *owned_bytes; float *owned_floats; qa_image_level lvl; qa_depth_image_level dlvl; } update;
typedef struct resident { double ordinal; bool before_texture_mode; qa_image_resource_operation creation;
  qa_image_level *levels; qa_depth_image_level *dlevels; uint8_t **owned; float **fowned; uint8_t *palette_colors; uint8_t *translation; qa_byte_view *translation_view; qa_fullbright_range *fullbright;
  update *updates; size_t update_count, update_cap; } resident;
typedef struct jimpl { resident *items; size_t count, cap; bool has_texture_mode; qa_image_resource_operation texture_mode; } jimpl;
static jimpl *J(qa_render_image_journal *j){ return (jimpl*)j->impl; }
static uint8_t *dup_bytes(const uint8_t *src,size_t n){ if(n==0)return NULL; uint8_t *o=(uint8_t*)malloc(n); if(!o)qa_range_error("image journal allocation failed"); memcpy(o,src,n); return o; }
static resident *find_res(jimpl *p,double ord){ for(size_t i=0;i<p->count;i++)if(p->items[i].ordinal==ord)return &p->items[i]; return NULL; }
static void free_resident(resident *r){
  if(r->owned){ for(size_t i=0;i<r->creation.content.level_count;i++)free(r->owned[i]); free(r->owned); }
  if(r->fowned){ for(size_t i=0;i<r->creation.content.depth_level_count;i++)free(r->fowned[i]); free(r->fowned); }
  free(r->levels);free(r->dlevels);free(r->palette_colors);free(r->translation);free(r->translation_view);free(r->fullbright);
  for(size_t i=0;i<r->update_count;i++){ free(r->updates[i].owned_bytes);free(r->updates[i].owned_floats); }
  free(r->updates); memset(r,0,sizeof *r);
}
bool qa_render_image_journal_init(qa_render_image_journal *j){ j->impl=calloc(1,sizeof(jimpl)); return j->impl!=NULL; }
void qa_render_image_journal_destroy(qa_render_image_journal *j){ if(!j->impl)return; qa_render_image_journal_clear(j); free(J(j)->items); free(j->impl); j->impl=NULL; }
void qa_render_image_journal_clear(qa_render_image_journal *j){ jimpl *p=J(j); for(size_t i=0;i<p->count;i++)free_resident(&p->items[i]); p->count=0; p->has_texture_mode=false; }
static void upload_res(qa_journal_backend b,resident *r){ b.apply_image_resource(b.ctx,r->creation); for(size_t i=0;i<r->update_count;i++){ update *u=&r->updates[i];
    /* The update array moves when it grows; rebase the self references. */
    if(u->op.update_depth_level)u->op.update_depth_level=&u->dlvl; else if(u->op.update_level)u->op.update_level=&u->lvl;
    b.apply_image_resource(b.ctx,u->op); } }
void qa_render_image_journal_replay(qa_render_image_journal *j,qa_journal_backend b){ jimpl *p=J(j);
  for(size_t i=0;i<p->count;i++)if(p->items[i].before_texture_mode)upload_res(b,&p->items[i]);
  if(p->has_texture_mode)b.apply_image_resource(b.ctx,p->texture_mode);
  for(size_t i=0;i<p->count;i++)if(!p->items[i].before_texture_mode)upload_res(b,&p->items[i]); }
void qa_render_image_journal_record(qa_render_image_journal *j,qa_image_resource_operation op){ jimpl *p=J(j);
  switch(op.kind){
  case QA_IMAGE_OP_CREATE: {
    resident *old=find_res(p,op.image.ordinal); if(old)free_resident(old);
    resident *r=old;
    if(!r){ if(p->count==p->cap){ size_t nc=p->cap?p->cap*2:8; resident *n=(resident*)realloc(p->items,nc*sizeof *n); if(!n)qa_range_error("image journal allocation failed"); p->items=n;p->cap=nc; }
      r=&p->items[p->count++]; memset(r,0,sizeof *r); }
    r->ordinal=op.image.ordinal; r->before_texture_mode=false; r->creation=op;
    qa_render_image *content=&r->creation.content;
    if(content->kind==QA_RENDER_IMAGE_DEPTH32F){
      r->dlevels=(qa_depth_image_level*)calloc(content->depth_level_count?content->depth_level_count:1,sizeof *r->dlevels);
      r->fowned=(float**)calloc(content->depth_level_count?content->depth_level_count:1,sizeof *r->fowned);
      if(!r->dlevels||!r->fowned)qa_range_error("image journal allocation failed");
      for(size_t i=0;i<content->depth_level_count;i++){ r->dlevels[i]=content->depth_levels[i];
        r->fowned[i]=(float*)malloc(content->depth_levels[i].pixel_count*sizeof(float)); if(content->depth_levels[i].pixel_count&&!r->fowned[i])qa_range_error("image journal allocation failed");
        if(content->depth_levels[i].pixel_count)memcpy(r->fowned[i],content->depth_levels[i].pixels,content->depth_levels[i].pixel_count*sizeof(float));
        r->dlevels[i].pixels=r->fowned[i]; }
      content->depth_levels=r->dlevels;
    } else {
      r->levels=(qa_image_level*)calloc(content->level_count?content->level_count:1,sizeof *r->levels);
      r->owned=(uint8_t**)calloc(content->level_count?content->level_count:1,sizeof *r->owned);
      if(!r->levels||!r->owned)qa_range_error("image journal allocation failed");
      for(size_t i=0;i<content->level_count;i++){ r->levels[i]=content->levels[i];
        r->owned[i]=dup_bytes(content->levels[i].pixels.bytes,content->levels[i].pixels.length);
        r->levels[i].pixels.bytes=r->owned[i]; }
      content->levels=r->levels;
      if(content->kind==QA_RENDER_IMAGE_INDEXED8){
        r->palette_colors=dup_bytes(content->palette.colors.bytes,content->palette.colors.length);
        content->palette.colors.bytes=r->palette_colors;
        /* Separately allocated: resident records move when the journal grows. */
        if(content->translation){ r->translation_view=(qa_byte_view*)malloc(sizeof *r->translation_view); if(!r->translation_view)qa_range_error("image journal allocation failed");
          r->translation=dup_bytes(content->translation->bytes,content->translation->length);
          r->translation_view->bytes=r->translation; r->translation_view->length=content->translation->length; content->translation=r->translation_view; }
        if(content->fullbright){ r->fullbright=(qa_fullbright_range*)malloc(sizeof *r->fullbright); if(!r->fullbright)qa_range_error("image journal allocation failed");
          *r->fullbright=*content->fullbright; content->fullbright=r->fullbright; }
      }
    }
    return; }
  case QA_IMAGE_OP_UPDATE: {
    resident *r=find_res(p,op.image.ordinal); if(!r) qa_range_error("Renderer update has no resident image");
    update *u=NULL;
    for(size_t i=0;i<r->update_count;i++)if(r->updates[i].level==op.level)u=&r->updates[i];
    if(!u){ if(r->update_count==r->update_cap){ size_t nc=r->update_cap?r->update_cap*2:4; update *n=(update*)realloc(r->updates,nc*sizeof *n); if(!n)qa_range_error("image journal allocation failed"); r->updates=n;r->update_cap=nc; }
      u=&r->updates[r->update_count++]; memset(u,0,sizeof *u); u->level=op.level; }
    free(u->owned_bytes);free(u->owned_floats);u->owned_bytes=NULL;u->owned_floats=NULL; u->op=op;
    if(op.update_depth_level){ u->dlvl=*op.update_depth_level;
      u->owned_floats=(float*)malloc(u->dlvl.pixel_count*sizeof(float)); if(u->dlvl.pixel_count&&!u->owned_floats)qa_range_error("image journal allocation failed");
      if (u->dlvl.pixel_count) memcpy(u->owned_floats, u->dlvl.pixels, u->dlvl.pixel_count * sizeof(float));
      u->dlvl.pixels = u->owned_floats; u->op.update_depth_level=&u->dlvl; }
    else if(op.update_level){ u->lvl=*op.update_level; u->owned_bytes=dup_bytes(u->lvl.pixels.bytes,u->lvl.pixels.length); u->lvl.pixels.bytes=u->owned_bytes; u->op.update_level=&u->lvl; }
    return; }
  case QA_IMAGE_OP_RELEASE: {
    for(size_t i=0;i<p->count;i++)if(p->items[i].ordinal==op.image.ordinal){ free_resident(&p->items[i]); memmove(&p->items[i],&p->items[i+1],(p->count-i-1)*sizeof *p->items); p->count--; break; }
    return; }
  case QA_IMAGE_OP_TEXTURE_MODE:
    p->texture_mode=op; p->has_texture_mode=true;
    for(size_t i=0;i<p->count;i++)p->items[i].before_texture_mode=true;
    return;
  }
}
size_t qa_render_image_journal_describe(qa_render_image_journal *j,qa_journal_image_description *out,size_t cap){ jimpl *p=J(j);
  for(size_t i=0;i<p->count&&i<cap;i++){ resident *r=&p->items[i];
    double w=0,h=0; int found=0;
    for(size_t u=0;u<r->update_count;u++)if(r->updates[u].level==0){ if(r->updates[u].op.update_depth_level){w=r->updates[u].dlvl.width;h=r->updates[u].dlvl.height;}else{w=r->updates[u].lvl.width;h=r->updates[u].lvl.height;} found=1; }
    if(!found){ if(r->creation.content.kind==QA_RENDER_IMAGE_DEPTH32F){w=r->creation.content.depth_levels[0].width;h=r->creation.content.depth_levels[0].height;}else{w=r->creation.content.levels[0].width;h=r->creation.content.levels[0].height;} }
    out[i].ordinal=r->creation.image.ordinal;
    out[i].name=r->creation.image.source_is_resource?r->creation.image.source_resource.requested_path:r->creation.image.source_name;
    out[i].width=w;out[i].height=h;out[i].encoding=r->creation.content.kind;
    out[i].mip_levels=(double)(r->creation.content.kind==QA_RENDER_IMAGE_DEPTH32F?r->creation.content.depth_level_count:r->creation.content.level_count); }
  return p->count; }
