/* Port of src/world/collision/q3/map-resource.ts. Records stored as
 * owned arrays; values follow CMod_Load* exactly. GPL-2.0-or-later. */
#include "qa/world/collision/q3/map_resource.h"
#include "qa/core/binary/index.h"
#include "qa/core/common_error.h"
#include "qa/formats/q3_map/ibsp44.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void *xm(size_t n){void*p=malloc(n?n:1);if(!p)abort();return p;}
static void *xz(size_t n,size_t s){void*p=calloc(n?n:1,s);if(!p)abort();return p;}
static int32_t i32at(const uint8_t*b,size_t len,size_t off,const char*src){if(off+4>len||off>len)qa_binary_error(src,(int64_t)off,"CM source read outside file");int32_t v;memcpy(&v,b+off,4);return v;}
static float f32at(const uint8_t*b,size_t len,size_t off,const char*src){if(off+4>len||off>len)qa_binary_error(src,(int64_t)off,"CM source read outside file");float v;memcpy(&v,b+off,4);return v;}
static char *xdup(const char*s){if(!s)s="";size_t n=strlen(s)+1;char*r=xm(n);memcpy(r,s,n);return r;}
double qa_collision_indexes_at(qa_collision_indexes ix,double idx){
  if(idx!=(double)(int64_t)idx||idx<0||(size_t)idx>=ix.length)qa_range_error("CM source index %g outside allocation",idx);
  return ix.values[(size_t)idx];
}
void qa_q3_collision_box_model_init(qa_q3_collision_box_model*m){m->bounds=(qa_bounds){{0,0,0},{0,0,0}};}
double qa_collision_portals_at(const qa_collision_map_data*m,double idx){
  if(!m->portals_loaded)qa_range_error("CM unloaded portal %g",idx);
  if(idx<0||(size_t)idx>=m->portal_count)qa_range_error("CM unloaded portal %g",idx);
  return m->portals[(size_t)idx];
}
void qa_collision_portals_set(qa_collision_map_data*m,double idx,double v){
  if(!m->portals_loaded)qa_range_error("CM unloaded portal %g",idx);
  if(idx<0||(size_t)idx>=m->portal_count)qa_range_error("CM unloaded portal %g",idx);
  m->portals[(size_t)idx]=(int32_t)v;
}
qa_q3_collision_patch_rec *qa_collision_map_patch(qa_collision_map_data*m,double idx){
  if(!m->surfaces_loaded)qa_range_error("CM surfaces have not been loaded");
  if(idx<0||(size_t)idx>=m->patch_count)qa_range_error("CM source record %g outside allocation",idx);
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
  if(si<0||(size_t)si>=m->brush_side_count)qa_range_error("CM source record %g outside allocation",si);
  qa_q3_collision_brush_side*side=&m->brush_sides[(size_t)si];
  if(side->plane<0||(size_t)side->plane>=m->plane_count)qa_range_error("CM source record %g outside allocation",side->plane);
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
qa_collision_map_resource *qa_collision_map_resource_create(const char*source,qa_hunk_accounting_profile memory,qa_collision_debug_surface*debug,qa_q3_collision_box_model*box_model){
  qa_collision_map_resource*r=xz(1,sizeof*r);
  r->data.kind="collision-map";
  r->source=xdup(source?source:""); r->memory=memory; r->debug=debug;
  if(box_model){r->box_model=box_model;r->owns_box_model=false;}
  else{r->box_model=xm(sizeof*r->box_model);qa_q3_collision_box_model_init(r->box_model);r->owns_box_model=true;}
  return r;
}
void qa_collision_map_resource_destroy(qa_collision_map_resource*r){
  if(!r)return;
  /* box_storage points into data.box allocation freed by destroy; clear first */
  r->data.box=NULL;
  qa_collision_map_data_destroy(&r->data);
  free(r->source); if(r->owns_box_model)free(r->box_model); free(r);
}
const char *qa_collision_map_resource_entities(qa_collision_map_resource*r){
  if(!r->data.entities)qa_range_error("CM entity string has not been loaded");
  return r->data.entities;
}
const uint8_t *qa_collision_map_resource_visibility(qa_collision_map_resource*r,size_t*out){
  if(!r->data.visibility_loaded)qa_range_error("CM visibility has not been loaded");
  *out=r->data.visibility_length; return r->data.visibility;
}
static void reserve(qa_collision_map_resource*r,const char*site,double bytes,const char*resource){
  if(r->memory.kind&&strcmp(r->memory.kind,"source-hunk")==0&&r->memory.reserve)
    (void)r->memory.reserve(r->memory.ctx,site,resource?resource:r->source,bytes,"high");
}
void qa_collision_map_resource_load(qa_collision_map_resource*r,const uint8_t*bytes_in,size_t byte_length){
  size_t raw_len=0; uint8_t*raw=qa_q3_normalize_bsp(bytes_in,byte_length,r->source,&raw_len);
  qa_collision_map_data*m=&r->data;
  /* range-checked readers */
  #define RANGE(off,len) do{if((off)<0||(len)<0||(size_t)(off)>raw_len||(size_t)(len)>raw_len-(size_t)(off))qa_binary_error(r->source,(int64_t)(off),"CM source read outside file");}while(0)
  #define RI32(off) (RANGE(off,4),ri32(raw+(off)))
  #define RF32(off) (RANGE(off,4),rf32(raw+(off)))
  /* helpers as macros need functions; declare via static inline below not possible mid-fn: use block */
  ;
  /* read header */
  if(raw_len<144)qa_binary_error(r->source,0,"CM source read outside file");
  /* local readers via lambdas unavailable: direct memcpy in macros */
  (void)0;
  /* version + lumps */
  int32_t version; memcpy(&version,raw+4,4);
  typedef struct{int32_t off,len;} Lump; Lump lumps[17];
  for(int i=0;i<17;i++){memcpy(&lumps[i].off,raw+8+i*8,4);memcpy(&lumps[i].len,raw+12+i*8,4);}
  if(version!=46){char msg[256];snprintf(msg,sizeof msg,"CM_LoadMap: %s has wrong version number (%d should be 46)",r->source,version);qa_common_error(QA_COMMON_ERROR_DROP,msg);}
  /* count helper inline */
  #define COUNT(l,stride,msg) (((l).len%(stride)!=0)?(qa_common_error(QA_COMMON_ERROR_DROP,(msg)),0):((l).len/(stride)))
  #define I32AT(off) i32at(raw,raw_len,(off),r->source)
  #define F32AT(off) f32at(raw,raw_len,(off),r->source)
  /* shaders */
  Lump sl=lumps[1]; int shader_count=COUNT(sl,72,"CMod_LoadShaders: funny lump size");
  if(shader_count<1)qa_common_error(QA_COMMON_ERROR_DROP,"Map with no shaders");
  reserve(r,"CMod_LoadShaders",shader_count*72,NULL);
  m->shader_count=(size_t)shader_count; m->shaders=xz((size_t)shader_count,sizeof*m->shaders); m->shader_names=xz((size_t)shader_count,sizeof*m->shader_names);
  for(int i=0;i<shader_count;i++){
    int off=sl.off+i*72;
    RANGE(off,72);
    char name[65]={0}; memcpy(name,raw+off,64); name[64]=0;
    m->shader_names[i]=xdup(name); m->shaders[i].name=m->shader_names[i];
    m->shaders[i].surface_flags=I32AT(off+64); m->shaders[i].content_flags=I32AT(off+68);
  }
  /* leaves */
  Lump ll=lumps[4]; int leaf_count=COUNT(ll,48,"MOD_LoadBmodel: funny lump size");
  if(leaf_count<1)qa_common_error(QA_COMMON_ERROR_DROP,"Map with no leafs");
  reserve(r,"CMod_LoadLeafs:leafs",(leaf_count+2)*24,NULL);
  m->leaf_count=(size_t)leaf_count; m->leaves=xz((size_t)leaf_count,sizeof*m->leaves);
  int area_count=0; m->cluster_count=0;
  for(int i=0;i<leaf_count;i++){
    int in=ll.off+i*48;
    m->leaves[i].cluster=I32AT(in); m->leaves[i].area=I32AT(in+4);
    m->leaves[i].first_brush=I32AT(in+40); m->leaves[i].brush_count=I32AT(in+44);
    m->leaves[i].first_surface=I32AT(in+32); m->leaves[i].surface_count=I32AT(in+36);
    if(m->leaves[i].cluster>=m->cluster_count)m->cluster_count=(double)((int32_t)m->leaves[i].cluster+1);
    if((int)m->leaves[i].area>=area_count)area_count=(int)m->leaves[i].area+1;
  }
  reserve(r,"CMod_LoadLeafs:areas",area_count*8,NULL);
  m->area_count=(size_t)area_count; m->areas=xz((size_t)(area_count?area_count:1),sizeof*m->areas);
  reserve(r,"CMod_LoadLeafs:areaPortals",(double)area_count*area_count*4,NULL);
  m->portal_count=(size_t)area_count*(size_t)area_count; m->portals=xz(m->portal_count?m->portal_count:1,sizeof*m->portals); m->portals_loaded=true;
  /* leaf brushes */
  Lump lbl=lumps[6]; int nlb=COUNT(lbl,4,"MOD_LoadBmodel: funny lump size"); r->num_leaf_brushes=(size_t)nlb;
  reserve(r,"CMod_LoadLeafBrushes",(nlb+1)*4,NULL);
  m->leaf_brush_values=xz((size_t)nlb+1,sizeof*m->leaf_brush_values);
  for(int i=0;i<nlb;i++)m->leaf_brush_values[i]=I32AT(lbl.off+i*4);
  m->leaf_brushes=(qa_collision_indexes){m->leaf_brush_values,(size_t)nlb+1};
  /* leaf surfaces */
  Lump lsl=lumps[5]; int nls=COUNT(lsl,4,"MOD_LoadBmodel: funny lump size");
  reserve(r,"CMod_LoadLeafSurfaces",nls*4,NULL);
  m->leaf_surface_values=xz((size_t)(nls?nls:1),sizeof*m->leaf_surface_values);
  for(int i=0;i<nls;i++)m->leaf_surface_values[i]=I32AT(lsl.off+i*4);
  m->leaf_surfaces=(qa_collision_indexes){m->leaf_surface_values,(size_t)nls};
  /* planes */
  Lump pl=lumps[2]; int nplanes=COUNT(pl,16,"MOD_LoadBmodel: funny lump size"); r->num_planes=(size_t)nplanes;
  if(nplanes<1)qa_common_error(QA_COMMON_ERROR_DROP,"Map with no planes");
  reserve(r,"CMod_LoadPlanes",(nplanes+12)*20,NULL);
  m->plane_count=(size_t)nplanes+12; m->planes=xz(m->plane_count,sizeof*m->planes);
  for(int i=0;i<nplanes;i++){
    int in=pl.off+i*16; int signs=0; double vals[3];
    for(int a=0;a<3;a++){vals[a]=F32AT(in+a*4);if(vals[a]<0)signs|=1<<a;}
    m->planes[i].plane.normal=(qa_vec3){vals[0],vals[1],vals[2]};
    m->planes[i].plane.distance=F32AT(in+12);
    m->planes[i].type=vals[0]==1?0:vals[1]==1?1:vals[2]==1?2:3;
    m->planes[i].signbits=signs;
  }
  /* brush sides */
  Lump bsl=lumps[9]; int nsides=COUNT(bsl,8,"MOD_LoadBmodel: funny lump size"); r->num_sides=(size_t)nsides;
  reserve(r,"CMod_LoadBrushSides",(nsides+6)*12,NULL);
  m->brush_side_count=(size_t)nsides+6; m->brush_sides=xz(m->brush_side_count,sizeof*m->brush_sides);
  for(int i=0;i<nsides;i++){
    int in=bsl.off+i*8;
    m->brush_sides[i].plane=I32AT(in); m->brush_sides[i].shader=I32AT(in+4);
    int sh=(int)m->brush_sides[i].shader;
    if(sh<0||sh>=shader_count){char msg[128];snprintf(msg,sizeof msg,"CMod_LoadBrushSides: bad shaderNum: %d",sh);qa_common_error(QA_COMMON_ERROR_DROP,msg);}
    m->brush_sides[i].surface_flags=m->shaders[sh].surface_flags;
  }
  /* brushes */
  Lump bl2=lumps[8]; int nbrush=COUNT(bl2,12,"MOD_LoadBmodel: funny lump size"); r->num_brushes=(size_t)nbrush;
  reserve(r,"CMod_LoadBrushes",(nbrush+1)*44,NULL);
  m->brush_count=(size_t)nbrush+1; m->brushes=xz(m->brush_count,sizeof*m->brushes);
  for(int i=0;i<nbrush;i++){
    int in=bl2.off+i*12;
    m->brushes[i].first_side=I32AT(in); m->brushes[i].side_count=I32AT(in+4); m->brushes[i].shader=I32AT(in+8);
    int sh=(int)m->brushes[i].shader;
    if(sh<0||sh>=shader_count){char msg[128];snprintf(msg,sizeof msg,"CMod_LoadBrushes: bad shaderNum: %d",sh);qa_common_error(QA_COMMON_ERROR_DROP,msg);}
    m->brushes[i].contents=m->shaders[sh].content_flags;
    double mn[3],mx[3];
    for(int a=0;a<3;a++){
      double s0=m->brushes[i].first_side+a*2,s1=s0+1;
      double p0=m->brush_sides[(size_t)s0].plane,p1=m->brush_sides[(size_t)s1].plane;
      mn[a]=-m->planes[(size_t)p0].plane.distance; mx[a]=m->planes[(size_t)p1].plane.distance;
    }
    m->brushes[i].bounds=(qa_bounds){{mn[0],mn[1],mn[2]},{mx[0],mx[1],mx[2]}};
  }
  /* models */
  Lump ml=lumps[7]; int nmodels=COUNT(ml,40,"CMod_LoadSubmodels: funny lump size");
  if(nmodels<1)qa_common_error(QA_COMMON_ERROR_DROP,"Map with no models");
  reserve(r,"CMod_LoadSubmodels",nmodels*48,NULL);
  m->model_count=(size_t)nmodels; m->models=xz((size_t)nmodels,sizeof*m->models);
  m->model_index_values=xz((size_t)nmodels*2,sizeof*m->model_index_values);
  if(nmodels>256)qa_common_error(QA_COMMON_ERROR_DROP,"MAX_SUBMODELS exceeded");
  for(int i=0;i<nmodels;i++){
    int in=ml.off+i*40; double mn[3],mx[3];
    for(int a=0;a<3;a++){mn[a]=F32AT(in+a*4)-1;mx[a]=F32AT(in+12+a*4)+1;}
    m->models[i].bounds=(qa_bounds){{mn[0],mn[1],mn[2]},{mx[0],mx[1],mx[2]}};
    if(i==0){m->models[i].brushes=(qa_collision_indexes){NULL,0};m->models[i].surfaces=(qa_collision_indexes){NULL,0};continue;}
    int bc=I32AT(in+36),sc=I32AT(in+28),fb=I32AT(in+32),fs=I32AT(in+24);
    reserve(r,"CMod_LoadSubmodels:brushes",bc*4,NULL); reserve(r,"CMod_LoadSubmodels:surfaces",sc*4,NULL);
    double*bv=xz((size_t)(bc?bc:1),sizeof*bv),*sv=xz((size_t)(sc?sc:1),sizeof*sv);
    for(int k=0;k<bc;k++)bv[k]=(double)((int32_t)(fb+k));
    for(int k=0;k<sc;k++)sv[k]=(double)((int32_t)(fs+k));
    m->model_index_values[i*2]=bv;m->model_index_values[i*2+1]=sv;
    m->models[i].brushes=(qa_collision_indexes){bv,(size_t)bc};
    m->models[i].surfaces=(qa_collision_indexes){sv,(size_t)sc};
  }
  /* nodes */
  Lump nl=lumps[3]; int nnodes=COUNT(nl,36,"MOD_LoadBmodel: funny lump size");
  if(nnodes<1)qa_common_error(QA_COMMON_ERROR_DROP,"Map has no nodes");
  reserve(r,"CMod_LoadNodes",nnodes*12,NULL);
  m->node_count=(size_t)nnodes; m->nodes=xz((size_t)nnodes,sizeof*m->nodes);
  for(int i=0;i<nnodes;i++){int in=nl.off+i*36;m->nodes[i].plane=I32AT(in);m->nodes[i].children[0]=I32AT(in+4);m->nodes[i].children[1]=I32AT(in+8);}
  /* entities */
  Lump el=lumps[0]; RANGE(el.off,el.len);
  reserve(r,"CMod_LoadEntityString",el.len,NULL);
  m->entities=xm((size_t)el.len+1); memcpy(m->entities,raw+el.off,(size_t)el.len); m->entities[el.len]=0;
  /* visibility */
  Lump vl=lumps[16];
  if(vl.len==0){
    size_t sz=(size_t)(((int)m->cluster_count+31)&~31);
    reserve(r,"CMod_LoadVisibility:novis",(double)sz,NULL);
    m->visibility=xm(sz?sz:1); memset(m->visibility,255,sz); m->visibility_length=sz; m->has_visibility_row_bytes=false;
  }else{
    reserve(r,"CMod_LoadVisibility",vl.len,NULL);
    m->cluster_count=I32AT(vl.off); m->visibility_row_bytes=I32AT(vl.off+4); m->has_visibility_row_bytes=true;
    RANGE(vl.off+8,vl.len-8);
    m->visibility_length=(size_t)(vl.len-8); m->visibility=xm(m->visibility_length?m->visibility_length:1);
    memcpy(m->visibility,raw+vl.off+8,m->visibility_length);
  }
  m->visibility_loaded=true;
  /* surfaces / patches */
  Lump fl=lumps[13]; int nsurf=COUNT(fl,104,"MOD_LoadBmodel: funny lump size");
  reserve(r,"CMod_LoadPatches:surfaces",nsurf*4,NULL);
  Lump vxl=lumps[10]; (void)COUNT(vxl,44,"MOD_LoadBmodel: funny lump size");
  m->patch_count=(size_t)nsurf; m->patches=xz((size_t)(nsurf?nsurf:1),sizeof*m->patches); m->surfaces_loaded=true;
  for(int i=0;i<nsurf;i++){
    int in=fl.off+i*104;
    if(I32AT(in+8)!=2)continue;
    char res[512]; snprintf(res,sizeof res,"%s#%d",r->source,i);
    reserve(r,"CMod_LoadPatches:patch",16,res);
    int width=I32AT(in+96),height=I32AT(in+100);
    int64_t pc=(int64_t)width*height;
    if(pc>1024)qa_common_error(QA_COMMON_ERROR_DROP,"ParseMesh: MAX_PATCH_VERTS");
    int first_vertex=I32AT(in+12);
    qa_vec3*pts=xm((size_t)(pc>0?pc:1)*sizeof*pts);
    for(int64_t k=0;k<pc;k++){int off=vxl.off+(first_vertex+(int)k)*44;pts[k]=(qa_vec3){F32AT(off),F32AT(off+4),F32AT(off+8)};}
    int sh=I32AT(in);
    if(sh<0||sh>=shader_count)qa_range_error("CM source record %d outside allocation",sh);
    qa_q3_collision_patch_rec*rec=xz(1,sizeof*rec);
    rec->contents=m->shaders[sh].content_flags; rec->surface_flags=m->shaders[sh].surface_flags;
    rec->collide=qa_generate_patch_collide(width,height,pts,(size_t)pc,r->debug,NULL,NULL);
    /* account allocations like TS allocator path */
    reserve(r,"CM_GeneratePatchCollide",40,res);
    reserve(r,"CM_PatchCollideFromGrid:facets",(double)rec->collide->facet_count*320,res);
    reserve(r,"CM_PatchCollideFromGrid:planes",(double)rec->collide->plane_count*20,res);
    m->patches[i]=rec; free(pts);
  }
  free(raw);
  #undef COUNT
  #undef I32AT
  #undef F32AT
  #undef RANGE
  #undef RI32
  #undef RF32
}
/* readers used by load (defined after use via macros -> move: defined here as static, load uses them through macros expanded above, so declare before load in practice by forward decl) */
void qa_collision_map_resource_initialize_box_hull(qa_collision_map_resource*r){
  qa_collision_map_data*m=&r->data;
  if(m->brush_count==0||m->plane_count<12)qa_range_error("CM box hull requires loaded collision allocations");
  size_t bi=r->num_brushes;
  m->brushes[bi].side_count=6; m->brushes[bi].first_side=(double)r->num_sides; m->brushes[bi].contents=0x02000000;
  if(r->num_leaf_brushes<m->leaf_brushes.length)m->leaf_brush_values[r->num_leaf_brushes]=(double)r->num_brushes;
  size_t first_plane=r->num_planes;
  for(int idx=0;idx<6;idx++){
    size_t side=r->num_sides+(size_t)idx;
    m->brush_sides[side].plane=(double)(first_plane+(size_t)idx*2+(size_t)(idx&1));
    m->brush_sides[side].surface_flags=0;
    for(int opp=0;opp<2;opp++){
      size_t pi=first_plane+(size_t)idx*2+(size_t)opp;
      m->planes[pi].type=(idx>>1)+opp*3;
      m->planes[pi].signbits=opp==0?0:(1<<(idx>>1));
      double v[3]={0,0,0}; v[idx>>1]=opp==0?1:-1;
      m->planes[pi].plane.normal=(qa_vec3){v[0],v[1],v[2]};
    }
  }
  qa_collision_box_storage*s=xz(1,sizeof*s);
  s->map=m; s->model=r->box_model; s->brush=&m->brushes[bi];
  r->box_storage=*s; /* storage struct copy; data.box points to resource field */
  free(s);
  m->box=&r->box_storage;
}
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
    if(s->shader<0||(size_t)s->shader>=map->shader_count)qa_range_error("CM source record %g outside allocation",s->shader);
    qa_q3_collision_patch_rec*rec=xz(1,sizeof*rec);
    rec->contents=map->shaders[(size_t)s->shader].content_flags; rec->surface_flags=map->shaders[(size_t)s->shader].surface_flags;
    size_t vc=(size_t)s->vertices.count; qa_vec3*pts=xm((vc?vc:1)*sizeof*pts);
    for(size_t k=0;k<vc;k++)pts[k]=map->vertices[(size_t)s->vertices.first+k].position;
    rec->collide=qa_generate_patch_collide(s->width,s->height,pts,vc,debug,NULL,NULL);
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
    if(sh<0||(size_t)sh>=map->shader_count)qa_range_error("CM source record %g outside allocation",sh);
    m->brush_sides[i]=(qa_q3_collision_brush_side){map->brush_sides[i].plane,sh,map->shaders[(size_t)sh].surface_flags};
  }
  m->brush_count=map->brush_count; m->brushes=xz(m->brush_count?m->brush_count:1,sizeof*m->brushes);
  for(size_t i=0;i<map->brush_count;i++){
    double sh=map->brushes[i].shader;
    if(sh<0||(size_t)sh>=map->shader_count)qa_range_error("CM source record %g outside allocation",sh);
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
