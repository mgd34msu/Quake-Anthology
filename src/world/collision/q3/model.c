/* Port of src/world/collision/q3/model.ts. GPL-2.0-or-later. */
#include "qa/world/collision/q3/model.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
#define BODY 0x02000000
static float f32(double v){return (float)v;}
static int finite3(qa_vec3 p){return isfinite(p.x)&&isfinite(p.y)&&isfinite(p.z);}
static void validate_bounds(qa_bounds b){
  if(!finite3(b.min)||!finite3(b.max)||b.min.x>b.max.x||b.min.y>b.max.y||b.min.z>b.max.z)
    qa_range_error("temporary hull requires finite ordered bounds");
}
typedef struct{qa_vec3 center,mins,extents,offset;int capsule;double radius,halfheight;} Prepared;
static Prepared prepare(qa_q3_trace_shape shape){
  Prepared r; memset(&r,0,sizeof r);
  if(shape.kind&&strcmp(shape.kind,"point")==0)return r;
  if(!finite3(shape.mins)||!finite3(shape.maxs))qa_range_error("trace shape requires finite bounds");
  r.center=qa_scale3(qa_add3(shape.mins,shape.maxs),0.5);
  r.mins=qa_sub3(shape.mins,r.center); r.extents=qa_sub3(shape.maxs,r.center);
  r.radius=qa_js_min(r.extents.x,r.extents.z);
  r.capsule=shape.kind&&strcmp(shape.kind,"capsule")==0;
  r.halfheight=r.extents.z;
  r.offset=qa_math_vec3(0,0,f32(r.extents.z-r.radius));
  return r;
}
static qa_source_trace_plane box_plane(qa_bounds b,int idx){
  switch(idx){
    case 0:return (qa_source_trace_plane){{{1,0,0},b.max.x},0,0};
    case 1:return (qa_source_trace_plane){{{-1,0,0},-b.min.x},3,1};
    case 2:return (qa_source_trace_plane){{{0,1,0},b.max.y},1,0};
    case 3:return (qa_source_trace_plane){{{0,-1,0},-b.min.y},4,2};
    case 4:return (qa_source_trace_plane){{{0,0,1},b.max.z},2,0};
    case 5:return (qa_source_trace_plane){{{0,0,-1},-b.min.z},5,4};
    default:qa_range_error("computed temporary box side outside six planes");return qa_empty_source_trace().plane;
  }
}
typedef struct{double fraction;int all_solid,start_solid;qa_source_trace_plane plane;double surface_flags,contents;} Work;
static void trace_box(Work*w,qa_bounds bounds,qa_vec3 start,qa_vec3 end,Prepared shape,int stationary,double mask,qa_collision_counters*counters,qa_collision_box_storage*storage,const qa_bounds*retained){
  qa_q3_collision_brush*brush=storage?storage->brush:NULL;
  if(storage&&brush){
    if(brush->check_count==qa_collision_box_storage_check_count(storage))return;
    brush->check_count=qa_collision_box_storage_check_count(storage);
  }
  double bcontents=brush?brush->contents:BODY;
  if(((qa_js_to_int32(mask))&(qa_js_to_int32(bcontents)))==0||(brush&&brush->side_count==0))return;
  if(stationary){
    qa_vec3 mn,mx;
    if(retained){mn=retained->min;mx=retained->max;}
    else if(shape.capsule){
      mn=qa_math_vec3(start.x-fabs(shape.offset.x)-shape.radius,start.y-fabs(shape.offset.y)-shape.radius,start.z-fabs(shape.offset.z)-shape.radius);
      mx=qa_math_vec3(start.x+fabs(shape.offset.x)+shape.radius,start.y+fabs(shape.offset.y)+shape.radius,start.z+fabs(shape.offset.z)+shape.radius);
    }else{mn=qa_add3(start,shape.mins);mx=qa_add3(start,shape.extents);}
    qa_bounds bb=brush?brush->bounds:bounds;
    if(mn.x>bb.max.x||mn.y>bb.max.y||mn.z>bb.max.z||mx.x<bb.min.x||mx.y<bb.min.y||mx.z<bb.min.z)return;
  }
  if(!stationary)counters->c_brush_traces=(int32_t)((uint32_t)counters->c_brush_traces+1u);
  double enter=-1,leave=1; int start_out=0,get_out=0,have_lead=0;
  qa_source_trace_plane lead_plane={{{0,0,0},0},0,0}; double lead_sf=0;
  int total=brush?(int)brush->side_count:6;
  for(int idx=stationary?6:0;idx<total;idx++){
    qa_source_trace_plane plane; double sf;
    if(!storage){plane=box_plane(bounds,idx);sf=0;}
    else{qa_box_side_read rd=qa_collision_box_read_side(storage,idx);plane=(qa_source_trace_plane){rd.plane.plane,rd.plane.type,rd.plane.signbits};sf=rd.surface_flags;}
    qa_vec3 n=plane.plane.normal;
    double expansion=shape.radius;
    if(!shape.capsule){
      double signs=plane.signbits;
      if(signs>=8)qa_range_error("CM brush plane signbits outside trace offsets");
      int s=(int)signs;
      double x=(s&1)?shape.extents.x:shape.mins.x,y=(s&2)?shape.extents.y:shape.mins.y,z=(s&4)?shape.extents.z:shape.mins.z;
      expansion=-(double)f32((double)f32((double)f32(n.x*(double)f32(x))+(double)f32(n.y*(double)f32(y)))+(double)f32(n.z*(double)f32(z)));
    }
    double dist=f32(plane.plane.distance+expansion);
    qa_vec3 first=start,last=end;
    if(shape.capsule){
      qa_vec3 off=qa_dot3(n,shape.offset)>0?qa_scale3(shape.offset,-1):shape.offset;
      first=qa_add3(start,off); last=qa_add3(end,off);
    }
    double d1=f32(qa_dot3(first,n)-dist);
    if(stationary){if(d1>0)return;continue;}
    double d2=f32(qa_dot3(last,n)-dist);
    if(d1>0)start_out=1;
    if(d2>0)get_out=1;
    if(d1>0&&(d2>=0.125||d2>=d1))return;
    if(d1<=0&&d2<=0)continue;
    if(d1>d2){
      double f=f32(f32(d1-0.125)/f32(d1-d2)); if(f<0)f=0;
      if(f>enter){enter=f;lead_plane=plane;lead_sf=sf;have_lead=1;}
    }else leave=qa_js_min(leave,qa_js_min(1,f32(f32(d1+0.125)/f32(d1-d2)))); /* Math.min: NaN propagates */
  }
  if(!start_out){
    w->start_solid=1;
    if(!get_out){w->all_solid=1;w->fraction=0;w->contents=bcontents;}
  }else if(enter<leave&&enter>-1&&enter<w->fraction&&have_lead){
    w->fraction=enter<0?0:enter; w->plane=lead_plane; w->surface_flags=lead_sf; w->contents=bcontents;
  }
}
static float capsule_sqrt(double number){
  double x=f32(number*0.5);
  float y=qa_bits_to_float32(0x5f3759dfu-(qa_float32_to_bits(number)>>1));
  y=f32(y*f32(1.5-f32(f32(x*y)*y)));
  y=f32(y*f32(1.5-f32(f32(x*y)*y)));
  return f32(number*y);
}
static double dist_line_sq(qa_vec3 point,qa_vec3 start,qa_vec3 end,qa_vec3 dir){
  qa_vec3 proj=qa_add3(start,qa_scale3(dir,qa_dot3(qa_sub3(point,start),dir)));
  double pp[3]={proj.x,proj.y,proj.z},ss[3]={start.x,start.y,start.z},ee[3]={end.x,end.y,end.z};
  for(int i=0;i<3;i++){
    if((pp[i]>ss[i]&&pp[i]>ee[i])||(pp[i]<ss[i]&&pp[i]<ee[i])){
      qa_vec3 d=qa_sub3(point,fabs(pp[i]-ss[i])<fabs(pp[i]-ee[i])?start:end);
      return qa_dot3(d,d);
    }
  }
  qa_vec3 d=qa_sub3(point,proj); return qa_dot3(d,d);
}
static void trace_rounded(Work*w,qa_vec3 origin,double radius,int has_hh,double halfheight,qa_vec3 start,qa_vec3 end,qa_vec3 model_origin){
  int cylinder=has_hh;
  qa_vec3 s2=cylinder?qa_math_vec3(start.x,start.y,0):start;
  qa_vec3 e2=cylinder?qa_math_vec3(end.x,end.y,0):end;
  qa_vec3 o2=cylinder?qa_math_vec3(origin.x,origin.y,0):origin;
  qa_vec3 delta=qa_sub3(s2,o2);
  if((!cylinder||(start.z<=f32(origin.z+halfheight)&&start.z>=f32(origin.z-halfheight)))&&qa_dot3(delta,delta)<f32(radius*radius)){
    w->fraction=0;w->start_solid=1;
    qa_vec3 ed=qa_sub3(e2,o2);
    if(qa_dot3(ed,ed)<f32(radius*radius))w->all_solid=1;
    return;
  }
  qa_vec3 movement=qa_sub3(e2,s2);
  double length=qa_length3(movement);
  qa_vec3 dir=length==0?qa_math_vec3(0,0,0):qa_scale3(movement,f32(1.0/length));
  double closest=dist_line_sq(o2,s2,e2,dir);
  qa_vec3 end_delta=qa_sub3(e2,o2);
  double near_r=f32(radius+0.125);
  if(closest>=f32(radius*radius)&&qa_dot3(end_delta,end_delta)>f32(near_r*near_r))return;
  double inflated=f32(radius+1);
  double b=f32(2*qa_dot3(dir,delta));
  double c=f32(qa_dot3(delta,delta)-f32(inflated*inflated));
  double det=f32(f32(b*b)-f32(4*c));
  if(det<=0)return;
  double fraction=f32(f32(-b-capsule_sqrt(det))*0.5);
  fraction=fraction<0?0:f32(fraction/length);
  if(!(fraction<w->fraction))return;
  qa_vec3 inter=qa_source_trace_end(start,end,fraction);
  if(cylinder&&(inter.z>f32(origin.z+halfheight)||inter.z<f32(origin.z-halfheight)))return;
  qa_vec3 normal=qa_sub3(inter,origin);
  if(cylinder)normal=qa_math_vec3(normal.x,normal.y,0);
  normal=qa_scale3(normal,f32(1.0/inflated));
  w->fraction=fraction;
  w->plane.plane.normal=normal;
  w->plane.plane.distance=qa_dot3(normal,qa_add3(model_origin,inter));
  w->contents=BODY;
}
static void position_capsule(Work*w,Prepared target,qa_vec3 start,Prepared shape){
  qa_vec3 top=qa_add3(start,shape.offset),bottom=qa_sub3(start,shape.offset);
  qa_vec3 upper=qa_add3(target.center,target.offset),lower=qa_sub3(target.center,target.offset);
  double radius=f32(shape.radius+target.radius),squared=f32(radius*radius);
  qa_vec3 ends_[2]={top,bottom},tends[2]={upper,lower};
  for(int i=0;i<2;i++)for(int j=0;j<2;j++){
    qa_vec3 d=qa_sub3(tends[j],ends_[i]);
    if(qa_dot3(d,d)<squared){w->all_solid=1;w->start_solid=1;w->fraction=0;}
  }
  if((top.z>=upper.z&&top.z<=lower.z)||(bottom.z>=upper.z&&bottom.z<=lower.z)){
    qa_vec3 d=qa_math_vec3(top.x-upper.x,top.y-upper.y,0);
    if(qa_dot3(d,d)<squared){w->all_solid=1;w->start_solid=1;w->fraction=0;}
  }
}
qa_temporary_collision_model *qa_temporary_collision_model_create(const char*kind,qa_bounds bounds,qa_collision_counters*counters,qa_collision_box_storage*storage){
  qa_temporary_collision_model*m=malloc(sizeof*m); if(!m)return NULL;
  memset(m,0,sizeof*m);
  /* TS owns vec3(...) copies: each bound coordinate is stored through Math.fround. */
  const qa_bounds owned={qa_math_vec3(bounds.min.x,bounds.min.y,bounds.min.z),qa_math_vec3(bounds.max.x,bounds.max.y,bounds.max.z)};
  m->kind=kind; m->bounds=owned; m->storage=storage;
  if(counters)m->counters=counters; else{qa_collision_counters_init(&m->owned_counters);m->counters=&m->owned_counters;}
  if(kind&&strcmp(kind,"box")==0){
    if(!finite3(owned.min)||!finite3(owned.max)){free(m);qa_range_error("temporary box requires finite plane distances");}
  }else{
    if(!finite3(bounds.min)||!finite3(bounds.max)||bounds.min.x>bounds.max.x||bounds.min.y>bounds.max.y||bounds.min.z>bounds.max.z)free(m);
    validate_bounds(bounds);
    qa_q3_trace_shape s={"capsule",owned.min,owned.max};
    Prepared t=prepare(s);
    m->target_center=t.center;m->target_mins=t.mins;m->target_extents=t.extents;m->target_offset=t.offset;
    m->target_radius=t.radius;m->target_halfheight=t.halfheight;
  }
  return m;
}
void qa_temporary_collision_model_destroy(qa_temporary_collision_model*m){free(m);}
qa_bounds qa_temporary_collision_model_bounds(qa_temporary_collision_model*m){
  if(m->storage&&m->kind&&strcmp(m->kind,"box")==0)return m->storage->brush->bounds;
  return m->bounds;
}
double qa_temporary_model_point_contents(qa_temporary_collision_model*m,qa_vec3 point){
  if(!finite3(point))qa_range_error("point contents requires finite coordinates");
  if(m->storage){
    qa_q3_collision_brush*brush=m->storage->brush;
    double i=0;
    for(;i<brush->side_count;i++){
      qa_box_side_read rd=qa_collision_box_read_side(m->storage,i);
      if(qa_dot3(point,rd.plane.plane.normal)>rd.plane.plane.distance)break;
    }
    return i==brush->side_count?brush->contents:0;
  }
  qa_bounds b=m->bounds;
  return point.x>=b.min.x&&point.x<=b.max.x&&point.y>=b.min.y&&point.y<=b.max.y&&point.z>=b.min.z&&point.z<=b.max.z?BODY:0;
}
double qa_temporary_model_transformed_point_contents(qa_temporary_collision_model*m,qa_vec3 point,qa_vec3 origin,qa_vec3 angles){
  if(!finite3(origin)||!finite3(angles))qa_range_error("model transform requires finite coordinates");
  qa_vec3 local=qa_sub3(point,origin);
  if(m->kind&&strcmp(m->kind,"capsule")==0){
    qa_axis axis=qa_angles_to_axis(angles);
    local=qa_math_vec3(qa_dot3(local,axis.vectors[0]),qa_dot3(local,axis.vectors[1]),qa_dot3(local,axis.vectors[2]));
  }
  return qa_temporary_model_point_contents(m,local);
}
static qa_source_trace_result do_trace(qa_temporary_collision_model*m,qa_temporary_trace_query query,qa_vec3 start,qa_vec3 end,Prepared shape,int stationary,qa_vec3 model_origin,qa_collision_world*replacement){
  if(!finite3(query.start)||!finite3(query.end)||query.mask!=floor(query.mask))
    qa_range_error("trace requires finite coordinates and integer contents mask");
  m->counters->c_traces=(int32_t)((uint32_t)m->counters->c_traces+1u);
  Work w={1,0,0,{{{0,0,0},0},0,0},0,0}; w.plane=qa_empty_source_trace().plane;
  int is_box=m->kind&&strcmp(m->kind,"box")==0;
  if(is_box)trace_box(&w,qa_temporary_collision_model_bounds(m),start,end,shape,stationary,query.mask,m->counters,m->storage,NULL);
  else if(!shape.capsule){
    Prepared target; memset(&target,0,sizeof target);
    target.center=m->target_center;target.mins=m->target_mins;target.extents=m->target_extents;target.offset=m->target_offset;
    target.radius=m->target_radius;target.halfheight=m->target_halfheight;target.capsule=1;
    if(replacement){
      qa_capsule_replacement_trace prep;
      prep.start=qa_sub3(start,target.center);prep.end=qa_sub3(end,target.center);
      prep.mins=shape.mins;prep.extents=shape.extents;prep.radius=target.radius;prep.offset=target.offset;
      prep.bounds=(qa_bounds){qa_add3(start,shape.mins),qa_add3(start,shape.extents)};
      prep.stationary=stationary!=0;
      prep.point_trace=shape.mins.x==0&&shape.mins.y==0&&shape.mins.z==0;
      return qa_collision_world_trace_capsule_replacement(replacement,query,prep);
    }else if(m->storage){
      qa_bounds retained={qa_add3(start,shape.mins),qa_add3(start,shape.extents)};
      trace_box(&w,m->storage->brush->bounds,qa_sub3(start,target.center),qa_sub3(end,target.center),target,stationary,query.mask,m->counters,m->storage,&retained);
    }else if(stationary){
      qa_vec3 mn=qa_add3(start,shape.mins),mx=qa_add3(start,shape.extents);
      if(((qa_js_to_int32(query.mask))&BODY)!=0&&mn.x<=shape.extents.x&&mn.y<=shape.extents.y&&mn.z<=shape.extents.z&&mx.x>=shape.mins.x&&mx.y>=shape.mins.y&&mx.z>=shape.mins.z){
        w.fraction=0;w.all_solid=1;w.start_solid=1;w.contents=BODY;
      }
    }else trace_box(&w,(qa_bounds){shape.mins,shape.extents},qa_sub3(start,target.center),qa_sub3(end,target.center),target,stationary,query.mask,m->counters,NULL,NULL);
  }else if(stationary){
    Prepared target; memset(&target,0,sizeof target);
    target.center=m->target_center;target.offset=m->target_offset;target.radius=m->target_radius;target.capsule=1;
    position_capsule(&w,target,start,shape);
  }else{
    qa_vec3 smin=qa_math_vec3(qa_js_min(start.x,end.x)-fabs(shape.offset.x)-shape.radius,qa_js_min(start.y,end.y)-fabs(shape.offset.y)-shape.radius,qa_js_min(start.z,end.z)-fabs(shape.offset.z)-shape.radius);
    qa_vec3 smax=qa_math_vec3(qa_js_max(start.x,end.x)+fabs(shape.offset.x)+shape.radius,qa_js_max(start.y,end.y)+fabs(shape.offset.y)+shape.radius,qa_js_max(start.z,end.z)+fabs(shape.offset.z)+shape.radius);
    qa_bounds b=m->bounds;
    if(!(smin.x>f32(b.max.x+1)||smin.y>f32(b.max.y+1)||smin.z>f32(b.max.z+1)||smax.x<f32(b.min.x-1)||smax.y<f32(b.min.y-1)||smax.z<f32(b.min.z-1))){
      double radius=f32(m->target_radius+shape.radius);
      double hh=f32(f32(m->target_halfheight+shape.halfheight)-radius);
      if((start.x!=end.x||start.y!=end.y)&&hh>0)trace_rounded(&w,m->target_center,radius,1,hh,start,end,model_origin);
      trace_rounded(&w,qa_add3(m->target_center,m->target_offset),radius,0,0,qa_sub3(start,shape.offset),qa_sub3(end,shape.offset),model_origin);
      trace_rounded(&w,qa_sub3(m->target_center,m->target_offset),radius,0,0,qa_add3(start,shape.offset),qa_add3(end,shape.offset),model_origin);
    }
  }
  qa_source_trace_result r={w.all_solid!=0,w.start_solid!=0,w.fraction,
    w.fraction==1?query.end:qa_source_trace_end(query.start,query.end,w.fraction),w.plane,w.surface_flags,w.contents};
  return r;
}
static qa_source_trace_result source_(qa_temporary_collision_model*m,qa_temporary_trace_query query,const qa_q3_model_transform*transform,qa_collision_world*replacement){
  qa_vec3 origin=transform?transform->origin:qa_math_vec3(0,0,0);
  qa_vec3 angles=transform?transform->angles:qa_math_vec3(0,0,0);
  if(transform&&(!finite3(origin)||!finite3(angles)))qa_range_error("model transform requires finite coordinates");
  Prepared shape=prepare(query.shape);
  int is_box=m->kind&&strcmp(m->kind,"box")==0;
  int has_axis=!is_box&&(angles.x!=0||angles.y!=0||angles.z!=0);
  qa_axis axis=has_axis?qa_angles_to_axis(angles):(qa_axis){{{1,0,0},{0,1,0},{0,0,1}}};
  if(has_axis)shape.offset=qa_math_vec3(axis.vectors[0].z*shape.offset.z,-axis.vectors[1].z*shape.offset.z,axis.vectors[2].z*shape.offset.z);
  #define ROT(p) (has_axis?qa_math_vec3(qa_dot3((p),axis.vectors[0]),qa_dot3((p),axis.vectors[1]),qa_dot3((p),axis.vectors[2])):(p))
  qa_vec3 cs=qa_add3(query.start,shape.center),ce=qa_add3(query.end,shape.center);
  qa_vec3 start=transform?ROT(qa_sub3(cs,origin)):cs;
  qa_vec3 finish=transform?ROT(qa_sub3(ce,origin)):ce;
  int stationary=transform?(start.x==finish.x&&start.y==finish.y&&start.z==finish.z)
    :(query.start.x==query.end.x&&query.start.y==query.end.y&&query.start.z==query.end.z);
  if(transform){
    qa_vec3 center=qa_scale3(qa_add3(shape.mins,shape.extents),0.5);
    shape.mins=qa_sub3(shape.mins,center); shape.extents=qa_sub3(shape.extents,center);
    start=qa_add3(start,center); finish=qa_add3(finish,center);
  }
  qa_source_trace_result result=do_trace(m,query,start,finish,shape,stationary,origin,replacement);
  if(!transform)return result;
  qa_vec3 end=qa_source_trace_end(query.start,query.end,result.fraction);
  result.end=end;
  if(!has_axis||result.fraction==1)return result;
  qa_vec3 n=result.plane.plane.normal;
  result.plane.plane.normal=qa_math_vec3(
    f32(f32(axis.vectors[0].x*n.x)+f32(axis.vectors[1].x*n.y))+f32(axis.vectors[2].x*n.z),
    f32(f32(axis.vectors[0].y*n.x)+f32(axis.vectors[1].y*n.y))+f32(axis.vectors[2].y*n.z),
    f32(f32(axis.vectors[0].z*n.x)+f32(axis.vectors[1].z*n.y))+f32(axis.vectors[2].z*n.z));
  #undef ROT
  return result;
}
qa_q3_trace_result qa_temporary_model_trace(qa_temporary_collision_model*m,qa_temporary_trace_query q){return qa_source_trace_view(qa_temporary_model_trace_source(m,q));}
qa_source_trace_result qa_temporary_model_trace_source(qa_temporary_collision_model*m,qa_temporary_trace_query q){return source_(m,q,NULL,NULL);}
qa_q3_trace_result qa_temporary_model_transformed_trace(qa_temporary_collision_model*m,qa_temporary_trace_query q,qa_vec3 o,qa_vec3 a){return qa_source_trace_view(qa_temporary_model_transformed_trace_source(m,q,o,a));}
qa_source_trace_result qa_temporary_model_transformed_trace_source(qa_temporary_collision_model*m,qa_temporary_trace_query q,qa_vec3 o,qa_vec3 a){
  qa_q3_model_transform t={o,a}; return source_(m,q,&t,NULL);
}
qa_source_trace_result qa_temporary_model_trace_capsule_replacement(qa_temporary_collision_model*m,qa_temporary_trace_query q,qa_collision_world*world,const qa_q3_model_transform*transform){
  return source_(m,q,transform,world);
}
qa_temporary_collision_model *qa_create_box_model(qa_bounds b,qa_collision_counters*c,qa_collision_box_storage*s){return qa_temporary_collision_model_create("box",b,c,s);}
qa_temporary_collision_model *qa_create_capsule_model(qa_bounds b,qa_collision_counters*c,qa_collision_box_storage*s){return qa_temporary_collision_model_create("capsule",b,c,s);}
