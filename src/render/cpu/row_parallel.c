/* Port of src/render/cpu/row-parallel.ts (direct/pumped). GPL-2.0-or-later. */
#include "qa/render/cpu/row_parallel.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static const char *const BLEND_FACTORS[]={"zero","one","src-color","one-minus-src-color","dst-color","one-minus-dst-color","src-alpha","one-minus-src-alpha","dst-alpha","one-minus-dst-alpha","src-alpha-saturate"};
static const char *const BLENDINGS[]={"opaque","alpha","add","multiply","dst-color-inverse-dst-alpha","general"};
static const char *const DEPTH_TESTS[]={"less-equal","equal","always"};
static const char *const ALPHA_TESTS[]={"none","gt0","lt128","ge128"};
static const char *const STENCIL_OPS[]={"keep","increment","decrement"};
static const char *const ENVS[]={"modulate","add","replace"};
static const char *const FOG_EFFECTS[]={"color","none","rgb","alpha","rgba","overlay"};
static double code_of(const char *const *vals,size_t n,const char *v){ for(size_t i=0;i<n;i++)if(!strcmp(vals[i],v))return (double)i; qa_range_error("Unknown row-parallel enum value %s",v); }
static const char *uncode(const char *const *vals,size_t n,double v){ long i=(long)v; if(i<0||(size_t)i>=n)qa_range_error("Invalid row-parallel enum code"); return vals[i]; }
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
typedef struct pimpl { qa_row_shared_framebuffer shared; qa_cpu_framebuffer fb; qa_cpu_sample sampled; bool closed; double workers;
  const void **tex_keys; qa_bound_texture *tex_vals; size_t tex_count, tex_cap;
  const void **lit_keys; qa_batch_lighting *lit_vals; size_t lit_count, lit_cap;
  const void **dep_keys; qa_depth_image_level *dep_vals; size_t dep_count, dep_cap;
  double *job; size_t job_cap; } pimpl;
static double pool_tex_id(void *ctx,const qa_bound_texture *t){ pimpl *p=(pimpl*)ctx; if(t->kind==QA_BOUND_TEXTURE_INCOMPLETE)return -1;
  for(size_t i=0;i<p->tex_count;i++)if(p->tex_keys[i]==(const void*)t->levels)return (double)i;
  if(p->tex_count==p->tex_cap){ size_t nc=p->tex_cap?p->tex_cap*2:16; p->tex_keys=(const void**)realloc(p->tex_keys,nc*sizeof *p->tex_keys); p->tex_vals=(qa_bound_texture*)realloc(p->tex_vals,nc*sizeof *p->tex_vals); if(!p->tex_keys||!p->tex_vals)qa_range_error("row parallel allocation failed"); p->tex_cap=nc; }
  size_t id=p->tex_count++; p->tex_keys[id]=(const void*)t->levels; p->tex_vals[id]=*t; return (double)id; }
static double pool_lit_id(void *ctx,const qa_batch_lighting *l){ pimpl *p=(pimpl*)ctx;
  for(size_t i=0;i<p->lit_count;i++)if(p->lit_keys[i]==(const void*)l->world_positions&&p->lit_vals[i].kind==l->kind)return (double)i;
  if(p->lit_count==p->lit_cap){ size_t nc=p->lit_cap?p->lit_cap*2:16; p->lit_keys=(const void**)realloc(p->lit_keys,nc*sizeof *p->lit_keys); p->lit_vals=(qa_batch_lighting*)realloc(p->lit_vals,nc*sizeof *p->lit_vals); if(!p->lit_keys||!p->lit_vals)qa_range_error("row parallel allocation failed"); p->lit_cap=nc; }
  size_t id=p->lit_count++; p->lit_keys[id]=(const void*)l->world_positions; p->lit_vals[id]=*l; return (double)id; }
static double pool_dep_id(void *ctx,const qa_depth_image_level *d){ pimpl *p=(pimpl*)ctx; if(d==NULL)return -1;
  for(size_t i=0;i<p->dep_count;i++)if(p->dep_keys[i]==(const void*)d->pixels)return (double)i;
  if(p->dep_count==p->dep_cap){ size_t nc=p->dep_cap?p->dep_cap*2:16; p->dep_keys=(const void**)realloc(p->dep_keys,nc*sizeof *p->dep_keys); p->dep_vals=(qa_depth_image_level*)realloc(p->dep_vals,nc*sizeof *p->dep_vals); if(!p->dep_keys||!p->dep_vals)qa_range_error("row parallel allocation failed"); p->dep_cap=nc; }
  size_t id=p->dep_count++; p->dep_keys[id]=(const void*)d->pixels; p->dep_vals[id]=*d; return (double)id; }
