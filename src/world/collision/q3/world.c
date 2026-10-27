/* Port of src/world/collision/q3/world.ts. GPL-2.0-or-later. */
#include "qa/world/collision/q3/world.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/stamp.h"
#include "qa/foundation/support.h"
#include "qa/world/collision/contents.h"
#include "qa/world/collision/q3/clip_models.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static float f32(double v){return (float)v;}
static int finite3(qa_vec3 p){return isfinite(p.x)&&isfinite(p.y)&&isfinite(p.z);}
static int is_int_d(double v){return isfinite(v)&&v==floor(v);}
qa_source_trace_result qa_empty_source_trace(void){
  qa_source_trace_result r={false,false,1,{0,0,0},{{{0,0,0},0},0,0},0,0}; return r;
}
qa_q3_trace_result qa_source_trace_view(qa_source_trace_result s){
  qa_vec3 n=s.plane.plane.normal;
  int has=!(s.all_solid||s.fraction==1||(n.x==0&&n.y==0&&n.z==0));
  qa_q3_trace_result r={s.fraction,s.end,s.all_solid?"all-solid":s.start_solid?"start-solid":"clear",has!=0,s.plane.plane,s.contents,s.surface_flags};
  return r;
}
qa_vec3 qa_source_trace_end(qa_vec3 start,qa_vec3 end,double fraction){
  return qa_math_vec3(start.x+f32(fraction*f32(end.x-start.x)),start.y+f32(fraction*f32(end.y-start.y)),start.z+f32(fraction*f32(end.z-start.z)));
}
double qa_collision_vector_distance_squared(qa_vec3 a,qa_vec3 b){qa_vec3 d=qa_sub3(b,a);return qa_dot3(d,d);}
typedef struct{qa_vec3 center,mins;qa_patch_shape shape;} ShapeInfo;
static ShapeInfo shape_info(qa_q3_trace_shape shape){
  ShapeInfo r; memset(&r,0,sizeof r);
  if(shape.kind&&strcmp(shape.kind,"point")==0){r.shape=(qa_patch_shape){"point",{0,0,0},{0,0,0},0,{0,0,0}};return r;}
  if(!finite3(shape.mins)||!finite3(shape.maxs)||shape.mins.x>shape.maxs.x||shape.mins.y>shape.maxs.y||shape.mins.z>shape.maxs.z)
    qa_range_error("trace shape requires finite ordered bounds");
  r.center=qa_scale3(qa_add3(shape.mins,shape.maxs),0.5);
  r.mins=qa_sub3(shape.mins,r.center);
  qa_vec3 ext=qa_sub3(shape.maxs,r.center);
  if(shape.kind&&strcmp(shape.kind,"capsule")==0){
    double radius=fmin(ext.x,ext.z);
    r.shape=(qa_patch_shape){"capsule",r.mins,ext,radius,{0,0,ext.z-radius}};
  }else r.shape=(qa_patch_shape){"box",r.mins,ext,0,{0,0,0}};
  return r;
}
static double expansion(qa_patch_shape shape,qa_q3_collision_plane*plane){
  if(shape.kind&&strcmp(shape.kind,"point")==0)return 0;
  if(shape.kind&&strcmp(shape.kind,"capsule")==0)return shape.radius+fabs(qa_dot3(plane->plane.normal,shape.offset));
  if(plane->signbits>=8)qa_range_error("CM brush plane signbits outside trace offsets");
  int s=(int)plane->signbits;
  return -qa_dot3(plane->plane.normal,qa_math_vec3((s&1)?shape.extents.x:shape.mins.x,(s&2)?shape.extents.y:shape.mins.y,(s&4)?shape.extents.z:shape.mins.z));
}
static qa_stamp_set *stamps_of(qa_collision_world*w){return (qa_stamp_set*)w->media_stamps;}
void qa_collision_world_init(qa_collision_world*w,qa_collision_map_data*map,qa_collision_world_profile profile,qa_collision_counters*counters){
  memset(w,0,sizeof*w);
  w->map=map;
  int shared=profile.kind&&strcmp(profile.kind,"shared")==0;
  w->debug=shared?profile.owner:NULL; w->settings=shared?profile.settings:NULL;
  if(counters)w->counters=counters; else{qa_collision_counters_init(&w->owned_counters);w->counters=&w->owned_counters;}
  qa_stamp_set*st=malloc(sizeof*st); if(!st)abort();
  if(!qa_stamp_set_init(st,map->brush_count))abort();
  w->media_stamps=st;
  qa_collision_topology_init(&w->topology,map,w->counters);
}
void qa_collision_world_destroy(qa_collision_world*w){
  if(w->source_models){qa_source_clip_models_destroy((qa_source_clip_models*)w->source_models);w->source_models=NULL;}
  if(w->media_stamps){qa_stamp_set_destroy(stamps_of(w));free(w->media_stamps);w->media_stamps=NULL;}
}
double qa_collision_world_area_count(qa_collision_world*w){return w->topology.area_count;}
void qa_collision_world_bind_settings(qa_collision_world*w,qa_collision_map_settings*s){qa_collision_map_settings_register_map(s);w->settings=s;}
double qa_collision_world_cluster_count(qa_collision_world*w){return w->topology.cluster_count;}
double qa_collision_world_model_count(qa_collision_world*w){return (double)w->map->model_count;}
bool qa_collision_world_has_nodes(qa_collision_world*w){return w->map->node_count!=0;}
qa_collision_box_storage *qa_collision_world_box_storage(qa_collision_world*w){return w->map->box;}
void qa_collision_world_advance_check_count(qa_collision_world*w){w->map->check_count=(int32_t)((uint32_t)w->map->check_count+1u);}
static qa_q3_collision_model_rec *model_at(qa_collision_world*w,double idx){
  if(idx<0||(size_t)idx>=w->map->model_count)qa_range_error("collision index %g out of range",idx);
  return &w->map->models[(size_t)idx];
}
qa_bounds qa_collision_world_model_bounds(qa_collision_world*w,double idx){return model_at(w,idx)->bounds;}
double qa_collision_world_point_leafnum(qa_collision_world*w,qa_vec3 p){return qa_topology_point_leafnum(&w->topology,p);}
qa_box_leaf_list qa_collision_world_box_leafnums(qa_collision_world*w,qa_bounds b,double mx){return qa_topology_box_leafnums(&w->topology,b,mx);}
qa_q3_collision_brush **qa_collision_world_box_brushes(qa_collision_world*w,qa_bounds b,double mx,size_t*out){return qa_topology_box_brushes(&w->topology,b,mx,out);}
double qa_collision_world_leaf_area(qa_collision_world*w,double i){return qa_topology_leaf_area(&w->topology,i);}
double qa_collision_world_leaf_cluster(qa_collision_world*w,double i){return qa_topology_leaf_cluster(&w->topology,i);}
qa_source_cluster_pvs qa_collision_world_cluster_pvs(qa_collision_world*w,double c){return qa_topology_cluster_pvs(&w->topology,c);}
bool qa_collision_world_cluster_visible(qa_collision_world*w,double a,double b){return qa_topology_cluster_visible(&w->topology,a,b);}
qa_save_value *qa_collision_world_capture_portal_checkpoint(qa_collision_world*w){return qa_topology_capture_portal_checkpoint(&w->topology);}
void qa_collision_world_restore_portal_checkpoint(qa_collision_world*w,const qa_save_value*v){qa_topology_restore_portal_checkpoint(&w->topology,v);}
void qa_collision_world_adjust_area_portal_state(qa_collision_world*w,double a,double b,bool o){qa_topology_adjust_area_portal_state(&w->topology,a,b,o);}
static int no_areas_flag(qa_collision_world*w){return w->settings?(qa_collision_map_settings_no_areas(w->settings)?1:0):-1;}
bool qa_collision_world_areas_connected(qa_collision_world*w,double a,double b){return qa_topology_areas_connected(&w->topology,a,b,no_areas_flag(w));}
double qa_collision_world_write_area_bits(qa_collision_world*w,uint8_t*buf,size_t len,double area){return qa_topology_write_area_bits(&w->topology,buf,len,area,no_areas_flag(w));}
uint8_t *qa_collision_world_area_bits(qa_collision_world*w,double area,size_t*out){return qa_topology_area_bits(&w->topology,area,no_areas_flag(w),out);}
void qa_collision_world_set_no_areas(qa_collision_world*w,bool e){
  if(w->settings)qa_range_error("Shared collision worlds read cm_noAreas from their common cvars");
  qa_topology_set_no_areas(&w->topology,e);
}
double qa_collision_world_point_contents(qa_collision_world*w,qa_vec3 point,double model_index){
  if(!finite3(point))qa_range_error("point contents requires finite coordinates");
  qa_collision_map_data*map=w->map;
  qa_q3_collision_model_rec*model=model_at(w,model_index);
  qa_collision_indexes brushes;
  qa_q3_collision_leaf leaf={0};
  double leaf_brush_vals[1]={0}; (void)leaf_brush_vals;
  if(model_index!=0)brushes=model->brushes;
  else{
    double li=qa_collision_world_point_leafnum(w,point);
    if(li<0||(size_t)li>=map->leaf_count)qa_range_error("collision index %g out of range",li);
    leaf=map->leaves[(size_t)li];
    brushes=(qa_collision_indexes){map->leaf_brush_values+(size_t)leaf.first_brush,(size_t)leaf.brush_count};
  }
  int32_t contents=0;
  for(size_t i=0;i<brushes.length;i++){
    double bi=brushes.values[i];
    if(bi<0||(size_t)bi>=map->brush_count)qa_range_error("collision index %g out of range",bi);
    qa_q3_collision_brush*brush=&map->brushes[(size_t)bi];
    int inside=1;
    for(double k=0;k<brush->side_count;k++){
      double si=brush->first_side+k;
      double pi=map->brush_sides[(size_t)si].plane;
      qa_q3_collision_plane*plane=&map->planes[(size_t)pi];
      if(qa_dot3(point,plane->plane.normal)>plane->plane.distance){inside=0;break;}
    }
    if(inside)contents|=(int32_t)brush->contents;
  }
  return contents;
}
double qa_collision_world_transformed_point_contents(qa_collision_world*w,qa_vec3 point,double model_index,qa_vec3 origin,qa_vec3 angles){
  qa_vec3 local=qa_sub3(point,origin);
  if(model_index==255||(angles.x==0&&angles.y==0&&angles.z==0))return qa_collision_world_point_contents(w,local,model_index);
  qa_axis axis=qa_angles_to_axis(angles);
  return qa_collision_world_point_contents(w,qa_math_vec3(qa_dot3(local,axis.vectors[0]),qa_dot3(local,axis.vectors[1]),qa_dot3(local,axis.vectors[2])),model_index);
}
typedef struct{
  qa_collision_world*w; qa_q3_trace_query q; qa_patch_shape shape; qa_vec3 size_mins,start,end;
  int stationary,point_trace; const qa_capsule_replacement_trace*replacement;
  double fraction,contents,surface_flags; int all_solid,start_solid;
  qa_source_trace_plane plane; qa_bounds position_bounds; int has_position_bounds;
} Trace;
static void brush_trace(Trace*t,double index){
  qa_collision_world*w=t->w; qa_collision_map_data*map=w->map;
  if(index<0||(size_t)index>=map->brush_count)qa_range_error("collision index %g out of range",index);
  qa_q3_collision_brush*brush=&map->brushes[(size_t)index];
  if(brush->check_count==map->check_count)return;
  brush->check_count=map->check_count;
  double flags=brush->contents;
  if((((int32_t)flags)&((int32_t)t->q.mask))==0||brush->side_count==0)return;
  if(!t->stationary)w->counters->c_brush_traces=(int32_t)((uint32_t)w->counters->c_brush_traces+1u);
  if(t->has_position_bounds){
    qa_bounds b=t->position_bounds,bb=brush->bounds;
    if(b.min.x>bb.max.x||b.min.y>bb.max.y||b.min.z>bb.max.z||b.max.x<bb.min.x||b.max.y<bb.min.y||b.max.z<bb.min.z)return;
  }
  double enter=-1,leave=1; int start_out=0,get_out=0,have_lead=0;
  qa_q3_collision_plane lead_plane={0}; double lead_sf=0;
  int capsule=t->shape.kind&&strcmp(t->shape.kind,"capsule")==0;
  for(double i=t->stationary?6:0;i<brush->side_count;i++){
    double si=brush->first_side+i;
    qa_q3_collision_brush_side*side=&map->brush_sides[(size_t)si];
    qa_q3_collision_plane*plane=&map->planes[(size_t)side->plane];
    double dist=f32(plane->plane.distance+(capsule?t->shape.radius:expansion(t->shape,plane)));
    qa_vec3 first=t->start,last=t->end;
    if(capsule){
      if(qa_dot3(plane->plane.normal,t->shape.offset)>0){first=qa_sub3(t->start,t->shape.offset);last=qa_sub3(t->end,t->shape.offset);}
      else{first=qa_add3(t->start,t->shape.offset);last=qa_add3(t->end,t->shape.offset);}
    }
    double d1=f32(qa_dot3(first,plane->plane.normal)-dist);
    if(t->stationary){if(d1>0)return;continue;}
    double d2=f32(qa_dot3(last,plane->plane.normal)-dist);
    if(d1>0)start_out=1;
    if(d2>0)get_out=1;
    if(d1>0&&(d2>=0.125||d2>=d1))return;
    if(d1<=0&&d2<=0)continue;
    if(d1>d2){
      double f=f32(f32(d1-0.125)/f32(d1-d2)); if(f<0)f=0;
      if(f>enter){enter=f;lead_plane=*plane;lead_sf=side->surface_flags;have_lead=1;}
    }else{
      double f=f32(f32(d1+0.125)/f32(d1-d2)); if(f>1)f=1; if(f<leave)leave=f;
    }
  }
  if(!start_out){
    t->start_solid=1;
    if(!get_out){t->all_solid=1;t->fraction=0;t->contents=flags;}
    return;
  }
  if(enter<leave&&enter>-1&&enter<t->fraction&&have_lead){
    t->fraction=enter<0?0:enter;
    t->plane=(qa_source_trace_plane){lead_plane.plane,lead_plane.type,lead_plane.signbits};
    t->contents=flags;t->surface_flags=lead_sf;
  }
}
static void patch_trace_(Trace*t,double index){
  qa_collision_world*w=t->w; qa_collision_map_data*map=w->map;
  qa_q3_collision_patch_rec*surface=qa_collision_map_patch(map,index);
  if(!surface)return;
  if(surface->check_count==map->check_count)return;
  surface->check_count=map->check_count;
  if((((int32_t)surface->contents)&((int32_t)t->q.mask))==0)return;
  if(t->stationary){
    qa_patch_shape ps=t->shape;
    if(ps.kind&&strcmp(ps.kind,"point")==0)ps.kind="box";
    if(qa_position_in_patch(surface->collide,t->start,ps)){t->all_solid=1;t->start_solid=1;t->fraction=0;t->contents=surface->contents;}
  }else{
    w->counters->c_patch_traces=(int32_t)((uint32_t)w->counters->c_patch_traces+1u);
    int pt=t->replacement==NULL?t->point_trace:t->replacement->point_trace;
    qa_patch_shape ps=t->shape;
    if(pt)ps=(qa_patch_shape){"point",t->size_mins,t->shape.extents,0,{0,0,0}};
    if(ps.kind&&strcmp(ps.kind,"point")==0&&((t->q.has_player_curve_clip&&!t->q.player_curve_clip)||(w->settings&&!qa_collision_map_settings_player_curve_clip(w->settings))))return;
    qa_patch_trace_hit hit;
    if(qa_trace_patch(surface->collide,t->start,t->end,ps,t->fraction,w->debug,&hit)){
      t->fraction=hit.fraction;
      t->plane.plane=hit.plane;
      t->contents=surface->contents;t->surface_flags=surface->surface_flags;
    }
  }
}
static void leaf_trace(Trace*t,double index){
  qa_collision_world*w=t->w; qa_collision_map_data*map=w->map;
  if(index<0||(size_t)index>=map->leaf_count)qa_range_error("collision index %g out of range",index);
  qa_q3_collision_leaf*leaf=&map->leaves[(size_t)index];
  for(double i=0;i<leaf->brush_count;i++){brush_trace(t,qa_collision_indexes_at(map->leaf_brushes,leaf->first_brush+i));if(t->fraction==0)return;}
  if((t->q.has_curves&&!t->q.curves)||(w->settings&&qa_collision_map_settings_no_curves(w->settings)))return;
  for(double i=0;i<leaf->surface_count;i++){patch_trace_(t,qa_collision_indexes_at(map->leaf_surfaces,leaf->first_surface+i));if(t->fraction==0)return;}
}
static void tree_trace(Trace*t,double index,double p1f,double p2f,qa_vec3 p1,qa_vec3 p2){
  if(t->fraction<=p1f)return;
  if(index<0){leaf_trace(t,-1-index);return;}
  qa_collision_map_data*map=t->w->map;
  if((size_t)index>=map->node_count)qa_range_error("collision index %g out of range",index);
  qa_q3_collision_node*node=&map->nodes[(size_t)index];
  qa_q3_collision_plane*plane=&map->planes[(size_t)node->plane];
  double t1=f32((plane->type==0?p1.x:plane->type==1?p1.y:plane->type==2?p1.z:qa_dot3(p1,plane->plane.normal))-plane->plane.distance);
  double t2=f32((plane->type==0?p2.x:plane->type==1?p2.y:plane->type==2?p2.z:qa_dot3(p2,plane->plane.normal))-plane->plane.distance);
  double offset=0;
  int is_point=t->shape.kind&&strcmp(t->shape.kind,"point")==0;
  if(!t->point_trace&&!is_point)
    offset=plane->type==0?t->shape.extents.x:plane->type==1?t->shape.extents.y:plane->type==2?t->shape.extents.z:2048;
  if(t1>=offset+1&&t2>=offset+1){tree_trace(t,node->children[0],p1f,p2f,p1,p2);return;}
  if(t1<-offset-1&&t2<-offset-1){tree_trace(t,node->children[1],p1f,p2f,p1,p2);return;}
  int side=0; double f1=1,f2=0;
  if(t1<t2){side=1;f2=(t1+offset+0.125)/(t1-t2);f1=(t1-offset+0.125)/(t1-t2);}
  else if(t1>t2){f2=(t1-offset-0.125)/(t1-t2);f1=(t1+offset+0.125)/(t1-t2);}
  f1=f32(f1); if(f1<0)f1=0; if(f1>1)f1=1;
  f2=f32(f2); if(f2<0)f2=0; if(f2>1)f2=1;
  tree_trace(t,node->children[side],p1f,f32(p1f+(p2f-p1f)*f1),p1,qa_lerp3(p1,p2,f1));
  tree_trace(t,node->children[side==0?1:0],f32(p1f+(p2f-p1f)*f2),p2f,qa_lerp3(p1,p2,f2),p2);
}
static qa_source_trace_result do_trace(qa_collision_world*w,qa_q3_trace_query query,qa_vec3 start,qa_vec3 end,qa_patch_shape shape,qa_vec3 size_mins,int stationary,const qa_capsule_replacement_trace*replacement){
  if(!finite3(query.start)||!finite3(query.end)||!is_int_d(query.mask))
    qa_range_error("trace requires finite coordinates and integer contents mask");
  qa_collision_map_data*map=w->map;
  double model_index=query.has_model_index?query.model_index:0;
  qa_q3_collision_model_rec*model=model_at(w,model_index);
  Trace t; memset(&t,0,sizeof t);
  t.w=w;t.q=query;t.shape=shape;t.size_mins=size_mins;t.start=start;t.end=end;t.stationary=stationary;t.replacement=replacement;
  t.fraction=1;t.plane=qa_empty_source_trace().plane;
  if(replacement==NULL){qa_collision_world_advance_check_count(w);w->counters->c_traces=(int32_t)((uint32_t)w->counters->c_traces+1u);}
  t.point_trace=!stationary&&size_mins.x==0&&size_mins.y==0&&size_mins.z==0;
  if(stationary){
    t.has_position_bounds=1;
    if(replacement)t.position_bounds=replacement->bounds;
    else if(shape.kind&&strcmp(shape.kind,"capsule")==0){
      t.position_bounds=(qa_bounds){
        {start.x-fabs(shape.offset.x)-shape.radius,start.y-fabs(shape.offset.y)-shape.radius,start.z-fabs(shape.offset.z)-shape.radius},
        {start.x+fabs(shape.offset.x)+shape.radius,start.y+fabs(shape.offset.y)+shape.radius,start.z+fabs(shape.offset.z)+shape.radius}};
    }else{
      qa_vec3 ext=(shape.kind&&strcmp(shape.kind,"point")==0)?qa_math_vec3(0,0,0):shape.extents;
      t.position_bounds=(qa_bounds){qa_add3(start,size_mins),qa_add3(start,ext)};
    }
  }
  if(model_index!=0){
    for(size_t i=0;i<model->brushes.length&&t.fraction!=0;i++)brush_trace(&t,qa_collision_indexes_at(model->brushes,(double)i));
    if(t.fraction!=0&&!(query.has_curves&&!query.curves)&&(w->settings==NULL||!qa_collision_map_settings_no_curves(w->settings)))
      for(size_t i=0;i<model->surfaces.length&&t.fraction!=0;i++)patch_trace_(&t,qa_collision_indexes_at(model->surfaces,(double)i));
  }else if(stationary){
    qa_vec3 ext=(shape.kind&&strcmp(shape.kind,"point")==0)?qa_math_vec3(0,0,0):shape.extents;
    qa_vec3 one={1,1,1};
    qa_bounds lb={qa_sub3(qa_add3(start,size_mins),one),qa_add3(qa_add3(start,ext),one)};
    qa_box_leaf_list leaves=qa_topology_box_leafnums(&w->topology,lb,1024);
    qa_collision_world_advance_check_count(w);
    for(size_t i=0;i<leaves.leaf_count;i++){leaf_trace(&t,leaves.leaves[i]);if(t.all_solid)break;}
    qa_box_leaf_list_destroy(&leaves);
  }else tree_trace(&t,map->node_count==0?-1:0,0,1,start,end);
  qa_source_trace_result r={t.all_solid!=0,t.start_solid!=0,t.fraction,
    t.fraction==1?query.end:qa_source_trace_end(query.start,query.end,t.fraction),t.plane,t.surface_flags,t.contents};
  return r;
}
void *qa_collision_world_source_clip_models(qa_collision_world*w){
  if(!w->source_models)w->source_models=qa_source_clip_models_create(w,"world");
  return w->source_models;
}
qa_q3_trace_result qa_collision_world_trace(qa_collision_world*w,qa_q3_trace_query q){return qa_source_trace_view(qa_collision_world_trace_source(w,q));}
qa_source_trace_result qa_collision_world_trace_source(qa_collision_world*w,qa_q3_trace_query query){
  if(query.has_model_index&&query.model_index==254)
    return qa_source_clip_trace((qa_source_clip_models*)qa_collision_world_source_clip_models(w),query,254);
  ShapeInfo info=shape_info(query.shape);
  int stationary=query.start.x==query.end.x&&query.start.y==query.end.y&&query.start.z==query.end.z;
  return do_trace(w,query,qa_add3(query.start,info.center),qa_add3(query.end,info.center),info.shape,info.mins,stationary,NULL);
}
qa_source_trace_result qa_collision_world_trace_capsule_replacement(qa_collision_world*w,qa_q3_trace_query query,qa_capsule_replacement_trace prepared){
  qa_patch_shape shape={"capsule",prepared.mins,prepared.extents,prepared.radius,prepared.offset};
  qa_q3_trace_query q=query; q.has_model_index=true; q.model_index=255;
  return do_trace(w,q,prepared.start,prepared.end,shape,prepared.mins,prepared.stationary,&prepared);
}
qa_q3_trace_result qa_collision_world_transformed_trace(qa_collision_world*w,qa_q3_trace_query q,qa_vec3 o,qa_vec3 a){return qa_source_trace_view(qa_collision_world_transformed_trace_source(w,q,o,a));}
qa_source_trace_result qa_collision_world_transformed_trace_source(qa_collision_world*w,qa_q3_trace_query query,qa_vec3 origin,qa_vec3 angles){
  double mi=query.has_model_index?query.model_index:0;
  if(query.has_model_index&&query.model_index==254)
    return qa_source_clip_transformed_trace((qa_source_clip_models*)qa_collision_world_source_clip_models(w),query,254,origin,angles);
  if(!finite3(origin)||(mi!=255&&!finite3(angles)))qa_range_error("model transform requires finite coordinates");
  ShapeInfo info=shape_info(query.shape);
  int has_axis=mi!=255&&(angles.x!=0||angles.y!=0||angles.z!=0);
  qa_axis axis=has_axis?qa_angles_to_axis(angles):(qa_axis){{{1,0,0},{0,1,0},{0,0,1}}};
  #define ROT(p) (has_axis?qa_math_vec3(qa_dot3((p),axis.vectors[0]),qa_dot3((p),axis.vectors[1]),qa_dot3((p),axis.vectors[2])):(p))
  qa_vec3 start=ROT(qa_sub3(qa_add3(query.start,info.center),origin));
  qa_vec3 finish=ROT(qa_sub3(qa_add3(query.end,info.center),origin));
  int stationary=start.x==finish.x&&start.y==finish.y&&start.z==finish.z;
  qa_vec3 first_ext=info.shape.extents;
  qa_vec3 center=qa_scale3(qa_add3(info.mins,first_ext),0.5);
  qa_vec3 mins=qa_sub3(info.mins,center);
  qa_patch_shape shape=info.shape;
  if(shape.kind&&strcmp(shape.kind,"capsule")==0){
    qa_vec3 off=has_axis?qa_math_vec3(axis.vectors[0].z*shape.offset.z,-axis.vectors[1].z*shape.offset.z,axis.vectors[2].z*shape.offset.z):shape.offset;
    shape.extents=qa_sub3(first_ext,center); shape.offset=off;
  }else if(shape.kind&&strcmp(shape.kind,"box")==0){shape.mins=mins;shape.extents=qa_sub3(first_ext,center);}
  qa_source_trace_result result=do_trace(w,query,qa_add3(start,center),qa_add3(finish,center),shape,mins,stationary,NULL);
  qa_vec3 end=qa_source_trace_end(query.start,query.end,result.fraction);
  result.end=end;
  if(result.fraction==1||!has_axis)return result;
  qa_vec3 n=result.plane.plane.normal;
  result.plane.plane.normal=qa_add3(qa_add3(qa_scale3(axis.vectors[0],n.x),qa_scale3(axis.vectors[1],n.y)),qa_scale3(axis.vectors[2],n.z));
  #undef ROT
  return result;
}
typedef struct{qa_collision_world*w;qa_vec3 start,end;qa_patch_shape shape;} MediaCtx;
static double media_dist(void*ctx,qa_bsp_plane plane,int endpoint){
  MediaCtx*c=ctx;
  qa_vec3 point=endpoint==0?c->start:c->end;
  int capsule=c->shape.kind&&strcmp(c->shape.kind,"capsule")==0;
  double exp_=0;
  if(capsule)exp_=c->shape.radius;
  else if(c->shape.kind&&strcmp(c->shape.kind,"point")!=0){
    int s=(int)plane.signbits;
    if(s>=8)qa_range_error("CM brush plane signbits outside trace offsets");
    exp_=-qa_dot3(plane.plane.normal,qa_math_vec3((s&1)?c->shape.extents.x:c->shape.mins.x,(s&2)?c->shape.extents.y:c->shape.mins.y,(s&4)?c->shape.extents.z:c->shape.mins.z));
  }
  double dist=f32(plane.plane.distance+exp_);
  if(capsule)point=qa_dot3(plane.plane.normal,c->shape.offset)>0?qa_sub3(point,c->shape.offset):qa_add3(point,c->shape.offset);
  return f32(qa_dot3(point,plane.plane.normal)-dist);
}
typedef struct{qa_collision_world*w;double*v;size_t n,cap;} CandCtx;
static void cand_leaf(void*ctx,double leafnum){
  CandCtx*c=ctx; qa_collision_map_data*map=c->w->map;
  qa_q3_collision_leaf*leaf=&map->leaves[(size_t)leafnum];
  for(double i=0;i<leaf->brush_count;i++){
    if(c->n==c->cap){c->cap=c->cap?c->cap*2:16;c->v=realloc(c->v,c->cap*sizeof*c->v);if(!c->v)abort();}
    c->v[c->n++]=qa_collision_indexes_at(map->leaf_brushes,leaf->first_brush+i);
  }
}
qa_trace_media qa_collision_world_trace_media(qa_collision_world*w,qa_q3_trace_query query,double fraction,const qa_q3_trace_transform*transform){
  ShapeInfo info=shape_info(query.shape);
  double model_index=query.has_model_index?query.model_index:0;
  int has_axis=transform&&model_index!=255;
  qa_axis axis=has_axis?qa_angles_to_axis(transform->angles):(qa_axis){{{1,0,0},{0,1,0},{0,0,1}}};
  #define ROT2(p) (has_axis?qa_math_vec3(qa_dot3((p),axis.vectors[0]),qa_dot3((p),axis.vectors[1]),qa_dot3((p),axis.vectors[2])):(p))
  qa_vec3 start=transform?ROT2(qa_sub3(qa_add3(query.start,info.center),transform->origin)):qa_add3(query.start,info.center);
  qa_vec3 end=transform?ROT2(qa_sub3(qa_add3(query.end,info.center),transform->origin)):qa_add3(query.end,info.center);
  qa_patch_shape shape=info.shape; qa_vec3 mins=info.mins;
  if(transform){
    qa_vec3 ext=shape.extents,center=qa_scale3(qa_add3(mins,ext),0.5);
    mins=qa_sub3(mins,center); start=qa_add3(start,center); end=qa_add3(end,center);
    if(shape.kind&&strcmp(shape.kind,"capsule")==0){
      shape.extents=qa_sub3(ext,center);
      if(has_axis)shape.offset=qa_math_vec3(axis.vectors[0].z*shape.offset.z,-axis.vectors[1].z*shape.offset.z,axis.vectors[2].z*shape.offset.z);
    }else if(shape.kind&&strcmp(shape.kind,"box")==0){shape.mins=mins;shape.extents=qa_sub3(ext,center);}
  }
  int capsule=shape.kind&&strcmp(shape.kind,"capsule")==0;
  qa_vec3 size_max=capsule?qa_math_vec3(fabs(shape.offset.x)+shape.radius,fabs(shape.offset.y)+shape.radius,fabs(shape.offset.z)+shape.radius):shape.extents;
  qa_vec3 size_min=capsule?qa_scale3(size_max,-1):mins;
  qa_vec3 reached=qa_source_trace_end(start,end,fraction);
  qa_bounds env={{fmin(start.x,reached.x)+size_min.x-1,fmin(start.y,reached.y)+size_min.y-1,fmin(start.z,reached.z)+size_min.z-1},
    {fmax(start.x,reached.x)+size_max.x+1,fmax(start.y,reached.y)+size_max.y+1,fmax(start.z,reached.z)+size_max.z+1}};
  qa_collision_map_data*map=w->map;
  CandCtx cand={w,NULL,0,0};
  if(model_index!=0){
    qa_q3_collision_model_rec*model=model_at(w,model_index);
    for(size_t i=0;i<model->brushes.length;i++){if(cand.n==cand.cap){cand.cap=cand.cap?cand.cap*2:16;cand.v=realloc(cand.v,cand.cap*sizeof*cand.v);if(!cand.v)abort();}cand.v[cand.n++]=model->brushes.values[i];}
  }else{
    double top=0; (void)qa_topology_visit_leaves(&w->topology,env,cand_leaf,&cand,&top);
  }
  qa_stamp_set*seen=stamps_of(w); qa_stamp_set_begin(seen);
  qa_medium_brush*brushes=malloc((cand.n?cand.n:1)*sizeof*brushes); if(!brushes)abort();
  qa_bsp_plane**plane_store=malloc((cand.n?cand.n:1)*sizeof*plane_store); if(!plane_store)abort();
  size_t bn=0;
  for(size_t i=0;i<cand.n;i++){
    double bi=cand.v[i];
    if(!qa_stamp_set_mark(seen,(size_t)bi))continue;
    qa_q3_collision_brush*brush=&map->brushes[(size_t)bi];
    if(qa_convert_contents(brush->contents,"q3","q1")==-1)continue;
    size_t sc=(size_t)brush->side_count;
    qa_bsp_plane*pls=malloc((sc?sc:1)*sizeof*pls); if(!pls)abort();
    for(size_t k=0;k<sc;k++){
      double pi=map->brush_sides[(size_t)brush->first_side+k].plane;
      qa_q3_collision_plane*cp=&map->planes[(size_t)pi];
      pls[k]=(qa_bsp_plane){cp->plane,cp->type,cp->signbits};
    }
    plane_store[bn]=pls;
    brushes[bn]=(qa_medium_brush){brush->contents,pls,sc}; bn++;
  }
  MediaCtx mc={w,start,end,shape};
  const qa_numeric_operations*numeric=qa_create_numeric_operations(&qa_q3_binary32_profile);
  qa_trace_media r=qa_trace_brush_media(brushes,bn,"q3",media_dist,&mc,numeric,fraction);
  for(size_t i=0;i<bn;i++)free(plane_store[i]);
  free(plane_store);free(brushes);free(cand.v);
  #undef ROT2
  return r;
}
