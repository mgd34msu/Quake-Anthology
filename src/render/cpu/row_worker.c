/* Port of src/render/cpu/row-worker.ts (direct/pumped). GPL-2.0-or-later. */
#include "qa/render/cpu/row_worker.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
typedef struct wimpl { qa_cpu_framebuffer fb; qa_cpu_sample sampled; double *job; size_t job_count;
  qa_row_texture_registration *tex; size_t tex_count,tex_cap;
  qa_row_lighting_registration *lit; size_t lit_count,lit_cap;
  qa_row_depth_registration *dep; size_t dep_count,dep_cap; } wimpl;
bool qa_row_worker_init(qa_row_worker *w,qa_row_shared_framebuffer shared){ wimpl *p=(wimpl*)calloc(1,sizeof *p); if(!p)return false; w->impl=p;
  p->fb.width=shared.width;p->fb.height=shared.height;p->fb.pixels=shared.color;p->fb.color_words=(int32_t*)shared.color;p->fb.depth=shared.depth;p->fb.stencil=shared.stencil;p->fb.stride=shared.width;p->fb.storage_count=(size_t)(shared.width*shared.height);
  p->sampled.r=p->sampled.g=p->sampled.b=p->sampled.a=1; return true; }
void qa_row_worker_destroy(qa_row_worker *w){ if(!w->impl)return; wimpl *p=(wimpl*)w->impl; free(p->tex);free(p->lit);free(p->dep);free(p); w->impl=NULL; }
static qa_bound_texture w_tex(void *ctx,double id){ wimpl *p=(wimpl*)ctx; qa_bound_texture o; memset(&o,0,sizeof o); o.kind=QA_BOUND_TEXTURE_INCOMPLETE; if(id==-1)return o;
  for (size_t i = 0; i < p->tex_count; i++) if (p->tex[i].id == id) return p->tex[i].texture;
  qa_range_error("Row worker missing texture"); }
static qa_batch_lighting w_lit(void *ctx,double id){ wimpl *p=(wimpl*)ctx; for(size_t i=0;i<p->lit_count;i++)if(p->lit[i].id==id)return p->lit[i].parameters; qa_range_error("Row worker missing lighting"); }
static const qa_depth_image_level *w_dep(void *ctx,double id){ wimpl *p=(wimpl*)ctx; if(id==-1)return NULL; for(size_t i=0;i<p->dep_count;i++)if(p->dep[i].id==id)return &p->dep[i].depth; qa_range_error("Row worker missing depth"); }
static void push_tex(wimpl *p,qa_row_texture_registration r){ for(size_t i=0;i<p->tex_count;i++)if(p->tex[i].id==r.id){p->tex[i]=r;return;} if(p->tex_count==p->tex_cap){size_t nc=p->tex_cap?p->tex_cap*2:16;qa_row_texture_registration*n=(qa_row_texture_registration*)realloc(p->tex,nc*sizeof*n);if(!n)qa_range_error("row worker allocation failed");p->tex=n;p->tex_cap=nc;} p->tex[p->tex_count++]=r; }
static void push_lit(wimpl *p,qa_row_lighting_registration r){ for(size_t i=0;i<p->lit_count;i++)if(p->lit[i].id==r.id){p->lit[i]=r;return;} if(p->lit_count==p->lit_cap){size_t nc=p->lit_cap?p->lit_cap*2:16;qa_row_lighting_registration*n=(qa_row_lighting_registration*)realloc(p->lit,nc*sizeof*n);if(!n)qa_range_error("row worker allocation failed");p->lit=n;p->lit_cap=nc;} p->lit[p->lit_count++]=r; }
static void push_dep(wimpl *p,qa_row_depth_registration r){ for(size_t i=0;i<p->dep_count;i++)if(p->dep[i].id==r.id){p->dep[i]=r;return;} if(p->dep_count==p->dep_cap){size_t nc=p->dep_cap?p->dep_cap*2:16;qa_row_depth_registration*n=(qa_row_depth_registration*)realloc(p->dep,nc*sizeof*n);if(!n)qa_range_error("row worker allocation failed");p->dep=n;p->dep_cap=nc;} p->dep[p->dep_count++]=r; }
void qa_row_worker_run_job(qa_row_worker *w,qa_row_job_message msg){
  wimpl *p=(wimpl*)w->impl;
  if(msg.kind==NULL||strcmp(msg.kind,"job")) qa_range_error("Invalid row worker job");
  for(size_t i=0;i<msg.texture_count;i++){ qa_row_texture_registration r=msg.textures[i];
    if (r.texture.kind == QA_BOUND_TEXTURE_IMAGE && r.texture.level_count == 0) qa_range_error("Row worker texture has no levels");
    push_tex(p, r); }
  for(size_t i=0;i<msg.lighting_count;i++){ qa_row_lighting_registration r=msg.lightings[i];
    if(strcmp(r.parameters.kind,"vertex")&&strcmp(r.parameters.kind,"q2-world")&&strcmp(r.parameters.kind,"q2-model-shadow")) qa_range_error("Invalid row worker lighting registration");
    push_lit(p, r); }
  for(size_t i=0;i<msg.depth_count;i++) push_dep(p,msg.depths[i]);
  if(msg.job_buffer!=NULL){ p->job=msg.job_buffer; p->job_count=msg.job_count; }
  if(p->job==NULL) qa_range_error("Row worker has no job buffer");
  double count=msg.count, band_rows=fmax(1,msg.band_rows);
  size_t n=(size_t)count;
  qa_triangle_setup *setups=(qa_triangle_setup*)calloc(n?n:1,sizeof *setups);
  qa_row_decode_storage *storage=(qa_row_decode_storage*)calloc(n?n:1,sizeof *storage);
  if(!setups||!storage)qa_range_error("row worker allocation failed");
  qa_row_decode_registries reg; reg.ctx=p; reg.texture=w_tex; reg.lighting=w_lit; reg.depth=w_dep;
  for(size_t i=0;i<n;i++) setups[i]=qa_decode_row_setup(p->job,p->job_count,(double)(i*QA_ROW_SETUP_STRIDE),reg,&storage[i]);
  double band_count=ceil(p->fb.height/band_rows);
  for(double band=0;band<band_count;band++){ double fy=band*band_rows,ly=fmin(p->fb.height-1,fy+band_rows-1);
    for(size_t i=0;i<n;i++) qa_run_worker_triangle_rows(&setups[i],&p->fb,&p->sampled,fy,ly); }
  free(setups); free(storage);
}
