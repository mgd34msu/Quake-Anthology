/* Port of src/render/worker-protocol.ts (direct/pumped). GPL-2.0-or-later. */
#include "qa/render/worker_protocol.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static double wire_integer(double v){ if(v!=(double)(long long)v||v<0)qa_range_error("Invalid renderer wire integer"); return v; }
static const char *choice(const char *value,const char *const *choices,size_t n){ for(size_t i=0;i<n;i++)if(!strcmp(value,choices[i]))return choices[i]; qa_range_error("Invalid renderer wire variant"); }
static const char *const CULLS[]={"none","back","front"};
static const char *const BLENDS[]={"zero","one","src-color","one-minus-src-color","dst-color","one-minus-dst-color","src-alpha","one-minus-src-alpha","dst-alpha","one-minus-dst-alpha","src-alpha-saturate"};
static const char *const FILTERS[]={"nearest","linear","nearest-mipmap-nearest","linear-mipmap-nearest","nearest-mipmap-linear","linear-mipmap-linear"};
static const char *const DEPTH_TESTS[]={"less-equal","equal","always"};
static const char *const ALPHA_TESTS[]={"none","gt0","lt128","ge128"};
static void validate_state(qa_render_state s){
  (void)choice(s.blend.source,BLENDS,11);(void)choice(s.blend.destination,BLENDS,11);
  (void)choice(s.depth_test,DEPTH_TESTS,3);(void)choice(s.alpha_test,ALPHA_TESTS,4);(void)choice(s.cull,CULLS,3);
}
qa_image_level qa_decode_wire_image_level(qa_image_level level){
  double w=wire_integer(level.width),h=wire_integer(level.height);
  if(level.pixels.length!=(size_t)(w*h*4)) qa_range_error("Invalid renderer RGBA readback");
  level.width=w;level.height=h; return level;
}
typedef struct img_entry { double ordinal; qa_renderer_image image; } img_entry;
typedef struct src_entry { double token; qa_dynamic_image_source source; } src_entry;
typedef struct ack_entry { double id; qa_image_resource_operation op; } ack_entry;
typedef struct eimpl { img_entry *images; size_t image_count,image_cap; src_entry *sources; size_t source_count,source_cap; const void **src_keys; double next_token, next_ack; ack_entry *acks; size_t ack_count,ack_cap; } eimpl;
static eimpl *E(qa_wire_encoder *e){ return (eimpl*)e->impl; }
bool qa_wire_encoder_init(qa_wire_encoder *e,qa_renderer_resource_owner owner){ e->impl=calloc(1,sizeof(eimpl)); if(!e->impl)return false; e->owner=owner; E(e)->next_token=1;E(e)->next_ack=1; return true; }
void qa_wire_encoder_destroy(qa_wire_encoder *e){ if(!e->impl)return; eimpl *p=E(e); free(p->images);free(p->sources);free(p->src_keys);free(p->acks);free(p); e->impl=NULL; }
void qa_wire_encoder_clear_sources(qa_wire_encoder *e){ eimpl *p=E(e); p->source_count=0; }
void qa_wire_encoder_clear(qa_wire_encoder *e){ eimpl *p=E(e); p->image_count=0;p->source_count=0;p->ack_count=0; }
static void *grow(void *ptr,size_t *cap,size_t elem){ size_t nc=*cap?*cap*2:8; void *n=realloc(ptr,nc*elem); if(!n)qa_range_error("wire encoder allocation failed"); *cap=nc; return n; }
qa_renderer_image qa_wire_encoder_original_image(qa_wire_encoder *e,double ordinal){ eimpl *p=E(e);
  for(size_t i=0;i<p->image_count;i++)if(p->images[i].ordinal==ordinal)return p->images[i].image;
  qa_range_error("Unknown renderer image ordinal"); }
qa_dynamic_image_source qa_wire_encoder_source(qa_wire_encoder *e,double token){ eimpl *p=E(e);
  for(size_t i=0;i<p->source_count;i++)if(p->sources[i].token==token)return p->sources[i].source;
  qa_range_error("Unknown renderer dynamic token"); }
