/* Port of src/render/cpu/lines.ts. GPL-2.0-or-later. */
#include "qa/render/cpu/lines.h"
#include "qa/foundation/support.h"
#include <math.h>
#include "qa/foundation/js.h"
static qa_cpu_vertex interp(qa_cpu_vertex a,qa_cpu_vertex b,double t){
  qa_cpu_vertex o;
  o.position.x=a.position.x*(1-t)+b.position.x*t;o.position.y=a.position.y*(1-t)+b.position.y*t;o.position.z=a.position.z*(1-t)+b.position.z*t;o.position.w=a.position.w*(1-t)+b.position.w*t;
  o.world_position.x=a.world_position.x*(1-t)+b.world_position.x*t;o.world_position.y=a.world_position.y*(1-t)+b.world_position.y*t;o.world_position.z=a.world_position.z*(1-t)+b.world_position.z*t;
  o.world_normal.x=a.world_normal.x*(1-t)+b.world_normal.x*t;o.world_normal.y=a.world_normal.y*(1-t)+b.world_normal.y*t;o.world_normal.z=a.world_normal.z*(1-t)+b.world_normal.z*t;
  o.color.x=a.color.x*(1-t)+b.color.x*t;o.color.y=a.color.y*(1-t)+b.color.y*t;o.color.z=a.color.z*(1-t)+b.color.z*t;o.color.w=a.color.w*(1-t)+b.color.w*t;
  o.tex_coord.x=a.tex_coord.x+(b.tex_coord.x-a.tex_coord.x)*t;o.tex_coord.y=a.tex_coord.y+(b.tex_coord.y-a.tex_coord.y)*t;
  o.tex_coord2.x=a.tex_coord2.x+(b.tex_coord2.x-a.tex_coord2.x)*t;o.tex_coord2.y=a.tex_coord2.y+(b.tex_coord2.y-a.tex_coord2.y)*t;
  return o;
}
static int clip_line(qa_cpu_vertex a,qa_cpu_vertex b,qa_cpu_vertex *oa,qa_cpu_vertex *ob){
  double mag=qa_js_max(qa_js_max(fabs(a.position.x),fabs(a.position.y)),qa_js_max(fabs(a.position.z),fabs(a.position.w)));
  mag=qa_js_max(mag,qa_js_max(qa_js_max(fabs(b.position.x),fabs(b.position.y)),qa_js_max(fabs(b.position.z),fabs(b.position.w))));
  if(mag>1.7976931348623157e308/4){ a.position.x/=mag;a.position.y/=mag;a.position.z/=mag;a.position.w/=mag;b.position.x/=mag;b.position.y/=mag;b.position.z/=mag;b.position.w/=mag; }
  double begin=0,end=1;
  double da[6]={a.position.w+a.position.x,a.position.w-a.position.x,a.position.w+a.position.y,a.position.w-a.position.y,a.position.w+a.position.z,a.position.w-a.position.z};
  double db[6]={b.position.w+b.position.x,b.position.w-b.position.x,b.position.w+b.position.y,b.position.w-b.position.y,b.position.w+b.position.z,b.position.w-b.position.z};
  for(int plane=0;plane<6;plane++){ double x=da[plane],y=db[plane];
    if(x<0&&y<0)return 0;
    if((x<0)!=(y<0)){ double sc=qa_js_max(fabs(x),fabs(y)),cross=(x/sc)/((x/sc)-(y/sc)); if(x<0)begin=qa_js_max(begin,cross); else end=qa_js_min(end,cross); } }
  if(begin>=end)return 0;
  *oa=interp(a,b,begin); *ob=interp(a,b,end);
  return !(oa->position.w<=0||ob->position.w<=0);
}
static int exits_diamond(double ax,double ay,double bx,double by,double x,double y){
  double enter=0,exitv=1; const double ss[2]={-1,1};
  for(int i=0;i<2;i++)for(int j=0;j<2;j++){ double sx=ss[i],sy=ss[j];
    double a=0.5-sx*(ax-x)-sy*(ay-y), b=0.5-sx*(bx-x)-sy*(by-y);
    if(a<=0&&b<=0)return 0;
    if((a<=0)!=(b<=0)){ double t=a/(a-b); if(a<=0)enter=qa_js_max(enter,t); else exitv=qa_js_min(exitv,t); } }
  return enter<exitv&&exitv<1;
}
void qa_rasterize_aliased_line(qa_cpu_vertex first,qa_cpu_vertex second,double width,double height,double line_width,qa_line_scissor sc,qa_line_emit_fn emit,void *ectx){
  if(sc.min_x>sc.max_x||sc.min_y>sc.max_y)return;
  qa_cpu_vertex a,b; if(!clip_line(first,second,&a,&b))return;
  double ax=(a.position.x/a.position.w+1)*width/2,ay=(a.position.y/a.position.w+1)*height/2;
  double bx=(b.position.x/b.position.w+1)*width/2,by=(b.position.y/b.position.w+1)*height/2;
  double dx=bx-ax,dy=by-ay,len2=dx*dx+dy*dy; if(len2==0)return;
  double length=sqrt(len2),iwa=1/a.position.w,iwb=1/b.position.w;
  double ddx=b.tex_coord.x-a.tex_coord.x,ddy=b.tex_coord.y-a.tex_coord.y;
  double d2x=b.tex_coord2.x-a.tex_coord2.x,d2y=b.tex_coord2.y-a.tex_coord2.y;
  int xmajor=fabs(dx)>=fabs(dy); double thick=qa_js_max(1,floor(line_width+0.5)),shift=(thick-1)/2;
  double paX=ax-(xmajor?0:shift)-1e-5,paY=ay-(xmajor?shift:0)-1e-10;
  double pbX=bx-(xmajor?0:shift)-1e-5,pbY=by-(xmajor?shift:0)-1e-10;
  double majorA=xmajor?paX:paY,majorB=xmajor?pbX:pbY,minorA=xmajor?paY:paX,minorB=xmajor?pbY:pbX;
  double bottom=height-1-sc.max_y,top=height-1-sc.min_y;
  double majorMin=xmajor?sc.min_x:bottom,majorMax=xmajor?sc.max_x:top;
  double minorMin=xmajor?bottom:sc.min_x,minorMax=xmajor?top:sc.max_x;
  for(double major=qa_js_max(majorMin,floor(qa_js_min(majorA,majorB)));major<=qa_js_min(majorMax,floor(qa_js_max(majorA,majorB)));major++){
    double frac=(major+0.5-majorA)/(majorB-majorA);
    double minorC=floor(minorA+(minorB-minorA)*frac);
    for(double minor=minorC-1;minor<=minorC+1;minor++){
      double x=xmajor?major:minor,y=xmajor?minor:major;
      if(!exits_diamond(paX,paY,pbX,pbY,x+0.5,y+0.5))continue;
      double baseX=x+(xmajor?0:shift),baseY=y+(xmajor?shift:0);
      double t=qa_js_max(0,qa_js_min(1,((baseX+0.5-ax)*dx+(baseY+0.5-ay)*dy)/len2));
      double iw=(1-t)/a.position.w+t/b.position.w;
      double rx=ddx*t/b.position.w/iw,ry=ddy*t/b.position.w/iw;
      double r2x=d2x*t/b.position.w/iw,r2y=d2y*t/b.position.w/iw;
      qa_line_fragment fr;
      fr.tex_coord.x=a.tex_coord.x+rx;fr.tex_coord.y=a.tex_coord.y+ry;
      fr.tex_coord2.x=a.tex_coord2.x+r2x;fr.tex_coord2.y=a.tex_coord2.y+r2y;
      fr.tex_coord_derivative.ds_per_pixel=(ddx*iwb-rx*(iwb-iwa))/iw/length;
      fr.tex_coord_derivative.dt_per_pixel=(ddy*iwb-ry*(iwb-iwa))/iw/length;
      fr.tex_coord2_derivative.ds_per_pixel=(d2x*iwb-r2x*(iwb-iwa))/iw/length;
      fr.tex_coord2_derivative.dt_per_pixel=(d2y*iwb-r2y*(iwb-iwa))/iw/length;
      fr.world_position.x=(a.world_position.x*(1-t)/a.position.w+b.world_position.x*t/b.position.w)/iw;
      fr.world_position.y=(a.world_position.y*(1-t)/a.position.w+b.world_position.y*t/b.position.w)/iw;
      fr.world_position.z=(a.world_position.z*(1-t)/a.position.w+b.world_position.z*t/b.position.w)/iw;
      fr.world_normal.x=(a.world_normal.x*(1-t)/a.position.w+b.world_normal.x*t/b.position.w)/iw;
      fr.world_normal.y=(a.world_normal.y*(1-t)/a.position.w+b.world_normal.y*t/b.position.w)/iw;
      fr.world_normal.z=(a.world_normal.z*(1-t)/a.position.w+b.world_normal.z*t/b.position.w)/iw;
      fr.color.x=(a.color.x*(1-t)/a.position.w+b.color.x*t/b.position.w)/iw;
      fr.color.y=(a.color.y*(1-t)/a.position.w+b.color.y*t/b.position.w)/iw;
      fr.color.z=(a.color.z*(1-t)/a.position.w+b.color.z*t/b.position.w)/iw;
      fr.color.w=(a.color.w*(1-t)/a.position.w+b.color.w*t/b.position.w)/iw;
      fr.depth=((1-t)*a.position.z/a.position.w+t*b.position.z/b.position.w)*0.5+0.5;
      fr.eye_depth=fabs(1/iw);
      for(double am=qa_js_max(minorMin,minor);am<=qa_js_min(minorMax,minor+thick-1);am++){
        fr.x=xmajor?major:am; fr.y=height-1-(xmajor?am:major); emit(ectx,fr);
      }
    }
  }
}
