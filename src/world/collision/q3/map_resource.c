/* Port of src/world/collision/q3/map-resource.ts. Records stored as
 * owned arrays; values follow CMod_Load* exactly. GPL-2.0-or-later. */
#include "qa/world/collision/q3/map_resource.h"
#include "qa/network/common/js_number.h"
#include "qa/core/binary/index.h"
#include "qa/core/common_error.h"
#include "qa/formats/q3_map/ibsp44.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
static void *xm(size_t n){void*p=malloc(n?n:1);if(!p)abort();return p;}
static void *xz(size_t n,size_t s){void*p=calloc(n?n:1,s);if(!p)abort();return p;}
static char *xdup(const char*s){if(!s)s="";size_t n=strlen(s)+1;char*r=xm(n);memcpy(r,s,n);return r;}
double qa_collision_indexes_at(qa_collision_indexes ix,double idx){
  /* diagnosticIndexes.at -> at(values, index) */
  if(!(idx>=0&&idx<(double)ix.length&&idx==floor(idx)))qa_range_error("CM source record %s outside allocation",QA_JS_NUMBER(idx));
  return ix.values[(size_t)idx];
}
void qa_q3_collision_box_model_init(qa_q3_collision_box_model*m){m->bounds=(qa_bounds){{0,0,0},{0,0,0}};}
double qa_collision_portals_at(const qa_collision_map_data*m,double idx){
  /* indexOffset(index, 4, portals.length) */
  if(!m->portals_loaded||!(idx>=0&&idx<(double)m->portal_count&&idx==floor(idx)))qa_range_error("CM source index %s outside allocation of %zu records",QA_JS_NUMBER(idx),m->portals_loaded?m->portal_count:(size_t)0);
  return m->portals[(size_t)idx];
}
void qa_collision_portals_set(qa_collision_map_data*m,double idx,double v){
  if(!m->portals_loaded||!(idx>=0&&idx<(double)m->portal_count&&idx==floor(idx)))qa_range_error("CM source index %s outside allocation of %zu records",QA_JS_NUMBER(idx),m->portals_loaded?m->portal_count:(size_t)0);
  m->portals[(size_t)idx]=qa_js_to_int32(v); /* Int32Array store */
}
qa_q3_collision_patch_rec *qa_collision_map_patch(qa_collision_map_data*m,double idx){
  if(!m->surfaces_loaded)qa_range_error("CM surfaces have not been loaded");
  if(!(idx>=0&&idx<(double)m->patch_count&&idx==floor(idx)))qa_range_error("CM source record %s outside allocation",QA_JS_NUMBER(idx));
  return m->patches[(size_t)idx];
}
void qa_collision_map_data_destroy(qa_collision_map_data*m){
  if(!m)return;
  free(m->entities);
  if(m->shader_names){for(size_t i=0;i<m->shader_count;i++)free(m->shader_names[i]);free(m->shader_names);}
  free(m->shaders);free(m->planes);free(m->nodes);free(m->leaves);
  free(m->leaf_brush_values);free(m->leaf_surface_values);free(m->brushes);free(m->brush_sides);
  if(m->model_index_values){for(size_t i=0;i<m->model_count*2;i++)free(m->model_index_values[i]);free(m->model_index_values);}
  free(m->models);
  if(m->patches){for(size_t i=0;i<m->patch_count;i++){if(m->patches[i]){qa_patch_collide_destroy(m->patches[i]->collide);free(m->patches[i]);}}free(m->patches);}
  free(m->areas);free(m->portals);free(m->visibility);free(m->box);
  memset(m,0,sizeof*m); m->kind="collision-map";
}
void qa_decoded_collision_map_destroy(qa_collision_map_data*m){if(!m)return;qa_collision_map_data_destroy(m);free(m);}
/* box storage */
qa_bounds qa_collision_box_storage_bounds(const qa_collision_box_storage*s){return s->model->bounds;}
int32_t qa_collision_box_storage_check_count(const qa_collision_box_storage*s){return s->map->check_count;}
qa_box_side_read qa_collision_box_read_side(qa_collision_box_storage*s,double idx){
  qa_collision_map_data*m=s->map;
  double si=s->brush->first_side+idx;
  if(!(si>=0&&si<(double)m->brush_side_count&&si==floor(si)))qa_range_error("CM source record %s outside allocation",QA_JS_NUMBER(si));
  qa_q3_collision_brush_side*side=&m->brush_sides[(size_t)si];
  if(!(side->plane>=0&&side->plane<(double)m->plane_count&&side->plane==floor(side->plane)))qa_range_error("CM source record %s outside allocation",QA_JS_NUMBER(side->plane));
  qa_box_side_read r={m->planes[(size_t)side->plane],side->surface_flags}; return r;
}
void qa_collision_box_set_bounds(qa_collision_box_storage*s,qa_vec3 mins,qa_vec3 maxs,bool capsule){
  s->model->bounds=(qa_bounds){mins,maxs};
  if(capsule)return;
  /* planes: box planes are last 12; distances per TS list */
  qa_collision_map_data*m=s->map;
  /* find first box plane: stored as plane_count-12 for resource maps; for safety use brush side planes to locate */
  /* TS: distances[index] written to firstPlane+index */
  double distances[12]={maxs.x,-maxs.x,mins.x,-mins.x,maxs.y,-maxs.y,mins.y,-mins.y,maxs.z,-maxs.z,mins.z,-mins.z};
  if(m->plane_count>=12){
    size_t first=m->plane_count-12;
    for(int i=0;i<12;i++)m->planes[first+(size_t)i].plane.distance=(float)distances[i];
  }
  s->brush->bounds=(qa_bounds){mins,maxs};
}
/* resource */
qa_collision_map_data *qa_decoded_collision_map(const qa_q3_world_geometry*map,qa_collision_debug_surface*debug){
  qa_collision_map_data*m=xz(1,sizeof*m); m->kind="collision-map";
  m->entities=xdup(map->entities);
  m->shader_count=map->shader_count; m->shaders=xz(m->shader_count?m->shader_count:1,sizeof*m->shaders); m->shader_names=xz(m->shader_count?m->shader_count:1,sizeof*m->shader_names);
  for(size_t i=0;i<map->shader_count;i++){m->shader_names[i]=xdup(map->shaders[i].name);m->shaders[i].name=m->shader_names[i];m->shaders[i].surface_flags=map->shaders[i].surface_flags;m->shaders[i].content_flags=map->shaders[i].content_flags;}
  m->plane_count=map->plane_count; m->planes=xz(m->plane_count?m->plane_count:1,sizeof*m->planes);
  for(size_t i=0;i<map->plane_count;i++){
    qa_vec3 n=map->planes[i].plane.normal;
    m->planes[i].plane=map->planes[i].plane;
    m->planes[i].type=n.x==1?0:n.y==1?1:n.z==1?2:3;
    m->planes[i].signbits=(n.x<0?1:0)|((n.y<0?1:0)<<1)|((n.z<0?1:0)<<2);
  }
  m->patch_count=map->surface_count; m->patches=xz(m->patch_count?m->patch_count:1,sizeof*m->patches); m->surfaces_loaded=true;
  for(size_t i=0;i<map->surface_count;i++){
    const qa_q3_bsp_surface*s=&map->surfaces[i];
    if(!s->kind||strcmp(s->kind,"patch")!=0)continue;
    if(!(s->shader>=0&&s->shader<(double)map->shader_count&&s->shader==floor(s->shader)))qa_range_error("CM source record %s outside allocation",QA_JS_NUMBER(s->shader));
    qa_q3_collision_patch_rec*rec=xz(1,sizeof*rec);
    rec->contents=map->shaders[(size_t)s->shader].content_flags; rec->surface_flags=map->shaders[(size_t)s->shader].surface_flags;
    size_t vc=(size_t)s->vertices.count; qa_vec3*pts=xm((vc?vc:1)*sizeof*pts);
    for(size_t k=0;k<vc;k++)pts[k]=map->vertices[(size_t)s->vertices.first+k].position;
    rec->collide=qa_generate_patch_collide(s->width,s->height,pts,vc,debug);
    free(pts); m->patches[i]=rec;
  }
  double area_count=0,cluster_count=0;
  for(size_t i=0;i<map->leaf_count;i++){if(map->leaves[i].area+1>area_count)area_count=map->leaves[i].area+1;if(map->leaves[i].cluster+1>cluster_count)cluster_count=map->leaves[i].cluster+1;}
  m->area_count=(size_t)area_count; m->areas=xz(m->area_count?m->area_count:1,sizeof*m->areas);
  m->portal_count=m->area_count*m->area_count; m->portals=xz(m->portal_count?m->portal_count:1,sizeof*m->portals); m->portals_loaded=true;
  if(!map->visibility){
    size_t sz=(size_t)(((int)cluster_count+31)&~31);
    m->visibility=xm(sz?sz:1); memset(m->visibility,255,sz); m->visibility_length=sz; m->has_visibility_row_bytes=false;
    m->cluster_count=cluster_count;
  }else{
    m->visibility_length=map->visibility->bits.length+8; m->visibility=xz(m->visibility_length,1);
    memcpy(m->visibility,map->visibility->bits.bytes,map->visibility->bits.length);
    m->cluster_count=map->visibility->cluster_count; m->visibility_row_bytes=map->visibility->bytes_per_cluster; m->has_visibility_row_bytes=true;
  }
  m->visibility_loaded=true;
  m->node_count=map->node_count; m->nodes=xz(m->node_count?m->node_count:1,sizeof*m->nodes);
  for(size_t i=0;i<map->node_count;i++){
    m->nodes[i].plane=map->nodes[i].plane;
    for(int k=0;k<2;k++){qa_bsp_child c=map->nodes[i].children[k];m->nodes[i].children[k]=c.is_node?c.index:-1-c.index;}
  }
  m->leaf_count=map->leaf_count; m->leaves=xz(m->leaf_count?m->leaf_count:1,sizeof*m->leaves);
  for(size_t i=0;i<map->leaf_count;i++){m->leaves[i]=(qa_q3_collision_leaf){map->leaves[i].cluster,map->leaves[i].area,map->leaves[i].brushes.first,map->leaves[i].brushes.count,map->leaves[i].surfaces.first,map->leaves[i].surfaces.count};}
  m->leaf_brush_values=xm((map->leaf_brushes.count?map->leaf_brushes.count:1)*sizeof*m->leaf_brush_values);
  if(map->leaf_brushes.count)memcpy(m->leaf_brush_values,map->leaf_brushes.values,map->leaf_brushes.count*sizeof*m->leaf_brush_values);
  m->leaf_brushes=(qa_collision_indexes){m->leaf_brush_values,map->leaf_brushes.count};
  m->leaf_surface_values=xm((map->leaf_surfaces.count?map->leaf_surfaces.count:1)*sizeof*m->leaf_surface_values);
  if(map->leaf_surfaces.count)memcpy(m->leaf_surface_values,map->leaf_surfaces.values,map->leaf_surfaces.count*sizeof*m->leaf_surface_values);
  m->leaf_surfaces=(qa_collision_indexes){m->leaf_surface_values,map->leaf_surfaces.count};
  m->brush_side_count=map->brush_side_count; m->brush_sides=xz(m->brush_side_count?m->brush_side_count:1,sizeof*m->brush_sides);
  for(size_t i=0;i<map->brush_side_count;i++){
    double sh=map->brush_sides[i].shader;
    if(!(sh>=0&&sh<(double)map->shader_count&&sh==floor(sh)))qa_range_error("CM source record %s outside allocation",QA_JS_NUMBER(sh));
    m->brush_sides[i]=(qa_q3_collision_brush_side){map->brush_sides[i].plane,sh,map->shaders[(size_t)sh].surface_flags};
  }
  m->brush_count=map->brush_count; m->brushes=xz(m->brush_count?m->brush_count:1,sizeof*m->brushes);
  for(size_t i=0;i<map->brush_count;i++){
    double sh=map->brushes[i].shader;
    if(!(sh>=0&&sh<(double)map->shader_count&&sh==floor(sh)))qa_range_error("CM source record %s outside allocation",QA_JS_NUMBER(sh));
    m->brushes[i].shader=sh; m->brushes[i].first_side=map->brushes[i].sides.first; m->brushes[i].side_count=map->brushes[i].sides.count;
    m->brushes[i].contents=map->shaders[(size_t)sh].content_flags;
    /* bounds computed on demand in TS via first 6 sides; precompute when available */
    if(map->brushes[i].sides.count>=6){
      double d[6]; for(int k=0;k<6;k++){double pi=map->brush_sides[(size_t)map->brushes[i].sides.first+(size_t)k].plane;d[k]=m->planes[(size_t)pi].plane.distance;}
      m->brushes[i].bounds=(qa_bounds){{-d[0],-d[2],-d[4]},{d[1],d[3],d[5]}};
    }
  }
  m->model_count=map->model_count; m->models=xz(m->model_count?m->model_count:1,sizeof*m->models);
  m->model_index_values=xz((m->model_count?m->model_count:1)*2,sizeof*m->model_index_values);
  for(size_t i=0;i<map->model_count;i++){
    qa_vec3 one={1,1,1};
    m->models[i].bounds=(qa_bounds){qa_sub3(map->models[i].bounds.min,one),qa_add3(map->models[i].bounds.max,one)};
    size_t bc=(size_t)map->models[i].brushes.count,sc=(size_t)map->models[i].surfaces.count;
    double*bv=xm((bc?bc:1)*sizeof*bv),*sv=xm((sc?sc:1)*sizeof*sv);
    for(size_t k=0;k<bc;k++)bv[k]=map->models[i].brushes.first+(double)k;
    for(size_t k=0;k<sc;k++)sv[k]=map->models[i].surfaces.first+(double)k;
    m->model_index_values[i*2]=bv;m->model_index_values[i*2+1]=sv;
    m->models[i].brushes=(qa_collision_indexes){bv,bc};
    m->models[i].surfaces=(qa_collision_indexes){sv,sc};
  }
  m->box=NULL;
  return m;
}
