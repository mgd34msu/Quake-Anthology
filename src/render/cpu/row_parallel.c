/* Port of src/render/cpu/row-parallel.ts. Row-parallel CPU rasterization: a
 * synchronous pthread pool sharing one framebuffer. Every worker receives the
 * same setup list but rasterizes only the row bands it claims, so each pixel has
 * exactly one owner processing triangles in draw order and output is bit-identical
 * to the serial kernel. GPL-2.0-or-later. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include "qa/render/cpu/row_parallel.h"
#include "qa/foundation/support.h"
#include "qa/render/cpu/row_worker.h"
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "qa/foundation/js.h"
static const char *const BLEND_FACTORS[]={"zero","one","src-color","one-minus-src-color","dst-color","one-minus-dst-color","src-alpha","one-minus-src-alpha","dst-alpha","one-minus-dst-alpha","src-alpha-saturate"};
static const char *const BLENDINGS[]={"opaque","alpha","add","multiply","dst-color-inverse-dst-alpha","general"};
static const char *const DEPTH_TESTS[]={"less-equal","equal","always"};
static const char *const ALPHA_TESTS[]={"none","gt0","lt128","ge128"};
static const char *const STENCIL_OPS[]={"keep","increment","decrement"};
static const char *const ENVS[]={"modulate","add","replace"};
static const char *const FOG_EFFECTS[]={"color","none","rgb","alpha","rgba","overlay"};
static double code_of(const char *const *vals,size_t n,const char *v){ for(size_t i=0;i<n;i++)if(!strcmp(vals[i],v))return (double)i; qa_range_error("Unknown row-parallel enum value %s",v); }
static const char *uncode(const char *const *vals,size_t n,double v){ if(!(v>=0&&v<(double)n)||floor(v)!=v)qa_range_error("Invalid row-parallel enum code"); return vals[(size_t)v]; }
#define N(a) (sizeof(a)/sizeof((a)[0]))
void qa_encode_row_setup(double *data,size_t data_count,double offset_d,const qa_triangle_setup *s,qa_row_registries reg){
  size_t o=(size_t)offset_d; if(o+QA_ROW_SETUP_STRIDE>data_count)qa_range_error("Row setup encoding is outside its job buffer");
  double *d=data+o;
  d[0]=s->min_x;d[1]=s->max_x;d[2]=s->min_y;d[3]=s->max_y;d[4]=s->inverse_area;d[5]=s->depth_near;d[6]=s->depth_far;
  d[7]=s->edge_ax;d[8]=s->edge_ay;d[9]=s->edge_ac;d[10]=s->edge_bx;d[11]=s->edge_by;d[12]=s->edge_bc;d[13]=s->edge_cx;d[14]=s->edge_cy;d[15]=s->edge_cc;
  d[16]=s->attribute_ax;d[17]=s->attribute_ay;d[18]=s->attribute_ac;d[19]=s->attribute_bx;d[20]=s->attribute_by;d[21]=s->attribute_bc;d[22]=s->attribute_cx;d[23]=s->attribute_cy;d[24]=s->attribute_cc;
  d[25]=s->az;d[26]=s->bz;d[27]=s->cz;d[28]=s->polygon_depth_offset;d[29]=s->plane_depth;
  d[30]=s->aiw;d[31]=s->biw;d[32]=s->ciw;d[33]=s->au;d[34]=s->bu;d[35]=s->cu;d[36]=s->av;d[37]=s->bv;d[38]=s->cv;
  d[39]=s->au2;d[40]=s->bu2;d[41]=s->cu2;d[42]=s->av2;d[43]=s->bv2;d[44]=s->cv2;
  d[45]=s->ar;d[46]=s->br;d[47]=s->cr;d[48]=s->ag;d[49]=s->bg;d[50]=s->cg;d[51]=s->ab;d[52]=s->bb;d[53]=s->cb;d[54]=s->aa;d[55]=s->ba;d[56]=s->ca;
  d[57]=s->edge_a_inclusive?1:0;d[58]=s->edge_b_inclusive?1:0;d[59]=s->edge_c_inclusive?1:0;
  d[60]=s->white?1:0;d[61]=s->depth_write?1:0;d[62]=s->stencil_enabled?1:0;d[63]=s->color_write?1:0;d[64]=s->texture_consumed?1:0;d[65]=s->primary_alpha?1:0;d[66]=s->secondary_alpha?1:0;d[67]=s->constant_depth?1:0;
  d[68]=s->width;d[69]=s->height;d[70]=s->alpha_bits;
  d[71]=code_of(BLENDINGS,N(BLENDINGS),s->blending);d[72]=code_of(DEPTH_TESTS,N(DEPTH_TESTS),s->depth_test);d[73]=code_of(ALPHA_TESTS,N(ALPHA_TESTS),s->alpha_test);
  d[74]=s->secondary_environment==NULL?-1:code_of(ENVS,N(ENVS),s->secondary_environment);
  d[75]=code_of(BLEND_FACTORS,N(BLEND_FACTORS),s->blend.source);d[76]=code_of(BLEND_FACTORS,N(BLEND_FACTORS),s->blend.destination);
  d[77]=!strcmp(s->stencil_function,"nonzero")?1:0;d[78]=s->stencil_compare_mask;d[79]=s->stencil_write_mask;d[80]=s->stencil_maximum;
  d[81]=code_of(STENCIL_OPS,N(STENCIL_OPS),s->stencil_depth_fail);d[82]=code_of(STENCIL_OPS,N(STENCIL_OPS),s->stencil_depth_pass);
  d[83]=reg.texture_id(reg.ctx,&s->texture);d[84]=reg.texture_id(reg.ctx,&s->secondary_texture);
  d[85]=reg.lighting_id(reg.ctx,&s->lighting->base.parameters);d[86]=reg.depth_id(reg.ctx,s->lighting->base.depth);
  d[87]=s->derivative.u_anchor;d[88]=s->derivative.v_anchor;d[89]=s->derivative.u_dx;d[90]=s->derivative.v_dx;d[91]=s->derivative.q_dx;d[92]=s->derivative.u_dy;d[93]=s->derivative.v_dy;d[94]=s->derivative.q_dy;
  d[95]=s->secondary_derivative.u_anchor;d[96]=s->secondary_derivative.v_anchor;d[97]=s->secondary_derivative.u_dx;d[98]=s->secondary_derivative.v_dx;d[99]=s->secondary_derivative.q_dx;d[100]=s->secondary_derivative.u_dy;d[101]=s->secondary_derivative.v_dy;d[102]=s->secondary_derivative.q_dy;
  const qa_batch_fog *fog=s->fog;
  d[103]=fog==NULL?0:!strcmp(fog->kind,"exp2")?1:2;
  d[104]=(fog&&!strcmp(fog->kind,"exp2"))?code_of(FOG_EFFECTS,N(FOG_EFFECTS),fog->effect?fog->effect:"color"):0;
  d[105]=fog?fog->color.x:0;d[106]=fog?fog->color.y:0;d[107]=fog?fog->color.z:0;
  d[108]=fog==NULL?0:!strcmp(fog->kind,"exp2")?fog->density:fog->amount;
  d[109]=s->has_fog_depth_scale?s->fog_depth_scale:1;
  d[110]=(s->texture_effect&&!strcmp(s->texture_effect,"luminance-alpha"))?1:0;
  for(int i=0;i<3;i++){ d[111+i*3]=s->lighting->positions[i].x;d[112+i*3]=s->lighting->positions[i].y;d[113+i*3]=s->lighting->positions[i].z;
    d[120+i*3]=s->lighting->normals[i].x;d[121+i*3]=s->lighting->normals[i].y;d[122+i*3]=s->lighting->normals[i].z; }
}
static double at(const double *d,size_t data_count,size_t o,int i){ return (o+(size_t)i<data_count)?d[o+(size_t)i]:0; }
qa_triangle_setup qa_decode_row_setup(const double *data,size_t data_count,double offset_d,qa_row_decode_registries reg,qa_row_decode_storage *st){
  size_t o=(size_t)offset_d; qa_triangle_setup s; memset(&s,0,sizeof s);
  for(int i=0;i<3;i++){ st->lighting.positions[i].x=at(data,data_count,o,111+i*3);st->lighting.positions[i].y=at(data,data_count,o,112+i*3);st->lighting.positions[i].z=at(data,data_count,o,113+i*3);
    st->lighting.normals[i].x=at(data,data_count,o,120+i*3);st->lighting.normals[i].y=at(data,data_count,o,121+i*3);st->lighting.normals[i].z=at(data,data_count,o,122+i*3); }
  double fk=at(data,data_count,o,103);
  if(fk!=0){ memset(&st->fog,0,sizeof st->fog); st->fog.color.x=at(data,data_count,o,105);st->fog.color.y=at(data,data_count,o,106);st->fog.color.z=at(data,data_count,o,107);
    if(fk==1){st->fog.kind="exp2";st->fog.effect=uncode(FOG_EFFECTS,N(FOG_EFFECTS),at(data,data_count,o,104));st->fog.density=at(data,data_count,o,108);}
    else {st->fog.kind="constant";st->fog.amount=at(data,data_count,o,108);}
    s.fog=&st->fog; s.has_fog_depth_scale=true; s.fog_depth_scale=at(data,data_count,o,109); }
  s.texture_effect=at(data,data_count,o,110)==0?NULL:"luminance-alpha";
  st->lighting.base.parameters=reg.lighting(reg.ctx,at(data,data_count,o,85));
  st->lighting.base.depth=reg.depth(reg.ctx,at(data,data_count,o,86));
  s.lighting=&st->lighting;
  s.min_x=at(data,data_count,o,0);s.max_x=at(data,data_count,o,1);s.min_y=at(data,data_count,o,2);s.max_y=at(data,data_count,o,3);s.inverse_area=at(data,data_count,o,4);s.depth_near=at(data,data_count,o,5);s.depth_far=at(data,data_count,o,6);
  s.edge_ax=at(data,data_count,o,7);s.edge_ay=at(data,data_count,o,8);s.edge_ac=at(data,data_count,o,9);s.edge_bx=at(data,data_count,o,10);s.edge_by=at(data,data_count,o,11);s.edge_bc=at(data,data_count,o,12);s.edge_cx=at(data,data_count,o,13);s.edge_cy=at(data,data_count,o,14);s.edge_cc=at(data,data_count,o,15);
  s.attribute_ax=at(data,data_count,o,16);s.attribute_ay=at(data,data_count,o,17);s.attribute_ac=at(data,data_count,o,18);s.attribute_bx=at(data,data_count,o,19);s.attribute_by=at(data,data_count,o,20);s.attribute_bc=at(data,data_count,o,21);s.attribute_cx=at(data,data_count,o,22);s.attribute_cy=at(data,data_count,o,23);s.attribute_cc=at(data,data_count,o,24);
  s.az=at(data,data_count,o,25);s.bz=at(data,data_count,o,26);s.cz=at(data,data_count,o,27);s.polygon_depth_offset=at(data,data_count,o,28);s.plane_depth=at(data,data_count,o,29);
  s.aiw=at(data,data_count,o,30);s.biw=at(data,data_count,o,31);s.ciw=at(data,data_count,o,32);s.au=at(data,data_count,o,33);s.bu=at(data,data_count,o,34);s.cu=at(data,data_count,o,35);s.av=at(data,data_count,o,36);s.bv=at(data,data_count,o,37);s.cv=at(data,data_count,o,38);
  s.au2=at(data,data_count,o,39);s.bu2=at(data,data_count,o,40);s.cu2=at(data,data_count,o,41);s.av2=at(data,data_count,o,42);s.bv2=at(data,data_count,o,43);s.cv2=at(data,data_count,o,44);
  s.ar=at(data,data_count,o,45);s.br=at(data,data_count,o,46);s.cr=at(data,data_count,o,47);s.ag=at(data,data_count,o,48);s.bg=at(data,data_count,o,49);s.cg=at(data,data_count,o,50);s.ab=at(data,data_count,o,51);s.bb=at(data,data_count,o,52);s.cb=at(data,data_count,o,53);s.aa=at(data,data_count,o,54);s.ba=at(data,data_count,o,55);s.ca=at(data,data_count,o,56);
  s.edge_a_inclusive=at(data,data_count,o,57)!=0;s.edge_b_inclusive=at(data,data_count,o,58)!=0;s.edge_c_inclusive=at(data,data_count,o,59)!=0;
  s.white=at(data,data_count,o,60)!=0;s.depth_write=at(data,data_count,o,61)!=0;s.stencil_enabled=at(data,data_count,o,62)!=0;s.color_write=at(data,data_count,o,63)!=0;s.texture_consumed=at(data,data_count,o,64)!=0;s.primary_alpha=at(data,data_count,o,65)!=0;s.secondary_alpha=at(data,data_count,o,66)!=0;s.constant_depth=at(data,data_count,o,67)!=0;
  s.width=at(data,data_count,o,68);s.height=at(data,data_count,o,69);
  s.blend.source=uncode(BLEND_FACTORS,N(BLEND_FACTORS),at(data,data_count,o,75));s.blend.destination=uncode(BLEND_FACTORS,N(BLEND_FACTORS),at(data,data_count,o,76));
  s.alpha_bits=at(data,data_count,o,70)==0?0:8;
  s.blending=uncode(BLENDINGS,N(BLENDINGS),at(data,data_count,o,71));s.depth_test=uncode(DEPTH_TESTS,N(DEPTH_TESTS),at(data,data_count,o,72));s.alpha_test=uncode(ALPHA_TESTS,N(ALPHA_TESTS),at(data,data_count,o,73));
  s.secondary_environment=at(data,data_count,o,74)<0?NULL:uncode(ENVS,N(ENVS),at(data,data_count,o,74));
  s.texture=reg.texture(reg.ctx,at(data,data_count,o,83));s.secondary_texture=reg.texture(reg.ctx,at(data,data_count,o,84));
  s.derivative.u_anchor=at(data,data_count,o,87);s.derivative.v_anchor=at(data,data_count,o,88);s.derivative.u_dx=at(data,data_count,o,89);s.derivative.v_dx=at(data,data_count,o,90);s.derivative.q_dx=at(data,data_count,o,91);s.derivative.u_dy=at(data,data_count,o,92);s.derivative.v_dy=at(data,data_count,o,93);s.derivative.q_dy=at(data,data_count,o,94);
  s.secondary_derivative.u_anchor=at(data,data_count,o,95);s.secondary_derivative.v_anchor=at(data,data_count,o,96);s.secondary_derivative.u_dx=at(data,data_count,o,97);s.secondary_derivative.v_dx=at(data,data_count,o,98);s.secondary_derivative.q_dx=at(data,data_count,o,99);s.secondary_derivative.u_dy=at(data,data_count,o,100);s.secondary_derivative.v_dy=at(data,data_count,o,101);s.secondary_derivative.q_dy=at(data,data_count,o,102);
  s.stencil_function=at(data,data_count,o,77)==0?"always":"nonzero";
  s.stencil_compare_mask=at(data,data_count,o,78);s.stencil_write_mask=at(data,data_count,o,79);s.stencil_maximum=at(data,data_count,o,80);
  s.stencil_depth_fail=uncode(STENCIL_OPS,N(STENCIL_OPS),at(data,data_count,o,81));s.stencil_depth_pass=uncode(STENCIL_OPS,N(STENCIL_OPS),at(data,data_count,o,82));
  return s;
}

/* Registries. TS keys images and lighting by object identity. C setups carry these
 * structs by value, so identity becomes field equality: two equal structs read the
 * same memory and shade the same. Each culled light set has its own `lights`
 * storage and so its own id, like each object cullTriangleLights creates in TS. */
