/* Port of src/render/commands/frame.ts. GPL-2.0-or-later. */
#include "qa/render/commands/frame.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
typedef struct bimpl { qa_render_command *commands; size_t count, cap; double sequence; bool active; } bimpl;
static bimpl *B(qa_scene_frame_builder *b){ return (bimpl*)b->impl; }
static void push(qa_scene_frame_builder *b,qa_render_command c){ bimpl *p=B(b);
  if(p->count==p->cap){ size_t nc=p->cap?p->cap*2:16; qa_render_command *n=(qa_render_command*)realloc(p->commands,nc*sizeof *n); if(!n)qa_range_error("frame builder allocation failed"); p->commands=n;p->cap=nc; }
  p->commands[p->count++]=c; }
static void require_active(qa_scene_frame_builder *b){ if(!B(b)->active) qa_range_error("Begin a scene frame before submitting commands"); }
bool qa_scene_frame_builder_init(qa_scene_frame_builder *b,qa_render_frame_image_registry images){ b->impl=calloc(1,sizeof(bimpl)); b->images=images; return b->impl!=NULL; }
void qa_scene_frame_builder_destroy(qa_scene_frame_builder *b){ if(!b->impl)return; free(B(b)->commands); free(b->impl); b->impl=NULL; }
void qa_scene_frame_builder_resources(qa_scene_frame_builder *b,const qa_image_resource_operation *ops,size_t count){ require_active(b);
  for(size_t i=0;i<count;i++){ qa_render_command c; memset(&c,0,sizeof c); c.kind=QA_RENDER_COMMAND_IMAGE_RESOURCE; c.operation=ops[i]; push(b,c); } }
void qa_scene_frame_builder_begin(qa_scene_frame_builder *b,const char *buffer,bool clear){ bimpl *p=B(b);
  if(p->active) qa_range_error("A scene frame is already being prepared");
  p->active=true; p->count=0;
  qa_render_command c; memset(&c,0,sizeof c); c.kind=QA_RENDER_COMMAND_DRAW_BUFFER; c.buffer=buffer; c.clear=clear; push(b,c);
  size_t n=0; const qa_image_resource_operation *ops=b->images.drain_operations(b->images.ctx,&n);
  qa_scene_frame_builder_resources(b,ops,n); }
void qa_scene_frame_builder_begin_default(qa_scene_frame_builder *b){ qa_scene_frame_builder_begin(b,"back",false); }
void qa_scene_frame_builder_view(qa_scene_frame_builder *b,qa_render_view view){ require_active(b);
  if(view.target_is_seat&&view.target_seat.session.token!=b->images.owner.session.token) qa_range_error("Render seat belongs to another session");
  size_t n=0; const qa_image_resource_operation *ops=b->images.drain_operations(b->images.ctx,&n);
  qa_scene_frame_builder_resources(b,ops,n);
  qa_render_command c; memset(&c,0,sizeof c); c.kind=QA_RENDER_COMMAND_VIEW; c.view=view; push(b,c); }
void qa_scene_frame_builder_world(qa_scene_frame_builder *b,qa_render_prepared_world_view prepared){
  qa_scene_frame_builder_resources(b,prepared.image_operations,prepared.image_operation_count);
  qa_scene_frame_builder_view(b,prepared.view); }
bool qa_clip_picture(qa_rect rect,qa_texture_rect uv,qa_rect clip,qa_clipped_picture *out){
  if(rect.width==0||rect.height==0)return false;
  double left=qa_js_max(qa_js_min(rect.x,rect.x+rect.width),clip.x),right=qa_js_min(qa_js_max(rect.x,rect.x+rect.width),clip.x+clip.width);
  double top=qa_js_max(qa_js_min(rect.y,rect.y+rect.height),clip.y),bottom=qa_js_min(qa_js_max(rect.y,rect.y+rect.height),clip.y+clip.height);
  if(left>=right||top>=bottom)return false;
  out->rect.x=left;out->rect.y=top;out->rect.width=right-left;out->rect.height=bottom-top;
  out->uv.s1=uv.s1+(uv.s2-uv.s1)*(left-rect.x)/rect.width; out->uv.s2=uv.s1+(uv.s2-uv.s1)*(right-rect.x)/rect.width;
  out->uv.t1=uv.t1+(uv.t2-uv.t1)*(top-rect.y)/rect.height; out->uv.t2=uv.t1+(uv.t2-uv.t1)*(bottom-rect.y)/rect.height;
  return true;
}
void qa_scene_frame_builder_picture(qa_scene_frame_builder *b,qa_seat_id seat,qa_rect viewport,qa_rect rect,qa_renderer_image image,qa_texture_rect uv,qa_vec4 color){
  if(seat.session.token!=b->images.owner.session.token) qa_range_error("HUD seat belongs to another session");
  b->images.require(b->images.ctx,image);
  qa_rect clip; clip.x=0;clip.y=0;clip.width=viewport.width;clip.height=viewport.height;
  qa_clipped_picture clipped; if(!qa_clip_picture(rect,uv,clip,&clipped))return;
  qa_render_command c; memset(&c,0,sizeof c); c.kind=QA_RENDER_COMMAND_SET_COLOR; c.color=color;
  qa_scene_frame_builder_command(b,c);
  memset(&c,0,sizeof c); c.kind=QA_RENDER_COMMAND_STRETCH_PIC; c.image=image; c.uv=clipped.uv;
  c.rect=clipped.rect; c.rect.x=viewport.x+clipped.rect.x; c.rect.y=viewport.y+clipped.rect.y;
  qa_scene_frame_builder_command(b,c); }
void qa_scene_frame_builder_command(qa_scene_frame_builder *b,qa_render_command command){ require_active(b); push(b,command); }
qa_render_frame qa_scene_frame_builder_finish(qa_scene_frame_builder *b,bool present){ require_active(b); bimpl *p=B(b);
  if(present){ qa_render_command c; memset(&c,0,sizeof c); c.kind=QA_RENDER_COMMAND_SWAP_BUFFERS; push(b,c); }
  qa_render_frame frame; frame.owner=b->images.owner; frame.sequence=p->sequence++; frame.commands=p->commands; frame.command_count=p->count;
  /* TS hands the array to the frame and starts a fresh one; C borrows
   * builder storage (documented in the header) and resets the count. */
  p->active=false; return frame; }
void qa_scene_frame_builder_discard(qa_scene_frame_builder *b){ bimpl *p=B(b); p->count=0; p->active=false; }
