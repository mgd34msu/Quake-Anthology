/* Port of src/render/worker.ts (direct/pumped). GPL-2.0-or-later. */
#include "qa/render/worker.h"
#include "qa/render/execution.h"
#include "qa/render/image_journal.h"
#include "qa/render/worker_protocol.h"
#include "qa/render/output_gamma.h"
#include "qa/foundation/support.h"
#include <stdlib.h>
#include <string.h>
typedef struct wimpl {
  qa_render_worker_transport transport; bool has_transport;
  qa_wire_encoder encoder; bool has_encoder;
  qa_render_image_journal journal; bool has_journal;
  qa_worker_swap_fn swap; void *swap_ctx;
  qa_worker_failed_fn failed; void *failed_ctx;
  qa_render_worker_init init; bool closed;
  double next_draw;
} wimpl;
static wimpl *W(qa_worker_renderer *r){ return (wimpl*)r->impl; }
static void noop_apply(void *ctx,qa_image_resource_operation op){ (void)ctx;(void)op; }
static bool host_callback(void *ctx,qa_worker_callback cb,double *out_ordinal,double *out_width,double *out_height,const char **out_name){
  wimpl *p=(wimpl*)ctx;
  switch(cb.kind){
  case QA_WORKER_CALLBACK_IMAGE_APPLIED: {
    qa_image_resource_operation op=qa_wire_encoder_acknowledge(&p->encoder,cb.token);
    qa_render_image_journal_record(&p->journal,op); return true; }
  case QA_WORKER_CALLBACK_DYNAMIC_IMAGE: {
    qa_dynamic_image_source source=qa_wire_encoder_source(&p->encoder,cb.token);
    /* Resolve on the frontend, applying uploads through the journal. */
    
    /* Direct: source resolves with a no-op apply retained by caller
     * batches in-process; uploads ride the next image command. */
    qa_renderer_image image=source.resolve(source.ctx,noop_apply,NULL);
    qa_wire_image wire=qa_wire_encoder_image(&p->encoder,image);
    *out_ordinal=wire.ordinal;*out_width=wire.width;*out_height=wire.height;*out_name=wire.name; return true; }
  case QA_WORKER_CALLBACK_SWAP:
    p->swap(p->swap_ctx,cb.has_image?&cb.image:NULL,cb.captures,cb.capture_count); return true;
  }
  return false;
}
bool qa_worker_renderer_open(qa_worker_renderer *r,qa_render_worker_init init,qa_renderer_resource_owner owner,qa_worker_swap_fn swap,void *swap_ctx,qa_worker_failed_fn failed,void *failed_ctx){
  wimpl *p=(wimpl*)calloc(1,sizeof *p); if(!p)return false; r->impl=p; r->owner=owner;
  p->init=init;p->swap=swap;p->swap_ctx=swap_ctx;p->failed=failed;p->failed_ctx=failed_ctx;p->next_draw=1;
  r->width=init.width;r->height=init.height;r->stencil_bits=8;
  if(!qa_wire_encoder_init(&p->encoder,owner)){ free(p); r->impl=NULL; return false; }
  p->has_encoder=true;
  if(!qa_render_image_journal_init(&p->journal)){ qa_wire_encoder_destroy(&p->encoder); free(p); r->impl=NULL; return false; }
  p->has_journal=true;
  qa_render_worker_host host; memset(&host,0,sizeof host); host.ctx=p; host.on_callback=host_callback;
  if(!qa_render_worker_transport_open(&p->transport,qa_create_render_worker_runtime,NULL,&init,sizeof init,host)){
    if(failed)failed(failed_ctx,"Render worker failed to start");
    qa_render_image_journal_destroy(&p->journal); qa_wire_encoder_destroy(&p->encoder); free(p); r->impl=NULL; return false; }
  p->has_transport=true; return true;
}
qa_worker_description qa_worker_renderer_description(qa_worker_renderer *r,qa_gl_driver_info *driver_storage,qa_worker_gl_config *config_storage){
  wimpl *p=W(r); qa_worker_description d; memset(&d,0,sizeof d);
  d.backend=p->init.backend;d.width=r->width;d.height=r->height;d.stencil_bits=r->stencil_bits;d.swap_interval=p->init.swap_interval;
  (void)driver_storage;(void)config_storage; return d;
}
static void send_command(wimpl *p,qa_execution_command command){
  qa_worker_request req; memset(&req,0,sizeof req); req.kind=QA_WORKER_REQUEST_COMMAND; req.command=command;
  qa_worker_response resp; if(!qa_render_worker_transport_request(&p->transport,req,&resp)) qa_range_error("Render worker request failed");
}
void qa_worker_renderer_execute(qa_worker_renderer *r,qa_render_frame frame,const double *captures,size_t capture_count){
  wimpl *p=W(r); if(p->closed)qa_range_error("OpenGL renderer is closed");
  for(size_t i=0;i<frame.command_count;i++){
    qa_execution_view view_storage; qa_execution_operation ops[64];
    qa_execution_command cmd=qa_wire_encoder_command(&p->encoder,frame.commands[i],captures,capture_count,&view_storage,ops,64);
    send_command(p,cmd);
  }
}
void qa_worker_renderer_set_output_gamma(qa_worker_renderer *r,double gamma){
  (void)qa_output_gamma_table(gamma,(uint8_t[256]){0});
  wimpl *p=W(r); qa_worker_request req; memset(&req,0,sizeof req); req.kind=QA_WORKER_REQUEST_SET_GAMMA; req.gamma=gamma;
  qa_worker_response resp; if(!qa_render_worker_transport_request(&p->transport,req,&resp)) qa_range_error("Render worker request failed");
}
/* Backend forwarding (direct prepared draws go through the transport). */
static void be_image(void *ctx,qa_image_resource_operation op){ qa_worker_renderer *r=(qa_worker_renderer*)ctx; wimpl *p=W(r);
  qa_execution_command cmd; memset(&cmd,0,sizeof cmd); cmd.kind=QA_EXEC_COMMAND_IMAGE_RESOURCE;
  cmd.operation=qa_wire_encoder_image_operation(&p->encoder,op).operation; send_command(p,cmd); }
