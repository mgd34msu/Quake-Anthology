/* Port of src/world/collision/q3/topology.ts. GPL-2.0-or-later. */
#include "qa/world/collision/q3/topology.h"
#include "qa/core/common_error.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static float f32(double v){return (float)v;}
static void *xm(size_t n){void*p=malloc(n?n:1);if(!p)abort();return p;}
static int finite3(qa_vec3 p){return isfinite(p.x)&&isfinite(p.y)&&isfinite(p.z);}
static int is_int(double v){return isfinite(v)&&v==floor(v);}
static int is_safe(double v){return is_int(v)&&v>=-9007199254740991.0&&v<=9007199254740991.0;}
static qa_q3_collision_leaf *leaf_at(qa_collision_topology*t,double idx){
  if(idx<0||(size_t)idx>=t->map->leaf_count)qa_range_error("collision topology index %g out of range",idx);
  return &t->map->leaves[(size_t)idx];
}
void qa_box_leaf_list_destroy(qa_box_leaf_list*l){free(l->leaves);l->leaves=NULL;l->leaf_count=0;}
static void flood_areas(qa_collision_topology*t){
  qa_collision_map_data*m=t->map;
  t->flood_valid=(int32_t)((uint32_t)t->flood_valid+1u);
  int32_t flood=0;
  size_t ac=(size_t)t->area_count;
  double*stack=xm((ac?ac:1)*sizeof*stack);
  for(size_t area=0;area<ac;area++){
    if(m->areas[area].flood_valid==t->flood_valid)continue;
    flood++;
    size_t sp=0; stack[sp++]=(double)area;
    while(sp>0){
      size_t cur=(size_t)stack[--sp];
      qa_q3_collision_area*cell=&m->areas[cur];
      if(cell->flood_valid==t->flood_valid){
        if(cell->flood!=flood)qa_common_error(QA_COMMON_ERROR_DROP,"FloodArea_r: reflooded");
        continue;
      }
      cell->flood=flood; cell->flood_valid=t->flood_valid;
      for(long adj=(long)ac-1;adj>=0;adj--){
        if(qa_collision_portals_at(m,(double)(cur*ac+(size_t)adj))>0){
          if(sp>=ac){/* stack may hold duplicates; grow */
            stack=realloc(stack,(sp+1)*sizeof*stack); if(!stack)abort();}
          stack[sp++]=(double)adj;
        }
      }
    }
  }
  free(stack);
}
void qa_collision_topology_init(qa_collision_topology*t,qa_collision_map_data*map,qa_collision_counters*counters){
  memset(t,0,sizeof*t);
  t->map=map; t->area_count=(double)map->area_count; t->cluster_count=map->cluster_count;
  t->has_visibility_row_bytes=map->has_visibility_row_bytes; t->visibility_row_bytes=map->visibility_row_bytes;
  if(counters)t->counters=counters; else{qa_collision_counters_init(&t->owned_counters);t->counters=&t->owned_counters;}
  flood_areas(t);
}
qa_save_value *qa_topology_capture_portal_checkpoint(qa_collision_topology*t){
  qa_save_value*root=qa_save_value_object(); if(!root)abort();
  qa_save_value_set(root,"version",qa_save_value_number(1));
  qa_save_value_set(root,"floodValid",qa_save_value_number(t->flood_valid));
  qa_save_value_set(root,"noAreas",qa_save_value_boolean(t->no_areas));
  qa_save_value*areas=qa_save_value_array();
  for(size_t i=0;i<t->map->area_count;i++){
    qa_save_value*a=qa_save_value_object();
    qa_save_value_set(a,"flood",qa_save_value_number(t->map->areas[i].flood));
    qa_save_value_set(a,"floodValid",qa_save_value_number(t->map->areas[i].flood_valid));
    qa_save_value_push(areas,a);
  }
  qa_save_value_set(root,"areas",areas);
  qa_save_value*portals=qa_save_value_array();
  size_t total=(size_t)t->area_count*(size_t)t->area_count;
  for(size_t i=0;i<total;i++)qa_save_value_push(portals,qa_save_value_number(qa_collision_portals_at(t->map,(double)i)));
  qa_save_value_set(root,"portals",portals);
  return root;
}
static double word_(qa_save_reader*r){
  double v=qa_save_reader_integer_min(r,-2147483648.0);
  if(v>2147483647.0)qa_save_format_error(r->path,"expected a signed 32-bit source word");
  return v;
}
void qa_topology_restore_portal_checkpoint(qa_collision_topology*t,const qa_save_value*value){
  qa_save_reader r; qa_save_reader_init(&r,value,"q3.collision.portals");
  qa_save_reader f;
  qa_save_reader_field(&r,"version",&f);
  /* literal(1) */
  { double v=qa_save_reader_number(&f); if(v!=1)qa_save_format_error(f.path,"expected 1"); }
  qa_save_reader_destroy(&f);
  qa_save_reader_field(&r,"floodValid",&f); double flood_valid=word_(&f); qa_save_reader_destroy(&f);
  qa_save_reader_field(&r,"noAreas",&f); bool no_areas=qa_save_reader_boolean(&f); qa_save_reader_destroy(&f);
  qa_save_reader_field(&r,"areas",&f);
  size_t ac=qa_save_reader_list_count(&f);
  double*af=xm((ac?ac:1)*sizeof*af),*afv=xm((ac?ac:1)*sizeof*afv);
  for(size_t i=0;i<ac;i++){
    qa_save_reader item,sub; qa_save_reader_item(&f,i,&item);
    qa_save_reader_field(&item,"flood",&sub); af[i]=word_(&sub); qa_save_reader_destroy(&sub);
    qa_save_reader_field(&item,"floodValid",&sub); afv[i]=word_(&sub); qa_save_reader_destroy(&sub);
    qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&f);
  qa_save_reader_field(&r,"portals",&f);
  size_t pc=qa_save_reader_list_count(&f);
  double*pv=xm((pc?pc:1)*sizeof*pv);
  for(size_t i=0;i<pc;i++){qa_save_reader item;qa_save_reader_item(&f,i,&item);pv[i]=word_(&item);qa_save_reader_destroy(&item);}
  qa_save_reader_destroy(&f);
  size_t area_count=(size_t)t->area_count;
  if(ac!=area_count||pc!=area_count*area_count)qa_save_format_error(r.path,"portal checkpoint belongs to another area table");
  for(size_t a=0;a<area_count;a++)for(size_t b=a+1;b<area_count;b++)
    if(pv[a*area_count+b]!=pv[b*area_count+a])qa_save_format_error(r.path,"asymmetric source portal references");
  for(size_t i=0;i<ac;i++){t->map->areas[i].flood=(int32_t)af[i];t->map->areas[i].flood_valid=(int32_t)afv[i];}
  for(size_t i=0;i<pc;i++)qa_collision_portals_set(t->map,(double)i,pv[i]);
  t->flood_valid=(int32_t)flood_valid; t->no_areas=no_areas;
  free(af);free(afv);free(pv);
  qa_save_reader_destroy(&r);
}
double qa_topology_point_leafnum(qa_collision_topology*t,qa_vec3 point){
  if(!finite3(point))qa_range_error("point leaf query requires finite coordinates");
  qa_collision_map_data*m=t->map;
  if(m->node_count==0)return 0;
  double idx=0;
  while(idx>=0){
    if((size_t)idx>=m->node_count)qa_range_error("collision topology index %g out of range",idx);
    qa_q3_collision_node*node=&m->nodes[(size_t)idx];
    if(node->plane<0||(size_t)node->plane>=m->plane_count)qa_range_error("collision topology index %g out of range",node->plane);
    qa_q3_collision_plane*plane=&m->planes[(size_t)node->plane];
    double dist=f32((plane->type==0?point.x:plane->type==1?point.y:plane->type==2?point.z:qa_dot3(point,plane->plane.normal))-plane->plane.distance);
    idx=node->children[dist<0?1:0];
  }
  t->counters->c_pointcontents=(int32_t)((uint32_t)t->counters->c_pointcontents+1u);
  return -1-idx;
}
static int box_side(qa_bounds b,qa_q3_collision_plane*plane){
  qa_vec3 n=plane->plane.normal;
  if(plane->type==0)return plane->plane.distance<=b.min.x?1:plane->plane.distance>=b.max.x?2:3;
  if(plane->type==1)return plane->plane.distance<=b.min.y?1:plane->plane.distance>=b.max.y?2:3;
  if(plane->type==2)return plane->plane.distance<=b.min.z?1:plane->plane.distance>=b.max.z?2:3;
  double signs=plane->signbits; int s=(int)signs;
  double far=signs<8?qa_dot3(n,(qa_vec3){(s&1)?b.min.x:b.max.x,(s&2)?b.min.y:b.max.y,(s&4)?b.min.z:b.max.z}):0;
  double near=signs<8?qa_dot3(n,(qa_vec3){(s&1)?b.max.x:b.min.x,(s&2)?b.max.y:b.min.y,(s&4)?b.max.z:b.min.z}):0;
  return far>=plane->plane.distance?(near<plane->plane.distance?3:1):(near<plane->plane.distance?2:0);
}
bool qa_topology_visit_leaves(qa_collision_topology*t,qa_bounds bounds,qa_store_leaf_fn store,void*ctx,double*out_topnode){
  qa_collision_map_data*m=t->map;
  int has_top=0; double topnode=0;
  size_t cap=64,sp=0; double*stack=xm(cap*sizeof*stack);
  stack[sp++]=m->node_count==0?-1:0;
  while(sp>0){
    double idx=stack[--sp];
    if(idx<0){store(ctx,-1-idx);continue;}
    if((size_t)idx>=m->node_count)qa_range_error("collision topology index %g out of range",idx);
    qa_q3_collision_node*node=&m->nodes[(size_t)idx];
    if(node->plane<0||(size_t)node->plane>=m->plane_count)qa_range_error("collision topology index %g out of range",node->plane);
    int side=box_side(bounds,&m->planes[(size_t)node->plane]);
    if(side!=1&&side!=2&&!has_top){has_top=1;topnode=idx;}
    if(sp+2>cap){cap*=2;stack=realloc(stack,cap*sizeof*stack);if(!stack)abort();}
    if(side!=1)stack[sp++]=node->children[1];
    if(side!=2)stack[sp++]=node->children[0];
  }
  free(stack);
  if(has_top&&out_topnode)*out_topnode=topnode;
  return has_top!=0;
}
typedef struct{qa_collision_topology*t;double*v;size_t n,cap;double maxl,last;int overflow;} LeafCtx;
static void store_leaf(void*ctx,double leafnum){
  LeafCtx*c=ctx;
  if(leaf_at(c->t,leafnum)->cluster!=-1)c->last=leafnum;
  if((double)c->n==c->maxl)c->overflow=1;
  else{if(c->n==c->cap){c->cap=c->cap?c->cap*2:16;c->v=realloc(c->v,c->cap*sizeof*c->v);if(!c->v)abort();}c->v[c->n++]=leafnum;}
}
qa_box_leaf_list qa_topology_box_leafnums(qa_collision_topology*t,qa_bounds bounds,double max_leaves){
  if(!finite3(bounds.min)||!finite3(bounds.max)||bounds.min.x>bounds.max.x||bounds.min.y>bounds.max.y||bounds.min.z>bounds.max.z)
    qa_range_error("box leaf query requires finite ordered bounds");
  if(!is_safe(max_leaves)||max_leaves<0)qa_range_error("leaf capacity must be a nonnegative integer");
  t->map->check_count=(int32_t)((uint32_t)t->map->check_count+1u);
  LeafCtx c={t,NULL,0,0,max_leaves,0,0};
  double top=0; bool has=qa_topology_visit_leaves(t,bounds,store_leaf,&c,&top);
  qa_box_leaf_list r={c.v,c.n,has,top,c.last,c.overflow!=0}; return r;
}
typedef struct{qa_collision_topology*t;qa_bounds q;qa_q3_collision_brush**v;size_t n,cap;double maxb;} BrushCtx;
static void store_brushes(void*ctx,double leafnum){
  BrushCtx*c=ctx; qa_collision_map_data*m=c->t->map;
  qa_q3_collision_leaf*leaf=leaf_at(c->t,leafnum);
  for(double i=0;i<leaf->brush_count;i++){
    double bi=qa_collision_indexes_at(m->leaf_brushes,leaf->first_brush+i);
    if(bi<0||(size_t)bi>=m->brush_count)qa_range_error("collision topology index %g out of range",bi);
    qa_q3_collision_brush*brush=&m->brushes[(size_t)bi];
    if(brush->check_count==m->check_count)continue;
    brush->check_count=m->check_count;
    qa_bounds bb=brush->bounds;
    if(bb.min.x>=c->q.max.x||bb.max.x<=c->q.min.x||bb.min.y>=c->q.max.y||bb.max.y<=c->q.min.y||bb.min.z>=c->q.max.z||bb.max.z<=c->q.min.z)continue;
    if((double)c->n>=c->maxb)return;
    if(c->n==c->cap){c->cap=c->cap?c->cap*2:16;c->v=realloc(c->v,c->cap*sizeof*c->v);if(!c->v)abort();}
    c->v[c->n++]=brush;
  }
}
qa_q3_collision_brush **qa_topology_box_brushes(qa_collision_topology*t,qa_bounds bounds,double max_brushes,size_t*out){
  if(!is_int(max_brushes)||max_brushes<-2147483648.0||max_brushes>2147483647.0)qa_range_error("brush capacity must be a source signed integer");
  t->map->check_count=(int32_t)((uint32_t)t->map->check_count+1u);
  BrushCtx c={t,bounds,NULL,0,0,max_brushes};
  double top=0; (void)qa_topology_visit_leaves(t,bounds,store_brushes,&c,&top);
  *out=c.n; return c.v;
}
double qa_topology_leaf_area(qa_collision_topology*t,double idx){
  if(is_int(idx)&&(idx<0||(size_t)idx>=t->map->leaf_count))qa_common_error(QA_COMMON_ERROR_DROP,"CM_LeafArea: bad number");
  return leaf_at(t,idx)->area;
}
double qa_topology_leaf_cluster(qa_collision_topology*t,double idx){
  if(is_int(idx)&&(idx<0||(size_t)idx>=t->map->leaf_count))qa_common_error(QA_COMMON_ERROR_DROP,"CM_LeafCluster: bad number");
  return leaf_at(t,idx)->cluster;
}
qa_source_cluster_pvs qa_topology_cluster_pvs(qa_collision_topology*t,double cluster){
  if(!is_int(cluster))qa_range_error("cluster must be an integer");
  int64_t base=(!t->has_visibility_row_bytes||cluster<0||cluster>=t->cluster_count)?0:(int64_t)(cluster*t->visibility_row_bytes);
  qa_source_cluster_pvs r={t->map,base}; return r;
}
double qa_source_cluster_pvs_byte_at(qa_source_cluster_pvs pvs,double offset){
  if(!is_safe(offset))qa_range_error("PVS byte offset must be a safe integer");
  int64_t idx=pvs.base_offset+(int64_t)offset;
  if(idx<0||(size_t)idx>=pvs.map->visibility_length)
    qa_range_error("PVS byte %lld outside stored visibility allocation of %zu bytes",(long long)idx,pvs.map->visibility_length);
  return pvs.map->visibility[idx];
}
bool qa_topology_cluster_visible(qa_collision_topology*t,double from,double to){
  if(!is_int(from)||!is_int(to))qa_range_error("clusters must be integers");
  if(to<0||to>=t->cluster_count)return false;
  double byte=qa_source_cluster_pvs_byte_at(qa_topology_cluster_pvs(t,from),(double)((int64_t)to>>3));
  return ((int)byte&(1<<((int64_t)to&7)))!=0;
}
void qa_topology_set_no_areas(qa_collision_topology*t,bool e){t->no_areas=e;}
static void check_area(qa_collision_topology*t,double area){
  if(!is_int(area)||area<0||area>=t->area_count)qa_range_error("invalid area %g",area);
}
void qa_topology_adjust_area_portal_state(qa_collision_topology*t,double a1,double a2,bool open){
  if(!is_int(a1)||!is_int(a2))qa_range_error("areas must be integers");
  if(a1<0||a2<0)return;
  if(a1>=t->area_count||a2>=t->area_count)qa_common_error(QA_COMMON_ERROR_DROP,"CM_ChangeAreaPortalState: bad area number");
  double i1=a1*t->area_count+a2,i2=a2*t->area_count+a1;
  double amt=open?1:-1;
  qa_collision_portals_set(t->map,i1,(double)(int32_t)(qa_collision_portals_at(t->map,i1)+amt));
  qa_collision_portals_set(t->map,i2,(double)(int32_t)(qa_collision_portals_at(t->map,i2)+amt));
  if(!open&&qa_collision_portals_at(t->map,i2)<0)qa_common_error(QA_COMMON_ERROR_DROP,"CM_AdjustAreaPortalState: negative reference count");
  flood_areas(t);
}
bool qa_topology_areas_connected(qa_collision_topology*t,double a1,double a2,int no_areas){
  int na=no_areas<0?(t->no_areas?1:0):no_areas;
  if(na)return true;
  if(!is_int(a1)||!is_int(a2))qa_range_error("areas must be integers");
  if(a1<0||a2<0)return false;
  if(a1>=t->area_count||a2>=t->area_count)qa_common_error(QA_COMMON_ERROR_DROP,"area >= cm.numAreas");
  return t->map->areas[(size_t)a1].flood==t->map->areas[(size_t)a2].flood;
}
double qa_topology_write_area_bits(qa_collision_topology*t,uint8_t*buffer,size_t len,double area,int no_areas){
  size_t bytes=(size_t)(((int64_t)t->area_count+7)>>3);
  if(len<bytes)qa_range_error("area bits need %zu bytes",bytes);
  int na=no_areas<0?(t->no_areas?1:0):no_areas;
  if(na||area==-1){memset(buffer,255,bytes);return (double)bytes;}
  check_area(t,area);
  int32_t flood=t->map->areas[(size_t)area].flood;
  for(size_t o=0;o<(size_t)t->area_count;o++){
    if(t->map->areas[o].flood!=flood)continue;
    buffer[o>>3]=(uint8_t)(buffer[o>>3]|(1u<<(o&7)));
  }
  return (double)bytes;
}
uint8_t *qa_topology_area_bits(qa_collision_topology*t,double area,int no_areas,size_t*out){
  size_t bytes=(size_t)(((int64_t)t->area_count+7)>>3);
  uint8_t*bits=xm(bytes?bytes:1); memset(bits,0,bytes);
  (void)qa_topology_write_area_bits(t,bits,bytes,area,no_areas);
  *out=bytes; return bits;
}