typedef struct lit_slot { uint64_t hash; uint32_t index_plus_one; } lit_slot;
typedef struct pimpl {
  qa_row_shared_framebuffer shared; qa_cpu_framebuffer fb; qa_cpu_sample sampled;
  qa_row_control control; qa_row_worker *workers; pthread_t *threads; size_t worker_count, started;
  qa_row_texture_registration *textures; size_t texture_count, texture_cap;
  qa_row_lighting_registration *lightings; size_t lighting_count, lighting_cap;
  lit_slot *lit_slots; size_t lit_slot_cap;
  qa_row_depth_registration *depths; size_t depth_count, depth_cap, pending_depth_start;
  double next_texture, next_lighting, next_depth;
  double *job; size_t job_cap;
} pimpl;
static void *grow(void *data,size_t *cap,size_t element){
  size_t next=*cap?*cap*2:16; void *grown=realloc(data,next*element);
  if(!grown)qa_range_error("row parallel allocation failed");
  *cap=next; return grown;
}
static bool same_texture(const qa_bound_texture *a,const qa_bound_texture *b){
  if(a->kind!=b->kind||strcmp(a->internal_format,b->internal_format))return false;
  if(a->kind==QA_BOUND_TEXTURE_CONSTANT)return a->sample.r==b->sample.r&&a->sample.g==b->sample.g&&a->sample.b==b->sample.b&&a->sample.a==b->sample.a;
  return a->levels==b->levels&&a->level_count==b->level_count&&!strcmp(a->wrap,b->wrap)&&a->minify_linear==b->minify_linear&&a->magnify_linear==b->magnify_linear
    &&!strcmp(a->mipmapping,b->mipmapping)&&a->magnification_limit==b->magnification_limit
    &&a->border_color.x==b->border_color.x&&a->border_color.y==b->border_color.y&&a->border_color.z==b->border_color.z&&a->border_color.w==b->border_color.w;
}
static double pool_tex_id(void *ctx,const qa_bound_texture *t){ pimpl *p=(pimpl*)ctx;
  if(t->kind==QA_BOUND_TEXTURE_INCOMPLETE)return -1;
  for(size_t i=0;i<p->texture_count;i++)if(same_texture(&p->textures[i].texture,t))return p->textures[i].id;
  if(p->texture_count==p->texture_cap)p->textures=(qa_row_texture_registration*)grow(p->textures,&p->texture_cap,sizeof *p->textures);
  qa_row_texture_registration *r=&p->textures[p->texture_count++]; r->id=p->next_texture++; r->texture=*t; return r->id;
}
static bool optional_str_eq(const char *a,const char *b){ return a==b||(a&&b&&!strcmp(a,b)); }
static bool same_lighting(const qa_batch_lighting *a,const qa_batch_lighting *b){
  return a->lights==b->lights&&a->light_count==b->light_count&&a->world_positions==b->world_positions&&a->world_position_count==b->world_position_count
    &&a->normals==b->normals&&a->normal_count==b->normal_count&&a->atlas==b->atlas&&a->model_shadow_lights==b->model_shadow_lights&&a->model_shadow_light_count==b->model_shadow_light_count
    &&a->has_shade_scale==b->has_shade_scale&&(!a->has_shade_scale||a->shade_scale==b->shade_scale)
    &&a->atlas_value.texel_size==b->atlas_value.texel_size&&a->atlas_value.near_plane==b->atlas_value.near_plane
    &&!strcmp(a->kind,b->kind)&&optional_str_eq(a->pass,b->pass);
}
static uint64_t lighting_hash(const qa_batch_lighting *l){
  const uintptr_t fields[5]={(uintptr_t)l->lights,(uintptr_t)l->world_positions,(uintptr_t)l->normals,(uintptr_t)l->model_shadow_lights,(uintptr_t)l->light_count};
  uint64_t h=1469598103934665603ULL;
  for(size_t i=0;i<5;i++){ h^=(uint64_t)fields[i]; h*=1099511628211ULL; h^=h>>29; }
  return h;
}
static void lit_slot_insert(lit_slot *slots,size_t cap,uint64_t hash,uint32_t index_plus_one){
  size_t i=(size_t)hash&(cap-1); while(slots[i].index_plus_one)i=(i+1)&(cap-1);
  slots[i].hash=hash; slots[i].index_plus_one=index_plus_one;
}
static double pool_lit_id(void *ctx,const qa_batch_lighting *l){ pimpl *p=(pimpl*)ctx;
  uint64_t hash=lighting_hash(l);
  if(p->lit_slot_cap){
    for(size_t i=(size_t)hash&(p->lit_slot_cap-1);p->lit_slots[i].index_plus_one;i=(i+1)&(p->lit_slot_cap-1)){
      if(p->lit_slots[i].hash!=hash)continue;
      const qa_row_lighting_registration *r=&p->lightings[p->lit_slots[i].index_plus_one-1];
      if(same_lighting(&r->parameters,l))return r->id;
    }
  }
  if((p->lighting_count+1)*2>p->lit_slot_cap){
    size_t cap=p->lit_slot_cap?p->lit_slot_cap*2:64; lit_slot *slots=(lit_slot*)calloc(cap,sizeof *slots);
    if(!slots)qa_range_error("row parallel allocation failed");
    for(size_t i=0;i<p->lit_slot_cap;i++)if(p->lit_slots[i].index_plus_one)lit_slot_insert(slots,cap,p->lit_slots[i].hash,p->lit_slots[i].index_plus_one);
    free(p->lit_slots); p->lit_slots=slots; p->lit_slot_cap=cap;
  }
  if(p->lighting_count==p->lighting_cap)p->lightings=(qa_row_lighting_registration*)grow(p->lightings,&p->lighting_cap,sizeof *p->lightings);
  qa_row_lighting_registration *r=&p->lightings[p->lighting_count++]; r->id=p->next_lighting++; r->parameters=*l;
  lit_slot_insert(p->lit_slots,p->lit_slot_cap,hash,(uint32_t)p->lighting_count);
  return r->id;
}
static double pool_dep_id(void *ctx,const qa_depth_image_level *d){ pimpl *p=(pimpl*)ctx;
  if(d==NULL)return -1;
  for(size_t i=0;i<p->depth_count;i++){ const qa_depth_image_level *known=&p->depths[i].depth;
    if(known->pixels==d->pixels&&known->width==d->width&&known->height==d->height&&known->pixel_count==d->pixel_count)return p->depths[i].id; }
  if(p->depth_count==p->depth_cap)p->depths=(qa_row_depth_registration*)grow(p->depths,&p->depth_cap,sizeof *p->depths);
  qa_row_depth_registration *r=&p->depths[p->depth_count++]; r->id=p->next_depth++; r->depth=*d; return r->id;
}
/* os.availableParallelism(): the CPUs this process may run on. */
static long available_parallelism(void){
  cpu_set_t set; CPU_ZERO(&set);
  if(sched_getaffinity(0,sizeof set,&set)==0){ int n=CPU_COUNT(&set); if(n>0)return n; }
  return sysconf(_SC_NPROCESSORS_ONLN);
}
static double configured_workers(void){
  const char *raw=getenv("QTS_CPU_WORKERS");
  if(raw){ /* Number.parseInt(raw, 10): a NaN parse means no workers. */
    double v=qa_js_parse_int(raw,10); /* strtol skipped only ASCII space and clamped at LONG_MAX */
    return isfinite(v)?qa_js_max(0,qa_js_min(16,v)):0;
  }
  long available=available_parallelism();
  if(available<1)return 0;
  long count=available-1;
  return count>4?4:(double)count;
}
static struct timespec deadline_after(long seconds){ struct timespec t; clock_gettime(CLOCK_REALTIME,&t); t.tv_sec+=seconds; return t; }
static void pool_free(pimpl *p){
  if(p->workers)for(size_t i=0;i<p->worker_count;i++)qa_row_worker_destroy(&p->workers[i]);
  free(p->workers);free(p->threads);free(p->textures);free(p->lightings);free(p->lit_slots);free(p->depths);free(p->job);
  qa_row_control_destroy(&p->control); free(p);
}
static void pool_stop_threads(pimpl *p){
  pthread_mutex_lock(&p->control.lock); p->control.shutdown=true; pthread_cond_broadcast(&p->control.job_ready); pthread_mutex_unlock(&p->control.lock);
  for(size_t i=0;i<p->started;i++)pthread_join(p->threads[i],NULL);
  p->started=0;
}
bool qa_row_parallel_pool_try_create(qa_row_parallel_pool *pool,qa_row_shared_framebuffer shared){
  pool->impl=NULL; pool->jobs_run=0; pool->worker_count=0; pool->failure=NULL;
  double count=configured_workers(); if(count<1)return false;
  pimpl *p=(pimpl*)calloc(1,sizeof *p); if(!p){ pool->failure="Out of memory"; return false; }
  if(!qa_row_control_init(&p->control)){ free(p); pool->failure="Row worker synchronization could not be created"; return false; }
  p->shared=shared; p->worker_count=(size_t)count;
  p->fb.width=shared.width;p->fb.height=shared.height;p->fb.pixels=shared.color;p->fb.color_words=(int32_t*)shared.color;
  p->fb.depth=shared.depth;p->fb.stencil=shared.stencil;p->fb.origin_x=0;p->fb.origin_y=0;p->fb.stride=shared.width;
  p->fb.storage_count=(size_t)(shared.width*shared.height);
  p->sampled.r=p->sampled.g=p->sampled.b=p->sampled.a=1;
  p->workers=(qa_row_worker*)calloc(p->worker_count,sizeof *p->workers); p->threads=(pthread_t*)calloc(p->worker_count,sizeof *p->threads);
  bool ok=p->workers&&p->threads;
  for(size_t i=0;ok&&i<p->worker_count;i++)ok=qa_row_worker_init(&p->workers[i],shared,&p->control);
  for(size_t i=0;ok&&i<p->worker_count;i++){ ok=pthread_create(&p->threads[i],NULL,qa_row_worker_main,&p->workers[i])==0; if(ok)p->started++; }
  /* The TS constructor waits 30s for readiness, then closes and yields null. */
  if(ok){
    struct timespec deadline=deadline_after(30);
    pthread_mutex_lock(&p->control.lock);
    while(ok&&p->control.ready<p->worker_count)ok=pthread_cond_timedwait(&p->control.progress,&p->control.lock,&deadline)!=ETIMEDOUT||p->control.ready>=p->worker_count;
    pthread_mutex_unlock(&p->control.lock);
  }
  if(!ok){ pool_stop_threads(p); pool_free(p); pool->failure="Row workers did not start"; return false; }
  pool->impl=p; pool->worker_count=count; return true;
}
double qa_row_parallel_pool_worker_count(const qa_row_parallel_pool *pool){ return pool?pool->worker_count:0; }
qa_row_registries qa_row_parallel_pool_registries(qa_row_parallel_pool *pool){
  if(!pool->impl)qa_range_error("Row-parallel pool is closed");
  qa_row_registries r; r.ctx=pool->impl; r.texture_id=pool_tex_id; r.lighting_id=pool_lit_id; r.depth_id=pool_dep_id; return r;
}
typedef struct coordinator_job { pimpl *pool; const qa_triangle_setup *setups; size_t count; double band_rows; } coordinator_job;
static void coordinator_bands(void *ctx){
  coordinator_job *job=(coordinator_job*)ctx; pimpl *p=job->pool; double height=p->shared.height, band_count=ceil(height/job->band_rows);
  for(;;){
    double band=(double)atomic_fetch_add(&p->control.next,1);
    if(band>=band_count)break;
    double first_y=band*job->band_rows, last_y=qa_js_min(height-1,first_y+job->band_rows-1);
    for(size_t i=0;i<job->count;i++)qa_run_triangle_rows(&job->setups[i],&p->fb,&p->sampled,first_y,last_y);
  }
}
void qa_row_parallel_pool_run(qa_row_parallel_pool *pool,const qa_triangle_setup *setups,size_t count){
  pimpl *p=(pimpl*)pool->impl; if(!p)qa_range_error("Row-parallel pool is closed");
  size_t floats=count*QA_ROW_SETUP_STRIDE; double *fresh=NULL;
  if(p->job==NULL||p->job_cap<floats){
    size_t cap=floats>8192?floats:8192; double *grown=(double*)realloc(p->job,cap*sizeof *grown);
    if(!grown)qa_range_error("row parallel allocation failed");
    p->job=grown; p->job_cap=cap; fresh=grown;
  }
  /* Texture and lighting ids last one job, because workers drop their copies when
   * the next job starts (RENDER-002). Depth ids persist, and only new depths are sent. */
  p->texture_count=0; p->lighting_count=0; p->pending_depth_start=p->depth_count;
  if(p->lit_slot_cap)memset(p->lit_slots,0,p->lit_slot_cap*sizeof *p->lit_slots);
  qa_row_registries registries=qa_row_parallel_pool_registries(pool);
  for(size_t i=0;i<count;i++)qa_encode_row_setup(p->job,p->job_cap,(double)(i*QA_ROW_SETUP_STRIDE),&setups[i],registries);
  double workers=(double)p->worker_count, height=p->shared.height;
  double band_rows=qa_js_max(1,ceil(height/(workers*4)));
  qa_row_job_message message; memset(&message,0,sizeof message);
  message.kind="job"; message.count=(double)count; message.band_rows=band_rows; message.job_buffer=fresh; message.job_count=fresh?p->job_cap:0;
  message.textures=p->textures; message.texture_count=p->texture_count;
  message.lightings=p->lightings; message.lighting_count=p->lighting_count;
  message.depths=p->depths+p->pending_depth_start; message.depth_count=p->depth_count-p->pending_depth_start;
  qa_row_control *control=&p->control;
  pthread_mutex_lock(&control->lock);
  atomic_store(&control->next,0); control->done=0; control->failed=false; control->error[0]='\0'; control->message=message; control->generation++;
  pthread_cond_broadcast(&control->job_ready);
  pthread_mutex_unlock(&control->lock);
  /* The coordinator claims bands alongside the workers instead of only blocking on
   * the barrier: bands are row-disjoint and setups run in order within a band, so
   * the output is identical. */
  coordinator_job job; job.pool=p; job.setups=setups; job.count=count; job.band_rows=band_rows;
  char failure[1024]; bool completed=qa_range_error_catch(coordinator_bands,&job,failure,sizeof failure);
  struct timespec deadline=deadline_after(120);
  pthread_mutex_lock(&control->lock);
  while(control->done<p->worker_count){
    if(pthread_cond_timedwait(&control->progress,&control->lock,&deadline)==ETIMEDOUT&&control->done<p->worker_count)qa_range_error("Row-parallel rasterization timed out");
  }
  bool worker_failed=control->failed; char worker_error[sizeof control->error]; memcpy(worker_error,control->error,sizeof worker_error);
  pthread_mutex_unlock(&control->lock);
  pool->jobs_run++;
  if(worker_failed)qa_range_error("Row-parallel worker failed: %s",worker_error);
  if(!completed)qa_range_error("%s",failure);
}
void qa_row_parallel_pool_close(qa_row_parallel_pool *pool){
  if(!pool||!pool->impl)return;
  pimpl *p=(pimpl*)pool->impl; pool_stop_threads(p); pool_free(p); pool->impl=NULL;
}
