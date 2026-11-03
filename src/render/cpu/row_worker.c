/* Port of src/render/cpu/row-worker.ts: decodes flat triangle setups from the
 * shared job buffer and rasterizes disjoint row bands into the shared framebuffer.
 * See row_parallel.c for the coordinator and encoding. GPL-2.0-or-later. */
#include "qa/render/cpu/row_worker.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
typedef struct wimpl {
  qa_cpu_framebuffer fb; qa_cpu_sample sampled; qa_row_control *control; double *job; size_t job_count;
  qa_row_texture_registration *tex; size_t tex_count,tex_cap;
  qa_row_lighting_registration *lit; size_t lit_count,lit_cap;
  qa_row_depth_registration *dep; size_t dep_count,dep_cap;
  qa_triangle_setup *setups; qa_row_decode_storage *storage; size_t setup_cap;
} wimpl;
bool qa_row_control_init(qa_row_control *c){
  memset(c,0,sizeof *c); atomic_init(&c->next,0);
  if(pthread_mutex_init(&c->lock,NULL))return false;
  if(pthread_cond_init(&c->job_ready,NULL)){ pthread_mutex_destroy(&c->lock); return false; }
  if(pthread_cond_init(&c->progress,NULL)){ pthread_cond_destroy(&c->job_ready); pthread_mutex_destroy(&c->lock); return false; }
  return true;
}
void qa_row_control_destroy(qa_row_control *c){ pthread_cond_destroy(&c->progress); pthread_cond_destroy(&c->job_ready); pthread_mutex_destroy(&c->lock); }
bool qa_row_worker_init(qa_row_worker *w,qa_row_shared_framebuffer shared,qa_row_control *control){
  wimpl *p=(wimpl*)calloc(1,sizeof *p); if(!p)return false; w->impl=p; p->control=control;
  p->fb.width=shared.width;p->fb.height=shared.height;p->fb.pixels=shared.color;p->fb.color_words=(int32_t*)shared.color;p->fb.depth=shared.depth;p->fb.stencil=shared.stencil;p->fb.stride=shared.width;p->fb.storage_count=(size_t)(shared.width*shared.height);
  p->sampled.r=p->sampled.g=p->sampled.b=p->sampled.a=1; return true;
}
void qa_row_worker_destroy(qa_row_worker *w){ if(!w->impl)return; wimpl *p=(wimpl*)w->impl; free(p->tex);free(p->lit);free(p->dep);free(p->setups);free(p->storage);free(p); w->impl=NULL; }
/* Registration ids ascend within a job and depth ids ascend across jobs, so a binary
 * search stands in for the TS Map. */
#define FIND_BY_ID(items, count, wanted, out) do { size_t lo_=0, hi_=(count); (out)=NULL; \
  while(lo_<hi_){ size_t mid_=lo_+(hi_-lo_)/2; if((items)[mid_].id==(wanted)){(out)=&(items)[mid_];break;} if((items)[mid_].id<(wanted))lo_=mid_+1; else hi_=mid_; } } while(0)
static qa_bound_texture w_tex(void *ctx,double id){ wimpl *p=(wimpl*)ctx; qa_bound_texture o; memset(&o,0,sizeof o); o.kind=QA_BOUND_TEXTURE_INCOMPLETE; if(id==-1)return o;
  const qa_row_texture_registration *found; FIND_BY_ID(p->tex,p->tex_count,id,found);
  if(!found)qa_range_error("Row worker missing texture %.0f",id);
  return found->texture; }
static qa_batch_lighting w_lit(void *ctx,double id){ wimpl *p=(wimpl*)ctx;
  const qa_row_lighting_registration *found; FIND_BY_ID(p->lit,p->lit_count,id,found);
  if(!found)qa_range_error("Row worker missing lighting %.0f",id);
  return found->parameters; }
static const qa_depth_image_level *w_dep(void *ctx,double id){ wimpl *p=(wimpl*)ctx; if(id==-1)return NULL;
  const qa_row_depth_registration *found; FIND_BY_ID(p->dep,p->dep_count,id,found);
  if(!found)qa_range_error("Row worker missing depth %.0f",id);
  return &found->depth; }
