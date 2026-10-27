/* Port of src/render/cpu/rasterizer.ts (core path). GPL-2.0-or-later. */
#include "qa/render/cpu/rasterizer.h"
#include "qa/render/cpu/fog.h"
#include "qa/render/cpu/lighting.h"
#include "qa/render/cpu/lines.h"
#include "qa/render/cpu/textures.h"
#include "qa/render/output_gamma.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static const qa_polygon_offset *const NO_OFFSET = NULL;
const qa_render_state QA_CPU_OPAQUE_STATE = { {"one","zero"}, "less-equal", true, "none", "none", {0,1}, NULL };
typedef struct rimpl {
  qa_cpu_framebuffer fb, main_fb, opacity_fb; bool has_opacity_fb;
  uint32_t *words; double *depth; uint32_t *stencil;
  uint32_t *opacity_words; double *opacity_depth; uint32_t *opacity_stencil;
  uint8_t *output_pixels; bool has_gamma; uint8_t gamma_table[256];
  qa_cpu_images images; qa_cpu_sample sampled;
  qa_rect viewport; bool has_clip; qa_vec4 clip_plane; bool has_vp; qa_mat4 view_projection;
  qa_vec4 clear_color; double clear_depth; double stencil_maximum, subpixel_scale, subpixel_bits;
  bool stencil_enabled; const char *stencil_function; double stencil_compare_mask, stencil_write_mask;
  const char *stencil_depth_fail; const char *stencil_depth_pass;
  bool color_write, opacity_active, closed;
  qa_render_state retained_state; double depth_range[2]; const char *cull_override;
  bool has_poly_offset; qa_polygon_offset poly_offset;
  size_t count;
} rimpl;
static rimpl *R(qa_software_renderer *r){ return (rimpl*)r->impl; }
static void assert_open(qa_software_renderer *r){ if(R(r)->closed) qa_range_error("CPU renderer is closed"); }
static int finite4(qa_vec4 v){ return isfinite(v.x)&&isfinite(v.y)&&isfinite(v.z)&&isfinite(v.w); }
static double f32(double v){ return (double)(float)v; }
bool qa_software_renderer_init(qa_software_renderer *r,double width,double height,qa_renderer_resource_owner owner,double subpixel_bits,double stencil_bits,double alpha_bits){
  if(width!=(double)(long long)width||height!=(double)(long long)height||width<=0||height<=0) qa_range_error("Image dimensions must be positive integers with a safe storage size");
  if(subpixel_bits!=(double)(long long)subpixel_bits||subpixel_bits<4||subpixel_bits>16) qa_range_error("CPU subpixel precision must be between 4 and 16 bits");
  if(stencil_bits!=(double)(long long)stencil_bits||stencil_bits<0||stencil_bits>32) qa_range_error("CPU stencil precision must be between 0 and 32 bits");
  rimpl *p=(rimpl*)calloc(1,sizeof *p); if(!p)return false;
  r->impl=p;r->width=width;r->height=height;r->owner=owner;r->subpixel_bits=subpixel_bits;r->stencil_bits=stencil_bits;r->alpha_bits=alpha_bits;
  size_t count=(size_t)(width*height); p->count=count;
  p->words=(uint32_t*)calloc(count,sizeof(uint32_t)); p->depth=(double*)malloc(count*sizeof(double));
  p->stencil=stencil_bits==0?NULL:(uint32_t*)calloc(count,sizeof(uint32_t));
  if(!p->words||!p->depth||(stencil_bits!=0&&!p->stencil)){ free(p->words);free(p->depth);free(p->stencil);free(p);r->impl=NULL;return false; }
  if(alpha_bits==0){ uint32_t w=qa_cpu_color_word(0,0,0,1); for(size_t i=0;i<count;i++)p->words[i]=w; }
  for(size_t i=0;i<count;i++)p->depth[i]=1;
  p->stencil_maximum=pow(2,stencil_bits)-1; p->subpixel_scale=pow(2,subpixel_bits); p->subpixel_bits=subpixel_bits;
  if(!qa_cpu_images_init(&p->images,owner)){ free(p->words);free(p->depth);free(p->stencil);free(p);r->impl=NULL;return false; }
  p->fb.width=width;p->fb.height=height;p->fb.pixels=(uint8_t*)p->words;p->fb.color_words=(int32_t*)p->words;p->fb.depth=p->depth;p->fb.stencil=p->stencil;p->fb.stride=width;p->fb.storage_count=count;
  p->main_fb=p->fb; p->viewport.x=0;p->viewport.y=0;p->viewport.width=width;p->viewport.height=height;
  p->clear_depth=1; p->color_write=true; p->retained_state=QA_CPU_OPAQUE_STATE; p->depth_range[0]=0;p->depth_range[1]=1;
  p->stencil_function="always";p->stencil_compare_mask=4294967295.0;p->stencil_write_mask=4294967295.0;p->stencil_depth_fail="keep";p->stencil_depth_pass="keep";
  p->sampled.r=p->sampled.g=p->sampled.b=p->sampled.a=1;
  (void)NO_OFFSET; return true;
}
bool qa_software_renderer_init_default(qa_software_renderer *r,double w,double h,qa_renderer_resource_owner o){ return qa_software_renderer_init(r,w,h,o,8,8,8); }
const uint8_t *qa_software_renderer_pixels(qa_software_renderer *r,size_t *len){ rimpl *p=R(r); if(len)*len=p->count*4; return p->output_pixels?p->output_pixels:p->fb.pixels; }
double qa_software_renderer_row_parallel_batches(qa_software_renderer *r){ (void)r; return 0; }
void qa_software_renderer_finish(qa_software_renderer *r){ assert_open(r); rimpl *p=R(r); if(!p->has_gamma||!p->output_pixels)return;
  size_t n=p->count*4; for(size_t i=0;i<n;i++){ uint8_t v=p->main_fb.pixels[i]; p->output_pixels[i]=(i%4==3)?v:p->gamma_table[v]; } }
