/* Port of src/world/collision/q1/index.ts. Clip-cell LRU cache is
 * performance-only and omitted (prepared deriver used directly).
 * GPL-2.0-or-later. */
#include "qa/world/collision/q1/index.h"
#include "qa/network/common/js_number.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/formats/q1_map/queries.h"
#include "qa/foundation/support.h"
#include "qa/materials/legacy.h"
#include "qa/world/geometry/q1_solid/clipspace.h"
#include "qa/world/geometry/q1_solid/polyhedron.h"
#include "qa/world/geometry/q1_solid/sweep.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
/* StampSet.mark(index) on a Uint32Array: a negative, fractional or NaN index
 * stores nothing and reports a first mark; a C cast of it is undefined. */
static bool mark_number(qa_stamp_set*set,double index){
  return !(index>=0&&index==floor(index)&&index<9007199254740992.0)||qa_stamp_set_mark(set,(size_t)index);
}
static qa_vec3 V0(void){qa_vec3 z={0,0,0};return z;}
static int veq(qa_vec3 a,qa_vec3 b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}
static void basis(qa_vec3 angles,qa_vec3 out[3]){
  if(veq(angles,V0())){out[0]=qa_cell_axes[0];out[1]=qa_cell_axes[1];out[2]=qa_cell_axes[2];return;}
  qa_angle_vectors v=qa_math_angle_vectors(angles);
  out[0]=v.forward; out[1]=qa_cell_scale(v.right,-1); out[2]=v.up;
}
static qa_vec3 to_local(qa_vec3 v,qa_vec3 ax[3]){qa_vec3 r={qa_cell_dot(v,ax[0]),qa_cell_dot(v,ax[1]),qa_cell_dot(v,ax[2])};return r;}
static qa_vec3 from_local(qa_vec3 v,qa_vec3 ax[3]){return qa_cell_add(qa_cell_add(qa_cell_scale(ax[0],v.x),qa_cell_scale(ax[1],v.y)),qa_cell_scale(ax[2],v.z));}
static qa_plane world_plane(qa_plane p,qa_vec3 ax[3],qa_vec3 origin){
  qa_vec3 n=from_local(p.normal,ax);
  qa_plane r={n,p.distance+qa_cell_dot(n,origin)}; return r;
}
static int blocks_default(qa_q1_collision*c,double contents,qa_trace_policy policy){
  if(c->options.blocks_contents)return c->options.blocks_contents(c->options.blocks_ctx,contents,policy);
  return contents==-2;
}
bool qa_q1_collision_init(qa_q1_collision*c,const qa_q1_world_geometry*geometry,qa_q1_collision_options options){
  memset(c,0,sizeof*c);
  c->geometry=geometry; c->options=options;
  if(!qa_stamp_set_init(&c->leaf_stamps,geometry->leaf_count))return false;
  qa_q1_solid_space_init(&c->solid_space,geometry);
  return true;
}
typedef struct{bool has_cell;qa_convex_cell*cell;} BrushCell; /* has_cell with NULL cell: clipped away */
struct qa_q1_authored_cells{BrushCell**models;};
struct qa_q1_clip_cell_entry{
  size_t hull_id; double bounds[6];
  qa_convex_cell**cells; size_t count,faces,vertices;
};
#define CLIP_CELL_ENTRY_LIMIT 128u
#define CLIP_CELL_LIMIT 1024u
#define CLIP_FACE_LIMIT 4096u
#define CLIP_VERTEX_LIMIT 16384u
void qa_q1_collision_destroy(qa_q1_collision*c){
  for(size_t i=0;i<c->clip_cell_entries;i++)qa_convex_cells_destroy(c->clip_cells[i].cells,c->clip_cells[i].count);
  free(c->clip_cells);
  for(size_t i=0;i<c->hull_count;i++)qa_prepared_clip_deriver_destroy(c->prepared_clip?c->prepared_clip[i]:NULL);
  free(c->prepared_clip);
  if(c->authored_cells){
    for(size_t m=0;m<c->geometry->brush_list_count;m++){
      BrushCell*slots=c->authored_cells->models[m];
      if(!slots)continue;
      for(size_t b=0;b<c->geometry->brush_list[m].brush_count;b++)qa_convex_cell_destroy(slots[b].cell);
      free(slots);
    }
    free(c->authored_cells->models);free(c->authored_cells);
  }
  for(size_t i=0;i<c->hull_count;i++)qa_q1_hulls_destroy(&c->hull_cache[i]);
  free(c->hull_cache);free(c->hull_models);
  qa_stamp_set_destroy(&c->leaf_stamps);
  free(c->leaf_scratch);free(c->vis_scratch);
  memset(c,0,sizeof*c);
}
qa_q1_collision *qa_create_q1_collision(const qa_q1_world_geometry*geometry,qa_q1_collision_options options){
  qa_q1_collision*c=malloc(sizeof*c); if(!c)return NULL;
  if(!qa_q1_collision_init(c,geometry,options)){free(c);return NULL;}
  return c;
}
const qa_q1_hull *qa_q1_collision_native_hulls(qa_q1_collision*c,double model,size_t*out_count){
  for(size_t i=0;i<c->hull_count;i++)if(c->hull_models[i]==model){*out_count=3;return c->hull_cache[i].hulls;}
  if(c->hull_count==c->hull_cap){
    c->hull_cap=c->hull_cap?c->hull_cap*2:4;
    c->hull_cache=realloc(c->hull_cache,c->hull_cap*sizeof*c->hull_cache);
    c->hull_models=realloc(c->hull_models,c->hull_cap*sizeof*c->hull_models);
    c->prepared_clip=realloc(c->prepared_clip,c->hull_cap*sizeof*c->prepared_clip);
    if(!c->hull_cache||!c->hull_models||!c->prepared_clip)abort();
  }
  qa_q1_hulls*h=&c->hull_cache[c->hull_count];
  memset(h,0,sizeof*h);
  if(!qa_create_q1_hulls(c->geometry,model,h))abort();
  c->prepared_clip[c->hull_count]=NULL;
  c->hull_models[c->hull_count++]=model;
  *out_count=3; return h->hulls;
}
qa_q1_geometry_coverage qa_q1_collision_geometry_coverage(qa_q1_collision*c,double model){
  const qa_q1_world_geometry*g=c->geometry;
  int found=0;
  if(g->brush_list)for(size_t i=0;i<g->brush_list_count;i++)if(g->brush_list[i].model==model)found=1;
  qa_q1_geometry_coverage r=found?(qa_q1_geometry_coverage){"bspx-brushes","brushes"}:(qa_q1_geometry_coverage){"drawing-bsp-cells","derived-native-clipspace"};
  return r;
}
static int surface_flags(qa_q1_collision*c,double model,qa_vec3 point,qa_plane plane,double*out){
  const qa_q1_world_geometry*g=c->geometry;
  if(!(model>=0&&model<(double)g->model_count&&model==floor(model)))qa_range_error("Unknown Quake model %s",QA_JS_NUMBER(model));
  qa_index_range range=g->models[(size_t)model].faces;
  size_t cap=16,n=0; double*cands=malloc(cap*sizeof*cands); if(!cands)abort();
  for(double i=range.first;i<range.first+range.count;i++){
    if(!(i>=0&&i<(double)g->face_count&&i==floor(i)))qa_range_error("Missing Quake face plane");
    const qa_bsp_face*face=&g->faces[(size_t)i];
    if(!(face->plane>=0&&face->plane<(double)g->plane_count&&face->plane==floor(face->plane)))qa_range_error("Missing Quake face plane");
    qa_bsp_plane authored=g->planes[(size_t)face->plane];
    qa_plane oriented=face->back?(qa_plane){qa_cell_scale(authored.plane.normal,-1),-authored.plane.distance}:authored.plane;
    if(oriented.normal.x==plane.normal.x&&oriented.normal.y==plane.normal.y&&oriented.normal.z==plane.normal.z&&oriented.distance==plane.distance){
      if(n==cap){cap*=2;cands=realloc(cands,cap*sizeof*cands);if(!cands)abort();}
      cands[n++]=i;
    }
  }
  if(n==0){free(cands);return 0;}
  bool found=false;
  double idx=qa_q1_face_at_contact(g,cands,n,point,plane,&found);
  free(cands);
  if(!found)return 0;
  const qa_bsp_face*face=&g->faces[(size_t)idx];
  if(!(face->texture_info>=0&&face->texture_info<(double)g->texture_info_count&&face->texture_info==floor(face->texture_info)))return 0;
  const qa_q1_texture_info*info=&g->texture_info[(size_t)face->texture_info];
  if(!(info->texture>=0&&info->texture<(double)g->texture_count&&info->texture==floor(info->texture)))return 0;
  const qa_q1_mip_texture*tex=g->textures[(size_t)info->texture];
  if(!tex)return 0;
  const char*kind=qa_q1_surface_kind(tex->name);
  *out=(kind&&strcmp(kind,"sky")==0)?4:0;
  return 1;
}
typedef struct{qa_q1_collision*c;qa_trace_policy policy;} BlocksCtx;
static bool blocks_fn(void*ctx,double contents){BlocksCtx*b=ctx;return blocks_default(b->c,contents,b->policy)!=0;}
typedef struct{qa_sweep_interval iv;double contents;size_t order;} Interval;
typedef struct{Interval*v;size_t n,cap;} IvList;
static void iv_push(IvList*l,qa_sweep_interval iv,double contents){
  if(l->n==l->cap){l->cap=l->cap?l->cap*2:16;l->v=realloc(l->v,l->cap*sizeof*l->v);if(!l->v)abort();}
  l->v[l->n]=(Interval){iv,contents,l->n};
  l->n++;
}
typedef struct{qa_q1_collision*c;qa_trace_policy policy;} IncCtx;
static bool include_fn(void*ctx,double contents){IncCtx*i=ctx;return blocks_default(i->c,contents,i->policy)!=0;}
typedef struct{
  qa_vec3 cs,ce; qa_cell_shape shape; IvList*list;
} SweepCtx;
static void sweep_visit(void*ctx,qa_q1_solid_cell cell){
  SweepCtx*s=ctx;
  qa_sweep_interval iv; int hit;
  int is_box=s->shape.kind&&strcmp(s->shape.kind,"box")==0;
  hit=is_box?qa_sweep_box_cell(cell.cell,s->cs,s->ce,s->shape,QA_Q1_DISTANCE_EPSILON,&iv)
            :qa_sweep_capsule_cell(cell.cell,s->cs,s->ce,s->shape,QA_Q1_DISTANCE_EPSILON,&iv);
  if(hit)iv_push(s->list,iv,cell.contents);
  qa_convex_cell_destroy(cell.cell);
}
/* Array.prototype.sort is stable: equal entries keep their discovery order. */
static int iv_cmp(const void*a,const void*b){
  const Interval*l=a,*r=b;
  double d=l->iv.enter-r->iv.enter;
  if(d<0)return -1;
  if(d>0)return 1;
  return l->order<r->order?-1:l->order>r->order?1:0;
}
/* TS derivedClipCells. Cached cells stay owned by the cache (*owned false). */
static qa_convex_cell **derived_clip_cells(qa_q1_collision*c,double model,const qa_q1_hull*hulls,size_t hc,qa_bounds env,size_t*out_count,bool*owned){
  *owned=true;
  qa_vec3 mn=env.min,mx=env.max;
  if(!isfinite(mn.x)||!isfinite(mn.y)||!isfinite(mn.z)||!isfinite(mx.x)||!isfinite(mx.y)||!isfinite(mx.z))
    return qa_derive_q1_clip_solids(hulls,hc,env,out_count);
  size_t hull_id=0;
  while(hull_id<c->hull_count&&c->hull_models[hull_id]!=model)hull_id++;
  if(hull_id==c->hull_count)qa_range_error("Quake clip-cell cache names an unknown hull");
  double bounds[6]={mn.x,mn.y,mn.z,mx.x,mx.y,mx.z};
  for(size_t i=0;i<c->clip_cell_entries;i++){
    qa_q1_clip_cell_entry*entry=&c->clip_cells[i];
    /* Bytewise: -0 and 0 name different envelopes, as TS envelopeCoordinate does. */
    if(entry->hull_id!=hull_id||memcmp(entry->bounds,bounds,sizeof bounds)!=0)continue;
    qa_q1_clip_cell_entry hit=*entry;
    memmove(entry,entry+1,(c->clip_cell_entries-i-1)*sizeof*entry);
    c->clip_cells[c->clip_cell_entries-1]=hit;
    *out_count=hit.count;*owned=false;
    return hit.cells;
  }
  if(c->prepared_clip[hull_id]==NULL)c->prepared_clip[hull_id]=qa_prepare_q1_clip_solids(hulls,hc);
  qa_convex_cell**cells=NULL;size_t count=0;
  if(!qa_prepared_clip_derive(c->prepared_clip[hull_id],env,&cells,&count))cells=qa_derive_q1_clip_solids(hulls,hc,env,&count);
  *out_count=count;
  if(count>CLIP_CELL_LIMIT)return cells;
  size_t faces=0,vertices=0;
  for(size_t i=0;i<count;i++){
    faces+=cells[i]->face_count;
    if(faces>CLIP_FACE_LIMIT)return cells;
    for(size_t f=0;f<cells[i]->face_count;f++){
      vertices+=cells[i]->faces[f].vertex_count;
      if(vertices>CLIP_VERTEX_LIMIT)return cells;
    }
  }
  while(c->clip_cell_entries>=CLIP_CELL_ENTRY_LIMIT||c->clip_cell_count+count>CLIP_CELL_LIMIT
        ||c->clip_face_count+faces>CLIP_FACE_LIMIT||c->clip_vertex_count+vertices>CLIP_VERTEX_LIMIT){
    if(c->clip_cell_entries==0)qa_range_error("Quake clip-cell cache accounting is inconsistent");
    qa_q1_clip_cell_entry oldest=c->clip_cells[0];
    memmove(c->clip_cells,c->clip_cells+1,(c->clip_cell_entries-1)*sizeof*c->clip_cells);
    c->clip_cell_entries--;
    c->clip_cell_count-=oldest.count;c->clip_face_count-=oldest.faces;c->clip_vertex_count-=oldest.vertices;
    qa_convex_cells_destroy(oldest.cells,oldest.count);
  }
  if(c->clip_cells==NULL){
    c->clip_cells=malloc(CLIP_CELL_ENTRY_LIMIT*sizeof*c->clip_cells);
    if(!c->clip_cells)abort();
  }
  qa_q1_clip_cell_entry*slot=&c->clip_cells[c->clip_cell_entries++];
  slot->hull_id=hull_id;memcpy(slot->bounds,bounds,sizeof bounds);
  slot->cells=cells;slot->count=count;slot->faces=faces;slot->vertices=vertices;
  c->clip_cell_count+=count;c->clip_face_count+=faces;c->clip_vertex_count+=vertices;
  *owned=false;
  return cells;
}
/* TS cells(): the brush's convex cell, clipped once. NULL when clipped away. */
static qa_convex_cell *authored_cell(qa_q1_collision*c,size_t list_index,size_t brush_index){
  const qa_q1_brush_list_model*authored=&c->geometry->brush_list[list_index];
  if(!c->authored_cells){
    c->authored_cells=calloc(1,sizeof*c->authored_cells);
    if(c->authored_cells)c->authored_cells->models=calloc(c->geometry->brush_list_count,sizeof*c->authored_cells->models);
    if(!c->authored_cells||!c->authored_cells->models)abort();
  }
  BrushCell**slots=&c->authored_cells->models[list_index];
  if(!*slots){*slots=calloc(authored->brush_count?authored->brush_count:1,sizeof**slots);if(!*slots)abort();}
  BrushCell*slot=&(*slots)[brush_index];
  if(slot->has_cell)return slot->cell;
  const qa_q1_brush_list_brush*brush=&authored->brushes[brush_index];
  qa_convex_cell*cell=qa_box_cell(brush->bounds);
  for(size_t pi=0;pi<brush->plane_count;pi++){
    qa_convex_cell*next=qa_clip_cell(cell,brush->planes[pi]);
    if(next!=cell)qa_convex_cell_destroy(cell);
    cell=next;
    if(!cell)break;
  }
  slot->has_cell=true;slot->cell=cell;
  return cell;
}
qa_trace_result qa_q1_collision_trace(qa_q1_collision*c,qa_trace_query query){
  (void)qa_create_numeric_operations(&query.numeric);
  const qa_q1_world_geometry*g=c->geometry;
  double model=query.target.is_model?query.target.model:0;
  qa_vec3 origin=query.target.is_model?query.target.origin:V0();
  qa_vec3 axis[3]; basis(query.target.is_model?query.target.angles:V0(),axis);
  int is_point=query.shape.kind&&strcmp(query.shape.kind,"point")==0;
  int is_box=query.shape.kind&&strcmp(query.shape.kind,"box")==0;
  int is_capsule=query.shape.kind&&strcmp(query.shape.kind,"capsule")==0;
  qa_bounds bounds=is_point?qa_q1_hull_bounds[0]:query.shape.bounds;
  int has_hull=0; double hull_index=0;
  if(is_point){has_hull=1;hull_index=0;}
  if(query.policy.kind&&strcmp(query.policy.kind,"q1")==0){
    if(query.policy.has_hull){has_hull=1;hull_index=query.policy.hull;}
    else if(is_box&&!query.policy.derived){
      /* SV_HullForEntity chooses by width alone; the hull's own mins then offset the origin. */
      double width=bounds.max.x-bounds.min.x;
      has_hull=1;hull_index=width<3?0:width<=32?1:2;
    }
  }
  if(has_hull){
    size_t hc=0; const qa_q1_hull*hulls=qa_q1_collision_native_hulls(c,model,&hc);
    if(!(hull_index>=0&&hull_index<(double)hc&&hull_index==floor(hull_index)))qa_range_error("Unsupported Quake native hull %s",QA_JS_NUMBER(hull_index));
    const qa_q1_hull*hull=&hulls[(size_t)hull_index];
    qa_vec3 offset=qa_cell_add(origin,qa_cell_sub(hull->clip_bounds.min,bounds.min));
    BlocksCtx bc={c,query.policy};
    qa_q1_hull_trace trace=qa_trace_q1_hull(hull,to_local(qa_cell_sub(query.start,offset),axis),to_local(qa_cell_sub(query.end,offset),axis),query.numeric,blocks_fn,&bc);
    qa_plane plane=world_plane(trace.plane,axis,offset);
    int hit=trace.fraction<1||trace.start_solid;
    double sf=0; int has_sf=0;
    if(hull_index==0&&trace.fraction<1&&!trace.all_solid)has_sf=surface_flags(c,model,trace.end,trace.plane,&sf);
    qa_trace_result r; memset(&r,0,sizeof r);
    r.kind="q1";r.has_surface_flags=has_sf!=0;r.surface_flags=sf;
    r.fraction=trace.fraction;
    r.end=trace.fraction==1?query.end:qa_cell_add(from_local(trace.end,axis),offset);
    r.start_solid=trace.start_solid;r.all_solid=trace.all_solid;r.in_open=trace.in_open;r.in_water=trace.in_water;
    r.source_plane_q1=(qa_plane){plane.normal,trace.plane.distance};
    r.contact=(qa_trace_contact){trace.fraction<1,plane};
    r.hit.kind=hit?QA_TRACE_HIT_WORLD:QA_TRACE_HIT_NONE; r.hit.model=model;
    r.contents=trace.contents;
    return r;
  }
  /* arbitrary shape sweep over cells */
  qa_vec3 center_offset=qa_cell_scale(qa_cell_add(bounds.min,bounds.max),0.5);
  qa_vec3 dims=qa_cell_sub(bounds.max,bounds.min);
  qa_vec3 extents=qa_cell_scale(dims,0.5);
  qa_vec3 cs=to_local(qa_cell_sub(qa_cell_add(query.start,center_offset),origin),axis);
  qa_vec3 ce=to_local(qa_cell_sub(qa_cell_add(query.end,center_offset),origin),axis);
  qa_vec3 local_axes[3]={to_local(qa_cell_axes[0],axis),to_local(qa_cell_axes[1],axis),to_local(qa_cell_axes[2],axis)};
  double radius=qa_js_min(extents.x,qa_js_min(extents.y,extents.z));
  qa_cell_shape shape; memset(&shape,0,sizeof shape);
  if(is_capsule){shape.kind="capsule";shape.axis=local_axes[2];shape.radius=radius;shape.half_segment=qa_js_max(0,extents.z-radius);}
  else{shape.kind="box";shape.axes[0]=local_axes[0];shape.axes[1]=local_axes[1];shape.axes[2]=local_axes[2];shape.extents=extents;}
  qa_vec3 pad={qa_shape_support(shape,qa_cell_axes[0])+1,qa_shape_support(shape,qa_cell_axes[1])+1,qa_shape_support(shape,qa_cell_axes[2])+1};
  #define ENV_TO(E) ((qa_bounds){{qa_js_min(cs.x,(E).x)-pad.x,qa_js_min(cs.y,(E).y)-pad.y,qa_js_min(cs.z,(E).z)-pad.z},{qa_js_max(cs.x,(E).x)+pad.x,qa_js_max(cs.y,(E).y)+pad.y,qa_js_max(cs.z,(E).z)+pad.z}})
  qa_bounds envelope=ENV_TO(ce);
  IvList intervals={0};
  /* cells: authored brushlist or solid space */
  const qa_q1_brush_list_model*authored=NULL;
  size_t authored_index=0;
  if(g->brush_list)for(size_t i=0;i<g->brush_list_count;i++)if(g->brush_list[i].model==model){authored=&g->brush_list[i];authored_index=i;break;}
  SweepCtx sw={cs,ce,shape,&intervals};
  if(authored){
    for(size_t bi=0;bi<authored->brush_count;bi++){
      const qa_q1_brush_list_brush*brush=&authored->brushes[bi];
      if(!blocks_default(c,brush->contents,query.policy))continue;
      qa_bounds box=brush->bounds;
      if(box.max.x<envelope.min.x||box.min.x>envelope.max.x||box.max.y<envelope.min.y||box.min.y>envelope.max.y
         ||box.max.z<envelope.min.z||box.min.z>envelope.max.z)continue;
      qa_convex_cell*cell=authored_cell(c,authored_index,bi);
      if(cell){
        qa_sweep_interval iv;
        int hit=strcmp(shape.kind,"box")==0?qa_sweep_box_cell(cell,cs,ce,shape,QA_Q1_DISTANCE_EPSILON,&iv)
                                            :qa_sweep_capsule_cell(cell,cs,ce,shape,QA_Q1_DISTANCE_EPSILON,&iv);
        if(hit)iv_push(&intervals,iv,brush->contents);
      }
    }
  }else{
    IncCtx inc={c,query.policy};
    qa_q1_solid_space_cells(&c->solid_space,envelope,model,include_fn,&inc,sweep_visit,&sw);
  }
  if(!authored&&blocks_default(c,-2,query.policy)){
    size_t drawing_count=intervals.n;
    double first=1;
    for(size_t i=0;i<intervals.n;i++)first=qa_js_min(first,intervals.v[i].iv.enter); /* Math.min: NaN propagates */
    int use_prefix=first>0&&first<1;
    qa_vec3 prefix_end=qa_cell_lerp(cs,ce,first);
    qa_bounds prefix=use_prefix?ENV_TO(prefix_end):envelope;
    size_t hc=0; const qa_q1_hull*hulls=qa_q1_collision_native_hulls(c,model,&hc);
    /* collect function inline twice */
    for(int pass=0;pass<2;pass++){
      if(pass==1&&!use_prefix)break;
      qa_bounds bnd=pass==0?prefix:envelope;
      qa_convex_cell**cells=NULL; size_t cc=0; bool owned=true;
      cells=derived_clip_cells(c,model,hulls,hc,bnd,&cc,&owned);
      int starts_solid=0;
      for(size_t i=0;i<cc;i++){
        qa_sweep_interval iv; int hit=is_box||!is_capsule?qa_sweep_box_cell(cells[i],cs,ce,shape,QA_Q1_DISTANCE_EPSILON,&iv):qa_sweep_capsule_cell(cells[i],cs,ce,shape,QA_Q1_DISTANCE_EPSILON,&iv);
        if(is_capsule)hit=qa_sweep_capsule_cell(cells[i],cs,ce,shape,QA_Q1_DISTANCE_EPSILON,&iv);
        if(hit){iv_push(&intervals,iv,-2);if(iv.enter<=0)starts_solid=1;}
      }
      if(owned)qa_convex_cells_destroy(cells,cc);
      if(pass==0){
        if(starts_solid&&use_prefix){intervals.n=drawing_count;continue;}
        break;
      }
    }
  }
  #undef ENV_TO
  if(intervals.n>0)qa_js_sort(intervals.v,intervals.n,sizeof*intervals.v,iv_cmp);
  int start_solid=0; double covered=-INFINITY,fraction=1,contents=-1; qa_plane plane={V0(),0};
  for(size_t i=0;i<intervals.n;i++){
    Interval*iv=&intervals.v[i];
    if(iv->iv.enter<=0){start_solid=1;contents=iv->contents;covered=qa_js_max(covered,iv->iv.exit);continue;}
    if(start_solid&&iv->iv.enter<=covered+1e-8){covered=qa_js_max(covered,iv->iv.exit);continue;}
    fraction=iv->iv.contact<0?0:iv->iv.contact; plane=iv->iv.plane; contents=iv->contents; break;
  }
  free(intervals.v);
  size_t hc2=0; const qa_q1_hull*hulls2=qa_q1_collision_native_hulls(c,model,&hc2);
  const qa_q1_hull*point_hull=&hulls2[0];
  qa_vec3 env_end=qa_cell_lerp(query.start,query.end,fraction);
  qa_q1_hull_trace environment=qa_trace_q1_hull(point_hull,to_local(qa_cell_sub(query.start,origin),axis),to_local(qa_cell_sub(env_end,origin),axis),query.numeric,NULL,NULL);
  qa_plane transformed=world_plane(plane,axis,origin);
  qa_trace_result r; memset(&r,0,sizeof r);
  r.kind="q1";r.fraction=fraction;
  r.end=fraction==1?query.end:qa_cell_lerp(query.start,query.end,fraction);
  r.start_solid=start_solid!=0;r.all_solid=start_solid&&covered>=1;
  r.in_open=environment.in_open;r.in_water=environment.in_water;
  r.source_plane_q1=(qa_plane){transformed.normal,plane.distance};
  r.contact=(qa_trace_contact){fraction<1,transformed};
  r.hit.kind=(fraction<1||start_solid)?QA_TRACE_HIT_WORLD:QA_TRACE_HIT_NONE; r.hit.model=model;
  r.contents=contents;
  return r;
}
qa_point_contents_result qa_q1_collision_point_contents(qa_q1_collision*c,qa_point_contents_query query){
  double model=query.target.is_model?query.target.model:0;
  qa_vec3 point=query.point;
  if(query.target.is_model){
    qa_vec3 ax[3]; basis(query.target.angles,ax);
    point=to_local(qa_cell_sub(query.point,query.target.origin),ax);
  }
  size_t hc=0; const qa_q1_hull*hulls=qa_q1_collision_native_hulls(c,model,&hc);
  qa_point_contents_result r={"q1",qa_q1_hull_point_contents(&hulls[0],point,query.numeric),0,0}; return r;
}
double qa_q1_collision_leaf_at(qa_q1_collision*c,qa_vec3 point,double model){
  const qa_q1_world_geometry*g=c->geometry;
  if(!(model>=0&&model<(double)g->model_count&&model==floor(model))||g->models[(size_t)model].headnodes.count==0)qa_range_error("Unknown Quake model %s",QA_JS_NUMBER(model));
  double root=g->models[(size_t)model].headnodes.values[0];
  qa_bsp_child child; child.is_node=root>=0; child.index=root<0?-1-root:root;
  for(size_t depth=0;depth<=g->node_count;depth++){
    if(!child.is_node)return child.index;
    if(!(child.index>=0&&child.index<(double)g->node_count&&child.index==floor(child.index)))qa_range_error("Invalid Quake BSP node");
    const qa_bsp_node*node=&g->nodes[(size_t)child.index];
    if(!(node->plane>=0&&node->plane<(double)g->plane_count&&node->plane==floor(node->plane)))qa_range_error("Invalid Quake BSP node");
    qa_bsp_plane plane=g->planes[(size_t)node->plane];
    child=node->children[qa_cell_dot(point,plane.plane.normal)-plane.plane.distance<0?1:0];
  }
  qa_range_error("Cycle in Quake BSP"); return 0;
}
double qa_q1_collision_point_leaf(qa_q1_collision*c,qa_vec3 p,double m){return qa_q1_collision_leaf_at(c,p,m);}
double qa_q1_collision_leaf_cluster(qa_q1_collision*c,double leaf){(void)c;return leaf==0?-1:leaf-1;}
double qa_q1_collision_leaf_area(qa_q1_collision*c,double leaf){(void)c;(void)leaf;return 0;}
qa_bounds qa_q1_collision_model_bounds(qa_q1_collision*c,double model){
  if(!(model>=0&&model<(double)c->geometry->model_count&&model==floor(model)))qa_range_error("Unknown Quake model %s",QA_JS_NUMBER(model));
  return c->geometry->models[(size_t)model].bounds;
}
static uint8_t one_byte=1;
const uint8_t *qa_q1_collision_area_bits(qa_q1_collision*c,double area,size_t*out){(void)c;(void)area;*out=1;one_byte=1;return &one_byte;}
bool qa_q1_collision_areas_connected(qa_q1_collision*c,double a,double b){(void)c;return a==0&&b==0;}
qa_leaf_query_result qa_q1_collision_box_leaves(qa_q1_collision*c,qa_bounds bounds,double limit){
  const qa_q1_world_geometry*g=c->geometry;
  if(g->model_count==0||g->models[0].headnodes.count==0)qa_range_error("Missing Quake world root");
  double root=g->models[0].headnodes.values[0];
  free(c->leaf_scratch);c->leaf_scratch=NULL;c->leaf_scratch_count=0;
  size_t cap=16,n=0; double*leaves=malloc(cap*sizeof*leaves); if(!leaves)abort();
  qa_stamp_set_begin(&c->leaf_stamps);
  int has_top=0; double topnode=0; int overflow=0;
  size_t scap=64,sp=0; qa_bsp_child*stack=malloc(scap*sizeof*stack); if(!stack)abort();
  qa_bsp_child first; first.is_node=root>=0; first.index=root<0?-1-root:root;
  stack[sp++]=first;
  while(sp>0){
    qa_bsp_child child=stack[--sp];
    if(!child.is_node){
      if(child.index==0||!mark_number(&c->leaf_stamps,child.index))continue;
      if((double)n<limit){if(n==cap){cap*=2;leaves=realloc(leaves,cap*sizeof*leaves);if(!leaves)abort();}leaves[n++]=child.index;}
      else overflow=1;
      continue;
    }
    if(!(child.index>=0&&child.index<(double)g->node_count&&child.index==floor(child.index)))qa_range_error("Invalid Quake leaf query node");
    const qa_bsp_node*node=&g->nodes[(size_t)child.index];
    if(!(node->plane>=0&&node->plane<(double)g->plane_count&&node->plane==floor(node->plane)))qa_range_error("Invalid Quake leaf query node");
    qa_bsp_plane plane=g->planes[(size_t)node->plane];
    qa_vec3 center=qa_cell_scale(qa_cell_add(bounds.min,bounds.max),0.5);
    qa_vec3 ext=qa_cell_scale(qa_cell_sub(bounds.max,bounds.min),0.5);
    double d=qa_cell_dot(center,plane.plane.normal)-plane.plane.distance;
    double rr=fabs(plane.plane.normal.x)*ext.x+fabs(plane.plane.normal.y)*ext.y+fabs(plane.plane.normal.z)*ext.z;
    if(sp+2>scap){scap*=2;stack=realloc(stack,scap*sizeof*stack);if(!stack)abort();}
    if(d>=rr)stack[sp++]=node->children[0];
    else if(d<-rr)stack[sp++]=node->children[1];
    else{if(!has_top){has_top=1;topnode=child.index;}stack[sp++]=node->children[1];stack[sp++]=node->children[0];}
  }
  free(stack);
  c->leaf_scratch=leaves;c->leaf_scratch_count=n;
  qa_leaf_query_result r; memset(&r,0,sizeof r);
  r.leaves=(qa_double_view){leaves,n};r.has_topnode=has_top!=0;r.topnode=topnode;r.overflow=overflow!=0;
  return r;
}
static void visibility_row(qa_q1_collision*c,double leaf,int phs,uint8_t**out,size_t*out_len){
  size_t len=0; uint8_t*row=qa_q1_leaf_pvs(c->geometry,leaf,&len);
  if(phs){
    uint8_t*src=malloc(len?len:1); if(!src)abort(); memcpy(src,row,len);
    for(size_t i=1;i<c->geometry->leaf_count;i++){
      size_t bit=i-1;
      if((bit>>3)>=len||((src[bit>>3])&(1u<<(bit&7)))==0)continue; /* source[...] ?? 0 */
      size_t vl=0; uint8_t*vis=qa_q1_leaf_pvs(c->geometry,(double)i,&vl);
      for(size_t j=0;j<len&&j<vl;j++)row[j]=(uint8_t)(row[j]|vis[j]);
      free(vis);
    }
    free(src);
  }
  *out=row;*out_len=len;
}
bool qa_q1_collision_cluster_visible(qa_q1_collision*c,double from,double to,const char*kind){
  if(to<0)return false;
  if(from<0)return true;
  uint8_t*row=NULL; size_t len=0;
  visibility_row(c,from+1,kind&&strcmp(kind,"phs")==0,&row,&len);
  /* row[to >> 3] ?? 0 and 1 << (to & 7): both operands are ToInt32 of `to`. */
  const int32_t word=qa_js_to_int32(to);
  const int32_t idx=word>>3;
  int r=idx>=0&&(size_t)idx<len&&((row[idx])&(1u<<(word&7)))!=0;
  free(row); return r!=0;
}
static qa_trace_result sq_trace(void*ctx,qa_trace_query q){return qa_q1_collision_trace((qa_q1_collision*)ctx,q);}
static qa_point_contents_result sq_pc(void*ctx,qa_point_contents_query q){return qa_q1_collision_point_contents((qa_q1_collision*)ctx,q);}
static qa_leaf_query_result sq_bl(void*ctx,qa_bounds b,double limit){return qa_q1_collision_box_leaves((qa_q1_collision*)ctx,b,limit);}
static bool sq_ac(void*ctx,double a,double b){return qa_q1_collision_areas_connected((qa_q1_collision*)ctx,a,b);}
static bool sq_cv(void*ctx,double a,double b,const char*k){return qa_q1_collision_cluster_visible((qa_q1_collision*)ctx,a,b,k);}
qa_scene_queries qa_q1_collision_as_scene_queries(qa_q1_collision*c){
  qa_scene_queries q={c,sq_trace,sq_pc,sq_bl,sq_ac,sq_cv}; return q;
}
