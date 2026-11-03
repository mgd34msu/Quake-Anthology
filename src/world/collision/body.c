/* Port of src/world/collision/body.ts. GPL-2.0-or-later. */
#include "qa/world/collision/body.h"
#include "qa/core/numeric.h"
#include "qa/foundation/js.h"
#include "qa/world/collision/contents.h"
#include "qa/world/collision/q1/hull.h"
#include "qa/world/collision/q3/model.h"
#include "qa/world/collision/q3/world.h"
#include <stdint.h>
#include <string.h>
static qa_vec3 Z3(void){qa_vec3 z={0,0,0};return z;}
/* Plane identities (qa_trace_result.plane_identity) of the shared TS
 * normal objects: body.ts AXIS_NORMALS[k] (the hull() box planes share
 * one normal per axis), body.ts `zero`, and hull.ts `zero` (the initial
 * plane of traceQ1Hull). */
static const char axis_normal_identity[3] = {0, 0, 0};
static const char body_zero_identity = 0;
static const char hull_zero_identity = 0;
static uintptr_t box_hull_plane_identity(int plane_index){
  if(plane_index>=0)return (uintptr_t)&axis_normal_identity[plane_index/2];
  return plane_index==-2?(uintptr_t)&hull_zero_identity:0;
}
static void make_hull(qa_bounds bounds,qa_bsp_plane planes[6],qa_q1_clip_node nodes[6],qa_q1_hull*out){
  planes[0]=(qa_bsp_plane){{{1,0,0},bounds.max.x},0,0};
  planes[1]=(qa_bsp_plane){{{1,0,0},bounds.min.x},0,0};
  planes[2]=(qa_bsp_plane){{{0,1,0},bounds.max.y},1,0};
  planes[3]=(qa_bsp_plane){{{0,1,0},bounds.min.y},1,0};
  planes[4]=(qa_bsp_plane){{{0,0,1},bounds.max.z},2,0};
  planes[5]=(qa_bsp_plane){{{0,0,1},bounds.min.z},2,0};
  for(int i=0;i<6;i++){
    qa_q1_clip_child empty={false,-1};
    qa_q1_clip_child next=i==5?(qa_q1_clip_child){false,-2}:(qa_q1_clip_child){true,i+1};
    nodes[i].plane=i;
    if(i%2==0){nodes[i].children[0]=empty;nodes[i].children[1]=next;}
    else{nodes[i].children[0]=next;nodes[i].children[1]=empty;}
  }
  out->clipnodes=nodes;out->clipnode_count=6;out->planes=planes;out->plane_count=6;
  out->first_clipnode=0;out->last_clipnode=5;out->clip_bounds=(qa_bounds){Z3(),Z3()};
}
static qa_trace_result trace_q2_box(qa_trace_query query,qa_spatial_actor actor){
  const qa_numeric_operations*n=qa_create_numeric_operations(&query.numeric);
  #define BNADD(a,b) n->add(n->ctx,(a),(b))
  #define BNSUB(a,b) n->subtract(n->ctx,(a),(b))
  #define BNMUL(a,b) n->multiply(n->ctx,(a),(b))
  #define BNDIV(a,b) n->divide(n->ctx,(a),(b))
  qa_vec3 origin=actor.body.state.origin; qa_bounds bounds=actor.body.state.bounds;
  int is_point=query.shape.kind&&strcmp(query.shape.kind,"point")==0;
  qa_bounds moving=is_point?(qa_bounds){Z3(),Z3()}:query.shape.bounds;
  qa_bounds expanded={{BNSUB(bounds.min.x,moving.max.x),BNSUB(bounds.min.y,moving.max.y),BNSUB(bounds.min.z,moving.max.z)},
    {BNSUB(bounds.max.x,moving.min.x),BNSUB(bounds.max.y,moving.min.y),BNSUB(bounds.max.z,moving.min.z)}};
  qa_bsp_plane planes[6]; qa_q1_clip_node nodes[6]; qa_q1_hull hull; make_hull(expanded,planes,nodes,&hull);
  double enter=-1,enter2=-1,leave=1; int start_out=0,get_out=0,misses=0;
  qa_bsp_plane plane={{{0,0,0},0},0,0};
  qa_bsp_plane secondary={{{0,0,0},0},0,0}; int has_secondary=0;
  int stationary=query.start.x==query.end.x&&query.start.y==query.end.y&&query.start.z==query.end.z;
  double qs[3]={query.start.x,query.start.y,query.start.z},qe[3]={query.end.x,query.end.y,query.end.z},org[3]={origin.x,origin.y,origin.z};
  double bmn[3]={bounds.min.x,bounds.min.y,bounds.min.z},bmx[3]={bounds.max.x,bounds.max.y,bounds.max.z};
  for(int idx=0;idx<6;idx++){
    int axis=planes[idx].type==0?0:planes[idx].type==1?1:2;
    double sign=idx%2==0?1:-1;
    double d1=BNMUL(BNSUB(BNSUB(qs[axis],org[axis]),planes[idx].plane.distance),sign);
    double d2=BNMUL(BNSUB(BNSUB(qe[axis],org[axis]),planes[idx].plane.distance),sign);
    double dist=sign>0?bmx[axis]:-bmn[axis];
    if(d1>0)start_out=1;
    if(d2>0)get_out=1;
    if(d1>0&&(d2>=0.03125||d2>=d1)){misses=1;break;}
    if(d1<=0&&d2<=0)continue;
    if(d1>d2){
      /* body.ts keeps the raw fraction under stored leaf contents and clamps once a side is chosen. */
      int stored=query.policy.kind&&strcmp(query.policy.kind,"q2")==0&&query.policy.leaf_contents&&strcmp(query.policy.leaf_contents,"stored")==0;
      double raw=BNDIV(BNSUB(d1,0.03125),BNSUB(d1,d2));
      double f=stored?raw:qa_js_max(0,raw);
      qa_vec3 normal={axis==0?sign:0,axis==1?sign:0,axis==2?sign:0};
      qa_bsp_plane sp={{normal,dist},sign<0?planes[idx].type+3:planes[idx].type,sign<0?(double)(1<<(int)planes[idx].type):0};
      if(f>enter){enter=f;plane=sp;}
      else if(f>enter2){enter2=f;secondary=sp;has_secondary=1;}
    }else{
      leave=qa_js_min(leave,qa_js_min(1,BNDIV(BNADD(d1,0.03125),BNSUB(d1,d2))));
    }
  }
  (void)hull;
  int start_solid=!misses&&!start_out;
  int all_solid=start_solid&&!get_out;
  int merged=query.policy.kind&&strcmp(query.policy.kind,"q2")==0&&query.policy.leaf_contents&&strcmp(query.policy.leaf_contents,"merged")==0;
  double fraction=misses?1:(all_solid&&(stationary||merged))?0:start_solid?1:(enter<leave&&enter>-1)?qa_js_max(0,enter):1;
  double contents=fraction<1?qa_actor_contents(actor.collision,"q2"):0;
  qa_bsp_plane selected=(misses||start_solid||fraction==1)?(qa_bsp_plane){{{0,0,0},0},0,0}:plane;
  qa_trace_result r; memset(&r,0,sizeof r);
  r.kind="q2";r.fraction=fraction;
  r.end=(qa_vec3){BNADD(query.start.x,BNMUL(fraction,BNSUB(query.end.x,query.start.x))),BNADD(query.start.y,BNMUL(fraction,BNSUB(query.end.y,query.start.y))),BNADD(query.start.z,BNMUL(fraction,BNSUB(query.end.z,query.start.z)))};
  r.start_solid=start_solid!=0;r.all_solid=all_solid!=0;
  r.contact=(qa_trace_contact){fraction<1&&!all_solid,selected.plane};
  r.source_plane=selected;
  /* The selected plane is fresh; its zero form carries body.ts `zero`. */
  r.plane_identity=(misses||start_solid||fraction==1)?(uintptr_t)&body_zero_identity:0;
  r.hit.kind=(fraction<1||start_solid)?QA_TRACE_HIT_ACTOR:QA_TRACE_HIT_NONE; r.hit.actor=actor.body.actor;
  r.contents=contents;r.surface=&qa_q2_null_surface;
  r.has_secondary=has_secondary&&fraction!=1&&!start_solid;
  r.secondary_plane=secondary;r.secondary_surface=r.has_secondary?&qa_q2_null_surface:NULL;
  #undef BNADD
  #undef BNSUB
  #undef BNMUL
  #undef BNDIV
  return r;
}
qa_trace_result qa_trace_actor_body(qa_trace_query query,qa_spatial_actor actor){
  qa_linked_body body=actor.body; qa_actor_collision collision=actor.collision; qa_bounds bounds=body.state.bounds;
  int pk_q1=query.policy.kind&&strcmp(query.policy.kind,"q1")==0;
  int pk_q2=query.policy.kind&&strcmp(query.policy.kind,"q2")==0;
  int pk_q3=query.policy.kind&&strcmp(query.policy.kind,"q3")==0;
  int shape_capsule=query.shape.kind&&strcmp(query.shape.kind,"capsule")==0;
  int col_box=collision.shape_kind&&strcmp(collision.shape_kind,"box")==0;
  int col_capsule=collision.shape_kind&&strcmp(collision.shape_kind,"capsule")==0;
  if(pk_q1&&!shape_capsule&&col_box){
    int is_point=query.shape.kind&&strcmp(query.shape.kind,"point")==0;
    qa_bounds moving=is_point?(qa_bounds){Z3(),Z3()}:query.shape.bounds;
    qa_bounds expanded={{bounds.min.x-moving.max.x,bounds.min.y-moving.max.y,bounds.min.z-moving.max.z},
      {bounds.max.x-moving.min.x,bounds.max.y-moving.min.y,bounds.max.z-moving.min.z}};
    qa_vec3 origin=body.state.origin;
    qa_bsp_plane planes[6]; qa_q1_clip_node nodes[6]; qa_q1_hull hull; make_hull(expanded,planes,nodes,&hull);
    qa_vec3 ls={query.start.x-origin.x,query.start.y-origin.y,query.start.z-origin.z};
    qa_vec3 le={query.end.x-origin.x,query.end.y-origin.y,query.end.z-origin.z};
    qa_q1_hull_trace result=qa_trace_q1_hull(&hull,ls,le,query.numeric,NULL,NULL);
    qa_vec3 end=result.fraction==1?query.end:(qa_vec3){result.end.x+origin.x,result.end.y+origin.y,result.end.z+origin.z};
    qa_trace_result r; memset(&r,0,sizeof r);
    r.kind="q1";r.fraction=result.fraction;r.end=end;r.start_solid=result.start_solid;r.all_solid=result.all_solid;
    r.in_open=result.in_open;r.in_water=result.in_water;r.source_plane_q1=result.plane;r.contents=result.contents;
    r.contact=(qa_trace_contact){result.fraction<1,result.plane};
    r.plane_identity=box_hull_plane_identity(result.plane_index);
    r.hit.kind=(result.fraction<1||result.start_solid)?QA_TRACE_HIT_ACTOR:QA_TRACE_HIT_NONE; r.hit.actor=body.actor;
    return r;
  }
  if(pk_q2&&!shape_capsule&&col_box)return trace_q2_box(query,actor);
  qa_temporary_collision_model*model=col_capsule?qa_create_capsule_model(bounds,NULL,NULL):qa_create_box_model(bounds,NULL,NULL);
  qa_q3_trace_query q; memset(&q,0,sizeof q);
  q.start=query.start;q.end=query.end;
  int fam_q3=collision.family&&strcmp(collision.family,"q3")==0;
  q.mask=(pk_q3&&fam_q3)?query.policy.contents_mask:0x02000000;
  int is_point=query.shape.kind&&strcmp(query.shape.kind,"point")==0;
  if(is_point)q.shape=(qa_q3_trace_shape){"point",Z3(),Z3()};
  else q.shape=(qa_q3_trace_shape){query.shape.kind,query.shape.bounds.min,query.shape.bounds.max};
  qa_source_trace_result result=qa_temporary_model_transformed_trace_source(model,q,body.state.origin,Z3());
  qa_temporary_collision_model_destroy(model);
  qa_q3_trace_result view=qa_source_trace_view(result);
  qa_trace_result inner; memset(&inner,0,sizeof inner);
  inner.kind="q3";inner.fraction=result.fraction;inner.end=result.end;inner.start_solid=result.start_solid;inner.all_solid=result.all_solid;
  inner.source_plane=(qa_bsp_plane){result.plane.plane,result.plane.type,result.plane.signbits};
  inner.contact=(qa_trace_contact){view.has_plane,view.plane};
  inner.hit.kind=(result.fraction<1||result.start_solid)?QA_TRACE_HIT_ACTOR:QA_TRACE_HIT_NONE; inner.hit.actor=body.actor;
  inner.contents=(pk_q3&&fam_q3)?result.contents:qa_actor_contents(collision,"q3");
  inner.has_surface_flags=true;inner.surface_flags=0;
  (void)pk_q3;
  return qa_adapt_trace_result(inner,query.policy);
}