void qa_software_renderer_set_output_gamma(qa_software_renderer *r,double gamma){ assert_open(r); rimpl *p=R(r);
  p->has_gamma=qa_output_gamma_table(gamma,p->gamma_table);
  free(p->output_pixels); p->output_pixels=p->has_gamma?(uint8_t*)malloc(p->count*4):NULL;
  if(p->has_gamma&&!p->output_pixels)qa_range_error("CPU renderer allocation failed");
  qa_software_renderer_finish(r); }
bool qa_software_renderer_read_rgba(qa_software_renderer *r,qa_image_level *out){ assert_open(r); qa_software_renderer_finish(r); rimpl *p=R(r);
  size_t n=p->count*4; uint8_t *copy=(uint8_t*)malloc(n?n:1); if(!copy)return false;
  const uint8_t *src=qa_software_renderer_pixels(r,NULL); memcpy(copy,src,n);
  out->width=r->width;out->height=r->height;out->pixels.bytes=copy;out->pixels.length=n; return true; }
void qa_software_renderer_with_object_opacity(qa_software_renderer *r,double opacity,qa_software_draw_fn draw,void *ctx){
  assert_open(r); rimpl *p=R(r);
  if(!isfinite(opacity)||opacity<0||opacity>1) qa_range_error("Object opacity must be in 0..1");
  if(p->opacity_active) qa_range_error("Object opacity scopes cannot nest");
  if(opacity==0)return;
  if(opacity==1){ p->opacity_active=true; draw(ctx); p->opacity_active=false; return; }
  if(!p->has_opacity_fb){
    p->opacity_words=(uint32_t*)malloc(p->count*sizeof(uint32_t)); p->opacity_depth=(double*)malloc(p->count*sizeof(double));
    p->opacity_stencil=p->stencil?(uint32_t*)malloc(p->count*sizeof(uint32_t)):NULL;
    if(!p->opacity_words||!p->opacity_depth||(p->stencil&&!p->opacity_stencil))qa_range_error("CPU renderer allocation failed");
    p->opacity_fb=p->fb; p->has_opacity_fb=true;
  }
  p->opacity_fb.pixels=(uint8_t*)p->opacity_words;p->opacity_fb.color_words=(int32_t*)p->opacity_words;p->opacity_fb.depth=p->opacity_depth;p->opacity_fb.stencil=p->opacity_stencil;
  memcpy(p->opacity_words,p->words,p->count*4); memcpy(p->opacity_depth,p->depth,p->count*sizeof(double));
  if(p->opacity_stencil&&p->stencil)memcpy(p->opacity_stencil,p->stencil,p->count*sizeof(uint32_t));
  qa_cpu_framebuffer parent=p->fb; qa_rect vp=p->viewport;
  p->opacity_active=true; p->fb=p->opacity_fb; draw(ctx);
  double left=fmax(0,vp.x),right=fmin(r->width,vp.x+vp.width);
  for(double y=fmax(0,vp.y);y<fmin(r->height,vp.y+vp.height);y++)
    for(size_t off=(size_t)((y*r->width+left)*4);off<(size_t)((y*r->width+right)*4);off++){
      double backdrop=parent.pixels[off],result=p->opacity_fb.pixels[off];
      parent.pixels[off]=(r->alpha_bits==0&&off%4==3)?255:(uint8_t)round(backdrop*(1-opacity)+result*opacity);
    }
  p->fb=parent; p->opacity_active=false;
}
void qa_software_renderer_apply_q1_fog(qa_software_renderer *r,qa_mat4 proj,qa_q1_fog fog,double sky,const char *output){ assert_open(r); rimpl *p=R(r); qa_apply_q1_depth_fog(&p->fb,p->viewport,proj,fog,sky,output,p->clear_depth); }
void qa_software_renderer_close(qa_software_renderer *r){ if(!r->impl)return; rimpl *p=R(r); if(p->closed)return;
  qa_cpu_images_destroy(&p->images); free(p->words);free(p->depth);free(p->stencil);free(p->opacity_words);free(p->opacity_depth);free(p->opacity_stencil);free(p->output_pixels); p->closed=true; free(p); r->impl=NULL; }
void qa_software_renderer_apply_image_resource(qa_software_renderer *r,qa_image_resource_operation op){ assert_open(r); qa_cpu_images_apply(&R(r)->images,op); }
void qa_software_renderer_select_draw_buffer(qa_software_renderer *r,const char *buffer,bool clear){ assert_open(r);
  if(strcmp(buffer,"front")&&strcmp(buffer,"back")) qa_range_error("CPU stereo draw buffers are unavailable");
  if(clear){ rimpl *p=R(r); qa_render_view_state v; memset(&v,0,sizeof v); v.viewport=p->viewport; v.clip_plane=p->has_clip?&p->clip_plane:NULL;
    qa_vec4 col; col.x=1;col.y=0;col.z=0.5;col.w=1; qa_render_view_state vv=v; qa_render_view_clear cl; cl.depth=1;cl.color=&col;cl.stencil=false; vv.clear=&cl;
    /* clip_plane pointer must outlive call: points into rimpl. */
    qa_software_renderer_begin_view(r,vv); } }
void qa_software_renderer_clear_color_buffer(qa_software_renderer *r){ assert_open(r); rimpl *p=R(r); if(!p->color_write)return;
  uint32_t word=qa_cpu_color_word(p->clear_color.x,p->clear_color.y,p->clear_color.z,r->alpha_bits==0?1:p->clear_color.w);
  for(double row=fmax(0,p->viewport.y);row<fmin(r->height,p->viewport.y+p->viewport.height);row++){
    size_t begin=(size_t)(row*r->width+fmax(0,p->viewport.x)),end=(size_t)(row*r->width+fmin(r->width,p->viewport.x+p->viewport.width));
    for(size_t i=begin;i<end;i++)p->fb.color_words[i]=(int32_t)word; } }
