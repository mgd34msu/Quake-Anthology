/* Port of src/world/collision/q3/polylib.ts. GPL-2.0-or-later. */
#include "qa/world/collision/q3/polylib.h"
#include "qa/core/common_error.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static float f32(double v){return (float)v;}
#define ON_EPS ((float)0.1f)
#define MAXB 65535
static void *xm(size_t n){void*p=malloc(n?n:1);if(!p)abort();return p;}
static int32_t rd_i32(const uint8_t*b,size_t o){int32_t v;memcpy(&v,b+o,4);return v;}
static void wr_i32(uint8_t*b,size_t o,int32_t v){memcpy(b+o,&v,4);}
static void wr_u32(uint8_t*b,size_t o,uint32_t v){memcpy(b+o,&v,4);}
static float rd_f32(const uint8_t*b,size_t o){float v;memcpy(&v,b+o,4);return v;}
static void wr_f32(uint8_t*b,size_t o,double v){float f=(float)v;memcpy(b+o,&f,4);}
static uint8_t *view(qa_collision_winding*w){if(w->freed)qa_range_error("Winding allocation is freed");return w->allocation.bytes;}
double qa_collision_winding_num_points(qa_collision_winding*w){return rd_i32(view(w),0);}
void qa_collision_winding_set_num_points(qa_collision_winding*w,double v){
  if(v!=floor(v)||v<0||v>w->capacity)qa_range_error("Winding count outside allocation");
  wr_i32(view(w),0,(int32_t)v);
}
qa_vec3 qa_collision_winding_point(qa_collision_winding*w,double idx){
  if(idx!=floor(idx)||idx<0||idx>=w->capacity)qa_range_error("Winding point %g outside allocation",idx);
  uint8_t*b=view(w); size_t o=4+(size_t)idx*12;
  qa_vec3 r={rd_f32(b,o),rd_f32(b,o+4),rd_f32(b,o+8)}; return r;
}
void qa_collision_winding_set_point(qa_collision_winding*w,double idx,qa_vec3 p){
  if(idx!=floor(idx)||idx<0||idx>=w->capacity)qa_range_error("Winding point %g outside allocation",idx);
  uint8_t*b=view(w); size_t o=4+(size_t)idx*12;
  wr_f32(b,o,p.x);wr_f32(b,o+4,p.y);wr_f32(b,o+8,p.z);
}
void qa_collision_winding_append(qa_collision_winding*w,qa_vec3 p){double i=qa_collision_winding_num_points(w);qa_collision_winding_set_point(w,i,p);qa_collision_winding_set_num_points(w,i+1);}
void qa_collision_winding_mark_freed(qa_collision_winding*w){
  if(w->freed){qa_common_error(QA_COMMON_ERROR_FATAL,"FreeWinding: freed a freed winding");}
  uint32_t cur; memcpy(&cur,w->allocation.bytes,4);
  if(cur==0xdeaddeadu){qa_common_error(QA_COMMON_ERROR_FATAL,"FreeWinding: freed a freed winding");}
  wr_u32(w->allocation.bytes,0,0xdeaddeadu); w->freed=true;
}
qa_vec3 *qa_collision_winding_points(qa_collision_winding*w,size_t*out){
  double n=qa_collision_winding_num_points(w); size_t c=(size_t)n;
  qa_vec3*r=xm((c?c:1)*sizeof*r); for(size_t i=0;i<c;i++)r[i]=qa_collision_winding_point(w,(double)i);
  *out=c; return r;
}
void qa_collision_winding_library_init(qa_collision_winding_library*lib,const qa_collision_winding_memory*mem){
  memset(lib,0,sizeof*lib); if(mem){lib->has_memory=true;lib->memory=*mem;}
}
qa_collision_winding *qa_winding_alloc(qa_collision_winding_library*lib,double points){
  lib->c_winding_allocs=(int32_t)((uint32_t)lib->c_winding_allocs+1u);
  lib->c_winding_points=(int32_t)((int32_t)lib->c_winding_points+(int32_t)points);
  lib->c_active_windings=(int32_t)((uint32_t)lib->c_active_windings+1u);
  if(lib->c_active_windings>lib->c_peak_windings)lib->c_peak_windings=lib->c_active_windings;
  if(points!=floor(points)||points<0||points>(2147483647.0-4)/12)qa_range_error("Invalid winding allocation size");
  size_t size=4+(size_t)points*12;
  qa_zone_allocation a;
  if(lib->has_memory){a=lib->memory.allocate(lib->memory.ctx,(double)size);}
  else{a.bytes=xm(size);a.byte_length=size;}
  memset(a.bytes,0,size);
  qa_collision_winding*w=xm(sizeof*w); w->allocation=a;w->capacity=points;w->freed=false; return w;
}
void qa_winding_free(qa_collision_winding_library*lib,qa_collision_winding*w){
  qa_collision_winding_mark_freed(w);
  lib->c_active_windings=(int32_t)((uint32_t)lib->c_active_windings-1u);
  if(lib->has_memory&&lib->memory.free){lib->memory.free(lib->memory.ctx,w->allocation);}
  else free(w->allocation.bytes);
  free(w);
}
static void fmt_fixed(double value,int digits,char*out,size_t cap){
  /* TS prints exact binary32 decimal rounding; snprintf %.Nf on the
   * float-promoted double matches for the magnitudes windings carry. */
  float v=(float)value;
  if(isnan(v)){snprintf(out,cap,"%s",signbit(v)?"-nan":"nan");return;}
  if(isinf(v)){snprintf(out,cap,"%s",v<0?"-inf":"inf");return;}
  if(digits==1)snprintf(out,cap,"%.1f",(double)v); else snprintf(out,cap,"%.6f",(double)v);
}
void qa_winding_pw(qa_collision_winding_library*lib,qa_collision_winding*w,qa_winding_print_fn print,void*ctx){
  (void)lib;
  size_t n=0; qa_vec3*pts=qa_collision_winding_points(w,&n);
  for(size_t i=0;i<n;i++){char a[64],b[64],c[64],line[256];
    fmt_fixed(pts[i].x,1,a,sizeof a);fmt_fixed(pts[i].y,1,b,sizeof b);fmt_fixed(pts[i].z,1,c,sizeof c);
    snprintf(line,sizeof line,"(%5s, %5s, %5s)\n",a,b,c); print(ctx,line);}
  free(pts);
}
qa_collision_winding *qa_winding_copy(qa_collision_winding_library*lib,qa_collision_winding*w){
  double n=qa_collision_winding_num_points(w);
  qa_collision_winding*cp=qa_winding_alloc(lib,n);
  memcpy(cp->allocation.bytes,w->allocation.bytes,4+(size_t)n*12);
  return cp;
}
qa_collision_winding *qa_winding_reverse(qa_collision_winding_library*lib,qa_collision_winding*w){
  double n=qa_collision_winding_num_points(w);
  qa_collision_winding*cp=qa_winding_alloc(lib,n);
  for(double i=n-1;i>=0;i--)qa_collision_winding_append(cp,qa_collision_winding_point(w,i));
  return cp;
}
static qa_vec3 at_pts(const qa_vec3*p,size_t n,size_t i){if(i>=n)qa_range_error("Winding point %zu outside allocation",i);return p[i];}
void qa_winding_remove_colinear(qa_collision_winding_library*lib,qa_collision_winding*w){
  size_t n=0; qa_vec3*p=qa_collision_winding_points(w,&n);
  qa_vec3*kept=xm((n?n:1)*sizeof*kept); size_t kn=0;
  if(n>0)for(size_t i=0;i<n;i++){
    qa_vec3 pt=at_pts(p,n,i);
    qa_vec3 a=qa_normalize3_stored_or_zero(qa_sub3(at_pts(p,n,(i+1)%n),pt));
    qa_vec3 b=qa_normalize3_stored_or_zero(qa_sub3(pt,at_pts(p,n,(i+n-1)%n)));
    if(qa_dot3_double(a,b)<0.999)kept[kn++]=pt;
  }
  if(kn==n){free(p);free(kept);return;}
  lib->c_removed=(int32_t)(lib->c_removed+(int32_t)(n-kn));
  qa_collision_winding_set_num_points(w,(double)kn);
  for(size_t i=0;i<kn;i++)qa_collision_winding_set_point(w,(double)i,kept[i]);
  free(p);free(kept);
}
qa_plane qa_winding_plane(qa_collision_winding_library*lib,qa_collision_winding*w){
  (void)lib; qa_vec3 p=qa_collision_winding_point(w,0);
  qa_vec3 normal=qa_normalize3_stored_or_zero(qa_cross3_stored(qa_sub3(qa_collision_winding_point(w,2),p),qa_sub3(qa_collision_winding_point(w,1),p)));
  qa_plane r={normal,f32(qa_dot3_double(p,normal))}; return r;
}
double qa_winding_area(qa_collision_winding_library*lib,qa_collision_winding*w){
  (void)lib; double total=0; double n=qa_collision_winding_num_points(w);
  for(double i=2;i<n;i++){
    qa_vec3 c=qa_cross3_stored(qa_sub3(qa_collision_winding_point(w,i-1),qa_collision_winding_point(w,0)),qa_sub3(qa_collision_winding_point(w,i),qa_collision_winding_point(w,0)));
    total=f32(total+0.5*sqrt(qa_dot3_double(c,c)));
  }
  return total;
}
qa_bounds qa_winding_bounds(const qa_vec3*p,size_t n){
  qa_vec3 mn={MAXB,MAXB,MAXB},mx={-MAXB,-MAXB,-MAXB};
  for(size_t i=0;i<n;i++){
    if(p[i].x<mn.x){mn.x=p[i].x;}
    if(p[i].y<mn.y){mn.y=p[i].y;}
    if(p[i].z<mn.z){mn.z=p[i].z;}
    if(p[i].x>mx.x){mx.x=p[i].x;}
    if(p[i].y>mx.y){mx.y=p[i].y;}
    if(p[i].z>mx.z){mx.z=p[i].z;}
  }
  /* TS stores via vec3() (binary32 staging in core/math vec3 is identity double? vec3 returns plain) */
  qa_bounds r={mn,mx}; return r;
}
qa_bounds qa_winding_library_bounds(qa_collision_winding_library*lib,qa_collision_winding*w){
  (void)lib; size_t n=0; qa_vec3*p=qa_collision_winding_points(w,&n); qa_bounds r=qa_winding_bounds(p,n); free(p); return r;
}
qa_vec3 qa_winding_center(qa_collision_winding_library*lib,qa_collision_winding*w){
  (void)lib; size_t n=0; qa_vec3*p=qa_collision_winding_points(w,&n);
  qa_vec3 sum=qa_math_vec3(0,0,0);
  for(size_t i=0;i<n;i++)sum=qa_add3(p[i],sum);
  qa_vec3 r=qa_scale3(sum,f32(1.0/qa_collision_winding_num_points(w))); free(p); return r;
}
qa_collision_winding *qa_winding_base_for_plane(qa_collision_winding_library*lib,qa_plane plane){
  qa_vec3 n=plane.normal; double comps[3]={n.x,n.y,n.z};
  double maximum=-MAXB; int major=-1;
  for(int a=0;a<3;a++){double m=fabs(comps[a]);if(m>maximum){maximum=m;major=a;}}
  if(major==-1)qa_common_error(QA_COMMON_ERROR_DROP,"BaseWindingForPlane: no axis found");
  qa_vec3 initial=major==2?qa_math_vec3(1,0,0):qa_math_vec3(0,0,1);
  double projected=qa_dot3_double(initial,n);
  qa_vec3 up=qa_normalize3_stored_or_zero(qa_math_vec3(initial.x-projected*n.x,initial.y-projected*n.y,initial.z-projected*n.z));
  qa_vec3 right=qa_math_vec3((f32(up.y*n.z)-up.z*n.y)*65535,(up.z*n.x-up.x*n.z)*65535,(up.x*n.y-up.y*n.x)*65535);
  qa_vec3 vertical=qa_scale3(up,65535),origin=qa_scale3(n,plane.distance);
  qa_collision_winding*r=qa_winding_alloc(lib,4);
  qa_collision_winding_append(r,qa_math_vec3(origin.x-right.x+vertical.x,origin.y-right.y+vertical.y,origin.z-right.z+vertical.z));
  qa_collision_winding_append(r,qa_math_vec3(origin.x+right.x+vertical.x,origin.y+right.y+vertical.y,origin.z+right.z+vertical.z));
  qa_collision_winding_append(r,qa_math_vec3(origin.x+right.x-vertical.x,origin.y+right.y-vertical.y,origin.z+right.z-vertical.z));
  qa_collision_winding_append(r,qa_math_vec3(origin.x-right.x-vertical.x,origin.y-right.y-vertical.y,origin.z-right.z-vertical.z));
  return r;
}
static void check_clip_counts(qa_collision_winding*front,qa_collision_winding*back){
  if(qa_collision_winding_num_points(front)>front->capacity||(back&&qa_collision_winding_num_points(back)>back->capacity))
    qa_common_error(QA_COMMON_ERROR_DROP,"ClipWinding: points exceeded estimate");
  if(qa_collision_winding_num_points(front)>64||(back&&qa_collision_winding_num_points(back)>64))
    qa_common_error(QA_COMMON_ERROR_DROP,"ClipWinding: MAX_POINTS_ON_WINDING");
}
static void split_(const qa_vec3*p,const double*d,size_t n,qa_plane plane,double eps,qa_collision_winding*front,qa_collision_winding*back){
  for(size_t i=0;i<n;i++){
    qa_vec3 pt=p[i]; double d1=d[i]; size_t nx=(i+1)%n; double d2=d[nx];
    int s1=d1>eps?0:d1<-eps?1:2, s2=d2>eps?0:d2<-eps?1:2;
    if(s1==2){qa_collision_winding_append(front,pt);if(back)qa_collision_winding_append(back,pt);continue;}
    if(s1==0)qa_collision_winding_append(front,pt); else if(back)qa_collision_winding_append(back,pt);
    if(s2==2||s1==s2)continue;
    qa_vec3 q=p[nx]; double fr=d1/(d1-d2); qa_vec3 nn=plane.normal;
    qa_vec3 mid=qa_math_vec3(nn.x==1?plane.distance:nn.x==-1?-plane.distance:pt.x+fr*(q.x-pt.x),
      nn.y==1?plane.distance:nn.y==-1?-plane.distance:pt.y+fr*(q.y-pt.y),
      nn.z==1?plane.distance:nn.z==-1?-plane.distance:pt.z+fr*(q.z-pt.z));
    qa_collision_winding_append(front,mid); if(back)qa_collision_winding_append(back,mid);
  }
}
qa_winding_clip_result qa_winding_clip(qa_collision_winding_library*lib,qa_collision_winding*w,qa_plane plane,double eps){
  size_t n=0; qa_vec3*p=qa_collision_winding_points(w,&n);
  double*d=xm((n?n:1)*sizeof*d); int any_pos=0,any_neg=0;
  for(size_t i=0;i<n;i++){d[i]=f32(qa_dot3_double(p[i],plane.normal)-plane.distance);if(d[i]>eps)any_pos=1;if(d[i]<-eps)any_neg=1;}
  qa_winding_clip_result r={NULL,NULL};
  if(!any_pos){free(p);free(d);r.back=qa_winding_copy(lib,w);return r;}
  if(!any_neg){free(p);free(d);r.front=qa_winding_copy(lib,w);return r;}
  r.front=qa_winding_alloc(lib,(double)n+4); r.back=qa_winding_alloc(lib,(double)n+4);
  split_(p,d,n,plane,eps,r.front,r.back);
  check_clip_counts(r.front,r.back);
  free(p);free(d); return r;
}
qa_collision_winding *qa_winding_chop_in_place(qa_collision_winding_library*lib,qa_collision_winding*w,qa_plane plane,double eps){
  size_t n=0; qa_vec3*p=qa_collision_winding_points(w,&n);
  double*d=xm((n?n:1)*sizeof*d); int any_pos=0,any_neg=0;
  for(size_t i=0;i<n;i++){d[i]=f32(qa_dot3_double(p[i],plane.normal)-plane.distance);if(d[i]>eps)any_pos=1;if(d[i]<-eps)any_neg=1;}
  if(!any_pos){free(p);free(d);qa_winding_free(lib,w);return NULL;}
  if(!any_neg){free(p);free(d);return w;}
  qa_collision_winding*front=qa_winding_alloc(lib,(double)n+4);
  split_(p,d,n,plane,eps,front,NULL);
  check_clip_counts(front,NULL);
  free(p);free(d); qa_winding_free(lib,w); return front;
}
qa_collision_winding *qa_winding_chop(qa_collision_winding_library*lib,qa_collision_winding*w,qa_plane plane){
  qa_winding_clip_result r=qa_winding_clip(lib,w,plane,ON_EPS);
  qa_winding_free(lib,w);
  if(r.back)qa_winding_free(lib,r.back);
  return r.front;
}
qa_winding_side qa_winding_on_plane_side(qa_collision_winding_library*lib,qa_collision_winding*w,qa_plane plane){
  (void)lib; size_t n=0; qa_vec3*p=qa_collision_winding_points(w,&n);
  int front=0,back=0; qa_winding_side r;
  for(size_t i=0;i<n;i++){
    double d=f32(qa_dot3_double(p[i],plane.normal)-plane.distance);
    if(d<-ON_EPS){if(front){free(p);return QA_WINDING_CROSS;} back=1; continue;}
    if(d>ON_EPS){if(back){free(p);return QA_WINDING_CROSS;} front=1;}
  }
  r=back?QA_WINDING_BACK:front?QA_WINDING_FRONT:QA_WINDING_ON; free(p); return r;
}
void qa_winding_check(qa_collision_winding_library*lib,qa_collision_winding*w){
  double np=qa_collision_winding_num_points(w);
  if(np<3)qa_common_error(QA_COMMON_ERROR_DROP,"CheckWinding: points");
  double area=qa_winding_area(lib,w);
  if(area<1){char b[64];fmt_fixed(area,6,b,sizeof b);char msg[128];snprintf(msg,sizeof msg,"CheckWinding: %s area",b);qa_common_error(QA_COMMON_ERROR_DROP,msg);}
  qa_plane face=qa_winding_plane(lib,w);
  size_t n=0; qa_vec3*p=qa_collision_winding_points(w,&n);
  for(size_t i=0;i<n;i++){
    double vals[3]={p[i].x,p[i].y,p[i].z};
    for(int k=0;k<3;k++)if(vals[k]>MAXB||vals[k]<-MAXB){char b[64];fmt_fixed(vals[k],6,b,sizeof b);char msg[160];snprintf(msg,sizeof msg,"CheckFace: BUGUS_RANGE: %s",b);qa_common_error(QA_COMMON_ERROR_DROP,msg);}
    double dist=f32(qa_dot3_double(p[i],face.normal)-face.distance);
    if(dist<-ON_EPS||dist>ON_EPS)qa_common_error(QA_COMMON_ERROR_DROP,"CheckWinding: point off plane");
    qa_vec3 dir=qa_sub3(at_pts(p,n,(i+1)%n),p[i]);
    if(sqrt(qa_dot3_double(dir,dir))<ON_EPS)qa_common_error(QA_COMMON_ERROR_DROP,"CheckWinding: degenerate edge");
    qa_vec3 en=qa_normalize3_stored_or_zero(qa_cross3_stored(face.normal,dir));
    double ed=f32(f32(qa_dot3_double(p[i],en))+ON_EPS);
    for(size_t j=0;j<n;j++)if(j!=i&&f32(qa_dot3_double(p[j],en))>ed)qa_common_error(QA_COMMON_ERROR_DROP,"CheckWinding: non-convex");
  }
  free(p);
}
qa_collision_winding *qa_winding_add_to_convex_hull(qa_collision_winding_library*lib,qa_collision_winding*w,qa_collision_winding*hull,qa_vec3 normal){
  if(!hull)return qa_winding_copy(lib,w);
  size_t hn=0; qa_vec3*points=qa_collision_winding_points(hull,&hn);
  size_t wn=0; qa_vec3*wp=qa_collision_winding_points(w,&wn);
  for(size_t wi=0;wi<wn;wi++){
    qa_vec3 p=wp[wi];
    qa_vec3*dirs=xm((hn?hn:1)*sizeof*dirs); double*dst=xm((hn?hn:1)*sizeof*dst); int*sides=xm((hn?hn:1)*sizeof*sides);
    int any=0;
    for(size_t j=0;j<hn;j++){dirs[j]=qa_cross3_stored(normal,qa_normalize3_stored_or_zero(qa_sub3(at_pts(points,hn,(j+1)%hn),points[j])));dst[j]=f32(qa_dot3_double(qa_sub3(p,points[j]),dirs[j]));if(dst[j]>=ON_EPS)any=1;sides[j]=dst[j]>=-ON_EPS;}
    if(!any){free(dirs);free(dst);free(sides);continue;}
    long transition=-1;
    for(size_t j=0;j<hn;j++)if(!sides[j]&&sides[(j+1)%hn]){transition=(long)j;break;}
    if(transition==-1){free(dirs);free(dst);free(sides);continue;}
    qa_vec3*next=xm((hn+1)*sizeof*next); size_t nn=0; next[nn++]=p;
    size_t start=((size_t)transition+1)%hn;
    for(size_t k=0;k<hn;k++){
      if(sides[(start+k)%hn]&&sides[(start+k+1)%hn])continue;
      next[nn++]=at_pts(points,hn,(start+k+1)%hn);
    }
    if(nn>128)qa_range_error("Winding convex hull exceeds MAX_HULL_POINTS");
    free(points); points=next; hn=nn;
    free(dirs);free(dst);free(sides);
  }
  free(wp);
  qa_winding_free(lib,hull);
  qa_collision_winding*r=qa_winding_alloc(lib,(double)hn);
  for(size_t i=0;i<hn;i++)qa_collision_winding_append(r,points[i]);
  free(points); return r;
}
