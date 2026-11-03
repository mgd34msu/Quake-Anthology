/* Port of src/world/collision/q3/index.ts. GPL-2.0-or-later. */
#include "qa/world/collision/q3/index.h"
#include "qa/core/numeric.h"
#include "qa/world/collision/contents.h"
#include "qa/foundation/js.h"
#include <stdlib.h>
#include <string.h>
void qa_q3_collision_init_geometry(qa_q3_collision*c,const qa_q3_world_geometry*geometry){
  memset(c,0,sizeof*c);
  qa_collision_map_data*map=qa_decoded_collision_map(geometry,NULL);
  qa_collision_world_profile prof={"disabled",NULL,NULL};
  qa_collision_world_init(&c->world_storage,map,prof,NULL);
  c->world=&c->world_storage; c->owns_world=true; c->owns_map=true;
  /* keep map pointer for destroy via world_storage.map */
}
void qa_q3_collision_init_world(qa_q3_collision*c,qa_collision_world*world){
  memset(c,0,sizeof*c); c->world=world; c->owns_world=false; c->owns_map=false;
}
void qa_q3_collision_destroy(qa_q3_collision*c){
  for(size_t i=0;i<c->phs_count;i++)free(c->phs_rows[i]);
  free(c->phs_keys);free(c->phs_rows);free(c->phs_lengths);
  if(c->owns_world){
    qa_collision_map_data*map=c->world_storage.map;
    qa_collision_world_destroy(&c->world_storage);
    if(c->owns_map)qa_decoded_collision_map_destroy(map);
  }
  memset(c,0,sizeof*c);
}
qa_q3_collision *qa_create_q3_collision(const qa_q3_world_geometry*geometry){
  qa_q3_collision*c=malloc(sizeof*c); if(!c)return NULL;
  qa_q3_collision_init_geometry(c,geometry); return c;
}
double qa_q3_collision_point_leaf(qa_q3_collision*c,qa_vec3 p){return qa_collision_world_point_leafnum(c->world,p);}
double qa_q3_collision_leaf_area(qa_q3_collision*c,double i){return qa_collision_world_leaf_area(c->world,i);}
double qa_q3_collision_leaf_cluster(qa_q3_collision*c,double i){return qa_collision_world_leaf_cluster(c->world,i);}
qa_bounds qa_q3_collision_model_bounds(qa_q3_collision*c,double i){return qa_collision_world_model_bounds(c->world,i);}
qa_leaf_query_result qa_q3_collision_box_leaves(qa_q3_collision*c,qa_bounds b,double limit,double*storage,size_t sc){
  qa_box_leaf_list r=qa_collision_world_box_leafnums(c->world,b,limit);
  size_t n=r.leaf_count<sc?r.leaf_count:sc;
  if(storage&&n)memcpy(storage,r.leaves,n*sizeof*storage);
  qa_leaf_query_result out; memset(&out,0,sizeof out);
  out.leaves=(qa_double_view){storage,n};
  out.has_topnode=r.has_topnode; out.topnode=r.topnode; out.overflow=r.overflowed;
  qa_box_leaf_list_destroy(&r);
  return out;
}
bool qa_q3_collision_areas_connected(qa_q3_collision*c,double a,double b){return qa_collision_world_areas_connected(c->world,a,b);}
uint8_t *qa_q3_collision_area_bits(qa_q3_collision*c,double area,size_t*out){return qa_collision_world_area_bits(c->world,area,out);}
bool qa_q3_collision_cluster_visible(qa_q3_collision*c,double from,double to,const char*kind){
  if(!kind||strcmp(kind,"pvs")==0)return qa_collision_world_cluster_visible(c->world,from,to);
  double cc=qa_collision_world_cluster_count(c->world);
  if(to<0||to>=cc)return false;
  uint8_t*row=NULL; size_t row_len=0;
  for(size_t i=0;i<c->phs_count;i++)if(c->phs_keys[i]==from){row=c->phs_rows[i];row_len=c->phs_lengths[i];break;}
  if(!row){
    row_len=(size_t)(((int64_t)cc+7)>>3);
    row=malloc(row_len?row_len:1); if(!row)abort(); memset(row,0,row_len);
    qa_source_cluster_pvs src=qa_collision_world_cluster_pvs(c->world,from);
    for(size_t i=0;i<row_len;i++)row[i]=(uint8_t)qa_source_cluster_pvs_byte_at(src,(double)i);
    for(double cl=0;cl<cc;cl++){
      if(!qa_collision_world_cluster_visible(c->world,from,cl))continue;
      qa_source_cluster_pvs adj=qa_collision_world_cluster_pvs(c->world,cl);
      for(size_t i=0;i<row_len;i++)row[i]=(uint8_t)(row[i]|(uint8_t)qa_source_cluster_pvs_byte_at(adj,(double)i));
    }
    if(c->phs_count==c->phs_cap){
      c->phs_cap=c->phs_cap?c->phs_cap*2:8;
      c->phs_keys=realloc(c->phs_keys,c->phs_cap*sizeof*c->phs_keys);
      c->phs_rows=realloc(c->phs_rows,c->phs_cap*sizeof*c->phs_rows);
      c->phs_lengths=realloc(c->phs_lengths,c->phs_cap*sizeof*c->phs_lengths);
      if(!c->phs_keys||!c->phs_rows||!c->phs_lengths)abort();
    }
    c->phs_keys[c->phs_count]=from;c->phs_rows[c->phs_count]=row;c->phs_lengths[c->phs_count]=row_len;c->phs_count++;
  }
  /* row[to >> 3] ?? 0 and 1 << (to & 7) apply ToInt32 (a NaN `to` passes the range test). */
  const int32_t word=qa_js_to_int32(to);
  return (size_t)(word>>3)<row_len&&((row[word>>3])&(1u<<(word&7)))!=0;
}
void qa_q3_collision_adjust_area_portal_state(qa_q3_collision*c,double a,double b,bool o){qa_collision_world_adjust_area_portal_state(c->world,a,b,o);}
qa_point_contents_result qa_q3_collision_point_contents(qa_q3_collision*c,qa_point_contents_query query){
  (void)qa_create_numeric_operations(&query.numeric);
  qa_point_contents_result r={0}; r.kind="q3";
  r.contents=!query.target.is_model?qa_collision_world_point_contents(c->world,query.point,0)
    :qa_collision_world_transformed_point_contents(c->world,query.point,query.target.model,query.target.origin,query.target.angles);
  return r;
}
static qa_q3_trace_query local_query(qa_trace_query query,double model){
  qa_q3_trace_query q; memset(&q,0,sizeof q);
  q.start=query.start;q.end=query.end;q.has_model_index=true;q.model_index=model;
  q.mask=qa_geometry_mask(query.policy,"q3");
  if(query.shape.kind&&strcmp(query.shape.kind,"point")==0)q.shape=(qa_q3_trace_shape){"point",{0,0,0},{0,0,0}};
  else q.shape=(qa_q3_trace_shape){query.shape.kind,query.shape.bounds.min,query.shape.bounds.max};
  return q;
}
qa_trace_result qa_q3_collision_trace(qa_q3_collision*c,qa_trace_query query){
  (void)qa_create_numeric_operations(&query.numeric);
  double model=query.target.is_model?query.target.model:0;
  qa_q3_trace_query local=local_query(query,model);
  int is_q3=query.policy.kind&&strcmp(query.policy.kind,"q3")==0;
  local.has_curves=true; local.curves=!is_q3||query.policy.curves;
  local.has_player_curve_clip=true; local.player_curve_clip=!is_q3||query.policy.player_curve_clip;
  qa_source_trace_result result=!query.target.is_model?qa_collision_world_trace_source(c->world,local)
    :qa_collision_world_transformed_trace_source(c->world,local,query.target.origin,query.target.angles);
  qa_q3_trace_result view=qa_source_trace_view(result);
  qa_trace_result r; memset(&r,0,sizeof r);
  r.kind="q3";r.fraction=result.fraction;r.end=result.end;r.start_solid=result.start_solid;r.all_solid=result.all_solid;
  r.contact=(qa_trace_contact){view.has_plane,view.plane};
  r.hit.kind=(result.fraction<1||result.start_solid)?QA_TRACE_HIT_WORLD:QA_TRACE_HIT_NONE; r.hit.model=model;
  r.contents=result.contents;r.has_surface_flags=true;r.surface_flags=result.surface_flags;
  r.source_plane=(qa_bsp_plane){result.plane.plane,result.plane.type,result.plane.signbits};
  return r;
}
qa_trace_media qa_q3_collision_trace_media(qa_q3_collision*c,qa_trace_query query,double fraction){
  double model=query.target.is_model?query.target.model:0;
  qa_q3_trace_query local=local_query(query,model);
  qa_q3_trace_transform tr={query.target.origin,query.target.angles};
  return qa_collision_world_trace_media(c->world,local,fraction,query.target.is_model?&tr:NULL);
}