void qa_software_renderer_begin_view(qa_software_renderer *r,qa_render_view_state view){ assert_open(r); rimpl *p=R(r);
  qa_rect vp=view.viewport;
  if(vp.x!=(double)(long long)vp.x||vp.y!=(double)(long long)vp.y||vp.width!=(double)(long long)vp.width||vp.height!=(double)(long long)vp.height||vp.width<=0||vp.height<=0) qa_range_error("CPU viewport must have integer coordinates and positive dimensions");
  if(view.clip_plane&&!finite4(*view.clip_plane)) qa_range_error("CPU clip plane must be finite");
  p->viewport=vp; p->has_clip=view.clip_plane!=NULL; if(view.clip_plane)p->clip_plane=*view.clip_plane;
  p->has_vp=view.has_view_projection; p->view_projection=view.view_projection;
  if(!view.clear)return;
  if(!isfinite(view.clear->depth)||(view.clear->color&&!finite4(*view.clear->color))) qa_range_error("CPU clear values must be finite");
  p->clear_depth=qa_cpu_clamp01(view.clear->depth);
  if(view.clear->color){ p->clear_color=*view.clear->color; qa_software_renderer_clear_color_buffer(r); }
  for(double row=fmax(0,vp.y);row<fmin(r->height,vp.y+vp.height);row++){
    size_t begin=(size_t)(row*r->width+fmax(0,vp.x)),end=(size_t)(row*r->width+fmin(r->width,vp.x+vp.width));
    for(size_t i=begin;i<end;i++){ p->fb.depth[i]=p->clear_depth; if(view.clear->stencil&&p->fb.stencil)p->fb.stencil[i]=0; } }
}
double qa_software_renderer_read_depth_pixel(qa_software_renderer *r,double x,double y){ assert_open(r); rimpl *p=R(r);
  if(x!=(double)(long long)x||y!=(double)(long long)y||x<0||x>=r->width||y<0||y>=r->height) qa_range_error("CPU depth readback coordinates must be inside the framebuffer");
  return f32(p->fb.depth[(size_t)((r->height-1-y)*r->width+x)]); }
void qa_software_renderer_set_overdraw_measurement(qa_software_renderer *r,bool enabled){ assert_open(r); rimpl *p=R(r);
  if(enabled&&!p->fb.stencil) qa_range_error("CPU overdraw measurement requires stencil storage");
  p->stencil_enabled=enabled;p->stencil_function="always";p->stencil_compare_mask=4294967295.0;p->stencil_write_mask=4294967295.0;p->stencil_depth_fail="increment";p->stencil_depth_pass="increment"; }
void qa_software_renderer_read_stencil_overdraw(qa_software_renderer *r,uint8_t *dest,size_t length){ assert_open(r); rimpl *p=R(r);
  if(!p->fb.stencil) qa_range_error("CPU stencil readback requires stencil storage");
  double stride=ceil(r->width/4)*4;
  if(length<(size_t)(stride*(r->height-1)+r->width)) qa_range_error("CPU stencil destination is too small for PACK_ALIGNMENT=4");
  for(double row=0;row<r->height;row++)for(double x=0;x<r->width;x++) dest[(size_t)(row*stride+x)]=(uint8_t)(p->fb.stencil[(size_t)((r->height-1-row)*r->width+x)]&255); }