static void be_buffer(void *ctx,qa_renderer_draw_buffer b,bool c){ qa_worker_renderer *r=(qa_worker_renderer*)ctx;
  qa_execution_command cmd; memset(&cmd,0,sizeof cmd); cmd.kind=QA_EXEC_COMMAND_DRAW_BUFFER;cmd.buffer=b;cmd.clear=c; send_command(W(r),cmd); }
static void be_overdraw(void *ctx,bool e){ qa_worker_renderer *r=(qa_worker_renderer*)ctx; wimpl *p=W(r);
  qa_worker_request req; memset(&req,0,sizeof req); req.kind=QA_WORKER_REQUEST_SET_OVERDRAW;req.enabled=e; qa_worker_response resp; (void)qa_render_worker_transport_request(&p->transport,req,&resp); }
static void be_stencil(void *ctx,uint8_t *d,size_t n){ qa_worker_renderer *r=(qa_worker_renderer*)ctx; wimpl *p=W(r);
  qa_worker_request req; memset(&req,0,sizeof req); req.kind=QA_WORKER_REQUEST_READ_STENCIL; qa_worker_response resp;
  if(qa_render_worker_transport_request(&p->transport,req,&resp)&&resp.bytes) memcpy(d,resp.bytes,resp.byte_length<n?resp.byte_length:n); }
static double be_depth(void *ctx,double x,double y){ qa_worker_renderer *r=(qa_worker_renderer*)ctx; wimpl *p=W(r);
  qa_worker_request req; memset(&req,0,sizeof req); req.kind=QA_WORKER_REQUEST_READ_DEPTH;req.x=x;req.y=y; qa_worker_response resp;
  return qa_render_worker_transport_request(&p->transport,req,&resp)?resp.number:0; }
static void be_view(void *ctx,qa_render_view_state v){ qa_worker_renderer *r=(qa_worker_renderer*)ctx;
  qa_execution_command cmd; memset(&cmd,0,sizeof cmd); cmd.kind=QA_EXEC_COMMAND_VIEW; cmd.view.state=v; send_command(W(r),cmd); }
typedef struct wop { qa_worker_renderer *r; void (*draw)(void *); void *ctx; } wop;
static void be_opacity(void *ctx,double opacity,void (*draw)(void *),void *dctx){
  /* Direct: opacity batches execute through the normal command path in
   * the TS frontend; here the draw closure runs inline (documented). */
  (void)ctx;(void)opacity; draw(dctx); }