static qa_bound_texture pool_tex(void *ctx,double id){ pimpl *p=(pimpl*)ctx; qa_bound_texture o; memset(&o,0,sizeof o); o.kind=QA_BOUND_TEXTURE_INCOMPLETE; if(id==-1)return o; long i=(long)id; if(i<0||(size_t)i>=p->tex_count)qa_range_error("Row worker missing texture"); return p->tex_vals[i]; }
static qa_batch_lighting pool_lit(void *ctx,double id){ pimpl *p=(pimpl*)ctx; long i=(long)id; if(i<0||(size_t)i>=p->lit_count)qa_range_error("Row worker missing lighting"); return p->lit_vals[i]; }
static const qa_depth_image_level *pool_dep(void *ctx,double id){ pimpl *p=(pimpl*)ctx; if(id==-1)return NULL; long i=(long)id; if(i<0||(size_t)i>=p->dep_count)qa_range_error("Row worker missing depth"); return &p->dep_vals[i]; }
static double configured_workers(void){ const char *raw=getenv("QTS_CPU_WORKERS"); if(raw){ char *end=NULL; long v=strtol(raw,&end,10); if(end==raw)return 0; if(v<0)v=0; if(v>16)v=16; return (double)v; } return 1; }
bool qa_row_parallel_pool_try_create(qa_row_parallel_pool *pool,qa_row_shared_framebuffer shared){
  pimpl *p=(pimpl*)calloc(1,sizeof *p); if(!p)return false; pool->impl=p; pool->jobs_run=0;
  p->shared=shared; p->workers=configured_workers();
  p->fb.width=shared.width;p->fb.height=shared.height;p->fb.pixels=shared.color;p->fb.color_words=(int32_t*)shared.color;
  p->fb.depth=shared.depth;p->fb.stencil=shared.stencil;p->fb.origin_x=0;p->fb.origin_y=0;p->fb.stride=shared.width;
  p->fb.storage_count=(size_t)(shared.width*shared.height);
  p->sampled.r=p->sampled.g=p->sampled.b=p->sampled.a=1; return true;
}
double qa_row_parallel_pool_worker_count(const qa_row_parallel_pool *pool){ return pool&&pool->impl?((pimpl*)pool->impl)->workers:0; }
qa_row_registries qa_row_parallel_pool_registries(qa_row_parallel_pool *pool){ qa_row_registries r; r.ctx=pool->impl; r.texture_id=pool_tex_id; r.lighting_id=pool_lit_id; r.depth_id=pool_dep_id; return r; }
void qa_row_parallel_pool_run(qa_row_parallel_pool *pool,const qa_triangle_setup *setups,size_t count){
  pimpl *p=(pimpl*)pool->impl; if(p->closed)qa_range_error("Row-parallel pool is closed");
  size_t need=count*QA_ROW_SETUP_STRIDE;
  if(need>p->job_cap){ size_t nc=need>8192?need:8192; double *nj=(double*)realloc(p->job,nc*sizeof *nj); if(!nj)qa_range_error("row parallel allocation failed"); p->job=nj;p->job_cap=nc; }
  qa_row_registries reg=qa_row_parallel_pool_registries(pool);
  for(size_t i=0;i<count;i++) qa_encode_row_setup(p->job,p->job_cap,(double)(i*QA_ROW_SETUP_STRIDE),&setups[i],reg);
  double workers=p->workers<1?1:p->workers;
  double band_rows=fmax(1,ceil(p->shared.height/(workers*4)));
  double band_count=ceil(p->shared.height/band_rows);
  /* Direct/pumped: decode each setup once, then claim bands in order. */
  qa_triangle_setup *decoded=(qa_triangle_setup*)calloc(count?count:1,sizeof *decoded);
  qa_row_decode_storage *storage=(qa_row_decode_storage*)calloc(count?count:1,sizeof *storage);
  if(!decoded||!storage)qa_range_error("row parallel allocation failed");
  qa_row_decode_registries dreg; dreg.ctx=p; dreg.texture=pool_tex; dreg.lighting=pool_lit; dreg.depth=pool_dep;
  for(size_t i=0;i<count;i++) decoded[i]=qa_decode_row_setup(p->job,p->job_cap,(double)(i*QA_ROW_SETUP_STRIDE),dreg,&storage[i]);
  for(double band=0;band<band_count;band++){
    double fy=band*band_rows, ly=fmin(p->shared.height-1,fy+band_rows-1);
    for(size_t i=0;i<count;i++) qa_run_worker_triangle_rows(&decoded[i],&p->fb,&p->sampled,fy,ly);
  }
  free(decoded); free(storage); pool->jobs_run++;
}
void qa_row_parallel_pool_close(qa_row_parallel_pool *pool){ if(!pool||!pool->impl)return; pimpl *p=(pimpl*)pool->impl; if(p->closed)return; p->closed=true;
  free(p->tex_keys);free(p->tex_vals);free(p->lit_keys);free(p->lit_vals);free(p->dep_keys);free(p->dep_vals);free(p->job); free(p); pool->impl=NULL; }
