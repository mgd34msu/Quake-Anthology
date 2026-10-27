/* Port of src/render/worker-runtime.ts (direct/pumped). GPL-2.0-or-later. */
#include "qa/render/worker_runtime.h"
#include "qa/render/cpu/rasterizer.h"
#include "qa/render/execution.h"
#include "qa/render/image_journal.h"
#include "qa/render/worker_protocol.h"
#include "qa/contracts/identity.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
typedef struct rimpl {
  qa_identity_owner identity; bool has_identity;
  qa_software_renderer cpu; bool has_cpu;
  qa_renderer_backend backend;
  qa_render_executor executor;
  qa_render_image_journal journal; bool has_journal;
  qa_wire_decoder decoder; bool has_decoder;
  qa_render_worker_host host;
  bool closed, overdraw, parked;
  double gamma, swap_interval;
  qa_prepared_backend_draw *prepared; double *prepared_ids; size_t prepared_count, prepared_cap;
  uint8_t *pixel_copy;
  uint8_t *stencil_copy; size_t stencil_length;
} rimpl;
static rimpl *R(qa_render_worker_runtime *r){ return (rimpl*)r->impl; }
static qa_renderer_image reached_dynamic(void *ctx,double token,void (*apply)(void *,qa_image_resource_operation),void *apply_ctx){
  (void)apply;(void)apply_ctx; rimpl *p=(rimpl*)ctx;
  qa_worker_callback cb; memset(&cb,0,sizeof cb); cb.kind=QA_WORKER_CALLBACK_DYNAMIC_IMAGE; cb.token=token;
  double ordinal=0,w=0,h=0; const char *name=NULL;
  if(!p->host.on_callback(p->host.ctx,cb,&ordinal,&w,&h,&name)) qa_range_error("Renderer dynamic image request failed");
  qa_wire_image wire; wire.ordinal=ordinal;wire.width=w;wire.height=h;wire.name=name;
  return qa_wire_decoder_image(&p->decoder,wire);
}
static void svc_image(void *ctx,qa_image_resource_operation op){
  rimpl *p=(rimpl*)ctx;
  qa_render_image_journal_record(&p->journal,op);
  qa_worker_callback cb; memset(&cb,0,sizeof cb); cb.kind=QA_WORKER_CALLBACK_IMAGE_APPLIED;
  cb.token=qa_wire_decoder_acknowledgment(&p->decoder,&op);
  double o=0,w=0,h=0; const char *n=NULL; (void)p->host.on_callback(p->host.ctx,cb,&o,&w,&h,&n);
}
static void svc_swap(void *ctx,const double *captures,size_t count){
  rimpl *p=(rimpl*)ctx;
  qa_worker_callback cb; memset(&cb,0,sizeof cb); cb.kind=QA_WORKER_CALLBACK_SWAP; cb.captures=captures;cb.capture_count=count;
  if(p->has_cpu){ qa_software_renderer_finish(&p->cpu); qa_image_level lvl;
    if(qa_software_renderer_read_rgba(&p->cpu,&lvl)){ cb.image=lvl;cb.has_image=true;
      double o=0,w=0,h=0; const char *n=NULL; (void)p->host.on_callback(p->host.ctx,cb,&o,&w,&h,&n);
      free((void*)lvl.pixels.bytes); return; } }
  double o=0,w=0,h=0; const char *n=NULL; (void)p->host.on_callback(p->host.ctx,cb,&o,&w,&h,&n);
}
bool qa_create_render_worker_runtime(void *ctx,const void *payload,size_t payload_size,qa_render_worker_host host,qa_render_worker_runtime *out){
  (void)ctx;
  if(payload_size<sizeof(qa_render_worker_init)) qa_range_error("Invalid render worker request");
  const qa_render_worker_init *init=(const qa_render_worker_init*)payload;
  if(!isfinite(init->gamma)) qa_range_error("Invalid render worker number");
  rimpl *p=(rimpl*)calloc(1,sizeof *p); if(!p)return false; out->impl=p;
  if(!qa_identity_owner_create(&p->identity,"render-worker")){ free(p); out->impl=NULL; return false; }
  p->has_identity=true; p->host=host; p->gamma=init->gamma; p->swap_interval=init->swap_interval;
  qa_renderer_resource_owner owner; memset(&owner,0,sizeof owner);
  owner.identity=p; owner.session=p->identity.session; owner.generation=0;
  if(!strcmp(init->backend,"cpu")){
    if(init->width!=(double)(long long)init->width||init->width<=0||init->height!=(double)(long long)init->height||init->height<=0) qa_range_error("Empty render worker dimension");
    if(!qa_software_renderer_init_default(&p->cpu,init->width,init->height,owner)){ qa_identity_owner_destroy(&p->identity); free(p); out->impl=NULL; return false; }
    p->has_cpu=true; p->backend=qa_software_renderer_backend(&p->cpu);
    qa_software_renderer_set_output_gamma(&p->cpu,init->gamma);
  } else if(!strcmp(init->backend,"gl")){
    if(!init->has_gl_backend) qa_range_error("Unknown render worker backend");
    p->backend=init->gl_backend;
  } else qa_range_error("Unknown render worker backend");
  if(!qa_render_image_journal_init(&p->journal)){ if(p->has_cpu)qa_software_renderer_close(&p->cpu); qa_identity_owner_destroy(&p->identity); free(p); out->impl=NULL; return false; }
  p->has_journal=true;
  qa_resolved_resource_reference palette; memset(&palette,0,sizeof palette);
  palette.requested_path="palette";
  if(!qa_wire_decoder_init(&p->decoder,owner,reached_dynamic,p,palette)) qa_range_error("render worker allocation failed");
  p->has_decoder=true;
  qa_execution_services services; services.ctx=p; services.image_applied=svc_image; services.swap=svc_swap;
  qa_render_executor_init(&p->executor,p->backend,services);
  return true;
}
bool qa_render_worker_runtime_handle(qa_render_worker_runtime *runtime,qa_worker_request request,qa_worker_response *out){
  rimpl *p=R(runtime); memset(out,0,sizeof *out); out->kind=request.kind;
  if(p->closed) qa_range_error("Render worker is closed");
  switch(request.kind){
  case QA_WORKER_REQUEST_COMMAND: {
    qa_execution_operation ops[64]; qa_draw_batch batches[256];
    qa_execution_command cmd=qa_wire_decoder_command(&p->decoder,request.command,ops,64,batches,256);
    qa_render_executor_execute(&p->executor,cmd); return true; }
  case QA_WORKER_REQUEST_PREPARE: {
    qa_prepared_backend_draw prepared=p->backend.prepare_geometry(p->backend.ctx,request.batch);
    if(p->prepared_count==p->prepared_cap){ size_t nc=p->prepared_cap?p->prepared_cap*2:8;
      p->prepared=(qa_prepared_backend_draw*)realloc(p->prepared,nc*sizeof *p->prepared);
      p->prepared_ids=(double*)realloc(p->prepared_ids,nc*sizeof *p->prepared_ids);
      if (!p->prepared || !p->prepared_ids) qa_range_error("render worker allocation failed");
      p->prepared_cap = nc; }
    p->prepared[p->prepared_count]=prepared; p->prepared_ids[p->prepared_count]=request.draw_id; p->prepared_count++; return true; }
  case QA_WORKER_REQUEST_BEGIN: case QA_WORKER_REQUEST_APPLY_TEXTURE: case QA_WORKER_REQUEST_DRAW: case QA_WORKER_REQUEST_CLEANUP: {
    for(size_t i=0;i<p->prepared_count;i++) if(p->prepared_ids[i]==request.draw_id){
      qa_prepared_backend_draw *d=&p->prepared[i];
      if(request.kind==QA_WORKER_REQUEST_BEGIN)d->begin(d->ctx);
      else if(request.kind==QA_WORKER_REQUEST_APPLY_TEXTURE)d->apply_texture(d->ctx,request.unit,request.binding);
      else if(request.kind==QA_WORKER_REQUEST_DRAW)d->draw(d->ctx);
      else { d->cleanup(d->ctx); memmove(&p->prepared[i],&p->prepared[i+1],(p->prepared_count-i-1)*sizeof *p->prepared); memmove(&p->prepared_ids[i],&p->prepared_ids[i+1],(p->prepared_count-i-1)*sizeof *p->prepared_ids); p->prepared_count--; }
      return true; }
    qa_range_error("Unknown render worker prepared draw"); }
  case QA_WORKER_REQUEST_SET_GAMMA:
    p->gamma=request.gamma;
    if(p->has_cpu)qa_software_renderer_set_output_gamma(&p->cpu,request.gamma);
    return true;
  case QA_WORKER_REQUEST_SET_OVERDRAW:
    p->overdraw=request.enabled; p->backend.set_overdraw_measurement(p->backend.ctx,request.enabled); return true;
  case QA_WORKER_REQUEST_READ_PIXELS:
    if(p->has_cpu){ qa_image_level lvl; if(!qa_software_renderer_read_rgba(&p->cpu,&lvl))return false;
      free(p->pixel_copy); p->pixel_copy=(uint8_t*)lvl.pixels.bytes; out->image=lvl; out->has_image=true; return true; }
    return false;
  case QA_WORKER_REQUEST_READ_DEPTH:
    out->number=p->backend.read_depth_pixel(p->backend.ctx,request.x,request.y); return true;
  case QA_WORKER_REQUEST_READ_STENCIL: {
    size_t need=(size_t)(p->backend.width*p->backend.height);
    free(p->stencil_copy); p->stencil_copy=(uint8_t*)calloc(need?need:1,1); if(!p->stencil_copy)return false;
    p->stencil_length=need; p->backend.read_stencil_overdraw(p->backend.ctx,p->stencil_copy,need);
    out->bytes=p->stencil_copy; out->byte_length=need; return true; }
  case QA_WORKER_REQUEST_DESCRIBE: out->number=p->swap_interval; return true;
  case QA_WORKER_REQUEST_PARK: p->parked=true; return true;
  case QA_WORKER_REQUEST_RESUME: p->parked=false; return true;
  case QA_WORKER_REQUEST_CLOSE: qa_render_worker_runtime_close(runtime); return true;
  }
  return false;
}
void qa_render_worker_runtime_close(qa_render_worker_runtime *runtime){ if(!runtime->impl)return; rimpl *p=R(runtime); if(p->closed)return;
  p->closed=true;
  if(p->has_cpu)qa_software_renderer_close(&p->cpu);
  if(p->has_journal)qa_render_image_journal_destroy(&p->journal);
  if(p->has_decoder)qa_wire_decoder_destroy(&p->decoder);
  free(p->prepared);free(p->prepared_ids);free(p->pixel_copy);free(p->stencil_copy);
  if(p->has_identity)qa_identity_owner_destroy(&p->identity);
  free(p); runtime->impl=NULL; }