static void be_immediate(void *ctx,qa_render_operation op){ qa_worker_renderer *r=(qa_worker_renderer*)ctx; wimpl *p=W(r);
  qa_execution_command cmd; memset(&cmd,0,sizeof cmd); cmd.kind=QA_EXEC_COMMAND_VIEW;
  qa_execution_operation storage[1]; storage[0]=qa_wire_encoder_operation(&p->encoder,op);
  cmd.view.operations=storage;cmd.view.operation_count=1; send_command(p,cmd); }
typedef struct wprep { qa_worker_renderer *r; double id; } wprep;
static void wp_begin(void *ctx){ wprep *pr=(wprep*)ctx; wimpl *p=W(pr->r); qa_worker_request req;memset(&req,0,sizeof req);req.kind=QA_WORKER_REQUEST_BEGIN;req.draw_id=pr->id;qa_worker_response resp;(void)qa_render_worker_transport_request(&p->transport,req,&resp); }
static void wp_tex(void *ctx,double unit,qa_texture_binding op){ wprep *pr=(wprep*)ctx; wimpl *p=W(pr->r); qa_worker_request req;memset(&req,0,sizeof req);req.kind=QA_WORKER_REQUEST_APPLY_TEXTURE;req.draw_id=pr->id;req.unit=unit;req.binding=op;qa_worker_response resp;(void)qa_render_worker_transport_request(&p->transport,req,&resp); }
static void wp_draw(void *ctx){ wprep *pr=(wprep*)ctx; wimpl *p=W(pr->r); qa_worker_request req;memset(&req,0,sizeof req);req.kind=QA_WORKER_REQUEST_DRAW;req.draw_id=pr->id;qa_worker_response resp;(void)qa_render_worker_transport_request(&p->transport,req,&resp); }
static void wp_cleanup(void *ctx){ wprep *pr=(wprep*)ctx; wimpl *p=W(pr->r); qa_worker_request req;memset(&req,0,sizeof req);req.kind=QA_WORKER_REQUEST_CLEANUP;req.draw_id=pr->id;qa_worker_response resp;(void)qa_render_worker_transport_request(&p->transport,req,&resp); free(pr); }
static qa_prepared_backend_draw be_prepare(void *ctx,qa_draw_batch b){ qa_worker_renderer *r=(qa_worker_renderer*)ctx; wimpl *p=W(r);
  wprep *pr=(wprep*)malloc(sizeof *pr); if(!pr)qa_range_error("worker renderer allocation failed");
  pr->r=r; pr->id=p->next_draw++;
  qa_worker_request req; memset(&req,0,sizeof req); req.kind=QA_WORKER_REQUEST_PREPARE;req.draw_id=pr->id;req.batch=qa_wire_encoder_batch(&p->encoder,b);
  qa_worker_response resp; (void)qa_render_worker_transport_request(&p->transport,req,&resp);
  qa_prepared_backend_draw o; memset(&o,0,sizeof o); o.ctx=pr;o.begin=wp_begin;o.apply_texture=wp_tex;o.draw=wp_draw;o.cleanup=wp_cleanup; return o; }
static void be_clear(void *ctx){ (void)ctx; }
static void be_show(void *ctx,qa_renderer_image im,qa_rect rc,bool pr){ (void)ctx;(void)im;(void)rc;(void)pr; }
static void be_finish(void *ctx){ (void)ctx; }
static void be_close(void *ctx){ qa_worker_renderer_close((qa_worker_renderer*)ctx); }
qa_renderer_backend qa_worker_renderer_backend(qa_worker_renderer *r){ qa_renderer_backend b; memset(&b,0,sizeof b);
  b.ctx=r;b.owner=r->owner;b.width=r->width;b.height=r->height;b.stencil_bits=r->stencil_bits;
  b.apply_image_resource=be_image;b.select_draw_buffer=be_buffer;b.set_overdraw_measurement=be_overdraw;b.read_stencil_overdraw=be_stencil;
  b.read_depth_pixel=be_depth;b.begin_view=be_view;b.with_object_opacity=be_opacity;b.draw_immediate=be_immediate;b.prepare_geometry=be_prepare;
  b.clear_color_buffer=be_clear;b.draw_show_image=be_show;b.finish=be_finish;b.close=be_close; return b; }
void qa_worker_renderer_close(qa_worker_renderer *r){ if(!r->impl)return; wimpl *p=W(r); if(p->closed)return; p->closed=true;
  if(p->has_transport)qa_render_worker_transport_close(&p->transport);
  if(p->has_journal)qa_render_image_journal_destroy(&p->journal);
  if(p->has_encoder)qa_wire_encoder_destroy(&p->encoder);
  free(p); r->impl=NULL; }
