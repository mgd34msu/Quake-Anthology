/* Port of src/world/geometry/q1-solid/polyhedron.ts. GPL-2.0-or-later. */
#include "qa/world/geometry/q1_solid/polyhedron.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
const qa_vec3 qa_cell_axes[3] = {{1,0,0},{0,1,0},{0,0,1}};
double qa_cell_dot(qa_vec3 a, qa_vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
qa_vec3 qa_cell_add(qa_vec3 a, qa_vec3 b){qa_vec3 r={a.x+b.x,a.y+b.y,a.z+b.z};return r;}
qa_vec3 qa_cell_sub(qa_vec3 a, qa_vec3 b){qa_vec3 r={a.x-b.x,a.y-b.y,a.z-b.z};return r;}
qa_vec3 qa_cell_scale(qa_vec3 a, double s){qa_vec3 r={a.x*s,a.y*s,a.z*s};return r;}
qa_vec3 qa_cell_cross(qa_vec3 a, qa_vec3 b){qa_vec3 r={a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};return r;}
double qa_cell_length(qa_vec3 a){return sqrt(qa_cell_dot(a,a));}
qa_vec3 qa_cell_unit(qa_vec3 a){return qa_cell_scale(a,1.0/qa_cell_length(a));}
qa_plane qa_negate_plane(qa_plane p){qa_plane r={qa_cell_scale(p.normal,-1),-p.distance};return r;}
qa_vec3 qa_cell_lerp(qa_vec3 a, qa_vec3 b, double t){return qa_cell_add(a,qa_cell_scale(qa_cell_sub(b,a),t));}
static void *xmalloc(size_t n){void*p=malloc(n?n:1);if(!p)abort();return p;}
static void *xrealloc(void*q,size_t n){void*p=realloc(q,n?n:1);if(!p)abort();return p;}
typedef struct{qa_vec3*v;size_t n,cap;} VecList;
static void vl_push(VecList*l,qa_vec3 x){if(l->n==l->cap){l->cap=l->cap?l->cap*2:8;l->v=xrealloc(l->v,l->cap*sizeof *l->v);}l->v[l->n++]=x;}
typedef struct{qa_cell_face*v;size_t n,cap;} FaceList;
static void fl_push(FaceList*l,qa_plane p,qa_vec3*v,size_t n){if(l->n==l->cap){l->cap=l->cap?l->cap*2:8;l->v=xrealloc(l->v,l->cap*sizeof*l->v);}l->v[l->n++]=(qa_cell_face){p,v,n};}
static qa_convex_cell *cell_new(qa_cell_face*f,size_t n){qa_convex_cell*c=xmalloc(sizeof*c);c->faces=f;c->face_count=n;return c;}
void qa_convex_cell_destroy(qa_convex_cell*c){if(!c)return;for(size_t i=0;i<c->face_count;i++)free(c->faces[i].vertices);free(c->faces);free(c);}
static qa_vec3 *dup_pts(const qa_vec3*p,size_t n){qa_vec3*r=xmalloc(n*sizeof*r);memcpy(r,p,n*sizeof*r);return r;}
qa_convex_cell *qa_box_cell(qa_bounds b){
  qa_vec3 a=b.min,c=b.max;
  qa_vec3 p000={a.x,a.y,a.z},p001={a.x,a.y,c.z},p010={a.x,c.y,a.z},p011={a.x,c.y,c.z};
  qa_vec3 p100={c.x,a.y,a.z},p101={c.x,a.y,c.z},p110={c.x,c.y,a.z},p111={c.x,c.y,c.z};
  qa_cell_face*f=xmalloc(6*sizeof*f);
  qa_vec3 v0[4]={p100,p110,p111,p101},v1[4]={p000,p001,p011,p010},v2[4]={p010,p011,p111,p110};
  qa_vec3 v3[4]={p000,p100,p101,p001},v4[4]={p001,p101,p111,p011},v5[4]={p000,p010,p110,p100};
  f[0]=(qa_cell_face){{qa_cell_axes[0],c.x},dup_pts(v0,4),4};
  f[1]=(qa_cell_face){{qa_cell_scale(qa_cell_axes[0],-1),-a.x},dup_pts(v1,4),4};
  f[2]=(qa_cell_face){{qa_cell_axes[1],c.y},dup_pts(v2,4),4};
  f[3]=(qa_cell_face){{qa_cell_scale(qa_cell_axes[1],-1),-a.y},dup_pts(v3,4),4};
  f[4]=(qa_cell_face){{qa_cell_axes[2],c.z},dup_pts(v4,4),4};
  f[5]=(qa_cell_face){{qa_cell_scale(qa_cell_axes[2],-1),-a.z},dup_pts(v5,4),4};
  return cell_new(f,6);
}
typedef struct{qa_vec3 point;double angle;} Angled;
static int ang_cmp(const void*A,const void*B){double d=((const Angled*)A)->angle-((const Angled*)B)->angle;return d<0?-1:d>0?1:0;}
static qa_convex_cell *close_cell(FaceList faces, VecList cap, qa_plane plane){
  if(cap.n>=3){
    qa_vec3 sum={0,0,0}; for(size_t i=0;i<cap.n;i++)sum=qa_cell_add(sum,cap.v[i]);
    qa_vec3 center=qa_cell_scale(sum,1.0/(double)cap.n);
    qa_vec3 axis=fabs(plane.normal.z)<0.9?qa_cell_axes[2]:qa_cell_axes[1];
    qa_vec3 u=qa_cell_unit(qa_cell_cross(axis,plane.normal)), v=qa_cell_cross(plane.normal,u);
    Angled*an=xmalloc(cap.n*sizeof*an);
    for(size_t i=0;i<cap.n;i++){qa_vec3 d=qa_cell_sub(cap.v[i],center);an[i]=(Angled){cap.v[i],atan2(qa_cell_dot(d,v),qa_cell_dot(d,u))};}
    qsort(an,cap.n,sizeof*an,ang_cmp);
    qa_vec3*pts=xmalloc(cap.n*sizeof*pts); for(size_t i=0;i<cap.n;i++)pts[i]=an[i].point; free(an);
    fl_push(&faces,plane,pts,cap.n);
  }
  free(cap.v);
  if(faces.n<4){for(size_t i=0;i<faces.n;i++)free(faces.v[i].vertices);free(faces.v);return NULL;}
  return cell_new(faces.v,faces.n);
}
static int cap_has(VecList*cap,qa_vec3 p){for(size_t i=0;i<cap->n;i++)if(qa_cell_length(qa_cell_sub(cap->v[i],p))<1e-7)return 1;return 0;}
qa_convex_cell *qa_clip_cell(qa_convex_cell*cell, qa_plane plane){
  int outside=0,inside=0;
  for(size_t i=0;i<cell->face_count&&!(outside&&inside);i++)for(size_t j=0;j<cell->faces[i].vertex_count;j++){
    double d=qa_cell_dot(cell->faces[i].vertices[j],plane.normal)-plane.distance;
    if(d>1e-8){outside=1;}
    if(d<-1e-8){inside=1;}
    if(outside&&inside){break;}}
  if(!outside){return cell;}
  if(!inside){return NULL;}
  FaceList faces={0}; VecList cap={0};
  for(size_t fi=0;fi<cell->face_count;fi++){
    VecList verts={0};
    for(size_t i=0;i<cell->faces[fi].vertex_count;i++){
      qa_vec3 a=cell->faces[fi].vertices[i],b=cell->faces[fi].vertices[(i+1)%cell->faces[fi].vertex_count];
      double da=qa_cell_dot(a,plane.normal)-plane.distance,db=qa_cell_dot(b,plane.normal)-plane.distance;
      if(da<=0)vl_push(&verts,a);
      if((da<0&&db>0)||(da>0&&db<0)){qa_vec3 pt=qa_cell_lerp(a,b,da/(da-db));vl_push(&verts,pt);if(!cap_has(&cap,pt))vl_push(&cap,pt);}
      else if(da==0&&!cap_has(&cap,a))vl_push(&cap,a);
    }
    if(verts.n>=3)fl_push(&faces,cell->faces[fi].plane,verts.v,verts.n); else free(verts.v);
  }
  return close_cell(faces,cap,plane);
}
qa_split_result qa_split_cell(qa_convex_cell*cell, qa_plane plane){
  int outside=0,inside=0,nonfinite=0;
  for(size_t i=0;i<cell->face_count;i++)for(size_t j=0;j<cell->faces[i].vertex_count;j++){
    double d=qa_cell_dot(cell->faces[i].vertices[j],plane.normal)-plane.distance;
    if(!isfinite(d)){nonfinite=1;break;}
    if(d>1e-8){outside=1;}
    if(d<-1e-8){inside=1;}
    if(outside&&inside){break;}}
  if(nonfinite){qa_split_result r={qa_clip_cell(cell,qa_negate_plane(plane)),qa_clip_cell(cell,plane)};return r;}
  if(!outside||!inside){qa_split_result r={inside?NULL:cell,outside?NULL:cell};return r;}
  qa_plane opp=qa_negate_plane(plane);
  FaceList front={0},back={0}; VecList cap={0};
  for(size_t fi=0;fi<cell->face_count;fi++){
    VecList fv={0},bv={0};
    for(size_t i=0;i<cell->faces[fi].vertex_count;i++){
      qa_vec3 a=cell->faces[fi].vertices[i],b=cell->faces[fi].vertices[(i+1)%cell->faces[fi].vertex_count];
      double da=qa_cell_dot(a,plane.normal)-plane.distance,db=qa_cell_dot(b,plane.normal)-plane.distance;
      if(!isfinite(da)||!isfinite(db)){free(fv.v);free(bv.v);for(size_t k=0;k<front.n;k++)free(front.v[k].vertices);free(front.v);for(size_t k=0;k<back.n;k++)free(back.v[k].vertices);free(back.v);free(cap.v);qa_split_result r={qa_clip_cell(cell,opp),qa_clip_cell(cell,plane)};return r;}
      if(da>=0){vl_push(&fv,a);}
      if(da<=0){vl_push(&bv,a);}
      if((da<0&&db>0)||(da>0&&db<0)){qa_vec3 pt=qa_cell_lerp(a,b,da/(da-db));vl_push(&fv,pt);vl_push(&bv,pt);if(!cap_has(&cap,pt))vl_push(&cap,pt);}
      else if(da==0&&!cap_has(&cap,a))vl_push(&cap,a);
    }
    if(fv.n>=3)fl_push(&front,cell->faces[fi].plane,fv.v,fv.n); else free(fv.v);
    if(bv.n>=3)fl_push(&back,cell->faces[fi].plane,bv.v,bv.n); else free(bv.v);
  }
  VecList cap2={0}; for(size_t i=0;i<cap.n;i++)vl_push(&cap2,cap.v[i]);
  qa_split_result r={close_cell(front,cap2,opp),close_cell(back,cap,plane)}; return r;
}
qa_vec3 *qa_cell_vertices(const qa_convex_cell*cell,size_t*out){
  size_t n=0; for(size_t i=0;i<cell->face_count;i++)n+=cell->faces[i].vertex_count;
  qa_vec3*r=xmalloc((n?n:1)*sizeof*r); size_t k=0;
  for(size_t i=0;i<cell->face_count;i++)for(size_t j=0;j<cell->faces[i].vertex_count;j++)r[k++]=cell->faces[i].vertices[j];
  *out=n; return r;
}
qa_plane *qa_box_separating_planes(const qa_convex_cell*cell,const qa_vec3*axes,size_t ac,size_t*out){
  VecList normals={0}; int axialSeen=0;
  /* insert inline via loop helper macro pattern */
  #define INSERT(N) do{qa_vec3 _n0=(N);double _m=qa_cell_length(_n0);if(_m>=1e-8){qa_vec3 _n=qa_cell_scale(_n0,1.0/_m);int _ax=(_n.y==0&&_n.z==0)?(_n.x==1?1:_n.x==-1?2:0):(_n.x==0&&_n.z==0)?(_n.y==1?4:_n.y==-1?8:0):(_n.x==0&&_n.y==0)?(_n.z==1?16:_n.z==-1?32:0):0;if(!(axialSeen&_ax)){int _dup=0;for(size_t _i=0;_i<normals.n;_i++)if(qa_cell_dot(normals.v[_i],_n)>1-1e-10)_dup=1;if(!_dup)vl_push(&normals,_n);axialSeen|=_ax;}}}while(0)
  for(size_t fi=0;fi<cell->face_count;fi++){
    INSERT(cell->faces[fi].plane.normal);
    for(size_t i=0;i<cell->faces[fi].vertex_count;i++){
      qa_vec3 a=cell->faces[fi].vertices[i],b=cell->faces[fi].vertices[(i+1)%cell->faces[fi].vertex_count];
      qa_vec3 edge=qa_cell_sub(b,a);
      for(size_t k=0;k<ac;k++){qa_vec3 n=qa_cell_cross(edge,axes[k]);INSERT(n);INSERT(qa_cell_scale(n,-1));}
    }
  }
  for(size_t k=0;k<ac;k++){INSERT(axes[k]);INSERT(qa_cell_scale(axes[k],-1));}
  #undef INSERT
  size_t vc=0; qa_vec3*verts=qa_cell_vertices(cell,&vc);
  qa_plane*r=xmalloc((normals.n?normals.n:1)*sizeof*r);
  for(size_t i=0;i<normals.n;i++){double d=-INFINITY;for(size_t j=0;j<vc;j++){double x=qa_cell_dot(normals.v[i],verts[j]);if(x>d)d=x;}r[i]=(qa_plane){normals.v[i],d};}
  free(verts); free(normals.v); *out=normals.n; return r;
}