qa_image_resource_operation qa_wire_encoder_acknowledge(qa_wire_encoder *e,double token){ eimpl *p=E(e); double id=wire_integer(token);
  for(size_t i=0;i<p->ack_count;i++)if(p->acks[i].id==id){ qa_image_resource_operation op=p->acks[i].op; memmove(&p->acks[i],&p->acks[i+1],(p->ack_count-i-1)*sizeof *p->acks); p->ack_count--; return op; }
  qa_range_error("Unknown renderer image acknowledgment"); }
qa_wire_image qa_wire_encoder_image(qa_wire_encoder *e,qa_renderer_image image){
  if(image.owner.identity!=e->owner.identity||image.owner.session.token!=e->owner.session.token||image.owner.generation!=e->owner.generation) qa_range_error("Image belongs to another renderer lifetime");
  eimpl *p=E(e);
  for(size_t i=0;i<p->image_count;i++)if(p->images[i].ordinal==image.ordinal){
    if(p->images[i].image.width!=image.width||p->images[i].image.height!=image.height) qa_range_error("Renderer image ordinal has another identity");
    qa_wire_image o;o.ordinal=image.ordinal;o.width=image.width;o.height=image.height;o.name=image.source_is_resource?image.source_resource.requested_path:image.source_name;return o; }
  if(p->image_count==p->image_cap)p->images=(img_entry*)grow(p->images,&p->image_cap,sizeof(img_entry));
  p->images[p->image_count].ordinal=image.ordinal;p->images[p->image_count].image=image;p->image_count++;
  qa_wire_image o;o.ordinal=image.ordinal;o.width=image.width;o.height=image.height;o.name=image.source_is_resource?image.source_resource.requested_path:image.source_name;return o;
}
qa_wire_binding qa_wire_encoder_binding(qa_wire_encoder *e,qa_texture_binding binding){
  qa_wire_binding o; memset(&o,0,sizeof o); o.kind=binding.kind;
  if(binding.kind==QA_TEXTURE_BINDING_BIND_IMAGE){ o.image=qa_wire_encoder_image(e,binding.image); return o; }
  if(binding.kind==QA_TEXTURE_BINDING_RETAIN_CURRENT) return o;
  eimpl *p=E(e);
  for(size_t i=0;i<p->source_count;i++) if(p->src_keys[i]==binding.source.ctx){ o.token=p->sources[i].token; return o; }
  if(p->source_count==p->source_cap){ size_t old=p->source_cap; p->sources=(src_entry*)grow(p->sources,&p->source_cap,sizeof(src_entry)); p->src_keys=(const void**)realloc(p->src_keys,p->source_cap*sizeof *p->src_keys); if(!p->src_keys)qa_range_error("wire encoder allocation failed"); (void)old; }
  double token=p->next_token++;
  p->sources[p->source_count].token=token;p->sources[p->source_count].source=binding.source;p->src_keys[p->source_count]=binding.source.ctx;p->source_count++;
  o.token=token; return o;
}
qa_wire_image_operation qa_wire_encoder_image_operation(qa_wire_encoder *e,qa_image_resource_operation op){
  if(op.kind!=QA_IMAGE_OP_TEXTURE_MODE)(void)qa_wire_encoder_image(e,op.image);
  else (void)choice(op.filter,FILTERS,6);
  if(op.kind==QA_IMAGE_OP_CREATE)(void)choice(op.sampling.filter,FILTERS,6);
  eimpl *p=E(e);
  if(p->ack_count==p->ack_cap)p->acks=(ack_entry*)grow(p->acks,&p->ack_cap,sizeof(ack_entry));
  double id=p->next_ack++;
  p->acks[p->ack_count].id=id;p->acks[p->ack_count].op=op;p->ack_count++;
  qa_wire_image_operation o;o.operation=op;o.acknowledgment=id;return o;
}
qa_draw_batch qa_wire_encoder_batch(qa_wire_encoder *e,qa_draw_batch batch){
  validate_state(batch.state);
  (void)qa_wire_encoder_binding(e,batch.texture);
  if(batch.texturing_is_pair)(void)qa_wire_encoder_binding(e,batch.second_texture.binding);
  if(batch.lighting.atlas)(void)qa_wire_encoder_image(e,batch.lighting.atlas->image);
  if(!strcmp(batch.lighting.kind,"q2-model-shadow"))(void)qa_wire_encoder_image(e,batch.lighting.atlas_value.image);
  return batch;
}
qa_render_operation qa_wire_encoder_operation(qa_wire_encoder *e,qa_render_operation op){
  switch(op.kind){
  case QA_RENDER_OP_DRAW: case QA_RENDER_OP_OBJECT_OPACITY:
    for (size_t i = 0; i < op.batch_count; i++) (void)qa_wire_encoder_batch(e, op.batches[i]);
    return op;
  case QA_RENDER_OP_DEPTH_ATLAS: case QA_RENDER_OP_SKY_SIDE: (void)qa_wire_encoder_image(e,op.image); return op;
  case QA_RENDER_OP_SHADOW_VOLUME: case QA_RENDER_OP_SHADOW_FINISH: (void)qa_wire_encoder_image(e,op.white_image); return op;
  default: return op;
  }
}
qa_execution_command qa_wire_encoder_command(qa_wire_encoder *e,qa_render_command command,const double *captures,size_t capture_count,qa_execution_view *view_storage,qa_execution_operation *op_storage,size_t op_cap){
  qa_execution_command o; memset(&o,0,sizeof o);
  switch(command.kind){
  case QA_RENDER_COMMAND_VIEW: {
    o.kind=QA_EXEC_COMMAND_VIEW;
    size_t total=command.view.before_view_count+command.view.operation_count;
    if(total>op_cap)qa_range_error("wire encoder operation storage is too small");
    for(size_t i=0;i<command.view.before_view_count;i++)op_storage[i]=qa_wire_encoder_operation(e,command.view.before_view[i]);
    for(size_t i=0;i<command.view.operation_count;i++)op_storage[command.view.before_view_count+i]=qa_wire_encoder_operation(e,command.view.operations[i]);
    view_storage->state=command.view.state;
    view_storage->before_view=op_storage;view_storage->before_view_count=command.view.before_view_count;
    view_storage->operations=op_storage+command.view.before_view_count;view_storage->operation_count=command.view.operation_count;
    o.view=*view_storage; return o; }
  case QA_RENDER_COMMAND_IMAGE_RESOURCE: o.kind=QA_EXEC_COMMAND_IMAGE_RESOURCE; o.operation=qa_wire_encoder_image_operation(e,command.operation).operation; return o;
  case QA_RENDER_COMMAND_STRETCH_PIC: o.kind=QA_EXEC_COMMAND_STRETCH_PIC; (void)qa_wire_encoder_image(e,command.image); o.rect=command.rect;o.uv=command.uv;o.image=command.image; return o;
  case QA_RENDER_COMMAND_SWAP_BUFFERS: o.kind=QA_EXEC_COMMAND_SWAP_BUFFERS; o.captures=captures;o.capture_count=capture_count; return o;
  case QA_RENDER_COMMAND_DRAW_BUFFER: o.kind=QA_EXEC_COMMAND_DRAW_BUFFER; o.buffer=command.buffer;o.clear=command.clear; return o;
  case QA_RENDER_COMMAND_SET_COLOR: o.kind=QA_EXEC_COMMAND_SET_COLOR; o.color=command.color; return o;
  }
  qa_range_error("Unhandled render command: %d",(int)command.kind);
}
typedef struct dimpl { img_entry *images; size_t image_count,image_cap; src_entry *sources; size_t source_count,source_cap; ack_entry *acks; size_t ack_count,ack_cap; const qa_image_resource_operation **ack_keys; } dimpl;
static dimpl *D(qa_wire_decoder *d){ return (dimpl*)d->impl; }
bool qa_wire_decoder_init(qa_wire_decoder *d,qa_renderer_resource_owner owner,qa_wire_reached_dynamic_fn reached,void *ctx,qa_resolved_resource_reference palette_source){
  d->impl=calloc(1,sizeof(dimpl)); if(!d->impl)return false; d->owner=owner;d->reached_dynamic=reached;d->reached_ctx=ctx;d->palette_source=palette_source; return true;
}
void qa_wire_decoder_destroy(qa_wire_decoder *d){ if(!d->impl)return; dimpl *p=D(d); free(p->images);free(p->sources);free(p->acks);free(p->ack_keys);free(p); d->impl=NULL; }
void qa_wire_decoder_clear_sources(qa_wire_decoder *d){ D(d)->source_count=0; }
qa_renderer_image qa_wire_decoder_image(qa_wire_decoder *d,qa_wire_image wire){
  double ordinal=wire_integer(wire.ordinal),width=wire_integer(wire.width),height=wire_integer(wire.height);
  dimpl *p=D(d);
  for(size_t i=0;i<p->image_count;i++)if(p->images[i].ordinal==ordinal){
    qa_renderer_image ex=p->images[i].image;
    if(ex.width!=width||ex.height!=height||ex.source_is_resource||strcmp(ex.source_name?ex.source_name:"",wire.name?wire.name:"")) qa_range_error("Renderer image metadata changed");
    return ex; }
  qa_renderer_image image; memset(&image,0,sizeof image); image.owner=d->owner;image.ordinal=ordinal;image.width=width;image.height=height;image.source_is_resource=false;image.source_name=wire.name;
  if(p->image_count==p->image_cap)p->images=(img_entry*)grow(p->images,&p->image_cap,sizeof(img_entry));
  p->images[p->image_count].ordinal=ordinal;p->images[p->image_count].image=image;p->image_count++;
  return image;
}
typedef struct dyn_ctx { qa_wire_decoder *decoder; double token; } dyn_ctx;
static qa_renderer_image dyn_resolve(void *ctx,void (*apply)(void *,qa_image_resource_operation),void *apply_ctx){
  dyn_ctx *c=(dyn_ctx*)ctx; return c->decoder->reached_dynamic(c->decoder->reached_ctx,c->token,apply,apply_ctx);
}
qa_texture_binding qa_wire_decoder_binding(qa_wire_decoder *d,qa_wire_binding wire){
  qa_texture_binding o; memset(&o,0,sizeof o); o.kind=wire.kind;
  if(wire.kind==QA_TEXTURE_BINDING_BIND_IMAGE){ o.image=qa_wire_decoder_image(d,wire.image); return o; }
  if(wire.kind==QA_TEXTURE_BINDING_RETAIN_CURRENT) return o;
  if(wire.kind!=QA_TEXTURE_BINDING_DYNAMIC_IMAGE) qa_range_error("Invalid renderer texture binding");
  double token=wire_integer(wire.token); dimpl *p=D(d);
  for(size_t i=0;i<p->source_count;i++)if(p->sources[i].token==token){ o.source=p->sources[i].source; return o; }
  dyn_ctx *c=(dyn_ctx*)malloc(sizeof *c); if(!c)qa_range_error("wire decoder allocation failed");
  c->decoder=d;c->token=token;
  qa_dynamic_image_source source; source.ctx=c;source.resolve=dyn_resolve;
  if(p->source_count==p->source_cap)p->sources=(src_entry*)grow(p->sources,&p->source_cap,sizeof(src_entry));
  p->sources[p->source_count].token=token;p->sources[p->source_count].source=source;p->source_count++;
  o.source=source; return o;
}
double qa_wire_decoder_acknowledgment(qa_wire_decoder *d,const qa_image_resource_operation *op){
  dimpl *p=D(d);
  for(size_t i=0;i<p->ack_count;i++)if(p->ack_keys[i]==op)return p->acks[i].id;
  for(size_t i=0;i<p->ack_count;i++)if(p->acks[i].op.kind==op->kind&&p->acks[i].op.image.ordinal==op->image.ordinal)return p->acks[i].id;
  qa_range_error("Renderer image operation has no acknowledgment");
}
qa_image_resource_operation qa_wire_decoder_image_operation(qa_wire_decoder *d,qa_wire_image_operation wire){
  double token=wire_integer(wire.acknowledgment);
  qa_image_resource_operation op=wire.operation;
  switch(op.kind){
  case QA_IMAGE_OP_TEXTURE_MODE: (void)choice(op.filter,FILTERS,6); break;
  case QA_IMAGE_OP_RELEASE: case QA_IMAGE_OP_UPDATE: case QA_IMAGE_OP_CREATE: {
    qa_wire_image w; w.ordinal=op.image.ordinal;w.width=op.image.width;w.height=op.image.height;w.name=op.image.source_is_resource?op.image.source_resource.requested_path:op.image.source_name;
    op.image=qa_wire_decoder_image(d,w);
    if(op.kind==QA_IMAGE_OP_CREATE){ (void)choice(op.sampling.filter,FILTERS,6);
      if(op.content.kind==QA_RENDER_IMAGE_INDEXED8){
        if(op.content.palette.colors.length!=768) qa_range_error("Invalid indexed palette");
        op.content.palette.source=d->palette_source; }
      size_t lc=op.content.kind==QA_RENDER_IMAGE_DEPTH32F?op.content.depth_level_count:op.content.level_count;
      if(lc==0) qa_range_error("Image has no levels"); }
    break; }
  default: qa_range_error("Invalid renderer resource operation");
  }
  dimpl *p=D(d);
  if(p->ack_count==p->ack_cap){ p->acks=(ack_entry*)grow(p->acks,&p->ack_cap,sizeof(ack_entry)); p->ack_keys=(const qa_image_resource_operation**)realloc(p->ack_keys,p->ack_cap*sizeof *p->ack_keys); if(!p->ack_keys)qa_range_error("wire decoder allocation failed"); }
  p->acks[p->ack_count].id=token;p->acks[p->ack_count].op=op;p->ack_keys[p->ack_count]=NULL;p->ack_count++;
  return op;
}
qa_draw_batch qa_wire_decoder_batch(qa_wire_decoder *d,qa_draw_batch batch){
  validate_state(batch.state);
  /* Canonicalize bound images through the decoder image map. */
  if(batch.texture.kind==QA_TEXTURE_BINDING_BIND_IMAGE){ qa_wire_image w;w.ordinal=batch.texture.image.ordinal;w.width=batch.texture.image.width;w.height=batch.texture.image.height;w.name=batch.texture.image.source_is_resource?batch.texture.image.source_resource.requested_path:batch.texture.image.source_name; batch.texture.image=qa_wire_decoder_image(d,w); }
  (void)d; return batch;
}
qa_execution_operation qa_wire_decoder_operation(qa_wire_decoder *d,qa_render_operation op,qa_draw_batch *batch_storage,size_t batch_cap){
  if(op.kind==QA_RENDER_OP_DRAW||op.kind==QA_RENDER_OP_OBJECT_OPACITY){
    if(op.batch_count>batch_cap)qa_range_error("wire decoder batch storage is too small");
    for(size_t i=0;i<op.batch_count;i++)batch_storage[i]=qa_wire_decoder_batch(d,op.batches[i]);
    op.batches=batch_storage;
  }
  return op;
}
qa_execution_command qa_wire_decoder_command(qa_wire_decoder *d,qa_execution_command command,qa_execution_operation *op_storage,size_t op_cap,qa_draw_batch *batch_storage,size_t batch_cap){
  if(command.kind==QA_EXEC_COMMAND_VIEW){
    size_t total=command.view.before_view_count+command.view.operation_count;
    if(total>op_cap)qa_range_error("wire decoder operation storage is too small");
    size_t used=0;
    for(size_t i=0;i<command.view.before_view_count;i++){ size_t bc=command.view.before_view[i].batch_count; op_storage[i]=qa_wire_decoder_operation(d,command.view.before_view[i],batch_storage+used,batch_cap-used); used+=bc; }
    for(size_t i=0;i<command.view.operation_count;i++){ size_t bc=command.view.operations[i].batch_count; op_storage[command.view.before_view_count+i]=qa_wire_decoder_operation(d,command.view.operations[i],batch_storage+used,batch_cap-used); used+=bc; }
    command.view.before_view=op_storage; command.view.operations=op_storage+command.view.before_view_count;
  } else if(command.kind==QA_EXEC_COMMAND_IMAGE_RESOURCE){
    qa_wire_image_operation w; w.operation=command.operation; w.acknowledgment=1;
    /* Acknowledgment was consumed by the encoder round-trip in-process;
     * decode validates/canonicalizes the operation directly. */
    command.operation=qa_wire_decoder_image_operation(d,w);
  }
  return command;
}
