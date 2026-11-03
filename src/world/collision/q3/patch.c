/* Port of src/world/collision/q3/patch.ts. Allocator path: reserves via
 * callback for accounting parity but stores records in owned arrays.
 * GPL-2.0-or-later. */
#include "qa/world/collision/q3/patch.h"
#include "qa/network/common/js_number.h"
#include "qa/core/common_error.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include <math.h>
#include "qa/foundation/js.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static float f32(double v){return (float)v;}
static void *xm(size_t n){void*p=malloc(n?n:1);if(!p)abort();return p;}
static void *xr(void*q,size_t n){void*p=realloc(q,n?n:1);if(!p)abort();return p;}
static qa_vec3 V(double x,double y,double z){return qa_math_vec3(x,y,z);}
static const qa_patch_plane *pat(const qa_patch_collide*p,double idx){
  if(!(idx>=0&&idx<(double)p->plane_count&&idx==floor(idx)))qa_range_error("patch index %s out of range",QA_JS_NUMBER(idx));
  return &p->planes[(size_t)idx];
}
void qa_patch_collide_destroy(qa_patch_collide*p){
  if(!p)return;
  for(size_t i=0;i<p->facet_count;i++)free(p->facets[i].borders);
  free(p->facets);free(p->planes);free(p);
}
/* Debug surface */
void qa_collision_debug_surface_init(qa_collision_debug_surface*d,qa_collision_debug_host host){
  memset(d,0,sizeof*d); d->host=host;
  qa_collision_winding_library_init(&d->windings,host.windings);
}
void qa_collision_debug_print_winding(qa_collision_debug_surface*d,qa_collision_winding*w){qa_winding_pw(&d->windings,w,(qa_winding_print_fn)d->host.print,d->host.ctx);}
void qa_collision_debug_print(qa_collision_debug_surface*d,const char*t){if(d->host.print)d->host.print(d->host.ctx,t);}
void qa_collision_debug_developer_print(qa_collision_debug_surface*d,const char*t){if(d->host.developer_print)d->host.developer_print(d->host.ctx,t);}
void qa_collision_debug_clear_level_patches(qa_collision_debug_surface*d){d->hit_patch=NULL;d->hit_facet=NULL;}
static qa_cvar_snapshot cvar_(qa_collision_debug_surface*d,const char*name){
  qa_cvar_snapshot v={0};
  if(!qa_cvar_registry_get(d->host.cvars,name,&v))qa_range_error("Collision debug cvar %s is not registered",name);
  return v;
}
void qa_collision_debug_record_trace(qa_collision_debug_surface*d,const qa_patch_collide*patch,const qa_patch_facet*facet,bool is_point){
  if(is_point?!d->point_update_registered:!d->shape_update_registered){
    qa_cvar_snapshot out={0}; qa_cvar_registry_register(d->host.cvars,"r_debugSurfaceUpdate","1",0,&out); qa_cvar_read_free(&out);
    if(is_point)d->point_update_registered=true; else d->shape_update_registered=true;
  }
  qa_cvar_snapshot v=cvar_(d,"r_debugSurfaceUpdate");
  if(v.integer_value!=0){d->hit_patch=patch;d->hit_facet=facet;}
  qa_cvar_read_free(&v);
}
void qa_collision_debug_record_mixed_border(qa_collision_debug_surface*d,const qa_vec3 pts[4]){
  qa_collision_debug_developer_print(d,"WARNING: CM_SetBorderInward: mixed plane sides\n");
  if(d->debug_block)return;
  d->debug_block=true;
  for(int i=0;i<4;i++)d->debug_block_points[i]=pts[i];
}
static qa_plane debug_plane(qa_collision_debug_surface*d,qa_plane plane,int flip,double direction){
  qa_vec3 normal=flip?qa_sub3(V(0,0,0),plane.normal):plane.normal;
  qa_vec3 corner=V(normal.x>0?15:-15,normal.y>0?15:-15,normal.z>0?28:-28);
  qa_cvar_snapshot v=cvar_(d,"cm_debugSize");
  double dist=f32((flip?-plane.distance:plane.distance)+direction*v.numeric_value);
  qa_cvar_read_free(&v);
  qa_plane r={normal,f32(dist+direction*fabs(qa_dot3_double(corner,qa_scale3(normal,-1))))}; return r;
}
void qa_collision_debug_draw(qa_collision_debug_surface*d,qa_collision_debug_polygon_fn draw,void*ctx){
  if(!d->hit_patch)return;
  if(!d->size_registered){qa_cvar_snapshot out={0};qa_cvar_registry_register(d->host.cvars,"cm_debugSize","2",0,&out);qa_cvar_read_free(&out);d->size_registered=true;}
  const qa_patch_collide*patch=d->hit_patch;
  for(size_t fi=0;fi<patch->facet_count;fi++){
    const qa_patch_facet*facet=&patch->facets[fi];
    size_t bn=facet->border_count+1;
    qa_patch_border*planes=xm(bn*sizeof*planes);
    memcpy(planes,facet->borders,facet->border_count*sizeof*planes);
    planes[facet->border_count]=(qa_patch_border){facet->surface,false,false};
    for(size_t bi=0;bi<bn;bi++){
      const qa_patch_plane*orig=pat(patch,planes[bi].plane);
      qa_collision_winding*winding=qa_winding_base_for_plane(&d->windings,debug_plane(d,orig->plane,planes[bi].inward,1));
      for(size_t ci=0;ci<bn;ci++){
        if(!winding)break;
        if(planes[ci].plane==planes[bi].plane)continue;
        const qa_patch_plane*cp=pat(patch,planes[ci].plane);
        winding=qa_winding_chop_in_place(&d->windings,winding,debug_plane(d,cp->plane,!planes[ci].inward,-1),f32(0.1));
      }
      if(winding){
        size_t pc=0; qa_vec3*pts=qa_collision_winding_points(winding,&pc);
        draw(ctx,facet==d->hit_facet?4:1,qa_collision_winding_num_points(winding),pts,pc);
        free(pts); qa_winding_free(&d->windings,winding);
      } else qa_collision_debug_print(d,"winding chopped away by border planes\n");
    }
    free(planes);
  }
  qa_vec3 t1[3]={d->debug_block_points[0],d->debug_block_points[1],d->debug_block_points[2]};
  qa_vec3 t2[3]={d->debug_block_points[2],d->debug_block_points[3],d->debug_block_points[0]};
  draw(ctx,2,3,t1,3); draw(ctx,2,3,t2,3);
}
/* generation */
static qa_plane negatep(qa_plane p){qa_plane r={qa_scale3(p.normal,-1),-p.distance};return r;}
static qa_vec3 curve_mid(qa_vec3 a,qa_vec3 b,qa_vec3 c){return V(0.5*(0.5*(a.x+b.x)+0.5*(b.x+c.x)),0.5*(0.5*(a.y+b.y)+0.5*(b.y+c.y)),0.5*(0.5*(a.z+b.z)+0.5*(b.z+c.z)));}
static int from_points(qa_vec3 a,qa_vec3 b,qa_vec3 c,qa_plane*out){
  qa_vec3 n=qa_normalize3_stored(qa_cross3_stored(V(c.x-a.x,c.y-a.y,c.z-a.z),V(b.x-a.x,b.y-a.y,b.z-a.z)));
  if(qa_length3(n)==0)return 0;
  *out=(qa_plane){n,f32(qa_dot3_double(a,n))}; return 1;
}
static int close_pts(qa_vec3 a,qa_vec3 b){double e=0.1;return fabs(a.x-b.x)<=e&&fabs(a.y-b.y)<=e&&fabs(a.z-b.z)<=e;}
static int plane_equal(qa_plane a,qa_plane b){return fabs(a.normal.x-b.normal.x)<0.0001&&fabs(a.normal.y-b.normal.y)<0.0001&&fabs(a.normal.z-b.normal.z)<0.0001&&fabs(a.distance-b.distance)<0.02;}
static double signbits_of(qa_vec3 n){return (n.x<0?1:0)|((n.y<0?1:0)<<1)|((n.z<0?1:0)<<2);}
typedef struct{qa_patch_plane*v;size_t n,cap;} PlaneList;
typedef struct{qa_patch_facet*v;size_t n,cap;} FacetList;
typedef struct{qa_patch_border*v;size_t n,cap;} BorderList;
static void pl_push(PlaneList*l,qa_plane p){if(l->n==l->cap){l->cap=l->cap?l->cap*2:16;l->v=xr(l->v,l->cap*sizeof*l->v);}l->v[l->n++]=(qa_patch_plane){p,signbits_of(p.normal)};}
static void bl_push(BorderList*l,qa_patch_border b){if(l->n==l->cap){l->cap=l->cap?l->cap*2:8;l->v=xr(l->v,l->cap*sizeof*l->v);}l->v[l->n++]=b;}
/* grid columns: array of columns each height points */
typedef struct{qa_vec3**cols;size_t ncols,cap;size_t height;} Grid;
static void grid_splice(Grid*g,size_t at,size_t del,qa_vec3**ins,size_t insn){
  size_t nn=g->ncols-del+insn;
  qa_vec3**nc=xm((nn?nn:1)*sizeof*nc); size_t k=0;
  for(size_t i=0;i<at;i++)nc[k++]=g->cols[i];
  for(size_t i=0;i<insn;i++)nc[k++]=ins[i];
  for(size_t i=at+del;i<g->ncols;i++)nc[k++]=g->cols[i];
  for(size_t i=at;i<at+del;i++)free(g->cols[i]);
  free(g->cols); g->cols=nc; g->ncols=nn; g->cap=nn;
}
static int grid_wrapped(Grid*g){
  for(size_t y=0;y<g->height;y++)if(!close_pts(g->cols[0][y],g->cols[g->ncols-1][y]))return 0;
  return 1;
}
static void grid_subdivide(Grid*g){
  for(size_t i=0;(long)i<(long)g->ncols-2;){
    qa_vec3*a=g->cols[i],*b=g->cols[i+1],*c=g->cols[i+2]; int needed=0;
    for(size_t y=0;y<g->height;y++)if(qa_length3(qa_sub3(curve_mid(a[y],b[y],c[y]),qa_lerp3(a[y],c[y],0.5)))>=16)needed=1;
    if(!needed){grid_splice(g,i+1,1,NULL,0);i++;continue;}
    if(g->ncols+2>129)qa_range_error("collision subdivision exceeds MAX_GRID_SIZE");
    qa_vec3*first=xm(g->height*sizeof*first),*mid=xm(g->height*sizeof*mid),*last=xm(g->height*sizeof*last);
    for(size_t y=0;y<g->height;y++){first[y]=qa_lerp3(a[y],b[y],0.5);mid[y]=curve_mid(a[y],b[y],c[y]);last[y]=qa_lerp3(b[y],c[y],0.5);}
    qa_vec3*ins[3]={first,mid,last};
    grid_splice(g,i+1,1,ins,3);
  }
  for(size_t i=0;(long)i<(long)g->ncols-1;){
    int same=1;
    for(size_t y=0;y<g->height;y++)if(!close_pts(g->cols[i][y],g->cols[i+1][y]))same=0;
    if(same)grid_splice(g,i+1,1,NULL,0); else i++;
  }
}
typedef struct{
  Grid*g; PlaneList*planes; qa_collision_debug_surface*debug; qa_collision_winding_library*windings;
  long*gp; size_t gw,gh; /* (width-1)x(height-1) x2 */
  /* Owned while a CommonError/RangeError can unwind generation: TS leaves all
   * of it to the GC; live windings are abandoned (counters unchanged). */
  FacetList*facets; BorderList*borders; qa_vec3*wpts; qa_collision_winding*live,*copy;
} Gen;
static void gen_release(void*arg){
  Gen*gen=arg;
  if(gen->g){for(size_t i=0;i<gen->g->ncols;i++)free(gen->g->cols[i]);free(gen->g->cols);}
  free(gen->gp); free(gen->planes->v);
  if(gen->facets){for(size_t i=0;i<gen->facets->n;i++)free(gen->facets->v[i].borders);free(gen->facets->v);}
  if(gen->borders)free(gen->borders->v);
  free(gen->wpts);
  qa_winding_abandon(gen->windings,gen->live); qa_winding_abandon(gen->windings,gen->copy);
}
static qa_vec3 gp_pt(Gen*gen,size_t x,size_t y){return gen->g->cols[x][y];}
static long find_plane(Gen*gen,qa_vec3 a,qa_vec3 b,qa_vec3 c){
  qa_plane plane; if(!from_points(a,b,c,&plane))return -1;
  qa_vec3 pts[3]={a,b,c};
  for(size_t i=0;i<gen->planes->n;i++){
    qa_plane e=gen->planes->v[i].plane;
    /* `>= 0` and `<= 0.1` are false for NaN (an overflowed binary32 cross). */
    if(!(qa_dot3_double(plane.normal,e.normal)>=0))continue;
    int ok=1; for(int k=0;k<3;k++)if(!(fabs(f32(qa_dot3_double(pts[k],e.normal)-e.distance))<=0.1))ok=0;
    if(ok)return (long)i;
  }
  if(gen->planes->n==2048)qa_common_error(QA_COMMON_ERROR_DROP,"MAX_PATCH_PLANES");
  pl_push(gen->planes,plane); return (long)gen->planes->n-1;
}
static qa_patch_border find_plane2(Gen*gen,qa_plane plane){
  for(size_t i=0;i<gen->planes->n;i++){
    qa_plane e=gen->planes->v[i].plane;
    if(plane_equal(e,plane))return (qa_patch_border){(double)i,false,false};
    if(plane_equal(e,negatep(plane)))return (qa_patch_border){(double)i,true,false};
  }
  if(gen->planes->n==2048)qa_common_error(QA_COMMON_ERROR_DROP,"MAX_PATCH_PLANES");
  pl_push(gen->planes,plane); return (qa_patch_border){(double)gen->planes->n-1,false,false};
}
static long grid_plane(Gen*gen,size_t x,size_t y,int tri){return gen->gp[((x*gen->gh)+y)*2+(size_t)tri];}
static long edge_plane(Gen*gen,size_t x,size_t y,int edge){
  qa_vec3 a,b,base; int tri;
  switch(edge){
    case 0:a=gp_pt(gen,x,y);b=gp_pt(gen,x+1,y);base=a;tri=0;break;
    case 1:a=gp_pt(gen,x+1,y);b=gp_pt(gen,x+1,y+1);base=a;tri=0;break;
    case 2:b=gp_pt(gen,x,y+1);a=gp_pt(gen,x+1,y+1);base=b;tri=1;break;
    case 3:b=gp_pt(gen,x,y);a=gp_pt(gen,x,y+1);base=b;tri=1;break;
    case 4:a=gp_pt(gen,x+1,y+1);b=gp_pt(gen,x,y);base=a;tri=0;break;
    case 5:a=gp_pt(gen,x,y);b=gp_pt(gen,x+1,y+1);base=a;tri=1;break;
    default:qa_range_error("unknown patch edge");return -1;
  }
  long pi=grid_plane(gen,x,y,tri);
  if(pi==-1)pi=grid_plane(gen,x,y,tri==0?1:0);
  if(pi==-1){if(gen->debug)qa_collision_debug_print(gen->debug,"WARNING: CM_GridPlane unresolvable\n");return -1;}
  return find_plane(gen,a,b,qa_add3(base,qa_scale3(gen->planes->v[pi].plane.normal,4)));
}
static void make_facet(Gen*gen,FacetList*facets,long surface,const long*raw,size_t rawn,const int*noadjust,const qa_vec3*verts,size_t vn,const qa_vec3 block[4]){
  BorderList borders={0}; gen->borders=&borders;
  for(size_t bi=0;bi<rawn;bi++){
    long idx=raw[bi];
    if(idx==-1){bl_push(&borders,(qa_patch_border){-1,false,noadjust[bi]!=0});continue;}
    qa_plane plane=gen->planes->v[idx].plane;
    int front=0,back=0;
    for(size_t k=0;k<vn;k++){double d=f32(qa_dot3_double(verts[k],plane.normal)-plane.distance);if(d>0.1)front=1;if(d<-0.1)back=1;}
    if(!front&&!back){bl_push(&borders,(qa_patch_border){-1,false,noadjust[bi]!=0});continue;}
    if(front&&back&&gen->debug)qa_collision_debug_record_mixed_border(gen->debug,block);
    bl_push(&borders,(qa_patch_border){(double)idx,front&&!back,noadjust[bi]!=0});
  }
  if(surface==-1){free(borders.v);gen->borders=NULL;return;}
  qa_collision_winding_library*W=gen->windings;
  qa_collision_winding*winding=gen->live=qa_winding_base_for_plane(W,gen->planes->v[surface].plane);
  for(size_t bi=0;bi<borders.n;bi++){
    if(!winding)break;
    /* CM_ValidateFacet leaves this allocation live: abandon keeps TS's
     * active-winding count and zone allocation. */
    if(borders.v[bi].plane==-1){qa_winding_abandon(W,winding);gen->live=NULL;free(borders.v);gen->borders=NULL;return;}
    qa_plane bp=gen->planes->v[(size_t)borders.v[bi].plane].plane;
    winding=gen->live=qa_winding_chop_in_place(W,winding,borders.v[bi].inward?bp:negatep(bp),f32(0.1));
  }
  if(!winding){free(borders.v);gen->borders=NULL;return;}
  qa_bounds wb=qa_winding_library_bounds(W,winding);
  qa_winding_free(W,winding); gen->live=NULL;
  qa_vec3 delta=qa_sub3(wb.max,wb.min);
  if(delta.x>65535||delta.y>65535||delta.z>65535||wb.min.x>=65535||wb.min.y>=65535||wb.min.z>=65535||wb.max.x<=-65535||wb.max.y<=-65535||wb.max.z<=-65535){free(borders.v);gen->borders=NULL;return;}
  winding=gen->live=qa_winding_base_for_plane(W,gen->planes->v[surface].plane);
  for(size_t bi=0;bi<borders.n;bi++){
    if(!winding)break;
    if(borders.v[bi].plane==(double)surface)continue;
    qa_plane bp=gen->planes->v[(size_t)borders.v[bi].plane].plane;
    winding=gen->live=qa_winding_chop_in_place(W,winding,borders.v[bi].inward?bp:negatep(bp),f32(0.1));
  }
  if(!winding){
    if(facets->n==facets->cap){facets->cap=facets->cap?facets->cap*2:16;facets->v=xr(facets->v,facets->cap*sizeof*facets->v);}
    facets->v[facets->n++]=(qa_patch_facet){(double)surface,borders.v,borders.n}; gen->borders=NULL; return;
  }
  qa_bounds bb=qa_winding_library_bounds(W,winding);
  size_t wpn=0; qa_vec3*wpts=gen->wpts=qa_collision_winding_points(winding,&wpn);
  qa_vec3 axes[3]={V(1,0,0),V(0,1,0),V(0,0,1)};
  /* duplicate check helper inline */
  for(int ax=0;ax<3;ax++)for(int dir=-1;dir<=1;dir+=2){
    qa_vec3 normal=qa_scale3(axes[ax],dir);
    double dist=dir==1?qa_dot3(normal,bb.max):qa_dot3(normal,bb.min);
    qa_plane plane={normal,dist}; int dup=0;
    if(plane_equal(gen->planes->v[surface].plane,plane)||plane_equal(gen->planes->v[surface].plane,negatep(plane)))dup=1;
    for(size_t bi=0;bi<borders.n&&!dup;bi++){qa_plane e=gen->planes->v[(size_t)borders.v[bi].plane].plane;if(plane_equal(e,plane)||plane_equal(e,negatep(plane)))dup=1;}
    if(!dup){if(borders.n>26&&gen->debug)qa_collision_debug_print(gen->debug,"ERROR: too many bevels\n");bl_push(&borders,find_plane2(gen,plane));}
  }
  for(size_t j=0;j<wpn;j++){
    qa_vec3 diff=qa_sub3(wpts[j],wpts[(j+1)%wpn]);
    if(qa_length3(diff)<0.5)continue;
    qa_vec3 edge=qa_normalize3_stored(diff);
    for(int ax=0;ax<3;ax++){double comp=qa_dot3(edge,axes[ax]);if(fabs(comp-1)<0.0001){edge=axes[ax];break;}if(fabs(comp+1)<0.0001){edge=qa_scale3(axes[ax],-1);break;}}
    if(fabs(edge.x)==1||fabs(edge.y)==1||fabs(edge.z)==1)continue;
    for(int ax=0;ax<3;ax++)for(int dir=-1;dir<=1;dir+=2){
      qa_vec3 raw=qa_cross3_stored(edge,qa_scale3(axes[ax],dir));
      if(qa_length3(raw)<0.5)continue;
      qa_vec3 normal=qa_normalize3_stored(raw);
      qa_plane plane={normal,f32(qa_dot3_double(wpts[j],normal))};
      int bad=0; for(size_t k=0;k<wpn;k++)if(f32(qa_dot3_double(wpts[k],normal)-plane.distance)>0.1)bad=1;
      int dup=0;
      if(plane_equal(gen->planes->v[surface].plane,plane)||plane_equal(gen->planes->v[surface].plane,negatep(plane)))dup=1;
      for(size_t bi=0;bi<borders.n&&!dup;bi++){qa_plane e=gen->planes->v[(size_t)borders.v[bi].plane].plane;if(plane_equal(e,plane)||plane_equal(e,negatep(plane)))dup=1;}
      if(bad||dup)continue;
      if(borders.n>26&&gen->debug)qa_collision_debug_print(gen->debug,"ERROR: too many bevels\n");
      qa_patch_border border=find_plane2(gen,plane);
      for(size_t bi=0;bi<borders.n;bi++)if(borders.v[bi].plane==border.plane&&gen->debug)qa_collision_debug_print(gen->debug,"WARNING: bevel plane already used\n");
      gen->copy=qa_winding_copy(W,winding);
      qa_collision_winding*clipped=gen->copy=qa_winding_chop_in_place(W,gen->copy,border.inward?gen->planes->v[(size_t)border.plane].plane:negatep(gen->planes->v[(size_t)border.plane].plane),f32(0.1));
      if(!clipped){if(gen->debug)qa_collision_debug_developer_print(gen->debug,"WARNING: CM_AddFacetBevels... invalid bevel\n");continue;}
      qa_winding_free(W,clipped); gen->copy=NULL;
      bl_push(&borders,border);
    }
  }
  free(wpts); gen->wpts=NULL;
  qa_winding_free(W,winding); gen->live=NULL;
  bl_push(&borders,(qa_patch_border){(double)surface,true,false});
  if(borders.n>27)qa_range_error("collision patch exceeds facet border storage");
  if(facets->n==facets->cap){facets->cap=facets->cap?facets->cap*2:16;facets->v=xr(facets->v,facets->cap*sizeof*facets->v);}
  facets->v[facets->n++]=(qa_patch_facet){(double)surface,borders.v,borders.n}; gen->borders=NULL;
}
qa_patch_collide *qa_generate_patch_collide(double width,double height,const qa_vec3*points,size_t pc,qa_collision_debug_surface*debug){
  /* Number.isInteger: NaN and the infinities are not integers. */
  if(!isfinite(width)||!isfinite(height)||width!=floor(width)||height!=floor(height))qa_range_error("collision patch requires integer dimensions");
  if(width<=2||height<=2){char msg[160];snprintf(msg,sizeof msg,"CM_GeneratePatchFacets: bad parameters: (%s, %s, managed points)",QA_JS_NUMBER(width),QA_JS_NUMBER(height));qa_common_error(QA_COMMON_ERROR_DROP,msg);}
  if(fmod(width,2)==0||fmod(height,2)==0)qa_common_error(QA_COMMON_ERROR_DROP,"CM_GeneratePatchFacets: even sizes are invalid for quadratic meshes");
  if(width>129||height>129)qa_common_error(QA_COMMON_ERROR_DROP,"CM_GeneratePatchFacets: source is > MAX_GRID_SIZE");
  if((double)pc!=width*height)qa_range_error("collision patch requires a complete control grid");
  for(size_t i=0;i<pc;i++)if(!isfinite(points[i].x)||!isfinite(points[i].y)||!isfinite(points[i].z))qa_range_error("nonfinite patch control point");
  size_t W=(size_t)width,H=(size_t)height;
  Grid g={0}; g.height=H; g.cols=xm(W*sizeof*g.cols); g.ncols=W; g.cap=W;
  for(size_t x=0;x<W;x++){g.cols[x]=xm(H*sizeof*g.cols[x]);for(size_t y=0;y<H;y++)g.cols[x][y]=points[y*W+x];}
  PlaneList planes={0}; FacetList facets={0};
  Gen gen={&g,&planes,debug,NULL,NULL,0,0,&facets,NULL,NULL,NULL,NULL};
  qa_cleanup node; qa_cleanup_push(&node,gen_release,&gen);
  int wrap_height=grid_wrapped(&g);
  grid_subdivide(&g);
  /* transpose */
  Grid t={0}; t.height=g.ncols; t.cols=xm(H*sizeof*t.cols); t.ncols=H; t.cap=H;
  for(size_t y=0;y<H;y++){t.cols[y]=xm(g.ncols*sizeof*t.cols[y]);for(size_t x=0;x<g.ncols;x++)t.cols[y][x]=g.cols[x][y];}
  for(size_t i=0;i<g.ncols;i++){free(g.cols[i]);}
  free(g.cols); g=t;
  int wrap_width=grid_wrapped(&g);
  grid_subdivide(&g);
  size_t gw=g.ncols,gh=g.height;
  /* bounds over all points */
  size_t total=gw*gh; qa_vec3*all=xm(total*sizeof*all); size_t k=0;
  for(size_t x=0;x<gw;x++)for(size_t y=0;y<gh;y++)all[k++]=g.cols[x][y];
  qa_bounds bounds=qa_winding_bounds(all,total); free(all);
  /* `(total + blocks) | 0` wraps; signed C overflow would be undefined. */
  if(debug)debug->c_total_patch_blocks=(int32_t)((uint32_t)debug->c_total_patch_blocks+(uint32_t)((gw-1)*(gh-1)));
  qa_collision_winding_library local_w; qa_collision_winding_library_init(&local_w,NULL);
  qa_collision_winding_library*Wl=debug?&debug->windings:&local_w;
  gen.windings=Wl;
  size_t gx=gw-1,gy=gh-1;
  gen.gp=xm(((gx*gy*2)>0?(gx*gy*2):1)*sizeof *gen.gp); gen.gw=gx; gen.gh=gy;
  for(size_t x=0;x<gx;x++)for(size_t y=0;y<gy;y++){
    gen.gp[((x*gy)+y)*2+0]=find_plane(&gen,gp_pt(&gen,x,y),gp_pt(&gen,x+1,y),gp_pt(&gen,x+1,y+1));
    gen.gp[((x*gy)+y)*2+1]=find_plane(&gen,gp_pt(&gen,x+1,y+1),gp_pt(&gen,x,y+1),gp_pt(&gen,x,y));
  }
  for(size_t x=0;x<gx;x++)for(size_t y=0;y<gy;y++){
    long first=grid_plane(&gen,x,y,0),second=grid_plane(&gen,x,y,1);
    long top=y>0?grid_plane(&gen,x,y-1,1):wrap_height?grid_plane(&gen,x,gh-2,1):-1;
    long bottom=y<gh-2?grid_plane(&gen,x,y+1,0):wrap_height?grid_plane(&gen,x,0,0):-1;
    long left=x>0?grid_plane(&gen,x-1,y,0):wrap_width?grid_plane(&gen,gw-2,y,0):-1;
    long right=x<gw-2?grid_plane(&gen,x+1,y,1):wrap_width?grid_plane(&gen,0,y,1):-1;
    int no_top=top==first,no_bottom=bottom==second,no_left=left==second,no_right=right==first;
    if(top==-1||top==first)top=edge_plane(&gen,x,y,0);
    if(bottom==-1||bottom==second)bottom=edge_plane(&gen,x,y,2);
    if(left==-1||left==second)left=edge_plane(&gen,x,y,3);
    if(right==-1||right==first)right=edge_plane(&gen,x,y,1);
    if(facets.n==1024)qa_common_error(QA_COMMON_ERROR_DROP,"MAX_FACETS");
    qa_vec3 block[4]={gp_pt(&gen,x,y),gp_pt(&gen,x+1,y),gp_pt(&gen,x+1,y+1),gp_pt(&gen,x,y+1)};
    if(first==second){
      if(first!=-1){long raw[4]={top,right,bottom,left};int na[4]={no_top,no_right,no_bottom,no_left};make_facet(&gen,&facets,first,raw,4,na,block,4,block);}
    }else{
      long third1=second!=-1?second:bottom!=-1?bottom:edge_plane(&gen,x,y,4);
      long raw1[3]={top,right,third1};int na1[3]={no_top,no_right,0};
      qa_vec3 v1[3]={gp_pt(&gen,x,y),gp_pt(&gen,x+1,y),gp_pt(&gen,x+1,y+1)};
      make_facet(&gen,&facets,first,raw1,3,na1,v1,3,block);
      if(facets.n==1024)qa_common_error(QA_COMMON_ERROR_DROP,"MAX_FACETS");
      long third2=first!=-1?first:top!=-1?top:edge_plane(&gen,x,y,5);
      long raw2[3]={bottom,left,third2};int na2[3]={no_bottom,no_left,0};
      qa_vec3 v2[3]={gp_pt(&gen,x+1,y+1),gp_pt(&gen,x,y+1),gp_pt(&gen,x,y)};
      make_facet(&gen,&facets,second,raw2,3,na2,v2,3,block);
    }
  }
  qa_cleanup_pop(&node);
  free(gen.gp);
  for(size_t i=0;i<g.ncols;i++){free(g.cols[i]);}
  free(g.cols);
  qa_patch_collide*r=xm(sizeof*r);
  r->planes=planes.v;r->plane_count=planes.n;r->facets=facets.v;r->facet_count=facets.n;
  r->bounds=(qa_bounds){qa_sub3(bounds.min,V(1,1,1)),qa_add3(bounds.max,V(1,1,1))};
  return r;
}
static double box_offset(qa_patch_plane plane,qa_vec3 normal,qa_vec3 mins,qa_vec3 extents){
  double sb=plane.signbits;
  if(sb<0||sb>=8)qa_range_error("CM patch plane signbits outside trace offsets");
  int s=(int)sb;
  return qa_dot3_double(V((s&1)?extents.x:mins.x,(s&2)?extents.y:mins.y,(s&4)?extents.z:mins.z),normal);
}
static qa_plane expanded(qa_patch_plane source,qa_patch_shape shape,const qa_patch_border*border){
  qa_plane plane=(border&&border->inward)?negatep(source.plane):source.plane;
  if(shape.kind&&strcmp(shape.kind,"point")==0)return plane;
  qa_vec3 n=plane.normal;
  if(shape.kind&&strcmp(shape.kind,"capsule")==0){
    double off=shape.radius+fabs(qa_dot3(n,shape.offset));
    return (qa_plane){n,f32(plane.distance+off)};
  }
  double off=box_offset(source,n,shape.mins,shape.extents);
  return (qa_plane){n,f32(plane.distance+(border==NULL?-off:fabs(off)))};
}
bool qa_position_in_patch(const qa_patch_collide*patch,qa_vec3 start,qa_patch_shape shape){
  if(shape.kind&&strcmp(shape.kind,"point")==0)return false;
  for(size_t fi=0;fi<patch->facet_count;fi++){
    const qa_patch_facet*f=&patch->facets[fi];
    qa_plane surf=expanded(*pat(patch,f->surface),shape,NULL);
    if(qa_dot3(start,surf.normal)>surf.distance)continue;
    int ok=1;
    for(size_t bi=0;bi<f->border_count;bi++){
      qa_plane pl=expanded(*pat(patch,f->borders[bi].plane),shape,&f->borders[bi]);
      if(!(qa_dot3(start,pl.normal)<=pl.distance))ok=0; /* every(<=): NaN fails */
    }
    if(ok)return true;
  }
  return false;
}
bool qa_trace_patch(const qa_patch_collide*patch,qa_vec3 start,qa_vec3 end,qa_patch_shape shape,double max_fraction,qa_collision_debug_surface*debug,qa_patch_trace_hit*out){
  int have=0; double fraction=max_fraction; qa_patch_trace_hit result={0,{{0,0,0},0}};
  if(shape.kind&&strcmp(shape.kind,"point")==0){
    size_t pn=patch->plane_count;
    double*front=xm((pn?pn:1)*sizeof*front); double*inter=xm((pn?pn:1)*sizeof*inter);
    for(size_t i=0;i<pn;i++){
      qa_patch_plane pl=patch->planes[i];
      double off=box_offset(pl,pl.plane.normal,shape.mins,shape.extents);
      double d1=f32(qa_dot3(start,pl.plane.normal)-pl.plane.distance+off),d2=f32(qa_dot3(end,pl.plane.normal)-pl.plane.distance+off);
      double crossing=d1==d2?99999:f32(d1/(d1-d2));
      front[i]=d1>0?1:0; inter[i]=crossing<=0?99999:crossing;
    }
    for(size_t fi=0;fi<patch->facet_count;fi++){
      const qa_patch_facet*f=&patch->facets[fi];
      size_t si=(size_t)f->surface;
      if(!front[si]||inter[si]>fraction)continue;
      int ok=1;
      for(size_t bi=0;bi<f->border_count;bi++){
        size_t bj=(size_t)f->borders[bi].plane;
        int cond=(front[bj]!=0)!=f->borders[bi].inward?inter[bj]<=inter[si]:inter[bj]>=inter[si];
        if(!cond)ok=0;
      }
      if(!ok)continue;
      if(debug)qa_collision_debug_record_trace(debug,patch,f,true);
      qa_patch_plane pl=patch->planes[si];
      double off=box_offset(pl,pl.plane.normal,shape.mins,shape.extents);
      double d1=f32(qa_dot3(start,pl.plane.normal)-pl.plane.distance+off),d2=f32(qa_dot3(end,pl.plane.normal)-pl.plane.distance+off);
      fraction=qa_js_max(0,f32((d1-0.125)/(d1-d2))); /* Math.max: NaN stays, -0 becomes 0 */
      result=(qa_patch_trace_hit){fraction,{pl.plane.normal,pl.plane.distance}}; have=1;
    }
    free(front);free(inter);
    if(have){*out=result;}
    return have!=0;
  }
  for(size_t fi=0;fi<patch->facet_count;fi++){
    const qa_patch_facet*f=&patch->facets[fi];
    double enter=-1,leave=1; long hit_index=-1; int have_best=0; qa_plane best={{0,0,0},0};
    /* clip surface then borders */
    int valid=1;
    size_t total=f->border_count+1;
    for(size_t ci=0;ci<total;ci++){
      qa_plane plane; long idx;
      if(ci==0){plane=expanded(*pat(patch,f->surface),shape,NULL);idx=-1;}
      else{plane=expanded(*pat(patch,f->borders[ci-1].plane),shape,&f->borders[ci-1]);idx=(long)ci-1;}
      double d1=f32(qa_dot3(start,plane.normal)-plane.distance),d2=f32(qa_dot3(end,plane.normal)-plane.distance);
      if(d1>0&&(d2>=0.125||d2>=d1)){valid=0;break;}
      if(d1<=0&&d2<=0)continue;
      if(d1>d2){
        double fr=qa_js_max(0,f32((d1-0.125)/(d1-d2)));
        if(fr>enter){enter=fr;hit_index=idx;have_best=1;
          best=(shape.kind&&strcmp(shape.kind,"capsule")==0)?(qa_plane){plane.normal,f32(plane.distance-fabs(qa_dot3(plane.normal,shape.offset)))}:plane;}
      }else{
        leave=qa_js_min(leave,qa_js_min(1,f32((d1+0.125)/(d1-d2)))); /* Math.min: NaN propagates */
      }
    }
    if(valid&&hit_index!=(long)f->border_count-1&&enter<leave&&enter>=0&&enter<fraction&&have_best){
      if(debug)qa_collision_debug_record_trace(debug,patch,f,false);
      fraction=enter; result=(qa_patch_trace_hit){fraction,best}; have=1;
    }
  }
  if(have){*out=result;}
    return have!=0;
}
