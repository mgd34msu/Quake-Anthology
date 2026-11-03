/* Port of src/world/geometry/q1-solid/clipspace.ts. Prepared deriver
 * stores hull copies + prepared bounds; per-envelope work reuses the
 * same subtraction over prepared free cells (grid index omitted as a
 * pure performance device; results identical). GPL-2.0-or-later. */
#include "qa/world/geometry/q1_solid/clipspace.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
static void *xm(size_t n){void*p=malloc(n?n:1);if(!p)abort();return p;}
static void *xr(void*q,size_t n){void*p=realloc(q,n?n:1);if(!p)abort();return p;}
typedef struct{qa_convex_cell**v;size_t n,cap;} CellList;
static void cl_push(CellList*l,qa_convex_cell*c){if(l->n==l->cap){l->cap=l->cap?l->cap*2:8;l->v=xr(l->v,l->cap*sizeof*l->v);}l->v[l->n++]=c;}
void qa_convex_cells_destroy(qa_convex_cell**cells,size_t n){if(!cells)return;for(size_t i=0;i<n;i++)qa_convex_cell_destroy(cells[i]);free(cells);}
typedef struct{qa_q1_clip_child child;qa_convex_cell*cell;double depth;} Frame;
static void free_cells(const qa_q1_hull*hull,qa_bounds env,CellList*out){
  qa_q1_clip_child root; if(hull->first_clipnode<0){root.is_clipnode=false;root.value=hull->first_clipnode;}else{root.is_clipnode=true;root.value=hull->first_clipnode;}
  size_t cap=16,n=0; Frame*st=xm(cap*sizeof*st);
  st[n++]=(Frame){root,qa_box_cell(env),0};
  while(n>0){Frame fr=st[--n];
    if(!fr.child.is_clipnode){if(fr.child.value!=-2)cl_push(out,fr.cell);else qa_convex_cell_destroy(fr.cell);continue;}
    if(fr.depth>(double)hull->clipnode_count)qa_range_error("Cycle in Quake clip-space BSP");
    size_t idx=(size_t)fr.child.value;
    if(idx>=hull->clipnode_count)qa_range_error("Invalid Quake clip-space node");
    const qa_q1_clip_node*node=&hull->clipnodes[idx];
    if(!(node->plane>=0&&node->plane<(double)hull->plane_count&&node->plane==floor(node->plane)))qa_range_error("Invalid Quake clip-space node");
    qa_plane pl=hull->planes[(size_t)node->plane].plane;
    qa_split_result sp=qa_split_cell(fr.cell,pl);
    int shared=(sp.front==fr.cell&&sp.back==fr.cell);
    if(shared){
      qa_convex_cell*dup=qa_convex_cell_clone(fr.cell);
      if(n+2>cap){cap*=2;st=xr(st,cap*sizeof*st);}
      st[n++]=(Frame){node->children[1],dup,fr.depth+1}; st[n++]=(Frame){node->children[0],fr.cell,fr.depth+1}; continue;
    }
    if(sp.front!=fr.cell&&sp.back!=fr.cell)qa_convex_cell_destroy(fr.cell);
    if(n+2>cap){cap*=2;st=xr(st,cap*sizeof*st);}
    if(sp.back)st[n++]=(Frame){node->children[1],sp.back,fr.depth+1};
    if(sp.front)st[n++]=(Frame){node->children[0],sp.front,fr.depth+1};
  }
  free(st);
}
static void subtract_region(qa_convex_cell*cell,const qa_plane*planes,size_t pc,CellList*out){
  qa_convex_cell*inside=cell;
  for(size_t i=0;i<pc;i++){
    qa_split_result sp=qa_split_cell(inside,planes[i]);
    int shared=(sp.front==inside&&sp.back==inside);
    if(shared){ /* TS keeps one object as both remainder and back; C owns a copy of each. */
      qa_convex_cell*dup=qa_convex_cell_clone(inside);
      cl_push(out,dup); continue;
    }
    if(sp.front)cl_push(out,sp.front);
    if(sp.front!=inside&&sp.back!=inside)qa_convex_cell_destroy(inside);
    inside=sp.back; if(!inside)break;
  }
  /* TS discards what stays inside every plane. */
  if(inside)qa_convex_cell_destroy(inside);
}
qa_convex_cell **qa_derive_q1_clip_solids(const qa_q1_hull*hulls,size_t hc,qa_bounds env,size_t*out_count){
  CellList solids={0}; cl_push(&solids,qa_box_cell(env)); int used=0;
  for(size_t hi=1;hi<hc;hi++){
    const qa_q1_hull*hull=&hulls[hi];
    if(hull->first_clipnode<0&&hull->first_clipnode!=-2)continue;
    used=1; qa_bounds size=hull->clip_bounds;
    qa_vec3 center=qa_cell_scale(qa_cell_add(size.min,size.max),0.5),ext=qa_cell_scale(qa_cell_sub(size.max,size.min),0.5);
    qa_vec3 one={1,1,1};
    qa_bounds fe={qa_cell_sub(env.min,qa_cell_add(size.max,one)),qa_cell_sub(env.max,qa_cell_sub(size.min,one))};
    CellList free_={0}; free_cells(hull,fe,&free_);
    for(size_t fi=0;fi<free_.n;fi++){
      size_t pc=0; qa_plane*sep=qa_box_separating_planes(free_.v[fi],qa_cell_axes,3,&pc);
      qa_plane*exp=xm((pc?pc:1)*sizeof*exp);
      for(size_t i=0;i<pc;i++)exp[i]=(qa_plane){sep[i].normal,sep[i].distance+qa_cell_dot(sep[i].normal,center)+fabs(sep[i].normal.x)*ext.x+fabs(sep[i].normal.y)*ext.y+fabs(sep[i].normal.z)*ext.z};
      free(sep);
      CellList next={0};
      for(size_t si=0;si<solids.n;si++)subtract_region(solids.v[si],exp,pc,&next);
      free(solids.v); solids=next; free(exp);
      if(solids.n==0)break;
    }
    qa_convex_cells_destroy(free_.v,free_.n);
    if(solids.n==0)break;
  }
  if(!used){qa_convex_cells_destroy(solids.v,solids.n);*out_count=0;return NULL;}
  *out_count=solids.n; return solids.v;
}
/* --- TS prepareQ1ClipSolids: whole-hull free cells and a 512-unit grid. --- */
#define CLIP_GRID 512.0
typedef struct{qa_plane*expanded;size_t plane_count;qa_bounds bounds;} PreparedFreeCell;
typedef struct{double x,y,z;uint32_t index;} GridItem;
typedef struct{
  PreparedFreeCell*entries;size_t entry_count;
  GridItem*grid;size_t grid_count; /* sorted by key, then entry order */
  uint32_t*always;size_t always_count;
  int32_t*seen;int32_t stamp;
} HullIndex;
struct qa_prepared_clip_deriver{HullIndex*hulls;size_t count;qa_bounds prepared;int used;};
static qa_bounds cell_bounds(const qa_convex_cell*cell){
  size_t vc=0;qa_vec3*v=qa_cell_vertices(cell,&vc);
  qa_bounds r={{INFINITY,INFINITY,INFINITY},{-INFINITY,-INFINITY,-INFINITY}};
  for(size_t i=0;i<vc;i++){
    r.min.x=qa_js_min(r.min.x,v[i].x);r.min.y=qa_js_min(r.min.y,v[i].y);r.min.z=qa_js_min(r.min.z,v[i].z);
    r.max.x=qa_js_max(r.max.x,v[i].x);r.max.y=qa_js_max(r.max.y,v[i].y);r.max.z=qa_js_max(r.max.z,v[i].z);
  }
  free(v);
  return r;
}
static int grid_cmp(const void*A,const void*B){
  const GridItem*a=A,*b=B;
  if(a->x!=b->x)return a->x<b->x?-1:1;
  if(a->y!=b->y)return a->y<b->y?-1:1;
  if(a->z!=b->z)return a->z<b->z?-1:1;
  return a->index<b->index?-1:a->index>b->index?1:0;
}
static void build_grid(HullIndex*index){
  size_t cap=0;
  for(size_t e=0;e<index->entry_count;e++){
    qa_bounds b=index->entries[e].bounds;
    double x0=floor(b.min.x/CLIP_GRID),x1=floor(b.max.x/CLIP_GRID);
    double y0=floor(b.min.y/CLIP_GRID),y1=floor(b.max.y/CLIP_GRID);
    double z0=floor(b.min.z/CLIP_GRID),z1=floor(b.max.z/CLIP_GRID);
    if((x1-x0+1)*(y1-y0+1)*(z1-z0+1)>16){index->always[index->always_count++]=(uint32_t)e;continue;}
    for(double x=x0;x<=x1;x++)for(double y=y0;y<=y1;y++)for(double z=z0;z<=z1;z++){
      if(index->grid_count==cap){cap=cap?cap*2:64;index->grid=xr(index->grid,cap*sizeof*index->grid);}
      index->grid[index->grid_count++]=(GridItem){x,y,z,(uint32_t)e};
    }
  }
  if(index->grid_count>1)qa_js_sort(index->grid,index->grid_count,sizeof*index->grid,grid_cmp);
}
qa_prepared_clip_deriver *qa_prepare_q1_clip_solids(const qa_q1_hull*hulls,size_t hc){
  qa_prepared_clip_deriver*d=xm(sizeof*d); memset(d,0,sizeof*d);
  qa_bounds b={{0,0,0},{0,0,0}}; int have=0;
  for(size_t i=1;i<hc;i++){if(hulls[i].first_clipnode<0&&hulls[i].first_clipnode!=-2)continue;d->used=1;qa_bounds s=hulls[i].clip_bounds;
    if(!have){b=s;have=1;}else{b.min.x=qa_js_min(b.min.x,s.min.x);b.min.y=qa_js_min(b.min.y,s.min.y);b.min.z=qa_js_min(b.min.z,s.min.z);b.max.x=qa_js_max(b.max.x,s.max.x);b.max.y=qa_js_max(b.max.y,s.max.y);b.max.z=qa_js_max(b.max.z,s.max.z);}}
  if(!d->used)return d;
  d->prepared=(qa_bounds){{b.min.x-4096,b.min.y-4096,b.min.z-4096},{b.max.x+4096,b.max.y+4096,b.max.z+4096}};
  d->hulls=xm((hc?hc:1)*sizeof*d->hulls); memset(d->hulls,0,(hc?hc:1)*sizeof*d->hulls);
  qa_vec3 one={1,1,1};
  for(size_t i=1;i<hc;i++){
    const qa_q1_hull*hull=&hulls[i];
    if(hull->first_clipnode<0&&hull->first_clipnode!=-2)continue;
    qa_bounds size=hull->clip_bounds;
    qa_vec3 center=qa_cell_scale(qa_cell_add(size.min,size.max),0.5),ext=qa_cell_scale(qa_cell_sub(size.max,size.min),0.5);
    qa_vec3 grow_max=qa_cell_add(size.max,one),grow_min=qa_cell_sub(size.min,one);
    qa_bounds free_env={qa_cell_sub(d->prepared.min,grow_max),qa_cell_sub(d->prepared.max,grow_min)};
    CellList free_={0}; free_cells(hull,free_env,&free_);
    HullIndex*index=&d->hulls[d->count++];
    index->entry_count=free_.n;
    index->entries=xm((free_.n?free_.n:1)*sizeof*index->entries);
    index->always=xm((free_.n?free_.n:1)*sizeof*index->always);
    index->seen=xm((free_.n?free_.n:1)*sizeof*index->seen);
    memset(index->seen,0,(free_.n?free_.n:1)*sizeof*index->seen);
    for(size_t fi=0;fi<free_.n;fi++){
      size_t pc=0; qa_plane*sep=qa_box_separating_planes(free_.v[fi],qa_cell_axes,3,&pc);
      for(size_t k=0;k<pc;k++)sep[k].distance+=qa_cell_dot(sep[k].normal,center)+fabs(sep[k].normal.x)*ext.x+fabs(sep[k].normal.y)*ext.y+fabs(sep[k].normal.z)*ext.z;
      qa_bounds fb=cell_bounds(free_.v[fi]);
      index->entries[fi]=(PreparedFreeCell){sep,pc,{qa_cell_sub(fb.min,grow_max),qa_cell_sub(fb.max,grow_min)}};
    }
    qa_convex_cells_destroy(free_.v,free_.n);
    build_grid(index);
  }
  return d;
}
void qa_prepared_clip_deriver_destroy(qa_prepared_clip_deriver*d){
  if(!d)return;
  for(size_t i=0;i<d->count;i++){
    HullIndex*index=&d->hulls[i];
    for(size_t e=0;e<index->entry_count;e++)free(index->entries[e].expanded);
    free(index->entries);free(index->grid);free(index->always);free(index->seen);
  }
  free(d->hulls);free(d);
}
static int overlaps(qa_bounds a,qa_bounds b){return a.min.x<=b.max.x&&a.max.x>=b.min.x&&a.min.y<=b.max.y&&a.max.y>=b.min.y&&a.min.z<=b.max.z&&a.max.z>=b.min.z;}
/* TS consider(): false once no solid remains. */
static bool consider_entry(HullIndex*hull,uint32_t index,qa_bounds env,CellList*solids){
  if(hull->seen[index]==hull->stamp)return true;
  hull->seen[index]=hull->stamp;
  const PreparedFreeCell*entry=&hull->entries[index];
  if(overlaps(entry->bounds,env)){
    CellList next={0};
    for(size_t si=0;si<solids->n;si++)subtract_region(solids->v[si],entry->expanded,entry->plane_count,&next);
    free(solids->v);*solids=next;
  }
  return solids->n>0;
}
bool qa_prepared_clip_derive(qa_prepared_clip_deriver*d,qa_bounds env,qa_convex_cell***out,size_t*out_count){
  if(!d->used){*out=NULL;*out_count=0;return true;}
  qa_bounds p=d->prepared;
  if(!overlaps(env,p)||env.min.x<p.min.x||env.min.y<p.min.y||env.min.z<p.min.z||env.max.x>p.max.x||env.max.y>p.max.y||env.max.z>p.max.z)return false;
  CellList solids={0}; cl_push(&solids,qa_box_cell(env));
  double x0=floor(env.min.x/CLIP_GRID),x1=floor(env.max.x/CLIP_GRID);
  double y0=floor(env.min.y/CLIP_GRID),y1=floor(env.max.y/CLIP_GRID);
  double z0=floor(env.min.z/CLIP_GRID),z1=floor(env.max.z/CLIP_GRID);
  for(size_t h=0;h<d->count;h++){
    HullIndex*hull=&d->hulls[h];
    hull->stamp++;
    for(size_t a=0;a<hull->always_count;a++)
      if(!consider_entry(hull,hull->always[a],env,&solids))goto done;
    for(double x=x0;x<=x1;x++)for(double y=y0;y<=y1;y++)for(double z=z0;z<=z1;z++){
      GridItem key={x,y,z,0};
      size_t low=0,high=hull->grid_count;
      while(low<high){size_t middle=(low+high)/2;if(grid_cmp(&hull->grid[middle],&key)<0)low=middle+1;else high=middle;}
      for(;low<hull->grid_count&&hull->grid[low].x==x&&hull->grid[low].y==y&&hull->grid[low].z==z;low++)
        if(!consider_entry(hull,hull->grid[low].index,env,&solids))goto done;
    }
  }
done:
  *out=solids.v;*out_count=solids.n;
  return true;
}
