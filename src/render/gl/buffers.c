/* Port of src/render/gl/buffers.ts. GPL-2.0-or-later. */
#include "qa/render/gl/buffers.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static double f32d(double v){ return (double)(float)v; }
typedef struct cache_entry { const void *vertices; const double *indices; const char *lighting; bool pair; const void *world_positions; const void *normals; double seen; bool has_arrays; qa_gl_geometry_arrays arrays; } cache_entry;
static cache_entry *g_cache; static size_t g_cache_count, g_cache_cap;
void qa_gl_geometry_arrays_destroy(qa_gl_geometry_arrays *a){ free(a->positions);free(a->colors);free(a->coordinates);free(a->coordinates2);free(a->world_positions);free(a->normals);free(a->indices); memset(a,0,sizeof *a); }
void qa_gl_packed_cache_clear(void){ for(size_t i=0;i<g_cache_count;i++)if(g_cache[i].has_arrays)qa_gl_geometry_arrays_destroy(&g_cache[i].arrays); free(g_cache); g_cache=NULL;g_cache_count=g_cache_cap=0; }
static float *alloc_f(size_t n){ float *p=(float*)malloc((n?n:1)*sizeof(float)); if(!p)qa_range_error("OpenGL geometry allocation failed"); return p; }
static qa_gl_geometry_arrays alloc_arrays(size_t verts,size_t indices,const char *lighting,float *storage){
  qa_gl_geometry_arrays a; memset(&a,0,sizeof a);
  size_t world=strcmp(lighting,"vertex")?verts*3:0, normals=!strcmp(lighting,"q2-world")?verts*3:0;
  if(storage){ size_t off=0;
    a.positions=storage+off;off+=verts*4;a.colors=storage+off;off+=verts*4;a.coordinates=storage+off;off+=verts*2;a.coordinates2=storage+off;off+=verts*2;
    a.world_positions=storage+off;off+=world;a.normals=storage+off;off+=normals;a.indices=(uint32_t*)(storage+off);
  } else {
    a.positions=alloc_f(verts*4);a.colors=alloc_f(verts*4);a.coordinates=alloc_f(verts*2);a.coordinates2=alloc_f(verts*2);
    a.world_positions=alloc_f(world);a.normals=alloc_f(normals);a.indices=(uint32_t*)malloc((indices?indices:1)*sizeof(uint32_t)); if(!a.indices)qa_range_error("OpenGL geometry allocation failed");
  }
  a.position_count=verts*4;a.color_count=verts*4;a.coordinate_count=verts*2;a.coordinate2_count=verts*2;
  a.world_position_count=world;a.normal_count=normals;a.index_count=indices; return a;
}
static qa_gl_geometry_arrays pack_into(qa_draw_batch batch,float *storage){
  size_t verts=batch.texturing_is_pair?batch.pair_vertex_count:batch.vertex_count;
  size_t size=batch.primitive_is_lines?2:3;
  if(batch.indices.count%size!=0||batch.indices.count>0x7fffffff) qa_range_error("OpenGL primitive index count is invalid");
  if(verts>0x1fffffff) qa_range_error("OpenGL vertex allocation is too large");
  if(batch.primitive_is_lines&&(!isfinite(f32d(batch.line_width))||batch.line_width<=0)) qa_range_error("OpenGL line width must be positive and finite");
  for(size_t i=0;i<batch.indices.count;i++){ double ix=batch.indices.values[i]; if(!(ix>=0)||ix>=(double)verts||ix!=(double)(int32_t)ix) qa_range_error("OpenGL vertex index is outside its allocation"); }
  qa_gl_geometry_arrays a=alloc_arrays(verts,batch.indices.count,batch.lighting.kind,storage);
  for(size_t i=0;i<batch.indices.count;i++)a.indices[i]=(uint32_t)batch.indices.values[i];
  if(!batch.texturing_is_pair)memset(a.coordinates2,0,a.coordinate2_count*sizeof(float));
  double worst=0; size_t four=0,two=0;
  for(size_t i=0;i<verts;i++){
    qa_vec4 pos,col; qa_vec2 tc;
    if(batch.texturing_is_pair){ pos=batch.pair_vertices[i].vertex.position;col=batch.pair_vertices[i].vertex.color;tc=batch.pair_vertices[i].vertex.tex_coord; }
    else { pos=batch.vertices[i].position;col=batch.vertices[i].color;tc=batch.vertices[i].tex_coord; }
    a.positions[four]=(float)pos.x;a.positions[four+1]=(float)pos.y;a.positions[four+2]=(float)pos.z;a.positions[four+3]=(float)pos.w;
    a.colors[four]=(float)col.x;a.colors[four+1]=(float)col.y;a.colors[four+2]=(float)col.z;a.colors[four+3]=(float)col.w;
    a.coordinates[two]=(float)tc.x;a.coordinates[two+1]=(float)tc.y;
    double m=fmax(fmax(fabs(pos.x),fabs(pos.y)),fmax(fabs(pos.z),fabs(pos.w)));
    m=fmax(m,fmax(fmax(fabs(col.x),fabs(col.y)),fmax(fabs(col.z),fabs(col.w))));
    worst=fmax(worst,fmax(m,fmax(fabs(tc.x),fabs(tc.y))));
    four+=4;two+=2;
  }
  if(batch.texturing_is_pair){ size_t off=0; for(size_t i=0;i<verts;i++){ qa_vec2 t=batch.pair_vertices[i].tex_coord2;
    a.coordinates2[off]=(float)t.x;a.coordinates2[off+1]=(float)t.y; worst=fmax(worst,fmax(fabs(t.x),fabs(t.y))); off+=2; } }
  if(strcmp(batch.lighting.kind,"vertex")){
    if(batch.lighting.world_position_count!=verts) qa_range_error("Q2 world positions must match the draw's vertex count");
    size_t off=0; for(size_t i=0;i<verts;i++){ qa_vec3 p=batch.lighting.world_positions[i];
      a.world_positions[off]=(float)p.x;a.world_positions[off+1]=(float)p.y;a.world_positions[off+2]=(float)p.z;
      worst=fmax(worst,fmax(fabs(p.x),fmax(fabs(p.y),fabs(p.z)))); off+=3; }
    if(!strcmp(batch.lighting.kind,"q2-world")){
      if(batch.lighting.normal_count!=verts) qa_range_error("Q2 normals must match the draw's vertex count");
      off=0; for(size_t i=0;i<verts;i++){ qa_vec3 n=batch.lighting.normals[i];
        a.normals[off]=(float)n.x;a.normals[off+1]=(float)n.y;a.normals[off+2]=(float)n.z;
        worst=fmax(worst,fmax(fabs(n.x),fmax(fabs(n.y),fabs(n.z)))); off+=3; }
    }
  }
  if(!(worst<3.4028235677973366e38)) qa_range_error("OpenGL attributes must be finite float32 values");
  return a;
}
qa_gl_geometry_arrays qa_pack_gl_geometry(qa_draw_batch batch){ return pack_into(batch,NULL); }
typedef struct bimpl { float *storage; size_t storage_floats; qa_gl_geometry_arrays current; bool has_current; } bimpl;
bool qa_gl_geometry_buffer_init(qa_gl_geometry_buffer *b){ b->impl=calloc(1,sizeof(bimpl)); return b->impl!=NULL; }
void qa_gl_geometry_buffer_destroy(qa_gl_geometry_buffer *b){ if(!b->impl)return; free(((bimpl*)b->impl)->storage); free(b->impl); b->impl=NULL; }
qa_gl_geometry_arrays qa_gl_geometry_buffer_pack(qa_gl_geometry_buffer *b,qa_draw_batch batch){
  bimpl *p=(bimpl*)b->impl;
  size_t verts=batch.texturing_is_pair?batch.pair_vertex_count:batch.vertex_count;
  const void *vkey=batch.texturing_is_pair?(const void*)batch.pair_vertices:(const void*)batch.vertices;
  const void *wp=strcmp(batch.lighting.kind,"vertex")?(const void*)batch.lighting.world_positions:NULL;
  const void *nm=!strcmp(batch.lighting.kind,"q2-world")?(const void*)batch.lighting.normals:NULL;
  for(size_t i=0;i<g_cache_count;i++){ cache_entry *e=&g_cache[i];
    if(e->vertices==vkey&&e->indices==batch.indices.values&&!strcmp(e->lighting,batch.lighting.kind)&&e->pair==batch.texturing_is_pair&&e->world_positions==wp&&e->normals==nm){
      if(e->has_arrays)return e->arrays;
      e->seen++; if(e->seen>=2){ e->arrays=pack_into(batch,NULL); e->has_arrays=true; return e->arrays; }
      goto uncached; } }
  if(verts>=64){
    if(g_cache_count==g_cache_cap){ size_t nc=g_cache_cap?g_cache_cap*2:8; cache_entry *n=(cache_entry*)realloc(g_cache,nc*sizeof *n); if(!n)qa_range_error("OpenGL geometry allocation failed"); g_cache=n;g_cache_cap=nc; }
    cache_entry *e=&g_cache[g_cache_count++]; memset(e,0,sizeof *e);
    e->vertices=vkey;e->indices=batch.indices.values;e->lighting=batch.lighting.kind;e->pair=batch.texturing_is_pair;e->world_positions=wp;e->normals=nm;e->seen=1;
  }
uncached:;
  size_t world=strcmp(batch.lighting.kind,"vertex")?verts*3:0, normals=!strcmp(batch.lighting.kind,"q2-world")?verts*3:0;
  size_t need=verts*12+world+normals+batch.indices.count;
  if(p->storage_floats<need){ size_t nc=need>p->storage_floats*2?need:p->storage_floats*2; float *n=(float*)realloc(p->storage,(nc?nc:1)*sizeof(float)); if(!n)qa_range_error("OpenGL geometry allocation failed"); p->storage=n;p->storage_floats=nc; p->has_current=false; }
  p->current=pack_into(batch,p->storage); p->has_current=true; return p->current;
}
