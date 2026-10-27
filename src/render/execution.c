/* Port of src/render/execution.ts. GPL-2.0-or-later. */
#include "qa/render/execution.h"
#include "qa/render/commands/dynamic_texture.h"
#include "qa/foundation/support.h"
#include <string.h>
void qa_render_executor_init(qa_render_executor *e,qa_renderer_backend b,qa_execution_services s){ e->backend=b;e->services=s;e->color.x=e->color.y=e->color.z=e->color.w=1; }
void qa_render_executor_replace_backend(qa_render_executor *e,qa_renderer_backend b){ e->backend=b; }
void qa_render_executor_image(qa_render_executor *e,qa_image_resource_operation op){ e->backend.apply_image_resource(e->backend.ctx,op); e->services.image_applied(e->services.ctx,op); }
static void apply_fwd(void *ctx,qa_image_resource_operation op){ qa_render_executor_image((qa_render_executor*)ctx,op); }
static void exec_draw(qa_render_executor *e,qa_draw_batch input){
  qa_draw_batch resolved; qa_draw_batch batch=qa_resolve_draw_textures(input,apply_fwd,e,&resolved)?resolved:input;
  qa_prepared_backend_draw prepared=e->backend.prepare_geometry(e->backend.ctx,batch);
  prepared.begin(prepared.ctx);
  prepared.apply_texture(prepared.ctx,0,batch.texture);
  if(batch.texturing_is_pair) prepared.apply_texture(prepared.ctx,1,batch.second_texture.binding);
  prepared.draw(prepared.ctx);
  prepared.cleanup(prepared.ctx);
}
typedef struct opacity_ctx { qa_render_executor *e; const qa_draw_batch *batches; size_t count; } opacity_ctx;
static void opacity_draw(void *ctx){ opacity_ctx *c=(opacity_ctx*)ctx; for(size_t i=0;i<c->count;i++)exec_draw(c->e,c->batches[i]); }
static void exec_ops(qa_render_executor *e,const qa_execution_operation *ops,size_t count){
  for(size_t i=0;i<count;i++){
    const qa_execution_operation *op=&ops[i];
    if(op->kind==QA_RENDER_OP_DRAW){ for(size_t j=0;j<op->batch_count;j++)exec_draw(e,op->batches[j]); }
    else if(op->kind==QA_RENDER_OP_OBJECT_OPACITY){ opacity_ctx c;c.e=e;c.batches=op->batches;c.count=op->batch_count; e->backend.with_object_opacity(e->backend.ctx,op->opacity,opacity_draw,&c); }
    else e->backend.draw_immediate(e->backend.ctx,*op);
  }
}
static void exec_picture(qa_render_executor *e,qa_execution_command command){
  double width=e->backend.width,height=e->backend.height;
  qa_render_view_state v; memset(&v,0,sizeof v); v.viewport.x=0;v.viewport.y=0;v.viewport.width=width;v.viewport.height=height;
  e->backend.begin_view(e->backend.ctx,v);
  double left=command.rect.x/width*2-1,right=(command.rect.x+command.rect.width)/width*2-1;
  double top=1-command.rect.y/height*2,bottom=1-(command.rect.y+command.rect.height)/height*2;
  qa_render_vertex verts[4]; double idx[6]={0,1,2,0,2,3};
  double xs[4]={left,right,right,left},ys[4]={top,top,bottom,bottom};
  double ss[4]={command.uv.s1,command.uv.s2,command.uv.s2,command.uv.s1},ts[4]={command.uv.t1,command.uv.t1,command.uv.t2,command.uv.t2};
  for(int i=0;i<4;i++){ verts[i].position.x=xs[i];verts[i].position.y=ys[i];verts[i].position.z=0;verts[i].position.w=1;verts[i].tex_coord.x=ss[i];verts[i].tex_coord.y=ts[i];verts[i].color=e->color; }
  qa_draw_batch b; memset(&b,0,sizeof b); b.lighting.kind="vertex"; b.indices.values=idx;b.indices.count=6;
  b.texture.kind=QA_TEXTURE_BINDING_BIND_IMAGE;b.texture.image=command.image; b.vertices=verts;b.vertex_count=4;
  b.state.blend.source="src-alpha";b.state.blend.destination="one-minus-src-alpha";b.state.depth_test="always";b.state.depth_write=false;b.state.alpha_test="none";b.state.cull="none";b.state.depth_range[0]=0;b.state.depth_range[1]=1;
  exec_draw(e,b);
}
void qa_render_executor_execute(qa_render_executor *e,qa_execution_command command){
  switch(command.kind){
  case QA_EXEC_COMMAND_DRAW_BUFFER: e->backend.select_draw_buffer(e->backend.ctx,command.buffer,command.clear); break;
  case QA_EXEC_COMMAND_IMAGE_RESOURCE: qa_render_executor_image(e,command.operation); break;
  case QA_EXEC_COMMAND_SET_COLOR: e->color=command.color; break;
  case QA_EXEC_COMMAND_STRETCH_PIC: exec_picture(e,command); break;
  case QA_EXEC_COMMAND_VIEW:
    exec_ops(e,command.view.before_view,command.view.before_view_count);
    e->backend.begin_view(e->backend.ctx,command.view.state);
    exec_ops(e,command.view.operations,command.view.operation_count);
    break;
  case QA_EXEC_COMMAND_SWAP_BUFFERS: e->services.swap(e->services.ctx,command.captures,command.capture_count); break;
  default: qa_range_error("Unhandled render command: %d",(int)command.kind);
  }
}
