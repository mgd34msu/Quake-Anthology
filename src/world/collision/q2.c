/* Port of src/world/collision/q2.ts. GPL-2.0-or-later. */
#include "qa/world/collision/q2.h"
#include "qa/core/numeric.h"
#include "qa/foundation/stamp.h"
#include "qa/foundation/support.h"
#include "qa/world/collision/contents.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define EPS 0.03125
static const qa_bsp_plane NO_PLANE = {{{0,0,0},0},0,0};
static qa_stamp_set *ms(qa_q2_collision*c){return (qa_stamp_set*)c->media_stamps;}
static qa_stamp_set *ts(qa_q2_collision*c){return (qa_stamp_set*)c->trace_stamps;}
typedef const qa_numeric_operations N;
#define NADD(n,a,b) (n)->add((n)->ctx,(a),(b))
#define NSUB(n,a,b) (n)->subtract((n)->ctx,(a),(b))
#define NMUL(n,a,b) (n)->multiply((n)->ctx,(a),(b))
#define NDIV(n,a,b) (n)->divide((n)->ctx,(a),(b))
#define NSTO(n,a) (n)->store((n)->ctx,(a))
static double dotn(qa_vec3 a,qa_vec3 b,N*n){return NADD(n,NADD(n,NMUL(n,a.x,b.x),NMUL(n,a.y,b.y)),NMUL(n,a.z,b.z));}
static qa_vec3 subn(qa_vec3 a,qa_vec3 b,N*n){qa_vec3 r={NSUB(n,a.x,b.x),NSUB(n,a.y,b.y),NSUB(n,a.z,b.z)};return r;}
static qa_vec3 lerpn(qa_vec3 a,qa_vec3 b,double f,N*n){qa_vec3 r={NADD(n,a.x,NMUL(n,f,NSUB(n,b.x,a.x))),NADD(n,a.y,NMUL(n,f,NSUB(n,b.y,a.y))),NADD(n,a.z,NMUL(n,f,NSUB(n,b.z,a.z)))};return r;}
static void axis_of(qa_vec3 angles,N*n,qa_vec3 out[3]){
  double yaw=angles.y*M_PI/180,pitch=angles.x*M_PI/180,roll=angles.z*M_PI/180;
  double sy=NSTO(n,sin(yaw)),cy=NSTO(n,cos(yaw)),sp=NSTO(n,sin(pitch)),cp=NSTO(n,cos(pitch)),sr=NSTO(n,sin(roll)),cr=NSTO(n,cos(roll));
  out[0]=(qa_vec3){NMUL(n,cp,cy),NMUL(n,cp,sy),-sp};
  out[1]=(qa_vec3){NSUB(n,NMUL(n,NMUL(n,sr,sp),cy),NMUL(n,cr,sy)),NADD(n,NMUL(n,NMUL(n,sr,sp),sy),NMUL(n,cr,cy)),NMUL(n,sr,cp)};
  out[2]=(qa_vec3){NADD(n,NMUL(n,NMUL(n,cr,sp),cy),NMUL(n,sr,sy)),NSUB(n,NMUL(n,NMUL(n,cr,sp),sy),NMUL(n,sr,cy)),NMUL(n,cr,cp)};
}
static qa_vec3 rotatep(qa_vec3 p,qa_vec3 a[3],N*n){qa_vec3 r={dotn(p,a[0],n),dotn(p,a[1],n),dotn(p,a[2],n)};return r;}
static double plane_dist(qa_vec3 p,qa_bsp_plane pl,N*n){
  return NSUB(n,pl.type==0?p.x:pl.type==1?p.y:pl.type==2?p.z:dotn(p,pl.plane.normal,n),pl.plane.distance);
}
static qa_bounds shape_bounds(qa_trace_shape s){
  if(s.kind&&strcmp(s.kind,"point")==0){qa_bounds z={{0,0,0},{0,0,0}};return z;}
  return s.bounds;
}
static double expandp(qa_bsp_plane plane,qa_trace_shape shape,N*n){
  qa_bounds b=shape_bounds(shape); qa_vec3 normal=plane.plane.normal;
  if(shape.kind&&strcmp(shape.kind,"capsule")==0){
    qa_vec3 center={NMUL(n,NADD(n,b.min.x,b.max.x),0.5),NMUL(n,NADD(n,b.min.y,b.max.y),0.5),NMUL(n,NADD(n,b.min.z,b.max.z),0.5)};
    double hh=NMUL(n,NSUB(n,b.max.z,b.min.z),0.5);
    double rx=NMUL(n,NSUB(n,b.max.x,b.min.x),0.5);
    double radius=rx<hh?rx:hh;
    return NSUB(n,NADD(n,radius,NMUL(n,fabs(normal.z),NSUB(n,hh,radius))),dotn(center,normal,n));
  }
  qa_vec3 corner={normal.x<0?b.max.x:b.min.x,normal.y<0?b.max.y:b.min.y,normal.z<0?b.max.z:b.min.z};
  return -dotn(corner,normal,n);
}
static const qa_q2_world_model *model_at(const qa_q2_world_geometry*g,double i){
  if(i<0||(size_t)i>=g->model_count)qa_range_error("Q2 collision index %g",i);
  return &g->models[(size_t)i];
}
static void flood_areas(qa_q2_collision*c){
  const qa_q2_world_geometry*g=c->geometry;
  for(size_t i=0;i<g->area_count;i++)c->flood[i]=0;
  double floodn=0;
  size_t cap=g->area_count?g->area_count:1;
  double*stack=malloc(cap*8*sizeof*stack); if(!stack)abort();
  for(size_t area=1;area<g->area_count;area++){
    if(c->flood[area]!=0)continue;
    floodn++;
    size_t sp=0; stack[sp++]=(double)area;
    while(sp>0){
      double cur=stack[--sp];
      size_t ci=(size_t)cur;
      if(ci>=g->area_count)qa_range_error("Q2 collision index %g",cur);
      if(c->flood[ci]!=0)continue;
      c->flood[ci]=floodn;
      qa_index_range portals=g->areas[ci].portals;
      for(double i=0;i<portals.count;i++){
        double pi=portals.first+i;
        if(pi<0||(size_t)pi>=g->area_portal_count)qa_range_error("Q2 collision index %g",pi);
        const qa_q2_area_portal*portal=&g->area_portals[(size_t)pi];
        int open=0;
        for(size_t k=0;k<c->open_count;k++)if(c->open_portals[k]==portal->portal)open=1;
        if(open){if(sp>=cap*8){stack=realloc(stack,(sp+16)*sizeof*stack);if(!stack)abort();}stack[sp++]=portal->other_area;}
      }
    }
  }
  free(stack);
}
bool qa_q2_collision_init(qa_q2_collision*c,const qa_q2_world_geometry*geometry){
  memset(c,0,sizeof*c); c->geometry=geometry;
  qa_stamp_set*a=malloc(sizeof*a),*b=malloc(sizeof*b);
  if(!a||!b)return false;
  if(!qa_stamp_set_init(a,geometry->brush_count)||!qa_stamp_set_init(b,geometry->brush_count))return false;
  c->media_stamps=a;c->trace_stamps=b;
  c->flood=calloc(geometry->area_count?geometry->area_count:1,sizeof*c->flood);
  if(!c->flood)return false;
  flood_areas(c);
  return true;
}
void qa_q2_collision_destroy(qa_q2_collision*c){
  if(c->media_stamps){qa_stamp_set_destroy(ms(c));free(c->media_stamps);}
  if(c->trace_stamps){qa_stamp_set_destroy(ts(c));free(c->trace_stamps);}
  free(c->open_portals);free(c->flood);free(c->leaf_scratch);free(c->area_bits_scratch);
  memset(c,0,sizeof*c);
}
qa_bounds qa_q2_collision_model_bounds(qa_q2_collision*c,double i){return model_at(c->geometry,i)->bounds;}
static double point_leaf(qa_q2_collision*c,qa_vec3 point,double headnode,N*n){
  const qa_q2_world_geometry*g=c->geometry;
  qa_bsp_child child; child.is_node=headnode>=0; child.index=headnode<0?-1-headnode:headnode;
  while(child.is_node){
    if(child.index<0||(size_t)child.index>=g->node_count)qa_range_error("Q2 collision index %g",child.index);
    const qa_bsp_node*node=&g->nodes[(size_t)child.index];
    if(node->plane<0||(size_t)node->plane>=g->plane_count)qa_range_error("Q2 collision index %g",node->plane);
    qa_bsp_plane plane=g->planes[(size_t)node->plane];
    double d=n==NULL?(point.x*plane.plane.normal.x+point.y*plane.plane.normal.y+point.z*plane.plane.normal.z-plane.plane.distance):plane_dist(point,plane,n);
    child=node->children[d<0?1:0];
  }
  return child.index;
}
double qa_q2_collision_point_leaf(qa_q2_collision*c,qa_vec3 p,double model){return point_leaf(c,p,model_at(c->geometry,model)->headnode,NULL);}
double qa_q2_collision_leaf_cluster(qa_q2_collision*c,double i){
  if(i<0||(size_t)i>=c->geometry->leaf_count)qa_range_error("Q2 collision index %g",i);
  return c->geometry->leaves[(size_t)i].cluster;
}
double qa_q2_collision_leaf_area(qa_q2_collision*c,double i){
  if(i<0||(size_t)i>=c->geometry->leaf_count)qa_range_error("Q2 collision index %g",i);
  return c->geometry->leaves[(size_t)i].area;
}
qa_point_contents_result qa_q2_collision_point_contents(qa_q2_collision*c,qa_point_contents_query query){
  N*n=qa_create_numeric_operations(&query.numeric);
  qa_vec3 point=query.point; double model=0;
  if(query.target.is_model){
    model=query.target.model;
    qa_vec3 ax[3]; axis_of(query.target.angles,n,ax);
    point=rotatep(subn(point,query.target.origin,n),ax,n);
  }
  double li=point_leaf(c,point,model_at(c->geometry,model)->headnode,n);
  if(li<0||(size_t)li>=c->geometry->leaf_count)qa_range_error("Q2 collision index %g",li);
  const qa_q2_leaf*leaf=&c->geometry->leaves[(size_t)li];
  qa_point_contents_result r={"q2",0,leaf->contents,leaf->merged_contents}; return r;
}
qa_leaf_query_result qa_q2_collision_box_leaves(qa_q2_collision*c,qa_bounds bounds,double limit,double headnode){
  if(limit!=floor(limit)||limit<0||limit>9007199254740991.0)qa_range_error("Leaf limit must be nonnegative");
  const qa_q2_world_geometry*g=c->geometry;
  free(c->leaf_scratch); c->leaf_scratch=NULL; c->leaf_scratch_count=0;
  size_t cap=16,n=0; double*leaves=malloc(cap*sizeof*leaves); if(!leaves)abort();
  int has_top=0; double topnode=0; int overflow=0;
  size_t scap=64,sp=0; qa_bsp_child*stack=malloc(scap*sizeof*stack); if(!stack)abort();
  qa_bsp_child first; first.is_node=headnode>=0; first.index=headnode<0?-1-headnode:headnode;
  stack[sp++]=first;
  while(sp>0){
    qa_bsp_child child=stack[--sp];
    if(!child.is_node){
      if((double)n==limit)overflow=1;
      else{if(n==cap){cap*=2;leaves=realloc(leaves,cap*sizeof*leaves);if(!leaves)abort();}leaves[n++]=child.index;}
      continue;
    }
    if(child.index<0||(size_t)child.index>=g->node_count)qa_range_error("Q2 collision index %g",child.index);
    const qa_bsp_node*node=&g->nodes[(size_t)child.index];
    qa_bsp_plane plane=g->planes[(size_t)node->plane]; qa_vec3 normal=plane.plane.normal;
    double far=(normal.x<0?bounds.min.x:bounds.max.x)*normal.x+(normal.y<0?bounds.min.y:bounds.max.y)*normal.y+(normal.z<0?bounds.min.z:bounds.max.z)*normal.z;
    double near=(normal.x<0?bounds.max.x:bounds.min.x)*normal.x+(normal.y<0?bounds.max.y:bounds.min.y)*normal.y+(normal.z<0?bounds.max.z:bounds.min.z)*normal.z;
    if(far>=plane.plane.distance&&near<plane.plane.distance&&!has_top){has_top=1;topnode=child.index;}
    if(sp+2>scap){scap*=2;stack=realloc(stack,scap*sizeof*stack);if(!stack)abort();}
    if(near<plane.plane.distance)stack[sp++]=node->children[1];
    if(far>=plane.plane.distance)stack[sp++]=node->children[0];
  }
  free(stack);
  c->leaf_scratch=leaves;c->leaf_scratch_count=n;
  qa_leaf_query_result r; memset(&r,0,sizeof r);
  r.leaves=(qa_double_view){leaves,n}; r.has_topnode=has_top!=0; r.topnode=topnode; r.overflow=overflow!=0;
  return r;
}
qa_leaf_query_result qa_q2_collision_box_leaves_default(qa_q2_collision*c,qa_bounds b,double limit){return qa_q2_collision_box_leaves(c,b,limit,0);}
typedef struct{qa_vec3 start,end;qa_trace_shape shape;N*n;} MediaDist;
static double media_dist(void*ctx,qa_bsp_plane plane,int endpoint){
  MediaDist*c=ctx;
  return NSUB(c->n,dotn(endpoint==0?c->start:c->end,plane.plane.normal,c->n),NADD(c->n,plane.plane.distance,expandp(plane,c->shape,c->n)));
}
qa_trace_media qa_q2_collision_trace_media(qa_q2_collision*c,qa_trace_query query,double fraction){
  N*n=qa_create_numeric_operations(&query.numeric);
  const qa_q2_world_geometry*g=c->geometry;
  double model=query.target.is_model?query.target.model:0;
  qa_vec3 ax[3]; int has_rot=query.target.is_model;
  if(has_rot)axis_of(query.target.angles,n,ax);
  qa_vec3 start=has_rot?rotatep(subn(query.start,query.target.origin,n),ax,n):query.start;
  qa_vec3 end=has_rot?rotatep(subn(query.end,query.target.origin,n),ax,n):query.end;
  qa_bounds sb=shape_bounds(query.shape);
  qa_vec3 reached=lerpn(start,end,fraction,n);
  qa_bounds env={{fmin(start.x,reached.x)+sb.min.x-1,fmin(start.y,reached.y)+sb.min.y-1,fmin(start.z,reached.z)+sb.min.z-1},
    {fmax(start.x,reached.x)+sb.max.x+1,fmax(start.y,reached.y)+sb.max.y+1,fmax(start.z,reached.z)+sb.max.z+1}};
  qa_leaf_query_result leaves=qa_q2_collision_box_leaves(c,env,(double)g->leaf_count,model_at(g,model)->headnode);
  qa_stamp_set*seen=ms(c); qa_stamp_set_begin(seen);
  size_t cap=16,bn=0; qa_medium_brush*brushes=malloc(cap*sizeof*brushes); if(!brushes)abort();
  qa_bsp_plane**store=malloc(cap*sizeof*store); if(!store)abort();
  for(size_t li=0;li<leaves.leaves.count;li++){
    double leaf_index=leaves.leaves.values[li];
    const qa_q2_leaf*leaf=&g->leaves[(size_t)leaf_index];
    for(double i=0;i<leaf->brushes.count;i++){
      double fi=leaf->brushes.first+i;
      if(fi<0||(size_t)fi>=g->leaf_brushes.count)qa_range_error("Q2 collision index %g",fi);
      double bi=g->leaf_brushes.values[(size_t)fi];
      if(!qa_stamp_set_mark(seen,(size_t)bi))continue;
      const qa_q2_brush*brush=&g->brushes[(size_t)bi];
      if(qa_convert_contents(brush->contents,"q2","q1")==-1)continue;
      size_t sc=(size_t)brush->sides.count;
      qa_bsp_plane*pls=malloc((sc?sc:1)*sizeof*pls); if(!pls)abort();
      for(size_t k=0;k<sc;k++){
        double si=brush->sides.first+(double)k;
        double pi=g->brush_sides[(size_t)si].plane;
        pls[k]=g->planes[(size_t)pi];
      }
      if(bn==cap){cap*=2;brushes=realloc(brushes,cap*sizeof*brushes);store=realloc(store,cap*sizeof*store);if(!brushes||!store)abort();}
      store[bn]=pls; brushes[bn]=(qa_medium_brush){brush->contents,pls,sc}; bn++;
    }
  }
  MediaDist mc={start,end,query.shape,n};
  qa_trace_media r=qa_trace_brush_media(brushes,bn,"q2",media_dist,&mc,n,fraction);
  for(size_t i=0;i<bn;i++)free(store[i]);
  free(store);free(brushes);
  return r;
}
typedef struct{
  qa_q2_collision*c; N*n; qa_trace_query q; qa_vec3 start,end,extents;
  int stationary,merged; int32_t mask;
  double fraction,contents; int start_solid,all_solid;
  qa_bsp_plane plane; const qa_q2_surface_info*surface;
  int has_secondary; qa_bsp_plane secondary_plane; const qa_q2_surface_info*secondary_surface;
} Work;
static void brush_trace(Work*w,double index){
  const qa_q2_world_geometry*g=w->c->geometry; N*n=w->n;
  if(!qa_stamp_set_mark(ts(w->c),(size_t)index))return;
  if(index<0||(size_t)index>=g->brush_count)qa_range_error("Q2 collision index %g",index);
  const qa_q2_brush*brush=&g->brushes[(size_t)index];
  if((((int32_t)brush->contents)&w->mask)==0||brush->sides.count==0)return;
  double enter=-1,enter2=-1,leave=1; int start_out=0,get_out=0,have_lead=0,have_second=0;
  qa_bsp_plane lead_plane=NO_PLANE,second_plane=NO_PLANE; const qa_q2_surface_info*lead_surf=NULL;
  for(double i=0;i<brush->sides.count;i++){
    double si=brush->sides.first+i;
    const qa_q2_brush_side*side=&g->brush_sides[(size_t)si];
    qa_bsp_plane plane=g->planes[(size_t)side->plane];
    double dist=NADD(n,plane.plane.distance,expandp(plane,w->q.shape,n));
    double d1=NSUB(n,dotn(w->start,plane.plane.normal,n),dist);
    if(w->stationary){if(d1>0)return;continue;}
    double d2=NSUB(n,dotn(w->end,plane.plane.normal,n),dist);
    if(d1>0)start_out=1;
    if(d2>0)get_out=1;
    if(d1>0&&(d2>=EPS||d2>=d1))return;
    if(d1<=0&&d2<=0)continue;
    if(d1>d2){
      double f=NDIV(n,NSUB(n,d1,EPS),NSUB(n,d1,d2)); if(f<0)f=0;
      if(f>enter){enter=f;lead_plane=plane;lead_surf=side->texture_info<0?NULL:((size_t)side->texture_info<g->texture_info_count?&g->texture_info[(size_t)side->texture_info].surface:(qa_range_error("Q2 collision index %g",side->texture_info),(const qa_q2_surface_info*)NULL));have_lead=1;}
      else if(f>enter2){enter2=f;second_plane=plane;have_second=1;}
    }else{
      double f=NDIV(n,NADD(n,d1,EPS),NSUB(n,d1,d2)); if(f>1)f=1; if(f<leave)leave=f;
    }
  }
  if(!start_out){
    w->start_solid=1;
    if(!get_out){
      w->all_solid=1;
      if(w->stationary||w->merged){w->fraction=0;w->contents=brush->contents;}
    }
    return;
  }
  if(enter<leave&&enter>-1&&enter<w->fraction&&have_lead){
    w->fraction=enter;w->plane=lead_plane;w->surface=lead_surf;w->contents=brush->contents;
    if(have_second){w->has_secondary=1;w->secondary_plane=second_plane;w->secondary_surface=lead_surf;}
  }
}
static void leaf_trace(Work*w,double index){
  const qa_q2_world_geometry*g=w->c->geometry;
  if(index<0||(size_t)index>=g->leaf_count)qa_range_error("Q2 collision index %g",index);
  const qa_q2_leaf*leaf=&g->leaves[(size_t)index];
  double lc=w->merged?leaf->merged_contents:leaf->contents;
  if((((int32_t)lc)&w->mask)==0)return;
  for(double i=0;i<leaf->brushes.count;i++){
    double fi=leaf->brushes.first+i;
    brush_trace(w,g->leaf_brushes.values[(size_t)fi]);
    if(w->fraction==0)return;
  }
}
static void walk_trace(Work*w,qa_bsp_child child,double p1f,double p2f,qa_vec3 p1,qa_vec3 p2){
  if(w->fraction<=p1f)return;
  if(!child.is_node){leaf_trace(w,child.index);return;}
  const qa_q2_world_geometry*g=w->c->geometry; N*n=w->n;
  const qa_bsp_node*node=&g->nodes[(size_t)child.index];
  qa_bsp_plane plane=g->planes[(size_t)node->plane];
  double t1=plane_dist(p1,plane,n),t2=plane_dist(p2,plane,n);
  double offset=plane.type==0?w->extents.x:plane.type==1?w->extents.y:plane.type==2?w->extents.z:
    NADD(n,NADD(n,fabs(NMUL(n,w->extents.x,plane.plane.normal.x)),fabs(NMUL(n,w->extents.y,plane.plane.normal.y))),fabs(NMUL(n,w->extents.z,plane.plane.normal.z)));
  if(t1>=offset&&t2>=offset){walk_trace(w,node->children[0],p1f,p2f,p1,p2);return;}
  if(t1<-offset&&t2<-offset){walk_trace(w,node->children[1],p1f,p2f,p1,p2);return;}
  int side=0; double f1=1,f2=0;
  if(t1<t2){
    double inv=NDIV(n,1,NSUB(n,t1,t2)); side=1;
    f2=NMUL(n,NADD(n,NADD(n,t1,offset),EPS),inv);
    f1=NMUL(n,NADD(n,NSUB(n,t1,offset),EPS),inv);
  }else if(t1>t2){
    double inv=NDIV(n,1,NSUB(n,t1,t2));
    f2=NMUL(n,NSUB(n,NSUB(n,t1,offset),EPS),inv);
    f1=NMUL(n,NADD(n,NADD(n,t1,offset),EPS),inv);
  }
  if(f1<0){f1=0;}
  if(f1>1){f1=1;}
  if(f2<0){f2=0;}
  if(f2>1){f2=1;}
  walk_trace(w,node->children[side],p1f,NADD(n,p1f,NMUL(n,NSUB(n,p2f,p1f),f1)),p1,lerpn(p1,p2,f1,n));
  walk_trace(w,node->children[side==0?1:0],NADD(n,p1f,NMUL(n,NSUB(n,p2f,p1f),f2)),p2f,lerpn(p1,p2,f2,n),p2);
}
qa_trace_result qa_q2_collision_trace(qa_q2_collision*c,qa_trace_query query){
  N*n=qa_create_numeric_operations(&query.numeric);
  const qa_q2_world_geometry*g=c->geometry;
  double model=query.target.is_model?query.target.model:0;
  double headnode=model_at(g,model)->headnode;
  qa_vec3 ax[3]; int has_t=query.target.is_model;
  if(has_t)axis_of(query.target.angles,n,ax);
  qa_vec3 start=has_t?rotatep(subn(query.start,query.target.origin,n),ax,n):query.start;
  qa_vec3 end=has_t?rotatep(subn(query.end,query.target.origin,n),ax,n):query.end;
  int stationary=start.x==end.x&&start.y==end.y&&start.z==end.z;
  int merged=!(query.policy.kind&&strcmp(query.policy.kind,"q2")==0)||(query.policy.leaf_contents&&strcmp(query.policy.leaf_contents,"merged")==0);
  double maskd=qa_geometry_mask(query.policy,"q2");
  qa_bounds sb=shape_bounds(query.shape);
  Work w; memset(&w,0,sizeof w);
  w.c=c;w.n=n;w.q=query;w.start=start;w.end=end;w.stationary=stationary;w.merged=merged;w.mask=(int32_t)maskd;
  w.fraction=1;w.plane=NO_PLANE;
  w.extents=(qa_vec3){fmax(-sb.min.x,sb.max.x),fmax(-sb.min.y,sb.max.y),fmax(-sb.min.z,sb.max.z)};
  qa_stamp_set_begin(ts(c));
  if(stationary){
    qa_bounds env={{start.x+sb.min.x-1,start.y+sb.min.y-1,start.z+sb.min.z-1},{start.x+sb.max.x+1,start.y+sb.max.y+1,start.z+sb.max.z+1}};
    qa_leaf_query_result leaves=qa_q2_collision_box_leaves(c,env,1024,headnode);
    for(size_t i=0;i<leaves.leaves.count;i++){leaf_trace(&w,leaves.leaves.values[i]);if(w.all_solid)break;}
  }else{
    qa_bsp_child root; root.is_node=headnode>=0; root.index=headnode<0?-1-headnode:headnode;
    walk_trace(&w,root,0,1,start,end);
  }
  qa_bsp_plane plane=w.plane;
  if(has_t&&w.fraction!=1){
    qa_vec3 inv_ax[3];
    axis_of((qa_vec3){-query.target.angles.x,-query.target.angles.y,-query.target.angles.z},n,inv_ax);
    plane.plane.normal=rotatep(plane.plane.normal,inv_ax,n);
  }
  qa_trace_result r; memset(&r,0,sizeof r);
  r.kind="q2";r.fraction=w.fraction;r.end=lerpn(query.start,query.end,w.fraction,n);
  r.start_solid=w.start_solid!=0;r.all_solid=w.all_solid!=0;
  r.contact=(qa_trace_contact){w.fraction<1&&!w.all_solid,plane.plane};
  r.hit.kind=(w.fraction<1||w.start_solid)?QA_TRACE_HIT_WORLD:QA_TRACE_HIT_NONE; r.hit.model=model;
  r.contents=w.contents;r.surface=w.surface;r.source_plane=plane;
  r.has_secondary=w.has_secondary!=0;r.secondary_plane=w.secondary_plane;r.secondary_surface=w.secondary_surface;
  return r;
}
void qa_q2_collision_set_area_portal_state(qa_q2_collision*c,double portal,bool open){
  const qa_q2_world_geometry*g=c->geometry;
  int found=0;
  for(size_t i=0;i<g->area_portal_count;i++)if(g->area_portals[i].portal==portal)found=1;
  if(!found)qa_range_error("Unknown Q2 area portal %g",portal);
  if(open){
    for(size_t i=0;i<c->open_count;i++)if(c->open_portals[i]==portal){flood_areas(c);return;}
    if(c->open_count==c->open_cap){c->open_cap=c->open_cap?c->open_cap*2:8;c->open_portals=realloc(c->open_portals,c->open_cap*sizeof*c->open_portals);if(!c->open_portals)abort();}
    c->open_portals[c->open_count++]=portal;
  }else{
    for(size_t i=0;i<c->open_count;i++)if(c->open_portals[i]==portal){memmove(c->open_portals+i,c->open_portals+i+1,(c->open_count-i-1)*sizeof*c->open_portals);c->open_count--;break;}
  }
  flood_areas(c);
}
const double *qa_q2_collision_portal_state(qa_q2_collision*c,size_t*out){*out=c->open_count;return c->open_portals;}
void qa_q2_collision_restore_portal_state(qa_q2_collision*c,const double*open,size_t count){
  free(c->open_portals); c->open_portals=NULL;c->open_count=0;c->open_cap=0;
  for(size_t i=0;i<count;i++){
    int exists=0; for(size_t k=0;k<c->open_count;k++)if(c->open_portals[k]==open[i])exists=1;
    if(exists)continue;
    if(c->open_count==c->open_cap){c->open_cap=c->open_cap?c->open_cap*2:8;c->open_portals=realloc(c->open_portals,c->open_cap*sizeof*c->open_portals);if(!c->open_portals)abort();}
    c->open_portals[c->open_count++]=open[i];
  }
  flood_areas(c);
}
bool qa_q2_collision_areas_connected(qa_q2_collision*c,double a,double b){
  if(a<0||(size_t)a>=c->geometry->area_count||b<0||(size_t)b>=c->geometry->area_count)qa_range_error("Q2 collision index %g",a<0||(size_t)a>=c->geometry->area_count?a:b);
  return c->flood[(size_t)a]==c->flood[(size_t)b];
}
const uint8_t *qa_q2_collision_area_bits(qa_q2_collision*c,double area,size_t*out){
  size_t bytes=(size_t)(((int64_t)c->geometry->area_count+7)>>3);
  free(c->area_bits_scratch); c->area_bits_scratch=calloc(bytes?bytes:1,1); if(!c->area_bits_scratch)abort();
  c->area_bits_length=bytes;
  for(size_t i=0;i<c->geometry->area_count;i++)
    if(area==0||qa_q2_collision_areas_connected(c,area,(double)i))
      c->area_bits_scratch[i>>3]=(uint8_t)(c->area_bits_scratch[i>>3]|(1u<<(i&7)));
  *out=bytes; return c->area_bits_scratch;
}
bool qa_q2_collision_cluster_visible(qa_q2_collision*c,double from,double to,const char*kind){
  if(to<0)return false;
  const qa_q2_visibility*vis=c->geometry->visibility;
  if(!vis)return true;
  if(from<0||(size_t)from>=vis->cluster_count||(size_t)to>=vis->cluster_count)return false;
  const qa_q2_visibility_cluster*row=&vis->clusters[(size_t)from];
  double offset=(kind&&strcmp(kind,"phs")==0)?row->phs_offset:row->pvs_offset;
  if(offset<0)return true;
  size_t input=(size_t)offset,output=0,wanted=(size_t)to>>3;
  while(output<=wanted){
    if(input>=vis->compressed.length)qa_range_error("Q2 visibility read outside lump");
    uint8_t byte=vis->compressed.bytes[input++];
    if(byte!=0){
      if(output==wanted)return (byte&(1u<<((size_t)to&7)))!=0;
      output++;
    }else{
      if(input>=vis->compressed.length)qa_range_error("Invalid Q2 visibility zero run");
      uint8_t run=vis->compressed.bytes[input++];
      if(run==0)qa_range_error("Invalid Q2 visibility zero run");
      if(output+run>wanted)return false;
      output+=run;
    }
  }
  return false;
}
