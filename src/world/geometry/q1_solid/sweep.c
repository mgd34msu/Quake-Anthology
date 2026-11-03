/* Port of src/world/geometry/q1-solid/sweep.ts. Non-convergence raises the
 * TS Error. GPL-2.0-or-later. */
#include "qa/world/geometry/q1_solid/sweep.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
double qa_shape_support(qa_cell_shape s, qa_vec3 n){
  if(s.kind&&strcmp(s.kind,"capsule")==0)return s.radius+fabs(qa_cell_dot(n,s.axis))*s.half_segment;
  return fabs(qa_cell_dot(n,s.axes[0]))*s.extents.x+fabs(qa_cell_dot(n,s.axes[1]))*s.extents.y+fabs(qa_cell_dot(n,s.axes[2]))*s.extents.z;
}
bool qa_sweep_box_cell(const qa_convex_cell*cell,qa_vec3 start,qa_vec3 end,qa_cell_shape shape,double eps,qa_sweep_interval*out){
  double enter=-INFINITY,exit_=INFINITY,contact=-INFINITY; qa_plane plane={{0,0,0},0};
  size_t pc=0; qa_plane*planes=qa_box_separating_planes(cell,shape.axes,3,&pc);
  for(size_t i=0;i<pc;i++){
    double dist=planes[i].distance+qa_shape_support(shape,planes[i].normal);
    double a=qa_cell_dot(start,planes[i].normal)-dist,b=qa_cell_dot(end,planes[i].normal)-dist;
    if(a>0&&b>0){free(planes);return false;}
    if(a<=0&&b<=0)continue;
    double f=a/(a-b);
    /* Math.max/Math.min: a NaN distance poisons contact/exit as in TS. */
    if(a>b){if(f>enter){enter=f;plane=planes[i];} contact=qa_js_max(contact,(a-eps)/(a-b));}
    else exit_=qa_js_min(exit_,f);
    if(enter>exit_){free(planes);return false;}
  }
  free(planes);
  if(!(enter<=1&&exit_>=0))return false;
  *out=(qa_sweep_interval){enter,exit_,plane,contact}; return true;
}
typedef struct{qa_vec3 a,b;double squared;} Closest;
static Closest mkpair(qa_vec3 a,qa_vec3 b){qa_vec3 d=qa_cell_sub(a,b);Closest r={a,b,qa_cell_dot(d,d)};return r;}
static Closest nearest(Closest a,Closest b){return a.squared<=b.squared?a:b;}
static Closest point_triangle(qa_vec3 p,qa_vec3 a,qa_vec3 b,qa_vec3 c){
  qa_vec3 ab=qa_cell_sub(b,a),ac=qa_cell_sub(c,a),ap=qa_cell_sub(p,a);
  double d1=qa_cell_dot(ab,ap),d2=qa_cell_dot(ac,ap);
  if(d1<=0&&d2<=0)return mkpair(p,a);
  qa_vec3 bp=qa_cell_sub(p,b); double d3=qa_cell_dot(ab,bp),d4=qa_cell_dot(ac,bp);
  if(d3>=0&&d4<=d3)return mkpair(p,b);
  double vc=d1*d4-d3*d2;
  if(vc<=0&&d1>=0&&d3<=0)return mkpair(p,qa_cell_add(a,qa_cell_scale(ab,d1/(d1-d3))));
  qa_vec3 cp=qa_cell_sub(p,c); double d5=qa_cell_dot(ab,cp),d6=qa_cell_dot(ac,cp);
  if(d6>=0&&d5<=d6)return mkpair(p,c);
  double vb=d5*d2-d1*d6;
  if(vb<=0&&d2>=0&&d6<=0)return mkpair(p,qa_cell_add(a,qa_cell_scale(ac,d2/(d2-d6))));
  double va=d3*d6-d5*d4;
  if(va<=0&&d4-d3>=0&&d5-d6>=0)return mkpair(p,qa_cell_lerp(b,c,(d4-d3)/(d4-d3+d5-d6)));
  double sum=va+vb+vc;
  if(fabs(sum)<1e-25)return nearest(nearest(mkpair(p,a),mkpair(p,b)),mkpair(p,c));
  return mkpair(p,qa_cell_add(a,qa_cell_add(qa_cell_scale(ab,vb/sum),qa_cell_scale(ac,vc/sum))));
}
static double clamp01(double f){return qa_js_max(0,qa_js_min(1,f));}
static Closest segments(qa_vec3 p,qa_vec3 q,qa_vec3 a,qa_vec3 b){
  qa_vec3 u=qa_cell_sub(q,p),v=qa_cell_sub(b,a),w=qa_cell_sub(p,a);
  double uu=qa_cell_dot(u,u),uv=qa_cell_dot(u,v),vv=qa_cell_dot(v,v),uw=qa_cell_dot(u,w),vw=qa_cell_dot(v,w);
  double den=uu*vv-uv*uv; if(den==0||isnan(den))den=1; /* `|| 1`: 0, -0 and NaN are falsy */
  double s=uu==0?0:clamp01((uv*vw-vv*uw)/den);
  double t=vv==0?0:(uv*s+vw)/vv;
  if(t<0){t=0;s=uu==0?0:clamp01(-uw/uu);} else if(t>1){t=1;s=uu==0?0:clamp01((uv-uw)/uu);}
  return mkpair(qa_cell_lerp(p,q,s),qa_cell_lerp(a,b,t));
}
static Closest segment_cell(const qa_convex_cell*cell,qa_vec3 p,qa_vec3 q){
  double enter=0,exit_=1;
  for(size_t i=0;i<cell->face_count;i++){qa_plane pl=cell->faces[i].plane;
    double a=qa_cell_dot(p,pl.normal)-pl.distance,b=qa_cell_dot(q,pl.normal)-pl.distance;
    if(a>0&&b>0){enter=INFINITY;break;}
    if(a>b&&a>0)enter=qa_js_max(enter,a/(a-b));
    if(a<b&&b>0)exit_=qa_js_min(exit_,a/(a-b));}
  if(enter<=exit_){qa_vec3 pt=qa_cell_lerp(p,q,enter);return mkpair(pt,pt);}
  Closest r={p,p,INFINITY};
  for(size_t fi=0;fi<cell->face_count;fi++){
    if(cell->faces[fi].vertex_count<3)continue;
    qa_vec3 a=cell->faces[fi].vertices[0];
    for(size_t i=1;i+1<cell->faces[fi].vertex_count;i++){
      qa_vec3 b=cell->faces[fi].vertices[i],c=cell->faces[fi].vertices[i+1];
      if(qa_cell_length(qa_cell_cross(qa_cell_sub(b,a),qa_cell_sub(c,a)))<1e-12)continue;
      r=nearest(r,point_triangle(p,a,b,c)); r=nearest(r,point_triangle(q,a,b,c));
      r=nearest(r,segments(p,q,a,b)); r=nearest(r,segments(p,q,b,c)); r=nearest(r,segments(p,q,c,a));
    }
  }
  return r;
}
typedef struct{double fraction;qa_plane plane;int ok;} Entrance;
static Entrance capsule_entrance(const qa_convex_cell*cell,qa_vec3 start,qa_vec3 end,qa_cell_shape shape,double margin){
  qa_vec3 offset=qa_cell_scale(shape.axis,shape.half_segment),movement=qa_cell_sub(end,start);
  double fraction=0;
  for(int it=0;it<96;it++){
    qa_vec3 center=qa_cell_lerp(start,end,fraction);
    Closest cl=segment_cell(cell,qa_cell_sub(center,offset),qa_cell_add(center,offset));
    double dist=sqrt(cl.squared),sep=dist-shape.radius-margin;
    qa_vec3 normal=dist>1e-12?qa_cell_scale(qa_cell_sub(cl.a,cl.b),1.0/dist):(qa_vec3){0,0,0};
    if(sep<=1e-7){Entrance e={fraction,{normal,qa_cell_dot(normal,cl.b)},1};return e;}
    double closing=-qa_cell_dot(movement,normal);
    if(closing<=0){Entrance e={0,{{0,0,0},0},0};return e;}
    double step=sep/closing;
    if(fraction+step>1){Entrance e={0,{{0,0,0},0},0};return e;}
    fraction+=step;
  }
  qa_raise("Error",0,0,"Quake solid-cell capsule sweep did not converge"); /* TS throws Error */
}
bool qa_sweep_capsule_cell(const qa_convex_cell*cell,qa_vec3 start,qa_vec3 end,qa_cell_shape shape,double eps,qa_sweep_interval*out){
  Entrance e=capsule_entrance(cell,start,end,shape,0);
  if(!e.ok)return false;
  Entrance rev=capsule_entrance(cell,end,start,shape,0);
  Entrance ct=capsule_entrance(cell,start,end,shape,eps);
  *out=(qa_sweep_interval){e.fraction,rev.ok?1-rev.fraction:e.fraction,e.plane,ct.ok?ct.fraction:e.fraction};
  return true;
}
