/* Port of src/render/cpu/commands.ts. GPL-2.0-or-later. */
#include "qa/render/cpu/commands.h"
#include "qa/foundation/support.h"
#include <string.h>
void qa_cpu_render_target_init(qa_cpu_render_target *t,qa_software_renderer *backend,const qa_cpu_presenter *presenter){
  if(presenter!=NULL&&strcmp(presenter->backend,"cpu")) qa_range_error("CPU rendering requires an SDL CPU window");
  t->backend=backend;t->presenter=presenter;t->color.x=t->color.y=t->color.z=t->color.w=1;
}
typedef struct op_ctx { qa_cpu_render_target *t; const qa_draw_batch *batches; size_t count; } op_ctx;
static void op_draw(void *ctx){ op_ctx *c=(op_ctx*)ctx; for(size_t i=0;i<c->count;i++)qa_software_renderer_draw(c->t->backend,c->batches[i]); }
static void run_ops(qa_cpu_render_target *t,const qa_render_operation *ops,size_t count){
  for(size_t i=0;i<count;i++){
    if(ops[i].kind==QA_RENDER_OP_DRAW){ for(size_t j=0;j<ops[i].batch_count;j++)qa_software_renderer_draw(t->backend,ops[i].batches[j]); }
    else if(ops[i].kind==QA_RENDER_OP_OBJECT_OPACITY){ op_ctx c;c.t=t;c.batches=ops[i].batches;c.count=ops[i].batch_count; qa_software_renderer_with_object_opacity(t->backend,ops[i].opacity,op_draw,&c); }
    else qa_software_renderer_draw_immediate(t->backend,ops[i]);
  }
}
void qa_cpu_render_target_execute(qa_cpu_render_target *t,qa_render_frame frame){
  qa_renderer_resource_owner owner=t->backend->owner;
  if(frame.owner.identity!=owner.identity||frame.owner.session.token!=owner.session.token||frame.owner.generation!=owner.generation) qa_range_error("CPU frame belongs to another renderer owner");
  for(size_t i=0;i<frame.command_count;i++){
    const qa_render_command *c=&frame.commands[i];
    switch(c->kind){
    case QA_RENDER_COMMAND_IMAGE_RESOURCE: qa_software_renderer_apply_image_resource(t->backend,c->operation); break;
    case QA_RENDER_COMMAND_DRAW_BUFFER: qa_software_renderer_select_draw_buffer(t->backend,c->buffer,c->clear); break;
    case QA_RENDER_COMMAND_SET_COLOR: t->color=c->color; break;
    case QA_RENDER_COMMAND_STRETCH_PIC: qa_software_renderer_draw_stretch_pic(t->backend,c->image,c->rect,c->uv,t->color); break;
    case QA_RENDER_COMMAND_VIEW:
      run_ops(t,c->view.before_view,c->view.before_view_count);
      qa_software_renderer_begin_view(t->backend,c->view.state);
      run_ops(t,c->view.operations,c->view.operation_count);
      break;
    case QA_RENDER_COMMAND_SWAP_BUFFERS: qa_cpu_render_target_present(t); break;
    default: qa_range_error("Invalid CPU command"); }
  }
}
void qa_cpu_render_target_present(qa_cpu_render_target *t){
  qa_software_renderer_finish(t->backend);
  if(t->presenter==NULL)return;
  double w=0,h=0; t->presenter->drawable_size(t->presenter->ctx,&w,&h);
  if(w!=t->backend->width||h!=t->backend->height) qa_range_error("CPU framebuffer dimensions must match the SDL drawable");
  size_t n=0; const uint8_t *px=qa_software_renderer_pixels(t->backend,&n);
  t->presenter->present(t->presenter->ctx,px,n);
}
void qa_cpu_render_target_close(qa_cpu_render_target *t){ qa_software_renderer_close(t->backend); }