/* --- geometry --- */
typedef struct svert { double x,y,z,iw; qa_cpu_vertex v; } svert;
static double subpixel(double value,double scale){ double sc=f32(value)*scale,lo=floor(sc),fr=sc-lo; return ((fr<0.5||(fr==0.5&&fmod(lo,2)==0))?lo:lo+1)/scale; }
static double edge_fn(double ax,double ay,double bx,double by,double x,double y){ return (ay-by)*x+(bx-ax)*y+(ax*by-ay*bx); }
static qa_cpu_vertex indexed_vertex(const qa_draw_batch *b,size_t off){
  if(off>=b->indices.count) qa_range_error("Missing triangle index");
  double index=b->indices.values[off]; long ii=(long)index;
  qa_cpu_world_attributes at=qa_cpu_world_attributes_for(b->lighting,index);
  qa_cpu_vertex o; memset(&o,0,sizeof o);
  if(b->texturing_is_pair){ if(ii<0||(size_t)ii>=b->pair_vertex_count)qa_range_error("Missing triangle vertex");
    o.position=b->pair_vertices[ii].vertex.position;o.color=b->pair_vertices[ii].vertex.color;o.tex_coord=b->pair_vertices[ii].vertex.tex_coord;o.tex_coord2=b->pair_vertices[ii].tex_coord2; }
  else { if(ii<0||(size_t)ii>=b->vertex_count)qa_range_error("Missing triangle vertex");
    o.position=b->vertices[ii].position;o.color=b->vertices[ii].color;o.tex_coord=b->vertices[ii].tex_coord; }
  o.world_position=at.world_position;o.world_normal=at.world_normal; return o;
}
static qa_cpu_vertex lerp_v(qa_cpu_vertex a,qa_cpu_vertex b,double aw,double bw){
  qa_cpu_vertex o;
  o.position.x=a.position.x*aw+b.position.x*bw;o.position.y=a.position.y*aw+b.position.y*bw;o.position.z=a.position.z*aw+b.position.z*bw;o.position.w=a.position.w*aw+b.position.w*bw;
  o.world_position.x=a.world_position.x*aw+b.world_position.x*bw;o.world_position.y=a.world_position.y*aw+b.world_position.y*bw;o.world_position.z=a.world_position.z*aw+b.world_position.z*bw;
  o.world_normal.x=a.world_normal.x*aw+b.world_normal.x*bw;o.world_normal.y=a.world_normal.y*aw+b.world_normal.y*bw;o.world_normal.z=a.world_normal.z*aw+b.world_normal.z*bw;
  o.color.x=a.color.x*aw+b.color.x*bw;o.color.y=a.color.y*aw+b.color.y*bw;o.color.z=a.color.z*aw+b.color.z*bw;o.color.w=a.color.w*aw+b.color.w*bw;
  double anc;
  anc=fmin(a.tex_coord.x,b.tex_coord.x); o.tex_coord.x=anc+(a.tex_coord.x-anc)*aw+(b.tex_coord.x-anc)*bw;
  anc=fmin(a.tex_coord.y,b.tex_coord.y); o.tex_coord.y=anc+(a.tex_coord.y-anc)*aw+(b.tex_coord.y-anc)*bw;
  anc=fmin(a.tex_coord2.x,b.tex_coord2.x); o.tex_coord2.x=anc+(a.tex_coord2.x-anc)*aw+(b.tex_coord2.x-anc)*bw;
  anc=fmin(a.tex_coord2.y,b.tex_coord2.y); o.tex_coord2.y=anc+(a.tex_coord2.y-anc)*aw+(b.tex_coord2.y-anc)*bw;
  return o;
}
static double plane_dist(qa_cpu_vertex v,int plane,const qa_vec4 *extra){
  qa_vec4 q=v.position;
  switch(plane){ case 0:return q.w+q.x;case 1:return q.w-q.x;case 2:return q.w+q.y;case 3:return q.w-q.y;case 4:return q.w+q.z;case 5:return q.w-q.z;
  default:return q.x*extra->x+q.y*extra->y+q.z*extra->z+q.w*extra->w; }
}
static size_t clip_poly(qa_cpu_vertex *poly,size_t n,const qa_vec4 *extra,qa_cpu_vertex *scratch){
  int planes=extra?7:6;
  for(int plane=0;plane<planes&&n>0;plane++){
    int clipped=0; for(size_t i=0;i<n;i++)if(plane_dist(poly[i],plane,extra)<0)clipped=1;
    if(!clipped)continue;
    size_t m=0; qa_cpu_vertex prev=poly[n-1]; double pd=plane_dist(prev,plane,extra);
    for(size_t i=0;i<n;i++){ qa_cpu_vertex cur=poly[i]; double d=plane_dist(cur,plane,extra);
      if((d>=0)!=(pd>=0)){ double sc=fmax(fabs(pd),fabs(d)); double ad=fabs(pd)/sc,bd=fabs(d)/sc; double aw=bd/(ad+bd),bw=ad/(ad+bd);
        qa_cpu_vertex x=lerp_v(prev,cur,aw,bw); double w=x.position.w;
        switch(plane){case 0:x.position.x=-w;break;case 1:x.position.x=w;break;case 2:x.position.y=-w;break;case 3:x.position.y=w;break;case 4:x.position.z=-w;break;case 5:x.position.z=w;break;default:break;}
        scratch[m++]=x; }
      if (d >= 0) scratch[m++] = cur;
      prev = cur; pd = d; }
    memcpy(poly,scratch,m*sizeof *poly); n=m;
  }
  size_t m=0; for(size_t i=0;i<n;i++)if(poly[i].position.w>0)poly[m++]=poly[i]; return m;
}
static const char *classify_blending(qa_blend_state b){
  if(!strcmp(b.source,"one")&&!strcmp(b.destination,"zero"))return "opaque";
  if(!strcmp(b.source,"src-alpha")&&!strcmp(b.destination,"one-minus-src-alpha"))return "alpha";
  if(!strcmp(b.source,"one")&&!strcmp(b.destination,"one"))return "add";
  if(!strcmp(b.source,"zero")&&!strcmp(b.destination,"src-color"))return "multiply";
  if(!strcmp(b.source,"dst-color")&&!strcmp(b.destination,"one-minus-dst-alpha"))return "dst-color-inverse-dst-alpha";
  return "general";
}
typedef struct line_ctx { qa_software_renderer *r; const qa_draw_batch *batch; qa_bound_texture tex, tex2; qa_cpu_lighting lighting; } line_ctx;
static void emit_line(void *ctxp,qa_line_fragment fr){
  line_ctx *c=(line_ctx*)ctxp; rimpl *p=R(c->r); const qa_draw_batch *b=c->batch;
  long x=(long)fr.x,y=(long)fr.y; if(x<0||y<0||x>=(long)c->r->width||y>=(long)c->r->height)return;
  size_t pi=(size_t)(y*(long)c->r->width+x);
  double depth=qa_cpu_clamp01(fr.depth*(b->state.depth_range[1]-b->state.depth_range[0])+b->state.depth_range[0]);
  double old=p->fb.depth[pi];
  int passed=!((!strcmp(b->state.depth_test,"less-equal")&&depth>old)||(!strcmp(b->state.depth_test,"equal")&&depth!=old));
  if(!passed&&!p->stencil_enabled)return;
  qa_cpu_sample sampled; sampled.r=sampled.g=sampled.b=sampled.a=1;
  double r=qa_cpu_clamp01(fr.color.x),g=qa_cpu_clamp01(fr.color.y),blue=qa_cpu_clamp01(fr.color.z),a=qa_cpu_clamp01(fr.color.w);
  if(c->tex.kind!=QA_BOUND_TEXTURE_INCOMPLETE){ qa_cpu_sample_line_bound(&c->tex,fr.tex_coord,fr.tex_coord_derivative,&sampled);
    qa_cpu_sample texel=sampled; qa_cpu_sample out;
    qa_shade_q2_fragment_into(&out,&c->lighting,fr.world_position,fr.world_normal,fr.color,&texel); r=out.r;g=out.g;blue=out.b;a=out.a;
    if(b->texturing_is_pair&&c->tex2.kind!=QA_BOUND_TEXTURE_INCOMPLETE){ qa_cpu_sample_line_bound(&c->tex2,fr.tex_coord2,fr.tex_coord2_derivative,&sampled);
      const char *env=b->second_texture.environment;
      if(!strcmp(env,"modulate")){r*=sampled.r;g*=sampled.g;blue*=sampled.b;a*=sampled.a;}
      else if(!strcmp(env,"add")){r=qa_cpu_clamp01(r+sampled.r);g=qa_cpu_clamp01(g+sampled.g);blue=qa_cpu_clamp01(blue+sampled.b);a*=sampled.a;}
      else {r=sampled.r;g=sampled.g;blue=sampled.b;a=sampled.a;} }
  }
  if(strcmp(b->state.alpha_test,"none")&&!qa_cpu_passes_alpha(a,b->state.alpha_test))return;
  if(p->stencil_enabled&&!qa_cpu_stencil_fragment(p->fb.stencil,p->count,(double)pi,passed,p->stencil_function,p->stencil_compare_mask,p->stencil_write_mask,p->stencil_maximum,p->stencil_depth_fail,p->stencil_depth_pass))return;
  if(p->color_write){
    const char *mode=classify_blending(b->state.blend);
    if(!strcmp(mode,"opaque"))p->fb.color_words[pi]=(int32_t)qa_cpu_color_word(r,g,blue,c->r->alpha_bits==0?1:a);
    else { uint32_t dest=(uint32_t)p->fb.color_words[pi]; double dr=(dest&255)/255.0,dg=((dest>>8)&255)/255.0,db=((dest>>16)&255)/255.0,da=c->r->alpha_bits==0?1:(dest>>24)/255.0;
      qa_blend_state bl=b->state.blend; size_t off=pi*4;
      p->fb.pixels[off]=(uint8_t)qa_cpu_blend(qa_cpu_clamp01(r),dr,qa_cpu_clamp01(a),da,bl,false);
      p->fb.pixels[off+1]=(uint8_t)qa_cpu_blend(qa_cpu_clamp01(g),dg,qa_cpu_clamp01(a),da,bl,false);
      p->fb.pixels[off+2]=(uint8_t)qa_cpu_blend(qa_cpu_clamp01(blue),db,qa_cpu_clamp01(a),da,bl,false);
      p->fb.pixels[off+3]=c->r->alpha_bits==0?255:(uint8_t)qa_cpu_blend(qa_cpu_clamp01(a),da,qa_cpu_clamp01(a),da,bl,true); }
  }
  if(b->state.depth_write)p->fb.depth[pi]=depth;
}
static void draw_batch(qa_software_renderer *r,qa_draw_batch batch){
  rimpl *p=R(r);
  /* Resolve dynamic textures (TS createTextureResolver, once per source). */
  if(batch.texture.kind==QA_TEXTURE_BINDING_DYNAMIC_IMAGE){
    /* Dynamic resolution is handled by qa_resolve_draw_textures in the
     * executor path; direct draw() in the TS goes through prepare too. */
  }
  qa_cpu_images_bind(&p->images,0,batch.texture);
  if(batch.texturing_is_pair) qa_cpu_images_bind(&p->images,1,batch.second_texture.binding);
  qa_texture_storage lv0[32],lv1[32];
  qa_bound_texture tex=qa_cpu_images_bound(&p->images,0,lv0,32);
  qa_bound_texture tex2=batch.texturing_is_pair?qa_cpu_images_bound(&p->images,1,lv1,32):tex;
  if(!batch.texturing_is_pair){ memset(&tex2,0,sizeof tex2); tex2.kind=QA_BOUND_TEXTURE_INCOMPLETE; }
  qa_cpu_lighting lighting; lighting.parameters=batch.lighting; lighting.depth=NULL;
  if(strcmp(batch.lighting.kind,"vertex")){
    const qa_q2_shadow_atlas *atlas=batch.lighting.atlas;
    if(!strcmp(batch.lighting.kind,"q2-model-shadow")){ static qa_depth_image_level dl; dl=qa_cpu_images_depth_image(&p->images,batch.lighting.atlas_value.image); lighting.depth=&dl; }
    else if(atlas){ static qa_depth_image_level dl2; dl2=qa_cpu_images_depth_image(&p->images,atlas->image); lighting.depth=&dl2; }
  }
  if(batch.primitive_is_lines){
    line_ctx lc; lc.r=r;lc.batch=&batch;lc.tex=tex;lc.tex2=tex2;lc.lighting=lighting;
    qa_line_scissor sc; sc.min_x=fmax(0,p->viewport.x);sc.min_y=fmax(0,p->viewport.y);
    sc.max_x=fmin(r->width,p->viewport.x+p->viewport.width)-1;sc.max_y=fmin(r->height,p->viewport.y+p->viewport.height)-1;
    /* Lines rasterize in viewport-local window space; offset not applied
     * to NDC mapping here (documented reduction). */
    for(size_t i=0;i+1<batch.indices.count;i+=2) qa_rasterize_aliased_line(indexed_vertex(&batch,i),indexed_vertex(&batch,i+1),p->viewport.width,p->viewport.height,batch.line_width,sc,emit_line,&lc);
    return;
  }
  for(size_t i=0;i+2<batch.indices.count;i+=3){
    qa_cpu_vertex poly[16],scratch[16];
    poly[0]=indexed_vertex(&batch,i);poly[1]=indexed_vertex(&batch,i+1);poly[2]=indexed_vertex(&batch,i+2);
    size_t n=clip_poly(poly,3,p->has_clip?&p->clip_plane:NULL,scratch);
    for(size_t k=1;k+1<n;k++){
      qa_cpu_vertex tri[3]={poly[0],poly[k],poly[k+1]};
      svert sv[3];
      for(int j=0;j<3;j++){ qa_vec4 q=tri[j].position; sv[j].x=subpixel(p->viewport.x+(q.x/q.w+1)*p->viewport.width*0.5,p->subpixel_scale);
        sv[j].y=subpixel(p->viewport.y+(1-q.y/q.w)*p->viewport.height*0.5,p->subpixel_scale); sv[j].z=q.z/q.w; sv[j].iw=1/q.w; sv[j].v=tri[j]; }
      double area=edge_fn(sv[0].x,sv[0].y,sv[1].x,sv[1].y,sv[2].x,sv[2].y);
      if(area==0)continue;
      const char *cull=p->cull_override?p->cull_override:batch.state.cull;
      /* Screen Y is down: positive area is clockwise (front in GL terms
       * after the viewport flip used by the TS project()). */
      if(!strcmp(cull,"back")&&area<0)continue;
      if(!strcmp(cull,"front")&&area>0)continue;
      if(area<0){ /* Reverse winding so edge terms stay positive inside. */ svert tmp=sv[1];sv[1]=sv[2];sv[2]=tmp; area=-area; }
      qa_q2_fragment_light light_storage[64];
      qa_cpu_triangle_lighting tl; tl.base=lighting;
      for(int j=0;j<3;j++){ tl.positions[j]=sv[j].v.world_position; tl.normals[j]=sv[j].v.world_normal; }
      if(strcmp(lighting.parameters.kind,"vertex")) tl.base=qa_cull_triangle_lights(lighting,tl.positions,light_storage);
      qa_triangle_setup s; memset(&s,0,sizeof s); s.lighting=&tl;
      s.fog=batch.fog; s.texture_effect=batch.texture_effect;
      s.depth_near=batch.state.depth_range[0]; s.depth_far=batch.state.depth_range[1];
      double ax_=sv[0].x,ay_=sv[0].y,bx_=sv[1].x,by_=sv[1].y,cx_=sv[2].x,cy_=sv[2].y;
      s.attribute_ax=by_-cy_;s.attribute_ay=cx_-bx_;s.attribute_ac=bx_*cy_-by_*cx_;
      s.attribute_bx=cy_-ay_;s.attribute_by=ax_-cx_;s.attribute_bc=cx_*ay_-cy_*ax_;
      s.attribute_cx=ay_-by_;s.attribute_cy=bx_-ax_;s.attribute_cc=ax_*by_-ay_*bx_;
      s.edge_ax=s.attribute_ax;s.edge_ay=s.attribute_ay;s.edge_ac=s.attribute_ac;
      s.edge_bx=s.attribute_bx;s.edge_by=s.attribute_by;s.edge_bc=s.attribute_bc;
      s.edge_cx=s.attribute_cx;s.edge_cy=s.attribute_cy;s.edge_cc=s.attribute_cc;
      s.inverse_area=1/area;
      s.edge_a_inclusive=(cy_<by_||(cy_==by_&&cx_<bx_));
      s.edge_b_inclusive=(ay_<cy_||(ay_==cy_&&ax_<cx_));
      s.edge_c_inclusive=(by_<ay_||(by_==ay_&&bx_<ax_));
      s.min_x=fmax(fmax(0,p->viewport.x),floor(fmin(ax_,fmin(bx_,cx_))));
      s.max_x=fmin(fmin(r->width,p->viewport.x+p->viewport.width)-1,ceil(fmax(ax_,fmax(bx_,cx_)))-1);
      s.min_y=fmax(fmax(0,p->viewport.y),floor(fmin(ay_,fmin(by_,cy_))));
      s.max_y=fmin(fmin(r->height,p->viewport.y+p->viewport.height)-1,ceil(fmax(ay_,fmax(by_,cy_)))-1);
      s.az=sv[0].z;s.bz=sv[1].z;s.cz=sv[2].z; s.aiw=sv[0].iw;s.biw=sv[1].iw;s.ciw=sv[2].iw;
      s.au=sv[0].v.tex_coord.x*sv[0].iw;s.bu=sv[1].v.tex_coord.x*sv[1].iw;s.cu=sv[2].v.tex_coord.x*sv[2].iw;
      s.av=sv[0].v.tex_coord.y*sv[0].iw;s.bv=sv[1].v.tex_coord.y*sv[1].iw;s.cv=sv[2].v.tex_coord.y*sv[2].iw;
      s.au2=sv[0].v.tex_coord2.x*sv[0].iw;s.bu2=sv[1].v.tex_coord2.x*sv[1].iw;s.cu2=sv[2].v.tex_coord2.x*sv[2].iw;
      s.av2=sv[0].v.tex_coord2.y*sv[0].iw;s.bv2=sv[1].v.tex_coord2.y*sv[1].iw;s.cv2=sv[2].v.tex_coord2.y*sv[2].iw;
      s.ar=sv[0].v.color.x*sv[0].iw;s.br=sv[1].v.color.x*sv[1].iw;s.cr=sv[2].v.color.x*sv[2].iw;
      s.ag=sv[0].v.color.y*sv[0].iw;s.bg=sv[1].v.color.y*sv[1].iw;s.cg=sv[2].v.color.y*sv[2].iw;
      s.ab=sv[0].v.color.z*sv[0].iw;s.bb=sv[1].v.color.z*sv[1].iw;s.cb=sv[2].v.color.z*sv[2].iw;
      s.aa=sv[0].v.color.w*sv[0].iw;s.ba=sv[1].v.color.w*sv[1].iw;s.ca=sv[2].v.color.w*sv[2].iw;
      s.white=false;s.depth_write=batch.state.depth_write;s.stencil_enabled=p->stencil_enabled;s.color_write=p->color_write;
      s.texture_consumed=true;s.primary_alpha=true;s.secondary_alpha=true;s.constant_depth=false;
      s.width=r->width;s.height=r->height;s.blend=batch.state.blend;s.alpha_bits=r->alpha_bits;
      s.blending=classify_blending(batch.state.blend);s.depth_test=batch.state.depth_test;s.alpha_test=batch.state.alpha_test;
      s.secondary_environment=batch.texturing_is_pair?batch.second_texture.environment:NULL;
      s.texture=tex;s.secondary_texture=tex2;
      s.stencil_function=p->stencil_function;s.stencil_compare_mask=p->stencil_compare_mask;s.stencil_write_mask=p->stencil_write_mask;s.stencil_maximum=p->stencil_maximum;
      s.stencil_depth_fail=p->stencil_depth_fail;s.stencil_depth_pass=p->stencil_depth_pass;
      s.polygon_depth_offset=0;
      qa_run_triangle_rows_default(&s,&p->fb,&p->sampled);
    }
  }
}
static void validate_batch(const qa_draw_batch *b){
  if(strcmp(b->lighting.kind,"vertex")&&b->lighting.light_count>8&&b->lighting.model_shadow_light_count==0){
    /* TS checks lights.length>8 for q2-world; model-shadow has no lights. */
    if(!strcmp(b->lighting.kind,"q2-world")) qa_range_error("Q2 fragment lighting accepts at most eight selected lights per draw");
  }
  size_t step=b->primitive_is_lines?2:3;
  if(b->indices.count%step!=0) qa_range_error("Incomplete CPU primitive indices");
  if(b->primitive_is_lines&&(!isfinite(b->line_width)||b->line_width<=0)) qa_range_error("CPU line width must be positive");
  size_t vc=b->texturing_is_pair?b->pair_vertex_count:b->vertex_count;
  for(size_t i=0;i<b->indices.count;i++){ double ix=b->indices.values[i]; if(ix!=(double)(long long)ix||ix<0||ix>=(double)vc)qa_range_error("CPU vertex index is outside its array"); }
  if(b->texturing_is_pair){ for(size_t i=0;i<b->pair_vertex_count;i++){ const qa_multitexture_vertex *v=&b->pair_vertices[i];
    if(!finite4(v->vertex.position)||!finite4(v->vertex.color)||!isfinite(v->vertex.tex_coord.x)||!isfinite(v->vertex.tex_coord.y))qa_range_error("CPU vertex attributes must be finite");
    if(!isfinite(v->tex_coord2.x)||!isfinite(v->tex_coord2.y))qa_range_error("CPU secondary coordinates must be finite"); } }
  else for(size_t i=0;i<b->vertex_count;i++){ const qa_render_vertex *v=&b->vertices[i];
    if(!finite4(v->position)||!finite4(v->color)||!isfinite(v->tex_coord.x)||!isfinite(v->tex_coord.y))qa_range_error("CPU vertex attributes must be finite"); }
}
void qa_software_renderer_draw(qa_software_renderer *r,qa_draw_batch batch){ assert_open(r); validate_batch(&batch); draw_batch(r,batch); }
typedef struct prep { qa_software_renderer *r; qa_draw_batch batch; int phase; double next_unit; } prep;
static void prep_begin(void *ctx){ prep *p=(prep*)ctx; assert_open(p->r); if(p->phase!=0)qa_range_error("CPU prepared draw has already begun"); p->phase=1; }
static void prep_tex(void *ctx,double unit,qa_texture_binding op){ prep *p=(prep*)ctx; assert_open(p->r);
  if(p->phase!=1||unit!=p->next_unit||(unit!=0&&unit!=1)||(unit==1&&!p->batch.texturing_is_pair)) qa_range_error("CPU texture slots must execute in order");
  qa_cpu_images_bind(&R(p->r)->images,unit,op); p->next_unit++; }
