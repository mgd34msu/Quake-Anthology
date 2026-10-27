/* Port of src/render/commands/dynamic-texture.ts. GPL-2.0-or-later. */
#include "qa/render/commands/dynamic_texture.h"
#include "qa/foundation/support.h"
#include <stdlib.h>
#include <string.h>
typedef qa_renderer_image (*resolve_fn)(void *, void (*)(void *, qa_image_resource_operation), void *);
typedef struct entry { const void *source_ctx; resolve_fn resolve; qa_renderer_image image; } entry;
typedef struct rimpl { entry *entries; size_t count, cap; } rimpl;
bool qa_texture_resolver_init(qa_texture_resolver *r,qa_image_operation_apply_fn apply,void *ctx){ r->impl=calloc(1,sizeof(rimpl)); r->apply=apply; r->apply_ctx=ctx; return r->impl!=NULL; }
void qa_texture_resolver_destroy(qa_texture_resolver *r){ if(!r->impl)return; free(((rimpl*)r->impl)->entries); free(r->impl); r->impl=NULL; }
static void apply_fwd(void *ctx,qa_image_resource_operation op){ qa_texture_resolver *r=(qa_texture_resolver*)ctx; r->apply(r->apply_ctx,op); }
qa_texture_binding qa_texture_resolver_resolve(qa_texture_resolver *r,qa_texture_binding binding){
  if(binding.kind!=QA_TEXTURE_BINDING_DYNAMIC_IMAGE) return binding;
  rimpl *p=(rimpl*)r->impl;
  for(size_t i=0;i<p->count;i++) if(p->entries[i].source_ctx==binding.source.ctx&&p->entries[i].resolve==binding.source.resolve){
    qa_texture_binding o; memset(&o,0,sizeof o); o.kind=QA_TEXTURE_BINDING_BIND_IMAGE; o.image=p->entries[i].image; return o; }
  qa_renderer_image image=binding.source.resolve(binding.source.ctx,apply_fwd,r);
  if(p->count==p->cap){ size_t nc=p->cap?p->cap*2:8; entry *ne=(entry*)realloc(p->entries,nc*sizeof *ne); if(!ne)qa_range_error("texture resolver allocation failed"); p->entries=ne;p->cap=nc; }
  p->entries[p->count].source_ctx=binding.source.ctx; p->entries[p->count].resolve=binding.source.resolve; p->entries[p->count].image=image; p->count++;
  qa_texture_binding o; memset(&o,0,sizeof o); o.kind=QA_TEXTURE_BINDING_BIND_IMAGE; o.image=image; return o;
}
static bool binding_same(qa_texture_binding a,qa_texture_binding b){
  if(a.kind!=b.kind)return false;
  if(a.kind==QA_TEXTURE_BINDING_DYNAMIC_IMAGE) return a.source.ctx==b.source.ctx&&a.source.resolve==b.source.resolve;
  if(a.kind==QA_TEXTURE_BINDING_BIND_IMAGE) return a.image.ordinal==b.image.ordinal&&a.image.owner.identity==b.image.owner.identity;
  return true;
}
bool qa_resolve_draw_textures(qa_draw_batch batch,qa_image_operation_apply_fn apply,void *apply_ctx,qa_draw_batch *out){
  qa_texture_resolver resolver; if(!qa_texture_resolver_init(&resolver,apply,apply_ctx)) qa_range_error("texture resolver allocation failed");
  qa_texture_binding texture=qa_texture_resolver_resolve(&resolver,batch.texture);
  bool changed;
  if(!batch.texturing_is_pair){ changed=!binding_same(texture,batch.texture); if(changed){ *out=batch; out->texture=texture; } }
  else { qa_texture_binding binding=qa_texture_resolver_resolve(&resolver,batch.second_texture.binding);
    changed=!(binding_same(texture,batch.texture)&&binding_same(binding,batch.second_texture.binding));
    if(changed){ *out=batch; out->texture=texture; out->second_texture.binding=binding; } }
  qa_texture_resolver_destroy(&resolver); return changed;
}
