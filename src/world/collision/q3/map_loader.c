/* Port of src/world/collision/q3/map-loader.ts. GPL-2.0-or-later. */
#include "qa/world/collision/q3/map_loader.h"
#include "qa/core/common_error.h"
#include "qa/core/md4.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char *xdup_n(const char*s,size_t n){char*r=malloc(n+1);if(!r)abort();memcpy(r,s,n);r[n]=0;return r;}
void qa_collision_map_loader_init(qa_collision_map_loader*l,qa_collision_map_loader_options options){
  memset(l,0,sizeof*l); l->options=options; l->name=xdup_n("",0);
  qa_q3_collision_box_model_init(&l->box_model);
}
void qa_collision_map_loader_clear(qa_collision_map_loader*l){
  free(l->name); l->name=xdup_n("",0);
  if(l->has_current){qa_collision_world_destroy(&l->world_storage);l->has_current=false;}
  if(l->resource){qa_collision_map_resource_destroy(l->resource);l->resource=NULL;}
  memset(&l->current,0,sizeof l->current);
  const qa_collision_world_profile*debug=&l->options.debug;
  if(debug->kind&&strcmp(debug->kind,"shared")==0&&debug->owner)qa_collision_debug_clear_level_patches(debug->owner);
}
void qa_collision_map_loader_destroy(qa_collision_map_loader*l){qa_collision_map_loader_clear(l);free(l->name);l->name=NULL;}
qa_loaded_collision_map qa_collision_map_loader_load(qa_collision_map_loader*l,const char*name,bool client_load){
  if(!name||name[0]==0)qa_common_error(QA_COMMON_ERROR_DROP,"CM_LoadMap: NULL name");
  qa_collision_world_profile debug=l->options.debug;
  if(debug.kind&&strcmp(debug.kind,"shared")==0&&debug.settings)qa_collision_map_settings_register_map(debug.settings);
  if(l->options.developer_print){char buf[512];snprintf(buf,sizeof buf,"CM_LoadMap( %s, %d )\n",name,client_load?1:0);l->options.developer_print(l->options.ctx,buf);}
  if(client_load&&l->name&&strcmp(l->name,name)==0&&l->has_current)return l->current;
  qa_collision_map_loader_clear(l);
  qa_retained_file_reader files=l->options.files(l->options.ctx);
  qa_retained_collision_file file={0};
  if(!files.read_file_retained_sync(files.ctx,name,&file)){
    char msg[512]; snprintf(msg,sizeof msg,"Couldn't load %s",name); qa_common_error(QA_COMMON_ERROR_DROP,msg);
  }
  double checksum=(double)(int32_t)qa_block_checksum(file.bytes,file.byte_length);
  qa_hunk_accounting_profile memory=l->options.memory(l->options.ctx);
  int shared=debug.kind&&strcmp(debug.kind,"shared")==0;
  l->resource=qa_collision_map_resource_create(name,memory,shared?debug.owner:NULL,&l->box_model);
  qa_collision_map_resource_load(l->resource,file.terminated_bytes,file.terminated_length);
  files.free_file(files.ctx,file);
  qa_collision_map_resource_initialize_box_hull(l->resource);
  qa_collision_world_init(&l->world_storage,&l->resource->data,debug,l->options.counters);
  l->current=(qa_loaded_collision_map){&l->resource->data,&l->world_storage,checksum};
  l->has_current=true;
  if(!client_load){free(l->name);size_t n=strlen(name);if(n>63)n=63;l->name=xdup_n(name,n);}
  return l->current;
}