static void prep_draw(void *ctx){ prep *p=(prep*)ctx; assert_open(p->r);
  if(p->phase!=1||p->next_unit!=(p->batch.texturing_is_pair?2:1)) qa_range_error("CPU prepared draw has unapplied texture slots");
  draw_batch(p->r,p->batch); p->phase=2; }
static void prep_cleanup(void *ctx){ prep *p=(prep*)ctx; assert_open(p->r); if(p->phase!=2)qa_range_error("CPU prepared draw has not completed"); p->phase=3; free(p); }
qa_prepared_backend_draw qa_software_renderer_prepare_geometry(qa_software_renderer *r,qa_draw_batch batch){ assert_open(r); validate_batch(&batch);
  prep *p=(prep*)calloc(1,sizeof *p); if(!p)qa_range_error("CPU renderer allocation failed");
  p->r=r;p->batch=batch; qa_prepared_backend_draw o; memset(&o,0,sizeof o); o.ctx=p;o.begin=prep_begin;o.apply_texture=prep_tex;o.draw=prep_draw;o.cleanup=prep_cleanup; return o; }
static void quad_pic(qa_software_renderer *r,qa_renderer_image image,qa_rect rect,qa_texture_rect uv,qa_vec4 color,qa_render_state state){
  qa_render_view_state v; memset(&v,0,sizeof v); v.viewport.x=0;v.viewport.y=0;v.viewport.width=r->width;v.viewport.height=r->height;
  qa_software_renderer_begin_view(r,v);
  double xs[4]={rect.x,rect.x+rect.width,rect.x+rect.width,rect.x},ys[4]={rect.y,rect.y,rect.y+rect.height,rect.y+rect.height};
  double ss[4]={uv.s1,uv.s2,uv.s2,uv.s1},ts[4]={uv.t1,uv.t1,uv.t2,uv.t2};
  qa_render_vertex verts[4]; double idx[6]={0,1,2,0,2,3};
  for(int i=0;i<4;i++){ verts[i].position.x=xs[i]*2/r->width-1;verts[i].position.y=1-ys[i]*2/r->height;verts[i].position.z=-1;verts[i].position.w=1;verts[i].tex_coord.x=ss[i];verts[i].tex_coord.y=ts[i];verts[i].color=color; }
  qa_draw_batch b; memset(&b,0,sizeof b); b.lighting.kind="vertex"; b.indices.values=idx;b.indices.count=6;
  b.texture.kind=QA_TEXTURE_BINDING_BIND_IMAGE;b.texture.image=image; b.state=state; b.vertices=verts;b.vertex_count=4;
  qa_software_renderer_draw(r,b);
}
void qa_software_renderer_draw_stretch_pic(qa_software_renderer *r,qa_renderer_image image,qa_rect rect,qa_texture_rect uv,qa_vec4 color){
  qa_render_state st=QA_CPU_OPAQUE_STATE; st.blend.source="src-alpha";st.blend.destination="one-minus-src-alpha";st.depth_test="always";st.depth_write=false;
  quad_pic(r,image,rect,uv,color,st); }
