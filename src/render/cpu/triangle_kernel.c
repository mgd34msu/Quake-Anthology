/* Port of src/render/cpu/triangle-kernel.ts (general loop). GPL-2.0-or-later. */
#include "qa/render/cpu/triangle_kernel.h"
#include "qa/render/cpu/lighting.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <string.h>
static double f32(double v){ return (double)(float)v; }
const bool QA_CPU_LITTLE_ENDIAN = true; /* x86-64 target; TS probes at runtime. */
double qa_cpu_clamp01(double v){ return fmax(0, fmin(1, v)); }
double qa_cpu_byte(double v){ return round(qa_cpu_clamp01(v)*255); }
uint32_t qa_cpu_color_word(double r,double g,double b,double a){
  uint32_t R=(uint32_t)qa_cpu_byte(r),G=(uint32_t)qa_cpu_byte(g),B=(uint32_t)qa_cpu_byte(b),A=(uint32_t)qa_cpu_byte(a);
  return R|(G<<8)|(B<<16)|(A<<24);
}
static double factor(const char *kind,double s,double d,double sa,double da,bool ac){
  if (!strcmp(kind,"zero")) return 0;
  if (!strcmp(kind,"one"))return 1;
  if (!strcmp(kind,"src-color")) return s;
  if (!strcmp(kind,"one-minus-src-color"))return 1-s;
  if (!strcmp(kind,"dst-color")) return d;
  if (!strcmp(kind,"one-minus-dst-color"))return 1-d;
  if (!strcmp(kind,"src-alpha")) return sa;
  if (!strcmp(kind,"one-minus-src-alpha"))return 1-sa;
  if (!strcmp(kind,"dst-alpha")) return da;
  if (!strcmp(kind,"one-minus-dst-alpha"))return 1-da;
  return ac?1:fmin(sa,1-da);
}
double qa_cpu_blend(double s,double d,double sa,double da,qa_blend_state bl,bool ac){
  return qa_cpu_byte(s*factor(bl.source,s,d,sa,da,ac)+d*factor(bl.destination,s,d,sa,da,ac));
}
bool qa_cpu_passes_alpha(double a,const char *t){
  if (!strcmp(t,"gt0")) return a>0;
  if (!strcmp(t,"lt128"))return a<0.5;
  if (!strcmp(t, "ge128")) return a >= 0.5;
  return true;
}
double qa_cpu_texture_color(double p,double t,const char *e){
  if (!strcmp(e,"add")) return qa_cpu_clamp01(p+t);
  if (!strcmp(e, "replace")) return t;
  return p * t;
}
bool qa_cpu_texture_has_alpha(const char *f){ return strcmp(f,"rgba8")==0; }
typedef struct span { double min, max; } span;
static void trim_span(span *sp,double slope,double row,double constant,bool inclusive){
  if(sp->min>sp->max)return;
  if(slope==0){ double v=row+constant; if(v<0||(v==0&&!inclusive))sp->max=sp->min-1; return; }
  double crossing=-(row+constant)/slope-0.5;
  if(slope>0){
    double mn=fmax(sp->min,fmin(sp->max+1,floor(crossing)));
    while(mn>sp->min){ double v=slope*(mn-0.5)+row+constant; if(!(v>0||(v==0&&inclusive)))break; mn--; }
    while(mn<=sp->max){ double v=slope*(mn+0.5)+row+constant; if(v>0||(v==0&&inclusive))break; mn++; }
    sp->min=mn;
  } else {
    double mx=fmax(sp->min-1,fmin(sp->max,ceil(crossing)));
    while(mx<sp->max){ double v=slope*(mx+1.5)+row+constant; if(!(v>0||(v==0&&inclusive)))break; mx++; }
    while(mx>=sp->min){ double v=slope*(mx+0.5)+row+constant; if(v>0||(v==0&&inclusive))break; mx--; }
    sp->max=mx;
  }
}
static double px(const qa_texture_storage *t,double off){ long o=(long)off; return (o>=0&&(size_t)o<t->pixel_length)?t->pixels[o]:0; }
static double wrap_texel(double index,double size){ return index<0?index+size:index>=size?index-size:index; }
static void sample_repeat(const qa_texture_storage *t,double u,double v,bool linear,qa_cpu_sample *o){
  double w=t->width,h=t->height,mx=t->component_maximum; u-=floor(u); v-=floor(v);
  if(!linear){ double x=fmin(w-1,floor(u*w)),y=fmin(h-1,floor(v*h)),off=(y*w+x)*4;
    o->r=px(t,off)/mx;o->g=px(t,off+1)/mx;o->b=px(t,off+2)/mx;o->a=px(t,off+3)/mx;return; }
  double x=u*w-0.5,y=v*h-0.5,x0=floor(x),y0=floor(y),fx=x-x0,fy=y-y0,x1=x0+1,y1=y0+1;
  long wi=(long)w,hi=(long)h;
  if((wi&(wi-1))==0&&(hi&(hi-1))==0){ x0=(long)x0&(wi-1);x1=(long)x1&(wi-1);y0=(long)y0&(hi-1);y1=(long)y1&(hi-1); }
  else { x0=wrap_texel(x0,w);x1=wrap_texel(x1,w);y0=wrap_texel(y0,h);y1=wrap_texel(y1,h); }
  double w00=(1-fx)*(1-fy)/mx,w10=fx*(1-fy)/mx,w01=(1-fx)*fy/mx,w11=fx*fy/mx;
  double p00=(y0*w+x0)*4,p10=(y0*w+x1)*4,p01=(y1*w+x0)*4,p11=(y1*w+x1)*4;
  o->r=px(t,p00)*w00+px(t,p10)*w10+px(t,p01)*w01+px(t,p11)*w11;
  o->g=px(t,p00+1)*w00+px(t,p10+1)*w10+px(t,p01+1)*w01+px(t,p11+1)*w11;
  o->b=px(t,p00+2)*w00+px(t,p10+2)*w10+px(t,p01+2)*w01+px(t,p11+2)*w11;
  o->a=t->has_alpha?px(t,p00+3)*w00+px(t,p10+3)*w10+px(t,p01+3)*w01+px(t,p11+3)*w11:1;
}
static void sample_level(const qa_texture_storage *t,qa_vec4 border,double u,double v,const char *wrap,bool linear,qa_cpu_sample *o){
  if(!strcmp(wrap,"repeat")){sample_repeat(t,u,v,linear,o);return;}
  double w=t->width,h=t->height,mx=t->component_maximum; u=qa_cpu_clamp01(u); v=qa_cpu_clamp01(v);
  if(!linear){ double x=fmin(w-1,floor(u*w)),y=fmin(h-1,floor(v*h)),off=(y*w+x)*4;
    o->r=px(t,off)/mx;o->g=px(t,off+1)/mx;o->b=px(t,off+2)/mx;o->a=px(t,off+3)/mx;return; }
  double x=u*w-0.5,y=v*h-0.5,x0=floor(x),y0=floor(y),fx=x-x0,fy=y-y0,x1=x0+1,y1=y0+1;
  double w00=(1-fx)*(1-fy)/mx,w10=fx*(1-fy)/mx,w01=(1-fx)*fy/mx,w11=fx*fy/mx;
  bool has_border=x0<0||y0<0||x1>=w||y1>=h; double bw=0;
  if(has_border){
    double offs[4]; offs[0]=(x0<0||y0<0)?-1:(y0*w+x0)*4; offs[1]=(x1>=w||y0<0)?-1:(y0*w+x1)*4;
    offs[2]=(x0<0||y1>=h)?-1:(y1*w+x0)*4; offs[3]=(x1>=w||y1>=h)?-1:(y1*w+x1)*4;
    double ws[4]={w00,w10,w01,w11}; double r=0,g=0,b=0,a=0;
    for(int i=0;i<4;i++){ if(offs[i]<0)continue; r+=px(t,offs[i])*ws[i];g+=px(t,offs[i]+1)*ws[i];b+=px(t,offs[i]+2)*ws[i];a+=px(t,offs[i]+3)*ws[i]; }
    bw=(((offs[0]<0)?w00:0)+((offs[1]<0)?w10:0)+((offs[2]<0)?w01:0)+((offs[3]<0)?w11:0))*mx;
    o->r=r;o->g=g;o->b=b;o->a=t->has_alpha?a:1;
  } else {
    double p00=(y0*w+x0)*4,p10=(y0*w+x1)*4,p01=(y1*w+x0)*4,p11=(y1*w+x1)*4;
    o->r=px(t,p00)*w00+px(t,p10)*w10+px(t,p01)*w01+px(t,p11)*w11;
    o->g=px(t,p00+1)*w00+px(t,p10+1)*w10+px(t,p01+1)*w01+px(t,p11+1)*w11;
    o->b=px(t,p00+2)*w00+px(t,p10+2)*w10+px(t,p01+2)*w01+px(t,p11+2)*w11;
    o->a=t->has_alpha?px(t,p00+3)*w00+px(t,p10+3)*w10+px(t,p01+3)*w01+px(t,p11+3)*w11:1;
  }
  if(has_border){ o->r+=f32(border.x)*bw;o->g+=f32(border.y)*bw;o->b+=f32(border.z)*bw; if(t->has_alpha)o->a+=f32(border.w)*bw; }
}
static double dlen(double x,double y){ if(x==0)return fabs(y); if(y==0)return fabs(x); return hypot(x,y); }
static void sample_components(const qa_bound_texture *t,double u,double v,bool has_rho,double rho,qa_cpu_sample *o,double xU,double xV,double yU,double yV){
  if(t->kind==QA_BOUND_TEXTURE_CONSTANT){*o=t->sample;return;}
  if(!has_rho) rho=fmax(dlen(xU,xV),dlen(yU,yV));
  if(rho<=t->magnification_limit){ sample_level(&t->levels[0],t->border_color,u,v,t->wrap,t->magnify_linear,o); return; }
  if(!strcmp(t->mipmapping,"none")){ sample_level(&t->levels[0],t->border_color,u,v,t->wrap,t->minify_linear,o); return; }
  double last=(double)t->level_count-1; double lambda=fmin(last,log2(rho));
  bool between=!strcmp(t->mipmapping,"linear");
  double sel=between?floor(lambda):fmax(0,ceil(lambda+0.5)-1);
  if(sel<0||sel>=t->level_count) qa_range_error("Texture LOD is outside its complete mip chain");
  sample_level(&t->levels[(size_t)sel],t->border_color,u,v,t->wrap,t->minify_linear,o);
  double fr=lambda-sel; if(!between||fr==0)return;
  if(sel+1>=t->level_count) qa_range_error("Texture LOD blend is outside its complete mip chain");
  qa_cpu_sample first=*o; sample_level(&t->levels[(size_t)sel+1],t->border_color,u,v,t->wrap,t->minify_linear,o);
  o->r=first.r*(1-fr)+o->r*fr;o->g=first.g*(1-fr)+o->g*fr;o->b=first.b*(1-fr)+o->b*fr;o->a=first.a*(1-fr)+o->a*fr;
}
void qa_cpu_sample_bound(const qa_bound_texture *t,double u,double v,double rho,qa_cpu_sample *o){ sample_components(t,u,v,true,rho,o,0,0,0,0); }
static void sample_perspective(const qa_bound_texture *t,double u,double v,double recip,qa_texture_plane_derivative d,qa_cpu_sample *o){
  if(t->kind==QA_BOUND_TEXTURE_IMAGE){
    if(!strcmp(t->mipmapping,"none")){ sample_level(&t->levels[0],t->border_color,d.u_anchor+u,d.v_anchor+v,t->wrap,t->magnify_linear,o); return; }
    double w=t->levels[0].width,h=t->levels[0].height;
    double xU=(d.u_dx-u*d.q_dx)*recip*w,xV=(d.v_dx-v*d.q_dx)*recip*h,yU=(d.u_dy-u*d.q_dy)*recip*w,yV=(d.v_dy-v*d.q_dy)*recip*h;
    sample_components(t,d.u_anchor+u,d.v_anchor+v,false,0,o,xU,xV,yU,yV);return;
  }
  sample_components(t,d.u_anchor+u,d.v_anchor+v,true,0,o,0,0,0,0);
}
void qa_cpu_sample_line_bound(const qa_bound_texture *t,qa_vec2 c,qa_line_texture_derivative d,qa_cpu_sample *o){
  double rho=(t->kind==QA_BOUND_TEXTURE_IMAGE&&strcmp(t->mipmapping,"none"))?hypot(d.ds_per_pixel*t->levels[0].width,d.dt_per_pixel*t->levels[0].height):0;
  qa_cpu_sample_bound(t,c.x,c.y,rho,o);
}
bool qa_cpu_stencil_fragment(uint32_t *stencil,size_t count,double index,bool passed,const char *fn,double cm,double wm,double mx,const char *df,const char *dp){
  if(stencil==NULL) qa_range_error("Stencil test has no configured storage");
  long i=(long)index; if(i<0||(size_t)i>=count) qa_range_error("Fragment is outside the stencil buffer");
  uint32_t prev=stencil[i];
  if(!strcmp(fn,"nonzero")&&(prev&(uint32_t)cm)==0) return false;
  const char *op=passed?dp:df;
  uint32_t next=!strcmp(op,"increment")?(uint32_t)fmin(mx,prev+1):!strcmp(op,"decrement")?(uint32_t)fmax(0,(double)prev-1):prev;
  stencil[i]=(prev&~(uint32_t)wm)|(next&(uint32_t)wm); return passed;
}
static void write_fragment(qa_cpu_framebuffer *fb,double ab,double pixel,double r,double g,double b,double a,qa_blend_state bl,const char *mode){
  r=qa_cpu_clamp01(r);g=qa_cpu_clamp01(g);b=qa_cpu_clamp01(b);a=qa_cpu_clamp01(a);
  long p=(long)pixel; if(p<0||(size_t)p>=fb->storage_count) qa_range_error("Fragment is outside the color buffer");
  uint32_t dest=(uint32_t)fb->color_words[p];
  double dr=(dest&255)/255.0,dg=((dest>>8)&255)/255.0,db=((dest>>16)&255)/255.0,da=ab==0?1:(dest>>24)/255.0;
  if(!strcmp(mode,"alpha")){double ia=1-a;fb->color_words[p]=(int32_t)qa_cpu_color_word(r*a+dr*ia,g*a+dg*ia,b*a+db*ia,ab==0?1:a*a+da*ia);}
  else if(!strcmp(mode,"add")){fb->color_words[p]=(int32_t)qa_cpu_color_word(r+dr,g+dg,b+db,ab==0?1:a+da);}
  else if(!strcmp(mode,"multiply")){fb->color_words[p]=(int32_t)qa_cpu_color_word(r*dr,g*dg,b*db,ab==0?1:a*da);}
  else if(!strcmp(mode,"dst-color-inverse-dst-alpha")){double ida=1-da;fb->color_words[p]=(int32_t)qa_cpu_color_word(r*dr+dr*ida,g*dg+dg*ida,b*db+db*ida,ab==0?1:a*da+da*ida);}
  else { size_t off=(size_t)p*4; fb->pixels[off]=(uint8_t)qa_cpu_blend(r,dr,a,da,bl,false);fb->pixels[off+1]=(uint8_t)qa_cpu_blend(g,dg,a,da,bl,false);fb->pixels[off+2]=(uint8_t)qa_cpu_blend(b,db,a,da,bl,false);fb->pixels[off+3]=ab==0?255:(uint8_t)qa_cpu_blend(a,da,a,da,bl,true); }
}
static bool run_internal(const qa_triangle_setup *s,qa_cpu_framebuffer *fb,qa_cpu_sample *sampled,double firstY,double lastY){
  bool did=false; double depthRange=s->depth_far-s->depth_near;
  double fogScale=s->has_fog_depth_scale?s->fog_depth_scale:1;
  bool lum=s->texture_effect&&!strcmp(s->texture_effect,"luminance-alpha");
  const qa_cpu_triangle_lighting *lighting=s->lighting;
  bool frag_lit=s->texture_consumed&&lighting&&strcmp(lighting->base.parameters.kind,"vertex");
  for(double y=fmax(s->min_y,firstY);y<=fmin(s->max_y,lastY);y++){
    double sy=y+0.5,rA=s->edge_ay*sy,rB=s->edge_by*sy,rC=s->edge_cy*sy;
    double aA=s->attribute_ay*sy,aB=s->attribute_by*sy,aC=s->attribute_cy*sy;
    double rowOff=(y-fb->origin_y)*fb->stride-fb->origin_x;
    span sp; sp.min=s->min_x; sp.max=s->max_x;
    trim_span(&sp,s->edge_ax,rA,s->edge_ac,s->edge_a_inclusive);
    trim_span(&sp,s->edge_bx,rB,s->edge_bc,s->edge_b_inclusive);
    trim_span(&sp,s->edge_cx,rC,s->edge_cc,s->edge_c_inclusive);
    for(double x=sp.min;x<=sp.max;x++){
      double sx=x+0.5;
      double wa=(s->attribute_ax*sx+aA+s->attribute_ac)*s->inverse_area;
      double wb=(s->attribute_bx*sx+aB+s->attribute_bc)*s->inverse_area;
      double wc=(s->attribute_cx*sx+aC+s->attribute_cc)*s->inverse_area;
      double depth=s->constant_depth?s->plane_depth:qa_cpu_clamp01(qa_cpu_clamp01((s->az*wa+s->bz*wb+s->cz*wc)*0.5+0.5)*depthRange+s->depth_near+s->polygon_depth_offset);
      double pixel=rowOff+x; long pi=(long)pixel;
      if(pi<0||(size_t)pi>=fb->storage_count) qa_range_error("Fragment is outside the depth buffer");
      double old=fb->depth[pi];
      bool passed=!((!strcmp(s->depth_test,"less-equal")&&depth>old)||(!strcmp(s->depth_test,"equal")&&depth!=old));
      if(!passed&&!s->stencil_enabled) continue;
      double iw=s->aiw*wa+s->biw*wb+s->ciw*wc, recip=1/iw;
      double r=0,g=0,blue=0,alpha=0;
      if(s->texture_consumed){
        if(s->white){ r=g=blue=alpha=qa_cpu_clamp01(iw*recip); }
        else { r=qa_cpu_clamp01((s->ar*wa+s->br*wb+s->cr*wc)*recip);g=qa_cpu_clamp01((s->ag*wa+s->bg*wb+s->cg*wc)*recip);blue=qa_cpu_clamp01((s->ab*wa+s->bb*wb+s->cb*wc)*recip);alpha=qa_cpu_clamp01((s->aa*wa+s->ba*wb+s->ca*wc)*recip); }
        qa_vec4 vc; vc.x=r;vc.y=g;vc.z=blue;vc.w=alpha;
        double tr=1,tg=1,tb=1,ta=1;
        if(s->texture.kind!=QA_BOUND_TEXTURE_INCOMPLETE){
          double u=(s->au*wa+s->bu*wb+s->cu*wc)*recip,v=(s->av*wa+s->bv*wb+s->cv*wc)*recip;
          sample_perspective(&s->texture,u,v,recip,s->derivative,sampled); did=true;
          tr=sampled->r;tg=sampled->g;tb=sampled->b;ta=s->primary_alpha?sampled->a:1;
          if(lum){ double m=(sampled->r+sampled->g+sampled->b)/3*alpha; sampled->r*=m;sampled->g*=m;sampled->b*=m; tr=sampled->r;tg=sampled->g;tb=sampled->b; }
          r*=sampled->r;g*=sampled->g;blue*=sampled->b; if(s->primary_alpha)alpha*=sampled->a;
        }
        if(frag_lit&&lighting){
          double wa2=s->aiw*wa*recip,wb2=s->biw*wb*recip,wc2=s->ciw*wc*recip;
          qa_vec3 pos,nor; qa_cpu_interpolate_world_into(&pos,lighting->positions[0],lighting->positions[1],lighting->positions[2],wa2,wb2,wc2);
          qa_cpu_interpolate_world_into(&nor,lighting->normals[0],lighting->normals[1],lighting->normals[2],wa2,wb2,wc2);
          qa_cpu_sample tex; tex.r=tr;tex.g=tg;tex.b=tb;tex.a=ta; qa_cpu_sample out;
          qa_shade_q2_fragment_into(&out,&lighting->base,pos,nor,vc,&tex); r=out.r;g=out.g;blue=out.b;alpha=out.a;
        }
        if(s->secondary_environment&&s->secondary_texture.kind!=QA_BOUND_TEXTURE_INCOMPLETE){
          double u=(s->au2*wa+s->bu2*wb+s->cu2*wc)*recip,v=(s->av2*wa+s->bv2*wb+s->cv2*wc)*recip;
          sample_perspective(&s->secondary_texture,u,v,recip,s->secondary_derivative,sampled); did=true;
          if(!strcmp(s->secondary_environment,"modulate")){r*=sampled->r;g*=sampled->g;blue*=sampled->b;if(s->secondary_alpha)alpha*=sampled->a;}
          else if(!strcmp(s->secondary_environment,"add")){r=qa_cpu_clamp01(r+sampled->r);g=qa_cpu_clamp01(g+sampled->g);blue=qa_cpu_clamp01(blue+sampled->b);if(s->secondary_alpha)alpha*=sampled->a;}
          else {r=sampled->r;g=sampled->g;blue=sampled->b;if(s->secondary_alpha)alpha=sampled->a;}
        }
        if(s->fog){
          double d=!strcmp(s->fog->kind,"exp2")?s->fog->density*fogScale*recip/64:0;
          double amount=!strcmp(s->fog->kind,"constant")?s->fog->amount:1-exp(-d*d);
          const char *eff=!strcmp(s->fog->kind,"constant")?"color":(s->fog->effect?s->fog->effect:"color");
          if(strcmp(eff,"none")){r=qa_cpu_clamp01(r);g=qa_cpu_clamp01(g);blue=qa_cpu_clamp01(blue);}
          if(!strcmp(eff,"color")){r+=(s->fog->color.x-r)*amount;g+=(s->fog->color.y-g)*amount;blue+=(s->fog->color.z-blue)*amount;}
          if(!strcmp(eff,"rgb")||!strcmp(eff,"rgba")){r*=1-amount;g*=1-amount;blue*=1-amount;}
          if(!strcmp(eff,"alpha")||!strcmp(eff,"rgba"))alpha*=1-amount;
          if(!strcmp(eff,"overlay")){r=s->fog->color.x;g=s->fog->color.y;blue=s->fog->color.z;alpha*=amount;}
        }
      }
      if(strcmp(s->alpha_test,"none")&&!qa_cpu_passes_alpha(alpha,s->alpha_test)) continue;
      if(s->stencil_enabled&&!qa_cpu_stencil_fragment(fb->stencil,fb->storage_count,pixel,passed,s->stencil_function,s->stencil_compare_mask,s->stencil_write_mask,s->stencil_maximum,s->stencil_depth_fail,s->stencil_depth_pass)) continue;
      if(s->color_write){
        if(!strcmp(s->blending,"opaque")) fb->color_words[pi]=(int32_t)qa_cpu_color_word(r,g,blue,s->alpha_bits==0?1:alpha);
        else write_fragment(fb,s->alpha_bits,pixel,r,g,blue,alpha,s->blend,s->blending);
      }
      if(s->depth_write) fb->depth[pi]=depth;
    }
  }
  return did;
}
bool qa_run_triangle_rows(const qa_triangle_setup *s,qa_cpu_framebuffer *fb,qa_cpu_sample *sm,double fy,double ly){ return run_internal(s,fb,sm,fy,ly); }
bool qa_run_triangle_rows_default(const qa_triangle_setup *s,qa_cpu_framebuffer *fb,qa_cpu_sample *sm){ return run_internal(s,fb,sm,s->min_y,s->max_y); }
bool qa_run_worker_triangle_rows(const qa_triangle_setup *s,qa_cpu_framebuffer *fb,qa_cpu_sample *sm,double fy,double ly){ return run_internal(s,fb,sm,fy,ly); }
