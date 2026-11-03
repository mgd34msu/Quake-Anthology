/* Port of src/world/collision/q1/hull.ts. GPL-2.0-or-later. */
#include "qa/world/collision/q1/hull.h"
#include <math.h>
#include "qa/network/common/js_number.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include <stdlib.h>
const qa_bounds qa_q1_hull_bounds[3]={
  {{0,0,0},{0,0,0}},{{-16,-16,-24},{16,16,32}},{{-32,-32,-24},{32,32,64}}};
#define NADD(a,b) n->add(n->ctx,(a),(b))
#define NSUB(a,b) n->subtract(n->ctx,(a),(b))
#define NMUL(a,b) n->multiply(n->ctx,(a),(b))
#define NDIV(a,b) n->divide(n->ctx,(a),(b))
#define NSTORE(a) n->store(n->ctx,(a))
bool qa_create_q1_hulls(const qa_q1_world_geometry*map,double model_index,qa_q1_hulls*out){
  if(!(model_index>=0&&model_index<(double)map->model_count&&model_index==floor(model_index)))qa_range_error("Unknown Quake model %s",QA_JS_NUMBER(model_index));
  const qa_q1_world_model*model=&map->models[(size_t)model_index];
  qa_q1_clip_node*drawing=malloc((map->node_count?map->node_count:1)*sizeof*drawing);
  if(!drawing)return false;
  for(size_t i=0;i<map->node_count;i++){
    drawing[i].plane=map->nodes[i].plane;
    for(int k=0;k<2;k++){
      qa_bsp_child ch=map->nodes[i].children[k];
      if(ch.is_node){drawing[i].children[k].is_clipnode=true;drawing[i].children[k].value=ch.index;}
      else{
        if(!(ch.index>=0&&ch.index<(double)map->leaf_count&&ch.index==floor(ch.index)))qa_range_error("Unknown Quake hull-zero leaf");
        drawing[i].children[k].is_clipnode=false;drawing[i].children[k].value=map->leaves[(size_t)ch.index].contents;
      }
    }
  }
  out->drawing=drawing; out->drawing_count=map->node_count;
  for(int idx=0;idx<3;idx++){
    if((size_t)idx>=model->headnodes.count)qa_range_error("Missing Quake hull %d",idx);
    double first=model->headnodes.values[idx];
    if(idx==0&&first<0){
      double li=-1-first;
      if(!(li>=0&&li<(double)map->leaf_count&&li==floor(li)))qa_range_error("Unknown Quake hull root leaf");
      first=map->leaves[(size_t)li].contents;
    }
    const qa_q1_clip_node*cn=idx==0?drawing:map->clipnodes;
    size_t cc=idx==0?map->node_count:map->clipnode_count;
    out->hulls[idx].clipnodes=cn; out->hulls[idx].clipnode_count=cc;
    out->hulls[idx].planes=map->planes; out->hulls[idx].plane_count=map->plane_count;
    out->hulls[idx].first_clipnode=first; out->hulls[idx].last_clipnode=(double)cc-1;
    out->hulls[idx].clip_bounds=qa_q1_hull_bounds[idx];
  }
  return true;
}
void qa_q1_hulls_destroy(qa_q1_hulls*h){if(!h)return;free(h->drawing);h->drawing=NULL;}
static double plane_distance(const qa_q1_hull*hull,double pi,qa_vec3 p,const qa_numeric_operations*n){
  if(!(pi>=0&&pi<(double)hull->plane_count&&pi==floor(pi)))qa_range_error("Invalid Quake hull plane");
  const qa_bsp_plane*pl=&hull->planes[(size_t)pi];
  if(pl->type==0)return NSUB(p.x,pl->plane.distance);
  if(pl->type==1)return NSUB(p.y,pl->plane.distance);
  if(pl->type==2)return NSUB(p.z,pl->plane.distance);
  return NSUB(NADD(NADD(NMUL(p.x,pl->plane.normal.x),NMUL(p.y,pl->plane.normal.y)),NMUL(p.z,pl->plane.normal.z)),pl->plane.distance);
}
static double child_number(qa_q1_clip_child c){return c.value;}
static const qa_q1_clip_node*node_at(const qa_q1_hull*hull,double node){
  if(node<hull->first_clipnode||node>hull->last_clipnode)qa_range_error("Invalid Quake hull node %s",QA_JS_NUMBER(node));
  if(!(node>=0&&node<(double)hull->clipnode_count&&node==floor(node)))qa_range_error("Missing Quake hull node %s",QA_JS_NUMBER(node));
  return &hull->clipnodes[(size_t)node];
}
static double contents_at(const qa_q1_hull*hull,qa_vec3 p,double node,const qa_numeric_operations*n){
  size_t visits=0;
  while(node>=0){
    if(++visits>hull->clipnode_count)qa_range_error("Cycle in Quake hull");
    const qa_q1_clip_node*clip=node_at(hull,node);
    node=child_number(clip->children[plane_distance(hull,clip->plane,p,n)<0?1:0]);
  }
  return node;
}
double qa_q1_hull_point_contents_at(const qa_q1_hull*hull,qa_vec3 p,qa_numeric_profile numeric,double start_node){
  return contents_at(hull,p,start_node,qa_create_numeric_operations(&numeric));
}
double qa_q1_hull_point_contents(const qa_q1_hull*hull,qa_vec3 p,qa_numeric_profile numeric){
  return qa_q1_hull_point_contents_at(hull,p,numeric,hull->first_clipnode);
}
typedef struct{const qa_q1_hull*hull;const qa_numeric_operations*n;qa_q1_hull_trace tr;qa_q1_blocks_fn blocks;void*bctx;} Walk;
static int blocks_(Walk*w,double c){return w->blocks?w->blocks(w->bctx,c):c==QA_Q1_CONTENTS_SOLID;}
static qa_vec3 interp(Walk*w,qa_vec3 a,qa_vec3 b,double f){
  const qa_numeric_operations*n=w->n;
  qa_vec3 r={NSTORE(NADD(a.x,NMUL(f,NSUB(b.x,a.x)))),NSTORE(NADD(a.y,NMUL(f,NSUB(b.y,a.y)))),NSTORE(NADD(a.z,NMUL(f,NSUB(b.z,a.z))))};
  return r;
}
static int walk(Walk*w,double node,double p1f,double p2f,qa_vec3 p1,qa_vec3 p2,double depth){
  const qa_q1_hull*hull=w->hull; const qa_numeric_operations*n=w->n;
  if(depth>(double)hull->clipnode_count)qa_range_error("Cycle in Quake trace hull");
  if(node<0){
    if(blocks_(w,node)){w->tr.start_solid=true;w->tr.contents=node;}
    else{w->tr.all_solid=false;if(node==QA_Q1_CONTENTS_EMPTY)w->tr.in_open=true;else w->tr.in_water=true;}
    return 1;
  }
  const qa_q1_clip_node*clip=node_at(hull,node);
  double t1=plane_distance(hull,clip->plane,p1,n),t2=plane_distance(hull,clip->plane,p2,n);
  if(t1>=0&&t2>=0)return walk(w,child_number(clip->children[0]),p1f,p2f,p1,p2,depth+1);
  if(t1<0&&t2<0)return walk(w,child_number(clip->children[1]),p1f,p2f,p1,p2,depth+1);
  double raw=NDIV(NADD(t1,t1<0?QA_Q1_DISTANCE_EPSILON:-QA_Q1_DISTANCE_EPSILON),NSUB(t1,t2));
  double fraction=raw<0?0:raw>1?1:raw;
  double midf=NADD(p1f,NMUL(NSUB(p2f,p1f),fraction));
  qa_vec3 mid=interp(w,p1,p2,fraction);
  qa_q1_clip_child near_=clip->children[t1<0?1:0],far_=clip->children[t1<0?0:1];
  if(!walk(w,child_number(near_),p1f,midf,p1,mid,depth+1))return 0;
  double far_contents=contents_at(hull,mid,child_number(far_),n);
  if(!blocks_(w,far_contents))return walk(w,child_number(far_),midf,p2f,mid,p2,depth+1);
  if(w->tr.all_solid)return 0;
  if(!(clip->plane>=0&&clip->plane<(double)hull->plane_count&&clip->plane==floor(clip->plane)))qa_range_error("Invalid Quake collision plane");
  const qa_bsp_plane*pl=&hull->planes[(size_t)clip->plane];
  if(t1<0){w->tr.plane.normal=(qa_vec3){-pl->plane.normal.x,-pl->plane.normal.y,-pl->plane.normal.z};w->tr.plane.distance=-pl->plane.distance;w->tr.plane_index=-1;}
  else{w->tr.plane=pl->plane;w->tr.plane_index=(int)clip->plane;}
  w->tr.contents=far_contents;
  while(blocks_(w,contents_at(hull,mid,hull->first_clipnode,n))){
    fraction=NSUB(fraction,0.1);
    if(fraction<0){w->tr.fraction=midf;w->tr.end=mid;return 0;}
    midf=NADD(p1f,NMUL(NSUB(p2f,p1f),fraction)); mid=interp(w,p1,p2,fraction);
  }
  w->tr.fraction=midf;w->tr.end=mid;w->tr.contents=far_contents; return 0;
}
qa_q1_hull_trace qa_trace_q1_hull(const qa_q1_hull*hull,qa_vec3 start,qa_vec3 end,qa_numeric_profile numeric,qa_q1_blocks_fn blocks,void*bctx){
  Walk w; w.hull=hull; w.n=qa_create_numeric_operations(&numeric); w.blocks=blocks; w.bctx=bctx;
  w.tr=(qa_q1_hull_trace){1,end,false,true,false,false,{{0,0,0},0},QA_Q1_CONTENTS_EMPTY,-2};
  (void)walk(&w,hull->first_clipnode,0,1,start,end,0);
  return w.tr;
}