static void *worker_alloc(void *data,size_t count,size_t element){ void *grown=realloc(data,count*element); if(!grown)qa_range_error("row worker allocation failed"); return grown; }
typedef struct job_ctx { wimpl *worker; qa_row_job_message message; } job_ctx;
static void run_job_body(void *ctx){
  wimpl *p=((job_ctx*)ctx)->worker; qa_row_job_message msg=((job_ctx*)ctx)->message;
  if(msg.kind==NULL||strcmp(msg.kind,"job")) qa_range_error("Invalid row worker job");
  /* Registrations cover one job, except depth images, whose ids persist. */
  if(msg.texture_count>p->tex_cap){ p->tex=(qa_row_texture_registration*)worker_alloc(p->tex,msg.texture_count,sizeof *p->tex); p->tex_cap=msg.texture_count; }
  p->tex_count=0;
  for(size_t i=0;i<msg.texture_count;i++){ const qa_row_texture_registration *r=&msg.textures[i];
    if(r->texture.kind==QA_BOUND_TEXTURE_IMAGE&&r->texture.level_count==0) qa_range_error("Row worker texture has no levels");
    p->tex[p->tex_count++]=*r; }
  if(msg.lighting_count>p->lit_cap){ p->lit=(qa_row_lighting_registration*)worker_alloc(p->lit,msg.lighting_count,sizeof *p->lit); p->lit_cap=msg.lighting_count; }
  p->lit_count=0;
  for(size_t i=0;i<msg.lighting_count;i++){ const qa_row_lighting_registration *r=&msg.lightings[i];
    if(strcmp(r->parameters.kind,"vertex")&&strcmp(r->parameters.kind,"q2-world")&&strcmp(r->parameters.kind,"q2-model-shadow")) qa_range_error("Invalid row worker lighting registration");
    p->lit[p->lit_count++]=*r; }
  if(p->dep_count+msg.depth_count>p->dep_cap){ size_t cap=p->dep_count+msg.depth_count; p->dep=(qa_row_depth_registration*)worker_alloc(p->dep,cap,sizeof *p->dep); p->dep_cap=cap; }
  for(size_t i=0;i<msg.depth_count;i++) p->dep[p->dep_count++]=msg.depths[i];
  if(msg.job_buffer!=NULL){ p->job=msg.job_buffer; p->job_count=msg.job_count; }
  if(p->job==NULL) qa_range_error("Row worker has no job buffer");
  size_t n=(size_t)msg.count; double band_rows=qa_js_max(1,msg.band_rows);
  if(n>p->setup_cap){
    p->setups=(qa_triangle_setup*)worker_alloc(p->setups,n,sizeof *p->setups);
    p->storage=(qa_row_decode_storage*)worker_alloc(p->storage,n,sizeof *p->storage); p->setup_cap=n;
  }
  qa_row_decode_registries reg; reg.ctx=p; reg.texture=w_tex; reg.lighting=w_lit; reg.depth=w_dep;
  for(size_t i=0;i<n;i++) p->setups[i]=qa_decode_row_setup(p->job,p->job_count,(double)(i*QA_ROW_SETUP_STRIDE),reg,&p->storage[i]);
  double height=p->fb.height, band_count=ceil(height/band_rows);
  for(;;){
    double band=(double)atomic_fetch_add(&p->control->next,1);
    if(band>=band_count)break;
    double first_y=band*band_rows, last_y=qa_js_min(height-1,first_y+band_rows-1);
    for(size_t i=0;i<n;i++) qa_run_worker_triangle_rows(&p->setups[i],&p->fb,&p->sampled,first_y,last_y);
  }
}
void qa_row_worker_run_job(qa_row_worker *w,qa_row_job_message msg){
  wimpl *p=(wimpl*)w->impl; job_ctx ctx; ctx.worker=p; ctx.message=msg; char error[1024];
  bool ok=qa_range_error_catch(run_job_body,&ctx,error,sizeof error);
  pthread_mutex_lock(&p->control->lock);
  if(!ok){ p->control->failed=true; memcpy(p->control->error,error,sizeof error); }
  p->control->done++; pthread_cond_signal(&p->control->progress);
  pthread_mutex_unlock(&p->control->lock);
}
void *qa_row_worker_main(void *arg){
  qa_row_worker *w=(qa_row_worker*)arg; qa_row_control *c=((wimpl*)w->impl)->control; uint64_t seen=0;
  pthread_mutex_lock(&c->lock); c->ready++; pthread_cond_signal(&c->progress);
  for(;;){
    while(!c->shutdown&&c->generation==seen)pthread_cond_wait(&c->job_ready,&c->lock);
    if(c->shutdown)break;
    seen=c->generation; qa_row_job_message message=c->message;
    pthread_mutex_unlock(&c->lock);
    qa_row_worker_run_job(w,message);
    pthread_mutex_lock(&c->lock);
  }
  pthread_mutex_unlock(&c->lock);
  return NULL;
}
