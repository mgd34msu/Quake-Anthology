/* Port of src/world/collision/q3/clip-models.ts. GPL-2.0-or-later. */
#include "qa/world/collision/q3/clip_models.h"
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"
#include "qa/core/common_error.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/persistence/shared.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int is_int_d(double v){return isfinite(v)&&v==floor(v);}
typedef struct{int is_inline;double index;qa_temporary_collision_model*model;} Resolved;
static Resolved resolve_(qa_source_clip_models*m,double handle){
  char msg[192];
  if(!is_int_d(handle))qa_range_error("CM_ClipHandleToModel: bad handle %s",QA_JS_NUMBER(handle));
  if(handle<0){snprintf(msg,sizeof msg,"CM_ClipHandleToModel: bad handle %s",QA_JS_NUMBER(handle));qa_common_error(QA_COMMON_ERROR_DROP,msg);}
  if(handle<qa_collision_world_model_count(m->world)){Resolved r={1,handle,NULL};return r;}
  if(handle==255){Resolved r={0,0,m->box};return r;}
  if(handle<256){snprintf(msg,sizeof msg,"CM_ClipHandleToModel: bad handle %s < %s < 256",QA_JS_NUMBER(qa_collision_world_model_count(m->world)),QA_JS_NUMBER(handle));qa_common_error(QA_COMMON_ERROR_DROP,msg);}
  snprintf(msg,sizeof msg,"CM_ClipHandleToModel: bad handle %d",(int)qa_js_to_int32(handle+256));qa_common_error(QA_COMMON_ERROR_DROP,msg);
  Resolved r={0,0,NULL}; return r;
}
qa_source_clip_models *qa_source_clip_models_create(qa_collision_world*world,const char*temporary_storage){
  qa_source_clip_models*m=malloc(sizeof*m); if(!m)return NULL;
  memset(m,0,sizeof*m); m->world=world;
  m->bounds=(qa_bounds){{0,0,0},{0,0,0}};
  m->box_storage=(temporary_storage&&strcmp(temporary_storage,"private")==0)?NULL:qa_collision_world_box_storage(world);
  m->box=qa_create_box_model(m->bounds,world->counters,m->box_storage);
  return m;
}
void qa_source_clip_models_destroy(qa_source_clip_models*m){if(!m)return;qa_temporary_collision_model_destroy(m->box);free(m);}
static qa_save_value *bounds_value(qa_bounds b){
  qa_save_value*o=qa_save_value_object();
  qa_save_value*mn=qa_save_value_object(); qa_save_value*mx=qa_save_value_object();
  qa_save_value_set(mn,"x",qa_save_value_number(b.min.x));qa_save_value_set(mn,"y",qa_save_value_number(b.min.y));qa_save_value_set(mn,"z",qa_save_value_number(b.min.z));
  qa_save_value_set(mx,"x",qa_save_value_number(b.max.x));qa_save_value_set(mx,"y",qa_save_value_number(b.max.y));qa_save_value_set(mx,"z",qa_save_value_number(b.max.z));
  qa_save_value_set(o,"min",mn);qa_save_value_set(o,"max",mx); return o;
}
qa_save_value *qa_source_clip_capture_temporary_checkpoint(qa_source_clip_models*m){
  qa_bounds bounds=m->box_storage?qa_collision_box_storage_bounds(m->box_storage):m->bounds;
  qa_save_value*o=qa_save_value_object();
  qa_save_value_set(o,"bounds",bounds_value(bounds));
  qa_save_value_set(o,"box",bounds_value(qa_temporary_collision_model_bounds(m->box)));
  return o;
}
void qa_source_clip_restore_temporary_checkpoint(qa_source_clip_models*m,const qa_save_value*value){
  qa_save_reader r; qa_save_reader_init(&r,value,"cgame-temporary-collision");
  qa_save_reader f;
  qa_save_reader_field(&r,"box",&f); qa_bounds box=qa_read_bounds(&f); qa_save_reader_destroy(&f);
  qa_save_reader_field(&r,"bounds",&f); qa_bounds bounds=qa_read_bounds(&f); qa_save_reader_destroy(&f);
  qa_save_reader_destroy(&r);
  qa_source_clip_temp_box_model(m,box.min,box.max,false);
  qa_source_clip_temp_box_model(m,bounds.min,bounds.max,true);
}
double qa_source_clip_model_count(qa_source_clip_models*m){return qa_collision_world_model_count(m->world);}
double qa_source_clip_inline_model(qa_source_clip_models*m,double idx){
  if(!is_int_d(idx))qa_range_error("CM_InlineModel: bad number");
  if(idx<0||idx>=qa_source_clip_model_count(m))qa_common_error(QA_COMMON_ERROR_DROP,"CM_InlineModel: bad number");
  return idx;
}
double qa_source_clip_temp_box_model(qa_source_clip_models*m,qa_vec3 mins,qa_vec3 maxs,bool capsule){
  m->bounds=(qa_bounds){mins,maxs};
  if(m->box_storage){qa_collision_box_set_bounds(m->box_storage,mins,maxs,capsule);return capsule?254:255;}
  if(capsule)return 254;
  qa_temporary_collision_model_destroy(m->box);
  m->box=qa_create_box_model(m->bounds,m->world->counters,NULL);
  return 255;
}
qa_bounds qa_source_clip_model_bounds(qa_source_clip_models*m,double handle){
  Resolved r=resolve_(m,handle);
  if(r.is_inline)return qa_collision_world_model_bounds(m->world,r.index);
  return m->box_storage?qa_collision_box_storage_bounds(m->box_storage):m->bounds;
}
double qa_source_clip_point_contents(qa_source_clip_models*m,qa_vec3 p,double handle){
  if(!qa_collision_world_has_nodes(m->world))return 0;
  Resolved r=resolve_(m,handle);
  return r.is_inline?qa_collision_world_point_contents(m->world,p,r.index):qa_temporary_model_point_contents(r.model,p);
}
double qa_source_clip_transformed_point_contents(qa_source_clip_models*m,qa_vec3 p,double handle,qa_vec3 origin,qa_vec3 angles){
  if(!qa_collision_world_has_nodes(m->world))return 0;
  Resolved r=resolve_(m,handle);
  qa_vec3 rot=handle==255?qa_math_vec3(0,0,0):angles;
  return r.is_inline?qa_collision_world_transformed_point_contents(m->world,p,r.index,origin,rot)
    :qa_temporary_model_transformed_point_contents(r.model,p,origin,rot);
}
static qa_temporary_collision_model *capsule_(qa_source_clip_models*m,qa_temporary_trace_query query,double handle,int transformed){
  qa_temporary_collision_model*capsule=qa_create_capsule_model(qa_source_clip_model_bounds(m,handle),m->world->counters,m->box_storage);
  int is_capsule=query.shape.kind&&strcmp(query.shape.kind,"capsule")==0;
  if(!is_capsule){
    int is_point=query.shape.kind&&strcmp(query.shape.kind,"point")==0;
    qa_vec3 mins=is_point?qa_math_vec3(0,0,0):query.shape.mins;
    qa_vec3 maxs=is_point?qa_math_vec3(0,0,0):query.shape.maxs;
    qa_vec3 center=qa_scale3(qa_add3(mins,maxs),0.5);
    qa_vec3 smin=qa_sub3(mins,center),smax=qa_sub3(maxs,center);
    if(transformed){
      qa_vec3 tc=qa_scale3(qa_add3(smin,smax),0.5);
      smin=qa_sub3(smin,tc); smax=qa_sub3(smax,tc);
    }
    qa_source_clip_temp_box_model(m,smin,smax,false);
  }
  return capsule;
}
qa_source_trace_result qa_source_clip_trace(qa_source_clip_models*m,qa_temporary_trace_query query,double handle){
  Resolved r=resolve_(m,handle);
  if(!qa_collision_world_has_nodes(m->world)){
    qa_collision_world_advance_check_count(m->world);
    m->world->counters->c_traces=(int32_t)((uint32_t)m->world->counters->c_traces+1u);
    return qa_empty_source_trace();
  }
  if(handle==254){
    qa_collision_world_advance_check_count(m->world);
    qa_temporary_collision_model*capsule=capsule_(m,query,handle,0);
    int is_capsule=query.shape.kind&&strcmp(query.shape.kind,"capsule")==0;
    qa_source_trace_result res=(!is_capsule&&qa_source_clip_model_count(m)>255)
      ?qa_temporary_model_trace_capsule_replacement(capsule,query,m->world,NULL)
      :qa_temporary_model_trace_source(capsule,query);
    qa_temporary_collision_model_destroy(capsule);
    return res;
  }
  if(r.is_inline){qa_q3_trace_query q=query;q.has_model_index=true;q.model_index=r.index;return qa_collision_world_trace_source(m->world,q);}
  qa_collision_world_advance_check_count(m->world);
  return qa_temporary_model_trace_source(r.model,query);
}
bool qa_source_clip_trace_without_nodes(qa_source_clip_models*m,double handle,qa_source_trace_result*out){
  if(qa_collision_world_has_nodes(m->world))return false;
  (void)resolve_(m,handle);
  qa_collision_world_advance_check_count(m->world);
  m->world->counters->c_traces=(int32_t)((uint32_t)m->world->counters->c_traces+1u);
  *out=qa_empty_source_trace(); return true;
}
qa_source_trace_result qa_source_clip_transformed_trace(qa_source_clip_models*m,qa_temporary_trace_query query,double handle,qa_vec3 origin,qa_vec3 angles){
  Resolved r=resolve_(m,handle);
  if(!qa_collision_world_has_nodes(m->world)){
    qa_collision_world_advance_check_count(m->world);
    m->world->counters->c_traces=(int32_t)((uint32_t)m->world->counters->c_traces+1u);
    qa_source_trace_result res=qa_empty_source_trace();
    res.end=qa_source_trace_end(query.start,query.end,1);
    return res;
  }
  if(handle==254){
    qa_collision_world_advance_check_count(m->world);
    qa_temporary_collision_model*capsule=capsule_(m,query,handle,1);
    int is_capsule=query.shape.kind&&strcmp(query.shape.kind,"capsule")==0;
    qa_q3_model_transform tr={origin,angles};
    qa_source_trace_result res=(!is_capsule&&qa_source_clip_model_count(m)>255)
      ?qa_temporary_model_trace_capsule_replacement(capsule,query,m->world,&tr)
      :qa_temporary_model_transformed_trace_source(capsule,query,origin,angles);
    qa_temporary_collision_model_destroy(capsule);
    return res;
  }
  qa_vec3 rot=handle==255?qa_math_vec3(0,0,0):angles;
  if(r.is_inline){qa_q3_trace_query q=query;q.has_model_index=true;q.model_index=r.index;return qa_collision_world_transformed_trace_source(m->world,q,origin,rot);}
  qa_collision_world_advance_check_count(m->world);
  return qa_temporary_model_transformed_trace_source(r.model,query,origin,rot);
}
