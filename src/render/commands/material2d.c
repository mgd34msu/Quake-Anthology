/* Port of src/render/commands/material2d.ts. GPL-2.0-or-later. */
#include "qa/render/commands/material2d.h"
#include "qa/render/commands/frame.h"
#include "qa/materials/compile.h"
#include "qa/render/scene/material_registrations.h"
#include <string.h>
typedef struct proj_ctx { qa_rect viewport; } proj_ctx;
static qa_vec4 project_2d(void *ctx,qa_vec3 position){ proj_ctx *p=(proj_ctx*)ctx; qa_vec4 o;
  o.x=2*(position.x-p->viewport.x)/p->viewport.width-1; o.y=1-2*(position.y-p->viewport.y)/p->viewport.height; o.z=0;o.w=1; return o; }
int qa_prepare_material_text(qa_material_text_draw draw,qa_rect viewport,qa_material_draw_context context,qa_draw_batch **out_batches,size_t *out_count){
  qa_texture_rect uv_in; uv_in.s1=draw.uv.s;uv_in.t1=draw.uv.t;uv_in.s2=draw.uv.s2;uv_in.t2=draw.uv.t2;
  qa_clipped_picture clipped; *out_batches=NULL; *out_count=0;
  if(!qa_clip_picture(draw.rect,uv_in,viewport,&clipped)) return 1;
  qa_rect rect=clipped.rect; qa_texture_rect uv=clipped.uv;
  double right=rect.x+rect.width,bottom=rect.y+rect.height;
  qa_vec4 color; color.x=draw.color.x*255;color.y=draw.color.y*255;color.z=draw.color.z*255;color.w=draw.color.w*255;
  double xs[4]={rect.x,right,right,rect.x},ys[4]={rect.y,rect.y,bottom,bottom};
  double ss[4]={uv.s1,uv.s2,uv.s2,uv.s1},ts[4]={uv.t1,uv.t1,uv.t2,uv.t2};
  qa_material_vertex verts[4]; double indices[6]={0,1,2,0,2,3};
  for(int i=0;i<4;i++){ memset(&verts[i],0,sizeof verts[i]);
    verts[i].position.x=xs[i];verts[i].position.y=ys[i];verts[i].position.z=0;
    verts[i].normal.z=1; verts[i].tex_coord.x=ss[i];verts[i].tex_coord.y=ts[i]; verts[i].color=color; }
  qa_material_geometry geometry; geometry.indices=indices;geometry.index_count=6;geometry.vertices=verts;geometry.vertex_count=4;
  /* original = draw.picture.material.compiled, remap = currentRemap(original): a MaterialHandle
   * draws its current material (or its remap); a plain CompiledMaterial has no remap. */
  const qa_scene_material_handle *handle=draw.picture.compiled_is_handle?(const qa_scene_material_handle*)draw.picture.compiled:NULL;
  const qa_mat_compiled_material *original=handle!=NULL?handle->current:(const qa_mat_compiled_material*)draw.picture.compiled;
  qa_scene_material_remap remap; bool has_remap=handle!=NULL&&qa_current_remap(original,handle,&remap);
  if(has_remap){ context.time_offset=context.time_offset+remap.time_offset; original=qa_registered_scene_material_compiled(remap.material); }
  proj_ctx pc; pc.viewport=viewport;
  context.fog=NULL; context.entity_rgba=color;
  context.project.ctx=&pc; context.project.project=project_2d;
  size_t count=0; qa_draw_batch *batches=qa_prepare_material_batches(original,geometry,&context,&count);
  if(count>0&&batches==NULL) return 0;
  for(size_t i=0;i<count;i++){ batches[i].state.depth_test="always"; batches[i].state.depth_write=false; batches[i].state.cull="none"; }
  *out_batches=batches; *out_count=count; return 1;
}
