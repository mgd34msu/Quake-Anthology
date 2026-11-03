/* Port of src/world/collision/index.ts. GPL-2.0-or-later. */
#include "qa/world/collision/index.h"
#include "qa/foundation/support.h"
#include "qa/world/collision/body.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
static float f32(double v){return (float)v;}
static qa_bounds swept_bounds(qa_trace_query q){
  int is_point=q.shape.kind&&strcmp(q.shape.kind,"point")==0;
  qa_bounds b=is_point?(qa_bounds){{0,0,0},{0,0,0}}:q.shape.bounds;
  if(q.policy.kind&&strcmp(q.policy.kind,"q3")==0){
    double mn[3],mx[3],s[3]={q.start.x,q.start.y,q.start.z},e[3]={q.end.x,q.end.y,q.end.z};
    double bmn[3]={b.min.x,b.min.y,b.min.z},bmx[3]={b.max.x,b.max.y,b.max.z};
    for(int i=0;i<3;i++){
      mn[i]=f32(f32(qa_js_min(s[i],e[i])+bmn[i])+-1);
      mx[i]=f32(f32(qa_js_max(s[i],e[i])+bmx[i])+1);
    }
    return (qa_bounds){{mn[0],mn[1],mn[2]},{mx[0],mx[1],mx[2]}};
  }
  return (qa_bounds){{qa_js_min(q.start.x,q.end.x)+b.min.x-1,qa_js_min(q.start.y,q.end.y)+b.min.y-1,qa_js_min(q.start.z,q.end.z)+b.min.z-1},
    {qa_js_max(q.start.x,q.end.x)+b.max.x+1,qa_js_max(q.start.y,q.end.y)+b.max.y+1,qa_js_max(q.start.z,q.end.z)+b.max.z+1}};
}
static bool blocks_q1_ctx(void*ctx,double contents,qa_trace_policy policy){(void)ctx;return qa_blocks_q1_contents(contents,policy);}
qa_shared_scene_queries *qa_create_scene_queries(qa_decoded_world world){
  qa_shared_scene_queries*q=calloc(1,sizeof*q); if(!q)return NULL;
  q->geometry=world;
  qa_bounds mb;
  switch(world.kind){
    case QA_DECODED_WORLD_Q1:{
      q->kind="q1";
      qa_q1_collision_options opts={blocks_q1_ctx,NULL};
      if(!qa_q1_collision_init(&q->q1,world.as.q1,opts)){free(q);return NULL;}
      mb=qa_q1_collision_model_bounds(&q->q1,0);
      break;
    }
    case QA_DECODED_WORLD_Q2:{
      q->kind="q2";
      if(!qa_q2_collision_init(&q->q2,world.as.q2)){free(q);return NULL;}
      mb=qa_q2_collision_model_bounds(&q->q2,0);
      break;
    }
    case QA_DECODED_WORLD_Q3:{
      q->kind="q3";
      q->q3=qa_create_q3_collision(world.as.q3);
      if(!q->q3){free(q);return NULL;}
      mb=qa_q3_collision_model_bounds(q->q3,0);
      break;
    }
    default:free(q);return NULL;
  }
  q->spatial=qa_spatial_index_create(mb);
  if(!q->spatial){qa_shared_scene_queries_destroy(q);return NULL;}
  return q;
}
void qa_shared_scene_queries_destroy(qa_shared_scene_queries*q){
  if(!q)return;
  if(q->spatial)qa_spatial_index_destroy(q->spatial);
  if(q->kind&&strcmp(q->kind,"q1")==0)qa_q1_collision_destroy(&q->q1);
  if(q->kind&&strcmp(q->kind,"q2")==0)qa_q2_collision_destroy(&q->q2);
  if(q->q3){qa_q3_collision_destroy(q->q3);free(q->q3);}
  free(q->portal_ids);free(q->portal_primary);free(q->portal_counts);
  free(q->leaf_scratch);free(q->area_bits_scratch);free(q->actor_scratch);
  free(q);
}
qa_source_clip_models *qa_shared_native_q3_clip_models(qa_shared_scene_queries*q){
  if(!(q->kind&&strcmp(q->kind,"q3")==0))return NULL;
  return (qa_source_clip_models*)qa_collision_world_source_clip_models(q->q3->world);
}
void qa_shared_bind_collision_settings(qa_shared_scene_queries*q,qa_collision_map_settings*s){
  if(q->kind&&strcmp(q->kind,"q3")==0)qa_collision_world_bind_settings(q->q3->world,s);
}
void qa_shared_link(qa_shared_scene_queries*q,qa_linked_body b,qa_actor_collision col){qa_spatial_index_link(q->spatial,b,col);}
void qa_shared_unlink(qa_shared_scene_queries*q,qa_actor_id a){qa_spatial_index_unlink(q->spatial,a);}
void qa_shared_bind_body_table(qa_shared_scene_queries*q,qa_shared_body_table*t){q->body_table=t;q->actor_state_fn=NULL;}
void qa_shared_bind_actor_state_fn(qa_shared_scene_queries*q,qa_actor_state_fn fn,void*ctx){q->actor_state_fn=fn;q->actor_state_ctx=ctx;q->body_table=NULL;}
void qa_shared_bind_actor_collision(qa_shared_scene_queries*q,qa_actor_collision_fn read,void*ctx){q->read_collision=read;q->read_collision_ctx=ctx;}
static int snapshot_actor(qa_shared_scene_queries*q,qa_spatial_actor linked,qa_actor_collision collision,qa_spatial_actor*out){
  qa_body_state state; int has=1;
  if(q->body_table){
    qa_body_state_result r=qa_shared_body_table_read(q->body_table,linked.body.actor);
    has=r.has_value; state=r.value;
    if(!has)return 0;
    *out=linked; out->body.state=state; out->collision=collision; return 1;
  }
  if(q->actor_state_fn){has=q->actor_state_fn(q->actor_state_ctx,linked.body.actor,&state);if(!has)return 0;}
  else state=linked.body.state;
  *out=linked; out->body.state=state; out->collision=collision; return 1;
}
static int current_actor(qa_shared_scene_queries*q,qa_spatial_actor linked,qa_spatial_actor*out){
  if(!q->body_table&&!q->actor_state_fn&&!q->read_collision){*out=linked;return 1;}
  qa_actor_collision collision=linked.collision;
  if(q->read_collision&&!q->read_collision(q->read_collision_ctx,linked.body.actor,&collision))return 0;
  return snapshot_actor(q,linked,collision,out);
}
bool qa_shared_linked_actor(qa_shared_scene_queries*q,qa_actor_id actor,qa_spatial_actor*out){
  qa_spatial_actor linked;
  if(!qa_spatial_index_get(q->spatial,actor,&linked))return false;
  return current_actor(q,linked,out)!=0;
}
const qa_spatial_actor *qa_shared_query_actors(qa_shared_scene_queries*q,qa_bounds bounds,const char*role,size_t*out_count){
  const char*qrole=q->read_collision?"both":role;
  size_t n=0; const qa_spatial_actor*found=qa_spatial_index_query(q->spatial,bounds,qrole,&n);
  free(q->actor_scratch); q->actor_scratch=malloc((n?n:1)*sizeof*q->actor_scratch); if(!q->actor_scratch)abort();
  size_t out_n=0;
  for(size_t i=0;i<n;i++){
    int refresh=q->body_table||q->actor_state_fn||q->read_collision;
    qa_actor_collision collision=found[i].collision;
    if(q->read_collision&&!q->read_collision(q->read_collision_ctx,found[i].body.actor,&collision))continue;
    if(!(role&&strcmp(role,"both")==0)&&collision.role&&strcmp(collision.role,role)!=0)continue;
    qa_spatial_actor actor;
    if(refresh){if(!snapshot_actor(q,found[i],collision,&actor))continue;}
    else actor=found[i];
    q->actor_scratch[out_n++]=actor;
  }
  q->actor_scratch_count=out_n; *out_count=out_n; return q->actor_scratch;
}
static double prov_point_leaf(qa_shared_scene_queries*q,qa_vec3 p){
  if(strcmp(q->kind,"q1")==0)return qa_q1_collision_point_leaf(&q->q1,p,0);
  if(strcmp(q->kind,"q2")==0)return qa_q2_collision_point_leaf(&q->q2,p,0);
  return qa_q3_collision_point_leaf(q->q3,p);
}
double qa_shared_point_leaf(qa_shared_scene_queries*q,qa_vec3 p){return prov_point_leaf(q,p);}
double qa_shared_leaf_cluster(qa_shared_scene_queries*q,double leaf){
  if(strcmp(q->kind,"q1")==0)return qa_q1_collision_leaf_cluster(&q->q1,leaf);
  if(strcmp(q->kind,"q2")==0)return qa_q2_collision_leaf_cluster(&q->q2,leaf);
  return qa_q3_collision_leaf_cluster(q->q3,leaf);
}
double qa_shared_leaf_area(qa_shared_scene_queries*q,double leaf){
  if(strcmp(q->kind,"q1")==0)return qa_q1_collision_leaf_area(&q->q1,leaf);
  if(strcmp(q->kind,"q2")==0)return qa_q2_collision_leaf_area(&q->q2,leaf);
  return qa_q3_collision_leaf_area(q->q3,leaf);
}
qa_bounds qa_shared_model_bounds(qa_shared_scene_queries*q,double m){
  if(strcmp(q->kind,"q1")==0)return qa_q1_collision_model_bounds(&q->q1,m);
  if(strcmp(q->kind,"q2")==0)return qa_q2_collision_model_bounds(&q->q2,m);
  return qa_q3_collision_model_bounds(q->q3,m);
}
qa_leaf_query_result qa_shared_box_leaves(qa_shared_scene_queries*q,qa_bounds b,double limit){
  qa_leaf_query_result src;
  if(strcmp(q->kind,"q1")==0)src=qa_q1_collision_box_leaves(&q->q1,b,limit);
  else if(strcmp(q->kind,"q2")==0)src=qa_q2_collision_box_leaves_default(&q->q2,b,limit);
  else{
    free(q->leaf_scratch);
    /* Unsafe limits (NaN, fractions, > 2^53) reach the topology RangeError; cast only safe ones. */
    size_t cap=limit>0&&limit<=9007199254740991.0&&limit==floor(limit)?(size_t)limit:1;
    q->leaf_scratch=malloc(cap*sizeof*q->leaf_scratch); if(!q->leaf_scratch)abort();
    src=qa_q3_collision_box_leaves(q->q3,b,limit,q->leaf_scratch,cap);
    q->leaf_scratch_count=src.leaves.count;
    return src;
  }
  free(q->leaf_scratch);
  q->leaf_scratch=malloc((src.leaves.count?src.leaves.count:1)*sizeof*q->leaf_scratch); if(!q->leaf_scratch)abort();
  if(src.leaves.count)memcpy(q->leaf_scratch,src.leaves.values,src.leaves.count*sizeof*q->leaf_scratch);
  q->leaf_scratch_count=src.leaves.count;
  src.leaves=(qa_double_view){q->leaf_scratch,src.leaves.count};
  return src;
}
bool qa_shared_areas_connected(qa_shared_scene_queries*q,double a,double b){
  if(strcmp(q->kind,"q1")==0)return qa_q1_collision_areas_connected(&q->q1,a,b);
  if(strcmp(q->kind,"q2")==0)return qa_q2_collision_areas_connected(&q->q2,a,b);
  return qa_q3_collision_areas_connected(q->q3,a,b);
}
const uint8_t *qa_shared_area_bits(qa_shared_scene_queries*q,double area,size_t*out){
  const uint8_t*src; size_t len=0;
  if(strcmp(q->kind,"q1")==0)src=qa_q1_collision_area_bits(&q->q1,area,&len);
  else if(strcmp(q->kind,"q2")==0)src=qa_q2_collision_area_bits(&q->q2,area,&len);
  else{uint8_t*owned=qa_q3_collision_area_bits(q->q3,area,&len);free(q->area_bits_scratch);q->area_bits_scratch=owned;q->area_bits_length=len;*out=len;return owned;}
  free(q->area_bits_scratch);
  q->area_bits_scratch=malloc(len?len:1); if(!q->area_bits_scratch)abort();
  memcpy(q->area_bits_scratch,src,len); q->area_bits_length=len; *out=len; return q->area_bits_scratch;
}
bool qa_shared_cluster_visible(qa_shared_scene_queries*q,double a,double b,const char*kind){
  if(strcmp(q->kind,"q1")==0)return qa_q1_collision_cluster_visible(&q->q1,a,b,kind);
  if(strcmp(q->kind,"q2")==0)return qa_q2_collision_cluster_visible(&q->q2,a,b,kind);
  return qa_q3_collision_cluster_visible(q->q3,a,b,kind);
}
typedef struct{size_t idx;int found;} PortalSlot;
static PortalSlot portal_slot(qa_shared_scene_queries*q,double portal){
  for(size_t i=0;i<q->portal_count;i++)if(q->portal_ids[i]==portal){PortalSlot s={i,1};return s;}
  PortalSlot s={0,0}; return s;
}
void qa_shared_set_area_portal_state(qa_shared_scene_queries*q,double portal,bool open){
  if(strcmp(q->kind,"q2")!=0)qa_range_error("Portal identifiers belong to Quake II maps");
  PortalSlot s=portal_slot(q,portal);
  if(s.found)q->portal_primary[s.idx]=open;
  double cnt=s.found?q->portal_counts[s.idx]:0;
  qa_q2_collision_set_area_portal_state(&q->q2,portal,open||cnt>0);
}
void qa_shared_adjust_area_portal_contribution(qa_shared_scene_queries*q,double portal,double delta){
  if(strcmp(q->kind,"q2")!=0)qa_range_error("Portal identifiers belong to Quake II maps");
  PortalSlot s=portal_slot(q,portal);
  int primary; double count;
  if(s.found){primary=q->portal_primary[s.idx];count=q->portal_counts[s.idx];}
  else{
    size_t n=0; const double*state=qa_q2_collision_portal_state(&q->q2,&n);
    primary=0; for(size_t i=0;i<n;i++)if(state[i]==portal)primary=1;
    count=0;
  }
  double nc=count+delta;
  if(nc<0)qa_range_error("Area portal contribution underflow");
  qa_q2_collision_set_area_portal_state(&q->q2,portal,primary||nc>0);
  if(nc==0){
    if(s.found){memmove(q->portal_ids+s.idx,q->portal_ids+s.idx+1,(q->portal_count-s.idx-1)*sizeof*q->portal_ids);
      memmove(q->portal_primary+s.idx,q->portal_primary+s.idx+1,(q->portal_count-s.idx-1)*sizeof*q->portal_primary);
      memmove(q->portal_counts+s.idx,q->portal_counts+s.idx+1,(q->portal_count-s.idx-1)*sizeof*q->portal_counts);q->portal_count--;}
    return;
  }
  if(!s.found){
    if(q->portal_count==q->portal_cap){q->portal_cap=q->portal_cap?q->portal_cap*2:8;
      q->portal_ids=realloc(q->portal_ids,q->portal_cap*sizeof*q->portal_ids);
      q->portal_primary=realloc(q->portal_primary,q->portal_cap*sizeof*q->portal_primary);
      q->portal_counts=realloc(q->portal_counts,q->portal_cap*sizeof*q->portal_counts);
      if(!q->portal_ids||!q->portal_primary||!q->portal_counts)abort();}
    s.idx=q->portal_count++; q->portal_ids[s.idx]=portal; q->portal_primary[s.idx]=primary!=0;
  }
  q->portal_counts[s.idx]=nc;
}
const double *qa_shared_q2_portal_state(qa_shared_scene_queries*q,size_t*out){
  if(strcmp(q->kind,"q2")!=0)qa_range_error("Portal identifiers belong to Quake II maps");
  return qa_q2_collision_portal_state(&q->q2,out);
}
void qa_shared_adjust_area_portal_pair(qa_shared_scene_queries*q,double a,double b,bool open){
  if(strcmp(q->kind,"q3")!=0)qa_range_error("Area-pair portal references belong to Quake III maps");
  qa_q3_collision_adjust_area_portal_state(q->q3,a,b,open);
}
static qa_trace_result prov_trace(qa_shared_scene_queries*q,qa_trace_query query){
  if(strcmp(q->kind,"q1")==0)return qa_q1_collision_trace(&q->q1,query);
  if(strcmp(q->kind,"q2")==0)return qa_q2_collision_trace(&q->q2,query);
  return qa_q3_collision_trace(q->q3,query);
}
static qa_trace_media prov_trace_media(qa_shared_scene_queries*q,qa_trace_query query,double fraction){
  if(strcmp(q->kind,"q2")==0)return qa_q2_collision_trace_media(&q->q2,query,fraction);
  return qa_q3_collision_trace_media(q->q3,query,fraction);
}
static qa_point_contents_result prov_pc(qa_shared_scene_queries*q,qa_point_contents_query query){
  if(strcmp(q->kind,"q1")==0)return qa_q1_collision_point_contents(&q->q1,query);
  if(strcmp(q->kind,"q2")==0)return qa_q2_collision_point_contents(&q->q2,query);
  return qa_q3_collision_point_contents(q->q3,query);
}
qa_trace_result qa_shared_geometry_trace(qa_shared_scene_queries*q,qa_trace_query query){
  int is_box=query.shape.kind&&strcmp(query.shape.kind,"box")==0;
  if(is_box&&query.shape.bounds.min.x==0&&query.shape.bounds.min.y==0&&query.shape.bounds.min.z==0&&
     query.shape.bounds.max.x==0&&query.shape.bounds.max.y==0&&query.shape.bounds.max.z==0)
    query.shape.kind="point";
  qa_trace_result raw=prov_trace(q,query);
  qa_trace_result result=qa_adapt_trace_result(raw,query.policy);
  if(!(result.kind&&strcmp(result.kind,"q1")==0)||strcmp(q->kind,"q1")==0)return result;
  qa_trace_media media=prov_trace_media(q,query,result.fraction);
  result.in_open=media.in_open; result.in_water=media.in_water;
  return result;
}
static qa_trace_result do_shared_trace(qa_shared_scene_queries*q,qa_trace_query query,const qa_actor_id*excluded,size_t exc){
  qa_trace_result result=qa_shared_geometry_trace(q,query);
  int pk3=query.policy.kind&&strcmp(query.policy.kind,"q3")==0;
  int not_q1=!(query.policy.kind&&strcmp(query.policy.kind,"q1")==0);
  if(query.target.is_model||result.all_solid||(not_q1&&result.fraction==0))return result;
  qa_spatial_actor pass; int has_pass=0; qa_actor_collision pass_col; int has_pass_col=0;
  if(query.pass_actor){
    has_pass=qa_spatial_index_get(q->spatial,*query.pass_actor,&pass)!=0;
    if(q->read_collision)has_pass_col=q->read_collision(q->read_collision_ctx,*query.pass_actor,&pass_col);
    else if(has_pass){pass_col=pass.collision;has_pass_col=1;}
  }
  /* SV_ClipToLinks: points never interact with a sized mover. */
  int pass_sized=0;
  if(query.policy.kind&&strcmp(query.policy.kind,"q1")==0&&has_pass){
    qa_spatial_actor current=pass; int live=1;
    if(q->body_table||q->actor_state_fn||q->read_collision){
      qa_actor_collision collision=pass.collision;
      if(q->read_collision)live=q->read_collision(q->read_collision_ctx,pass.body.actor,&collision);
      live=live&&snapshot_actor(q,pass,collision,&current);
    }
    pass_sized=live&&current.body.state.bounds.max.x!=current.body.state.bounds.min.x;
  }
  qa_bounds envelope=swept_bounds(query);
  int missile=query.policy.kind&&strcmp(query.policy.kind,"q1")==0&&query.policy.move&&strcmp(query.policy.move,"missile")==0;
  if(missile){
    qa_trace_query mq=query; mq.shape.kind="box"; mq.shape.bounds=(qa_bounds){{-15,-15,-15},{15,15,15}};
    envelope=swept_bounds(mq);
  }
  size_t n=0; const qa_spatial_actor*found=qa_spatial_index_query(q->spatial,envelope,q->read_collision?"both":"solid",&n);
  /* copy: spatial scratch may be reused by nested geometry actor traces */
  qa_spatial_actor*list=malloc((n?n:1)*sizeof*list); if(!list)abort();
  if(n)memcpy(list,found,n*sizeof*list);
  for(size_t i=0;i<n;i++){
    qa_spatial_actor linked=list[i];
    int refresh=q->body_table||q->actor_state_fn||q->read_collision;
    qa_actor_id id=linked.body.actor;
    qa_actor_collision collision=linked.collision;
    if(q->read_collision&&!q->read_collision(q->read_collision_ctx,id,&collision))continue;
    if(!(collision.role&&strcmp(collision.role,"solid")==0))continue;
    int skip=0;
    for(size_t k=0;k<exc;k++)if(qa_same_actor(excluded[k],id))skip=1;
    if(skip)continue;
    int corpse=collision.q1_corpse||collision.dead_monster||(!(collision.family&&strcmp(collision.family,"q1")==0)&&(qa_js_to_int32(collision.contents)&0x04000000)!=0);
    int is_point=query.shape.kind&&strcmp(query.shape.kind,"point")==0;
    int point_like=is_point||(query.shape.bounds.min.x==query.shape.bounds.max.x&&query.shape.bounds.min.y==query.shape.bounds.max.y&&query.shape.bounds.min.z==query.shape.bounds.max.z);
    /* sv_world.c skips corpses unless the mask carries DEADMONSTER, so the exception is only for other families' corpses. */
    int corpse_point=corpse&&point_like&&!(collision.family&&query.policy.kind&&strcmp(collision.family,query.policy.kind)==0);
    if(query.pass_actor){
      if(qa_same_actor(*query.pass_actor,id))continue;
      int have_q3=has_pass_col&&pass_col.has_q3_owner&&collision.has_q3_owner;
      if(pk3&&have_q3){
        double pass_owner=pass_col.q3_owner_number==1023?-1:pass_col.q3_owner_number;
        if(!corpse_point&&(collision.q3_owner_number==pass_col.q3_entity_number||collision.q3_owner_number==pass_owner))continue;
      }else if(!corpse_point){
        if(collision.owner&&qa_same_actor(*query.pass_actor,*collision.owner))continue;
        if(has_pass_col&&pass_col.owner){
          if(pk3?(collision.owner&&qa_same_actor(*pass_col.owner,*collision.owner)):qa_same_actor(*pass_col.owner,id))continue;
        }
      }
    }
    if(collision.q1_corpse&&!is_point&&
       (query.shape.bounds.min.x!=query.shape.bounds.max.x||query.shape.bounds.min.y!=query.shape.bounds.max.y||query.shape.bounds.min.z!=query.shape.bounds.max.z))continue;
    int no_monsters=query.policy.kind&&strcmp(query.policy.kind,"q1")==0&&query.policy.move&&strcmp(query.policy.move,"no-monsters")==0;
    int is_model_shape=collision.shape_kind&&strcmp(collision.shape_kind,"model")==0;
    if(no_monsters&&!is_model_shape)continue;
    if(!corpse_point){
      if(query.policy.kind&&strcmp(query.policy.kind,"q1")==0){if(qa_actor_contents(collision,"q1")!=-2)continue;}
      else if((qa_js_to_int32(qa_actor_contents(collision,query.policy.kind))&qa_js_to_int32(query.policy.contents_mask))==0)continue;
    }
    qa_spatial_actor actor;
    if(refresh){if(!snapshot_actor(q,linked,collision,&actor))continue;}
    else{actor=linked;actor.collision=collision;}
    if(pass_sized&&collision.shape_kind&&strcmp(collision.shape_kind,"box")==0&&actor.body.state.bounds.max.x==actor.body.state.bounds.min.x)continue;
    qa_trace_query moving=query;
    if(missile&&collision.monster){moving.shape.kind="box";moving.shape.bounds=(qa_bounds){{-15,-15,-15},{15,15,15}};}
    qa_trace_result hit;
    if(is_model_shape){
      qa_trace_query mq=moving;
      mq.target.is_model=true;mq.target.model=collision.shape_model;mq.target.origin=actor.body.state.origin;mq.target.angles=actor.body.state.angles;
      hit=qa_shared_geometry_trace(q,mq);
      if(hit.hit.kind!=QA_TRACE_HIT_NONE){hit.hit.kind=QA_TRACE_HIT_ACTOR;hit.hit.actor=id;}
    }else hit=qa_trace_actor_body(moving,actor);
    int pk1=query.policy.kind&&strcmp(query.policy.kind,"q1")==0;
    if(pk3){
      if(hit.fraction<result.fraction){int ss=hit.start_solid||result.start_solid;result=hit;result.start_solid=ss!=0;}
      else{result.all_solid=result.all_solid||hit.all_solid;result.start_solid=result.start_solid||(!hit.all_solid&&hit.start_solid);}
    }else if(hit.all_solid||hit.fraction<result.fraction||((pk1||(query.policy.kind&&strcmp(query.policy.kind,"q2")==0))&&hit.start_solid)){
      int ss=hit.start_solid||result.start_solid;result=hit;result.start_solid=ss!=0;
    }else if(hit.start_solid)result.start_solid=true;
    if(result.all_solid)break;
  }
  free(list);
  (void)has_pass;
  return result;
}
qa_trace_result qa_shared_trace(qa_shared_scene_queries*q,qa_trace_query query){return do_shared_trace(q,query,NULL,0);}
qa_trace_result qa_shared_trace_excluding(qa_shared_scene_queries*q,qa_trace_query query,const qa_actor_id*ex,size_t n){return do_shared_trace(q,query,ex,n);}
qa_point_contents_result qa_shared_point_contents(qa_shared_scene_queries*q,qa_point_contents_query query){
  qa_point_contents_result result=qa_adapt_point_contents(prov_pc(q,query),query.policy);
  if(query.target.is_model||(query.policy.kind&&strcmp(query.policy.kind,"q1")==0))return result;
  qa_bounds pt={query.point,query.point};
  size_t n=0; const qa_spatial_actor*found=qa_spatial_index_query(q->spatial,pt,q->read_collision?"both":"solid",&n);
  qa_spatial_actor*list=malloc((n?n:1)*sizeof*list); if(!list)abort();
  if(n)memcpy(list,found,n*sizeof*list);
  for(size_t i=0;i<n;i++){
    if(query.pass_actor&&qa_same_actor(*query.pass_actor,list[i].body.actor))continue;
    qa_spatial_actor actor;
    if(!current_actor(q,list[i],&actor))continue;
    if(!(actor.collision.role&&strcmp(actor.collision.role,"solid")==0))continue;
    qa_actor_collision collision=actor.collision; qa_body_state state=actor.body.state;
    double added;
    int is_model_shape=collision.shape_kind&&strcmp(collision.shape_kind,"model")==0;
    if(is_model_shape){
      qa_point_contents_query mq=query;
      mq.target.is_model=true;mq.target.model=collision.shape_model;mq.target.origin=state.origin;mq.target.angles=state.angles;
      qa_point_contents_result sample=qa_adapt_point_contents(prov_pc(q,mq),query.policy);
      added=(sample.kind&&strcmp(sample.kind,"q2")==0)
        ?((query.policy.kind&&strcmp(query.policy.kind,"q2")==0&&query.policy.leaf_contents&&strcmp(query.policy.leaf_contents,"stored")==0)?sample.stored:sample.merged)
        :sample.contents;
    }else{
      qa_vec3 p={query.point.x-state.origin.x,query.point.y-state.origin.y,query.point.z-state.origin.z};
      qa_bounds b=state.bounds;
      if(p.x<b.min.x||p.y<b.min.y||p.z<b.min.z||p.x>b.max.x||p.y>b.max.y||p.z>b.max.z)continue;
      added=qa_actor_contents(collision,query.policy.kind);
    }
    if(result.kind&&strcmp(result.kind,"q1")==0){if(added==-2)result.contents=-2;}
    else if(result.kind&&strcmp(result.kind,"q2")==0){result.stored=(double)(qa_js_to_int32(result.stored)|qa_js_to_int32(added));result.merged=(double)(qa_js_to_int32(result.merged)|qa_js_to_int32(added));}
    else result.contents=(double)(qa_js_to_int32(result.contents)|qa_js_to_int32(added));
  }
  free(list);
  return result;
}
static qa_trace_result sq_trace(void*ctx,qa_trace_query q){return qa_shared_trace((qa_shared_scene_queries*)ctx,q);}
static qa_point_contents_result sq_pc(void*ctx,qa_point_contents_query q){return qa_shared_point_contents((qa_shared_scene_queries*)ctx,q);}
static qa_leaf_query_result sq_bl(void*ctx,qa_bounds b,double limit){return qa_shared_box_leaves((qa_shared_scene_queries*)ctx,b,limit);}
static bool sq_ac(void*ctx,double a,double b){return qa_shared_areas_connected((qa_shared_scene_queries*)ctx,a,b);}
static bool sq_cv(void*ctx,double a,double b,const char*k){return qa_shared_cluster_visible((qa_shared_scene_queries*)ctx,a,b,k);}
qa_scene_queries qa_shared_as_scene_queries(qa_shared_scene_queries*q){
  qa_scene_queries sq={q,sq_trace,sq_pc,sq_bl,sq_ac,sq_cv}; return sq;
}