void qa_software_renderer_draw_show_image(qa_software_renderer *r,qa_renderer_image image,qa_rect rect,bool proportional){
  qa_rect rr=rect; rr.width=rect.width*(proportional?image.width/512:1); rr.height=rect.height*(proportional?image.height/512:1);
  qa_texture_rect uv; uv.s1=0;uv.t1=0;uv.s2=1;uv.t2=1; qa_vec4 white; white.x=white.y=white.z=white.w=1;
  /* TS drawShowImage does not reset the view; quad_pic does. Viewport is
   * restored by the next beginView in practice (documented reduction). */
  quad_pic(r,image,rr,uv,white,R(r)->retained_state); }
void qa_software_renderer_draw_immediate(qa_software_renderer *r,qa_render_operation op){ assert_open(r); rimpl *p=R(r);
  switch(op.kind){
  case QA_RENDER_OP_Q2_FOG: qa_apply_q2_depth_fog(&p->fb,op.fog,r->alpha_bits); return;
  case QA_RENDER_OP_DEPTH_RANGE: p->depth_range[0]=op.range[0];p->depth_range[1]=op.range[1]; return;
  case QA_RENDER_OP_CULL: p->cull_override=op.cull; return;
  case QA_RENDER_OP_POLYGON_OFFSET: p->has_poly_offset=op.polygon_offset!=NULL; if(op.polygon_offset)p->poly_offset=*op.polygon_offset; return;
  case QA_RENDER_OP_DISABLE_PORTAL_CLIP: p->has_clip=false; return;
  case QA_RENDER_OP_DEPTH_ATLAS: case QA_RENDER_OP_SKY_SIDE: case QA_RENDER_OP_SHADOW_VOLUME: case QA_RENDER_OP_SHADOW_FINISH:
    /* Reduced: atlas/sky/shadow immediate ops are not rasterized by this
     * core port (see slice unresolved list). */ return;
  default: qa_range_error("Invalid CPU immediate operation"); } }
