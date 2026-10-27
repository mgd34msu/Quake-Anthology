/* Port of src/render/gl/depth-atlas.ts (reduced). GPL-2.0-or-later. */
#include "qa/render/gl/depth_atlas.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>
typedef struct dimpl { qa_gl *gl; qa_gl_stage_program *program; } dimpl;
bool qa_depth_atlas_target_init(qa_depth_atlas_target *t,const qa_gl_context *context,qa_gl *gl,qa_gl_stage_program *program){
  (void)context; t->impl=calloc(1,sizeof(dimpl)); if(!t->impl)return false;
  ((dimpl*)t->impl)->gl=gl; ((dimpl*)t->impl)->program=program; return true;
}
void qa_depth_atlas_target_draw(qa_depth_atlas_target *t,double texture,double width,double height,const qa_depth_atlas_pass *passes,size_t pass_count){
  (void)t;(void)texture;
  if(width!=(double)(long long)width||height!=(double)(long long)height||width<=0||height<=0) qa_range_error("OpenGL depth atlas dimensions must be positive integers");
  for(size_t i=0;i<pass_count;i++) for(size_t j=0;j<passes[i].draw_count;j++){
    if(passes[i].draws[j].indices.count%3!=0) qa_range_error("OpenGL depth atlas indices must form complete triangles");
    for(size_t k=0;k<passes[i].draws[j].position_count;k++) if(!isfinite(passes[i].draws[j].positions[k])) qa_range_error("OpenGL depth atlas positions must be finite");
  }
  /* Reduced: FBO rasterization pending (see header). */
}
void qa_depth_atlas_target_close(qa_depth_atlas_target *t){ free(t->impl); t->impl=NULL; }