/* Backend adapter (contracts qa_renderer_backend). */
static void be_image(void *ctx,qa_image_resource_operation op){ qa_software_renderer_apply_image_resource((qa_software_renderer*)ctx,op); }
static void be_buffer(void *ctx,qa_renderer_draw_buffer b,bool c){ qa_software_renderer_select_draw_buffer((qa_software_renderer*)ctx,b,c); }
static void be_overdraw(void *ctx,bool e){ qa_software_renderer_set_overdraw_measurement((qa_software_renderer*)ctx,e); }
static void be_read_stencil(void *ctx,uint8_t *d,size_t n){ qa_software_renderer_read_stencil_overdraw((qa_software_renderer*)ctx,d,n); }
static double be_depth(void *ctx,double x,double y){ return qa_software_renderer_read_depth_pixel((qa_software_renderer*)ctx,x,y); }
static void be_view(void *ctx,qa_render_view_state v){ qa_software_renderer_begin_view((qa_software_renderer*)ctx,v); }
static void be_opacity(void *ctx,double o,void (*draw)(void *),void *dctx){ qa_software_renderer_with_object_opacity((qa_software_renderer*)ctx,o,draw,dctx); }
static void be_immediate(void *ctx,qa_render_operation op){ qa_software_renderer_draw_immediate((qa_software_renderer*)ctx,op); }
static qa_prepared_backend_draw be_prepare(void *ctx,qa_draw_batch b){ return qa_software_renderer_prepare_geometry((qa_software_renderer*)ctx,b); }
static void be_clear(void *ctx){ qa_software_renderer_clear_color_buffer((qa_software_renderer*)ctx); }
static void be_show(void *ctx,qa_renderer_image im,qa_rect rc,bool pr){ qa_software_renderer_draw_show_image((qa_software_renderer*)ctx,im,rc,pr); }
static void be_finish(void *ctx){ qa_software_renderer_finish((qa_software_renderer*)ctx); }
static void be_close(void *ctx){ qa_software_renderer_close((qa_software_renderer*)ctx); }
qa_renderer_backend qa_software_renderer_backend(qa_software_renderer *r){ qa_renderer_backend b; memset(&b,0,sizeof b);
  b.ctx=r;b.owner=r->owner;b.width=r->width;b.height=r->height;b.stencil_bits=r->stencil_bits;
  b.apply_image_resource=be_image;b.select_draw_buffer=be_buffer;b.set_overdraw_measurement=be_overdraw;b.read_stencil_overdraw=be_read_stencil;
  b.read_depth_pixel=be_depth;b.begin_view=be_view;b.with_object_opacity=be_opacity;b.draw_immediate=be_immediate;b.prepare_geometry=be_prepare;
  b.clear_color_buffer=be_clear;b.draw_show_image=be_show;b.finish=be_finish;b.close=be_close; return b; }
