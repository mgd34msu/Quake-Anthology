/* Port of src/render/gl/renderer.ts (core path). GPL-2.0-or-later. */
#include "qa/render/gl/renderer.h"
#include "qa/render/gl/buffers.h"
#include "qa/render/gl/programs.h"
#include "qa/render/gl/textures.h"
#include "qa/render/output_gamma.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
typedef struct gimpl {
  qa_gl_render_context window; qa_gl gl; bool gl_loaded;
  qa_gl_stage_program program; bool has_program;
  qa_gl_textures textures; bool has_textures;
  qa_gl_geometry_buffer geometry; bool has_geometry;
  bool closed, opacity_active, overdraw;
  uint32_t draw_buffer; const char *alpha_test;
  bool has_vp; float view_projection[16];
  bool has_cull; const char *cull; bool has_range; double range_near, range_far;
  bool offset_known, offset_enabled; double offset_factor, offset_units;
  double gamma; bool has_gamma_table; uint8_t gamma_table[256];
} gimpl;
static gimpl *G(qa_gl_renderer *r){ return (gimpl*)r->impl; }
static void opened(qa_gl_renderer *r){ gimpl *p=G(r); if(p->closed)qa_range_error("OpenGL renderer is closed"); p->window.make_current(p->window.ctx); }
static double get_int(qa_gl *gl,uint32_t name){ int32_t v=0; gl->glGetIntegerv(name,&v); return v; }
double qa_gl_renderer_width(qa_gl_renderer *r){ double w=0,h=0; G(r)->window.drawable_size(G(r)->window.ctx,&w,&h); return w; }
double qa_gl_renderer_height(qa_gl_renderer *r){ double w=0,h=0; G(r)->window.drawable_size(G(r)->window.ctx,&w,&h); return h; }
static void identity_matrices(gimpl *p){ qa_gl *gl=&p->gl;
  gl->glMatrixMode(0x1701); gl->glLoadIdentity(); gl->glMatrixMode(0x1700); gl->glLoadIdentity(); }
static void world_matrices(gimpl *p){ qa_gl *gl=&p->gl;
  if(p->has_vp){ gl->glMatrixMode(0x1701); gl->glLoadMatrixf(p->view_projection); gl->glMatrixMode(0x1700); gl->glLoadIdentity(); }
  else identity_matrices(p); }
bool qa_gl_renderer_init(qa_gl_renderer *r,qa_gl_render_context window,qa_renderer_resource_owner owner){
  gimpl *p=(gimpl*)calloc(1,sizeof *p); if(!p)return false; r->impl=p; r->owner=owner; p->window=window;
  window.set_rendering_enabled(window.ctx,true); window.make_current(window.ctx);
  if(!qa_load_gl(&window.gl_context,&p->gl)){ free(p); r->impl=NULL; return false; }
  p->gl_loaded=true; qa_gl *gl=&p->gl;
  r->driver.vendor=gl->glGetString(0x1f00); r->driver.renderer=gl->glGetString(0x1f01);
  r->driver.version=gl->glGetString(0x1f02); r->driver.shading_language=gl->glGetString(0x8b8c);
  r->stencil_bits=get_int(gl,0xd57); r->depth_bits=get_int(gl,0xd56);
  r->color_bits=get_int(gl,0xd52)+get_int(gl,0xd53)+get_int(gl,0xd54); r->alpha_bits=get_int(gl,0xd55);
  r->max_texture_size=get_int(gl,0xd33);
  double tu=get_int(gl,0x8872),tu2=get_int(gl,0x8871); r->texture_units=tu<tu2?tu:tu2;
  r->stereo_enabled=get_int(gl,0xc33)!=0;
  if(r->texture_units<4||r->max_texture_size<1||r->depth_bits<1) qa_range_error("OpenGL renderer requires four texture coordinate units and a depth framebuffer");
  if(!qa_gl_stage_program_init(&p->program,&window.gl_context)){ qa_gl_close(&p->gl); free(p); r->impl=NULL; return false; }
  p->has_program=true;
  if(!qa_gl_textures_init(&p->textures,gl,owner,r->max_texture_size)) qa_range_error("OpenGL texture allocation failed");
  p->has_textures=true;
  if(!qa_gl_geometry_buffer_init(&p->geometry)) qa_range_error("OpenGL geometry allocation failed");
  p->has_geometry=true;
  identity_matrices(p);
  gl->glFrontFace(0x901); gl->glShadeModel(0x1d01); gl->glPolygonMode(0x408,0x1b02);
  gl->glDisable(0xb50);gl->glDisable(0xb60);gl->glDisable(0xbc0);gl->glDisable(0xb90);gl->glDisable(0x3000);gl->glDisable(0x8037);
  gl->glColorMask(1,1,1,1); gl->glDepthMask(1); gl->glClearStencil(0); gl->glStencilMask(0xffffffffu); gl->glColor4f(1,1,1,1);
  p->draw_buffer=0x405; p->alpha_test="none"; p->gamma=1;
  return true;
}
void qa_gl_renderer_set_output_gamma(qa_gl_renderer *r,double gamma){
  uint8_t table[256]; bool has=qa_output_gamma_table(gamma,table);
  opened(r); gimpl *p=G(r); if(gamma==p->gamma)return;
  p->gamma=gamma; p->has_gamma_table=has; if(has)memcpy(p->gamma_table,table,256);
}
static uint32_t blend_factor(const char *f){
  if (!strcmp(f,"zero")) return 0;
  if (!strcmp(f,"one"))return 1;
  if (!strcmp(f,"src-color")) return 0x300;
  if (!strcmp(f,"one-minus-src-color"))return 0x301;
  if (!strcmp(f,"dst-color")) return 0x306;
  if (!strcmp(f,"one-minus-dst-color"))return 0x307;
  if (!strcmp(f,"src-alpha")) return 0x302;
  if (!strcmp(f,"one-minus-src-alpha"))return 0x303;
  if (!strcmp(f,"dst-alpha")) return 0x304;
  if (!strcmp(f,"one-minus-dst-alpha"))return 0x305;
  return 0x308;
}
static void apply_cull(gimpl *p,const char *cull){
  qa_gl *gl=&p->gl;
  if(!strcmp(cull,"none")){ gl->glDisable(0xb44); }
  else { gl->glEnable(0xb44); gl->glCullFace(!strcmp(cull,"front")?0x404:0x405); }
  p->cull=cull;p->has_cull=true;
}
static void apply_offset(gimpl *p,const qa_polygon_offset *offset){
  qa_gl *gl=&p->gl;
  if(offset==NULL){ gl->glDisable(0x8037); p->offset_enabled=false; }
  else { gl->glEnable(0x8037); gl->glPolygonOffset((float)offset->factor,(float)offset->units); p->offset_enabled=true;p->offset_factor=offset->factor;p->offset_units=offset->units; }
  p->offset_known=true;
}
static void apply_range(gimpl *p,const double range[2]){ p->gl.glDepthRange(range[0],range[1]); p->has_range=true;p->range_near=range[0];p->range_far=range[1]; }
static void apply_state(gimpl *p,qa_render_state state){
  qa_gl *gl=&p->gl;
  if(!strcmp(state.blend.destination,"src-alpha-saturate")) qa_range_error("OpenGL destination blend cannot use source alpha saturate");
  gl->glEnable(0xb71);
  gl->glDepthFunc(!strcmp(state.depth_test,"less-equal")?0x203:!strcmp(state.depth_test,"equal")?0x202:0x207);
  gl->glDepthMask(state.depth_write?1:0);
  if(!strcmp(state.blend.source,"one")&&!strcmp(state.blend.destination,"zero")) gl->glDisable(0xbe2);
  else { gl->glEnable(0xbe2); gl->glBlendFunc(blend_factor(state.blend.source),blend_factor(state.blend.destination)); }
  gl->glDisable(0xbc0); p->alpha_test=state.alpha_test;
  apply_cull(p,state.cull); apply_range(p,state.depth_range); apply_offset(p,state.polygon_offset);
}
void qa_gl_renderer_apply_image_resource(qa_gl_renderer *r,qa_image_resource_operation op){
  opened(r); gimpl *p=G(r); qa_gl *gl=&p->gl;
  gl->glActiveTexture(0x84c0);
  int32_t previous=0; gl->glGetIntegerv(0x8069,&previous);
  bool is_release=op.kind==QA_IMAGE_OP_RELEASE;
  uint32_t released=0; if(is_release) released=qa_gl_textures_registered(&p->textures,op.image).name;
  qa_gl_textures_apply(&p->textures,op);
  gl->glBindTexture(0xde1,((uint32_t)previous==released&&is_release)?0:(uint32_t)previous);
}
static uint32_t buffer_value(const char *b){
  if (!strcmp(b,"front")) return 0x404;
  if (!strcmp(b, "back-left")) return 0x402;
  if (!strcmp(b, "back-right")) return 0x403;
  return 0x405;
}
void qa_gl_renderer_select_draw_buffer(qa_gl_renderer *r,const char *buffer,bool clear){
  opened(r); gimpl *p=G(r);
  if(!strcmp(buffer,"back-right")&&!r->stereo_enabled) qa_range_error("OpenGL stereo draw buffer requires a stereo context");
  p->draw_buffer=buffer_value(buffer);
  p->gl.glDrawBuffer(p->draw_buffer);
  if(clear){ p->gl.glClearColor(1,0,0.5f,1); p->gl.glClear(0x4000|0x100); }
}
static int finite32v(double v){ return isfinite(v)&&fabs(v)<3.4028235677973366e38; }
void qa_gl_renderer_begin_view(qa_gl_renderer *r,qa_render_view_state view){
  opened(r); gimpl *p=G(r); qa_gl *gl=&p->gl;
  double height=qa_gl_renderer_height(r);
  qa_rect vp=view.viewport; double bottom=height-vp.y-vp.height;
  double vals[5]={vp.x,vp.y,vp.width,vp.height,bottom};
  for(int i=0;i<5;i++) if(vals[i]!=(double)(long long)vals[i]||vals[i]<-2147483648.0||vals[i]>2147483647.0) qa_range_error("OpenGL viewport requires positive int32 dimensions");
  if(vp.width<1||vp.height<1) qa_range_error("OpenGL viewport requires positive int32 dimensions");
  p->has_vp=view.has_view_projection;
  if(view.has_view_projection){ for(int i=0;i<16;i++){ if(!isfinite(view.view_projection.values[i]))qa_range_error("OpenGL view projection must be finite"); p->view_projection[i]=(float)view.view_projection.values[i]; } }
  gl->glViewport((int32_t)vp.x,(int32_t)bottom,(int32_t)vp.width,(int32_t)vp.height);
  gl->glEnable(0xc11); gl->glScissor((int32_t)vp.x,(int32_t)bottom,(int32_t)vp.width,(int32_t)vp.height);
  identity_matrices(p);
  if(view.clip_plane==NULL) gl->glDisable(0x3000);
  else { qa_vec4 plane=*view.clip_plane;
    if(!finite32v(plane.x)||!finite32v(plane.y)||!finite32v(plane.z)||!finite32v(plane.w)) qa_range_error("OpenGL clip plane must be finite");
    double eq[4]={plane.x,plane.y,plane.z,plane.w}; gl->glClipPlane(0x3000,eq); gl->glEnable(0x3000); }
  if(view.clear!=NULL){
    if(!isfinite(view.clear->depth)||(view.clear->color&&!(finite32v(view.clear->color->x)&&finite32v(view.clear->color->y)&&finite32v(view.clear->color->z)&&finite32v(view.clear->color->w)))) qa_range_error("OpenGL clear values must be finite");
    gl->glDepthMask(1); gl->glClearDepth(view.clear->depth);
    if(view.clear->color) gl->glClearColor((float)view.clear->color->x,(float)view.clear->color->y,(float)view.clear->color->z,(float)view.clear->color->w);
    if(view.clear->stencil){ gl->glStencilMask(0xffffffffu); gl->glClearStencil(0); }
    gl->glClear(0x100|(view.clear->color?0x4000:0)|(view.clear->stencil?0x400:0));
  }
}
typedef struct gprep { qa_gl_renderer *r; qa_draw_batch batch; qa_gl_geometry_arrays arrays; int phase; double next_unit; } gprep;
static void select_unit(gimpl *p,double unit){ p->gl.glActiveTexture(0x84c0+(uint32_t)unit); p->gl.glClientActiveTexture(0x84c0+(uint32_t)unit); }
static void gprep_begin(void *ctx){ gprep *pr=(gprep*)ctx; opened(pr->r); if(pr->phase!=0)qa_range_error("OpenGL prepared draw is already active");
  gimpl *p=G(pr->r);
  if(strcmp(pr->batch.lighting.kind,"vertex")&&pr->batch.lighting.atlas!=NULL){
    qa_gl_texture_info info=qa_gl_textures_registered(&p->textures,pr->batch.lighting.atlas->image);
    if(info.content_kind!=QA_RENDER_IMAGE_DEPTH32F) qa_range_error("Q2 shadow atlas requires a depth32f image");
    select_unit(p,2); p->gl.glEnable(0xde1); p->gl.glBindTexture(0xde1,info.name); select_unit(p,0);
  }
  apply_state(p,pr->batch.state); pr->phase=1; }
static void gprep_tex(void *ctx,double unit,qa_texture_binding op){ gprep *pr=(gprep*)ctx; opened(pr->r);
  if(pr->phase!=1||unit!=pr->next_unit||(unit!=0&&unit!=1)||(unit==1&&!pr->batch.texturing_is_pair)) qa_range_error("OpenGL texture slots must execute in order");
  gimpl *p=G(pr->r); select_unit(p,unit); p->gl.glEnable(0xde1); qa_gl_textures_bind(&p->textures,op); pr->next_unit++; }
static void gprep_draw(void *ctx){ gprep *pr=(gprep*)ctx; opened(pr->r);
  if(pr->phase!=1||pr->next_unit!=(pr->batch.texturing_is_pair?2:1)) qa_range_error("OpenGL prepared draw has unapplied texture slots");
  gimpl *p=G(pr->r); qa_gl *gl=&p->gl;
  const char *env=pr->batch.texturing_is_pair?pr->batch.second_texture.environment:NULL;
  bool lum=pr->batch.texture_effect&&!strcmp(pr->batch.texture_effect,"luminance-alpha");
  qa_gl_stage_program_use(&p->program,env,p->alpha_test,pr->batch.lighting,lum,pr->batch.fog);
  if(pr->batch.space&&!strcmp(pr->batch.space,"world")) world_matrices(p); else identity_matrices(p);
  qa_gl_geometry_arrays *a=&pr->arrays;
  gl->glEnableClientState(0x8074); gl->glVertexPointer(4,0x1406,0,a->positions);
  gl->glEnableClientState(0x8076); gl->glColorPointer(4,0x1406,0,a->colors);
  select_unit(p,0); gl->glEnableClientState(0x8078); gl->glTexCoordPointer(2,0x1406,0,a->coordinates);
  select_unit(p,1); gl->glEnableClientState(0x8078); gl->glTexCoordPointer(2,0x1406,0,a->coordinates2);
  if(a->world_position_count){ select_unit(p,2); gl->glEnableClientState(0x8078); gl->glTexCoordPointer(3,0x1406,0,a->world_positions); }
  if(a->normal_count){ select_unit(p,3); gl->glEnableClientState(0x8078); gl->glTexCoordPointer(3,0x1406,0,a->normals); }
  if(pr->batch.primitive_is_lines) gl->glLineWidth((float)pr->batch.line_width);
  gl->glDrawElements(pr->batch.primitive_is_lines?1:4,(int32_t)a->index_count,0x1405,a->indices);
  pr->phase=2; }
static void gprep_cleanup(void *ctx){ gprep *pr=(gprep*)ctx; opened(pr->r); if(pr->phase!=2)qa_range_error("OpenGL prepared draw has not completed");
  gimpl *p=G(pr->r); qa_gl *gl=&p->gl;
  for(int u=3;u>=0;u--){ select_unit(p,u); gl->glDisableClientState(0x8078); gl->glDisable(0xde1); }
  gl->glDisableClientState(0x8074); gl->glDisableClientState(0x8076);
  select_unit(p,0); pr->phase=3; free(pr); }
qa_prepared_backend_draw qa_gl_renderer_prepare_geometry(qa_gl_renderer *r,qa_draw_batch batch){
  opened(r); gimpl *p=G(r);
  gprep *pr=(gprep*)calloc(1,sizeof *pr); if(!pr)qa_range_error("OpenGL renderer allocation failed");
  pr->r=r;pr->batch=batch; pr->arrays=qa_gl_geometry_buffer_pack(&p->geometry,batch);
  qa_prepared_backend_draw o; memset(&o,0,sizeof o); o.ctx=pr;o.begin=gprep_begin;o.apply_texture=gprep_tex;o.draw=gprep_draw;o.cleanup=gprep_cleanup; return o;
}
void qa_gl_renderer_with_object_opacity(qa_gl_renderer *r,double opacity,qa_gl_draw_fn draw,void *ctx){
  opened(r); gimpl *p=G(r);
  if(!isfinite(opacity)||opacity<0||opacity>1) qa_range_error("Object opacity must be in 0..1");
  if(p->opacity_active) qa_range_error("Object opacity scopes cannot nest");
  if(opacity==0)return;
  p->opacity_active=true; draw(ctx); p->opacity_active=false;
}
void qa_gl_renderer_draw_immediate(qa_gl_renderer *r,qa_render_operation op){
  opened(r); gimpl *p=G(r); qa_gl *gl=&p->gl;
  switch(op.kind){
  case QA_RENDER_OP_DEPTH_RANGE: apply_range(p,op.range); return;
  case QA_RENDER_OP_CULL: apply_cull(p,op.cull); return;
  case QA_RENDER_OP_POLYGON_OFFSET: apply_offset(p,op.polygon_offset); return;
  case QA_RENDER_OP_DISABLE_PORTAL_CLIP: gl->glDisable(0x3000); return;
  case QA_RENDER_OP_OBJECT_OPACITY: case QA_RENDER_OP_DRAW: qa_range_error("Invalid OpenGL immediate operation");
  default: return; /* q2-fog/depth-atlas/sky/shadow: reduced helpers. */
  }
}
void qa_gl_renderer_clear_color_buffer(qa_gl_renderer *r){ opened(r); G(r)->gl.glClear(0x4000); }
void qa_gl_renderer_draw_show_image(qa_gl_renderer *r,qa_renderer_image image,qa_rect rect,bool proportional){
  opened(r); gimpl *p=G(r);
  double w=qa_gl_renderer_width(r),h=qa_gl_renderer_height(r);
  double width=rect.width*(proportional?image.width/512:1),height=rect.height*(proportional?image.height/512:1);
  qa_texture_binding binding; memset(&binding,0,sizeof binding); binding.kind=QA_TEXTURE_BINDING_BIND_IMAGE;binding.image=image;
  select_unit(p,0); p->gl.glEnable(0xde1); qa_gl_textures_bind(&p->textures,binding);
  identity_matrices(p);
  double xs[4]={rect.x,rect.x+width,rect.x+width,rect.x},ys[4]={rect.y,rect.y,rect.y+height,rect.y+height};
  double ss[4]={0,1,1,0},ts[4]={0,0,1,1};
  p->gl.glBegin(7);
  for(int i=0;i<4;i++){ p->gl.glTexCoord2f((float)ss[i],(float)ts[i]); p->gl.glVertex2f((float)(xs[i]*2/w-1),(float)(1-ys[i]*2/h)); }
  p->gl.glEnd();
}
void qa_gl_renderer_set_overdraw_measurement(qa_gl_renderer *r,bool enabled){
  opened(r); gimpl *p=G(r);
  if(enabled&&r->stencil_bits==0) qa_range_error("OpenGL overdraw measurement requires stencil storage");
  p->overdraw=enabled;
  if(enabled){ p->gl.glEnable(0xb90); p->gl.glStencilFunc(0x207,0,0xffffffffu); p->gl.glStencilOp(0x1e00,0x8507,0x8507); }
  else p->gl.glDisable(0xb90);
}
void qa_gl_renderer_read_stencil_overdraw(qa_gl_renderer *r,uint8_t *dest,size_t length){
  opened(r); gimpl *p=G(r);
  if(r->stencil_bits==0) qa_range_error("OpenGL stencil readback requires stencil storage");
  double w=qa_gl_renderer_width(r),h=qa_gl_renderer_height(r);
  if(length<(size_t)(w*h)) qa_range_error("OpenGL stencil destination is too small");
  p->gl.glReadPixels(0,0,(int32_t)w,(int32_t)h,0x1a98,0x1401,dest);
}
double qa_gl_renderer_read_depth_pixel(qa_gl_renderer *r,double x,double y){
  opened(r); gimpl *p=G(r);
  double w=qa_gl_renderer_width(r),h=qa_gl_renderer_height(r);
  if(x!=(double)(long long)x||y!=(double)(long long)y||x<0||x>=w||y<0||y>=h) qa_range_error("OpenGL depth readback coordinates must be inside the framebuffer");
  float v=0; p->gl.glReadPixels((int32_t)x,(int32_t)y,1,1,0x1902,0x1406,&v); return (double)(float)v;
}
uint8_t *qa_gl_renderer_read_pixels(qa_gl_renderer *r,size_t *out_length){
  opened(r); gimpl *p=G(r);
  double w=qa_gl_renderer_width(r),h=qa_gl_renderer_height(r); size_t n=(size_t)(w*h*4);
  uint8_t *out=(uint8_t*)malloc(n?n:1); if(!out){if(out_length)*out_length=0;return NULL;}
  p->gl.glReadPixels(0,0,(int32_t)w,(int32_t)h,0x1908,0x1401,out);
  if (out_length) *out_length = n;
  return out;
}
bool qa_gl_renderer_read_depth_image(qa_gl_renderer *r,qa_renderer_image image,qa_depth_image_level *out,float *storage,size_t storage_count){
  opened(r); gimpl *p=G(r);
  qa_gl_texture_info info=qa_gl_textures_registered(&p->textures,image);
  if(info.content_kind!=QA_RENDER_IMAGE_DEPTH32F) qa_range_error("OpenGL depth readback requires a depth32f image");
  size_t need=(size_t)(info.width*info.height);
  if(storage_count<need)return false;
  p->gl.glBindTexture(0xde1,info.name);
  p->gl.glGetTexImage(0xde1,0,0x1902,0x1406,storage);
  out->width=info.width;out->height=info.height;out->pixels=storage;out->pixel_count=need; return true;
}
void qa_gl_renderer_finish(qa_gl_renderer *r){ opened(r); G(r)->gl.glFinish(); }
void qa_gl_renderer_close(qa_gl_renderer *r){ if(!r->impl)return; gimpl *p=G(r); if(p->closed)return;
  if(p->has_textures)qa_gl_textures_close(&p->textures);
  if(p->has_geometry)qa_gl_geometry_buffer_destroy(&p->geometry);
  if(p->has_program)qa_gl_stage_program_close(&p->program);
  if(p->gl_loaded)qa_gl_close(&p->gl);
  p->closed=true; free(p); r->impl=NULL; }
static void be_image(void *ctx,qa_image_resource_operation op){ qa_gl_renderer_apply_image_resource((qa_gl_renderer*)ctx,op); }
static void be_buffer(void *ctx,qa_renderer_draw_buffer b,bool c){ qa_gl_renderer_select_draw_buffer((qa_gl_renderer*)ctx,b,c); }
static void be_overdraw(void *ctx,bool e){ qa_gl_renderer_set_overdraw_measurement((qa_gl_renderer*)ctx,e); }
static void be_stencil(void *ctx,uint8_t *d,size_t n){ qa_gl_renderer_read_stencil_overdraw((qa_gl_renderer*)ctx,d,n); }
static double be_depth(void *ctx,double x,double y){ return qa_gl_renderer_read_depth_pixel((qa_gl_renderer*)ctx,x,y); }
static void be_view(void *ctx,qa_render_view_state v){ qa_gl_renderer_begin_view((qa_gl_renderer*)ctx,v); }
static void be_opacity(void *ctx,double o,void (*draw)(void *),void *d){ qa_gl_renderer_with_object_opacity((qa_gl_renderer*)ctx,o,draw,d); }
static void be_immediate(void *ctx,qa_render_operation op){ qa_gl_renderer_draw_immediate((qa_gl_renderer*)ctx,op); }
static qa_prepared_backend_draw be_prepare(void *ctx,qa_draw_batch b){ return qa_gl_renderer_prepare_geometry((qa_gl_renderer*)ctx,b); }
static void be_clear(void *ctx){ qa_gl_renderer_clear_color_buffer((qa_gl_renderer*)ctx); }
static void be_show(void *ctx,qa_renderer_image im,qa_rect rc,bool pr){ qa_gl_renderer_draw_show_image((qa_gl_renderer*)ctx,im,rc,pr); }
static void be_finish(void *ctx){ qa_gl_renderer_finish((qa_gl_renderer*)ctx); }
static void be_close(void *ctx){ qa_gl_renderer_close((qa_gl_renderer*)ctx); }
qa_renderer_backend qa_gl_renderer_backend(qa_gl_renderer *r){ qa_renderer_backend b; memset(&b,0,sizeof b);
  b.ctx=r;b.owner=r->owner;b.width=qa_gl_renderer_width(r);b.height=qa_gl_renderer_height(r);b.stencil_bits=r->stencil_bits;
  b.apply_image_resource=be_image;b.select_draw_buffer=be_buffer;b.set_overdraw_measurement=be_overdraw;b.read_stencil_overdraw=be_stencil;
  b.read_depth_pixel=be_depth;b.begin_view=be_view;b.with_object_opacity=be_opacity;b.draw_immediate=be_immediate;b.prepare_geometry=be_prepare;
  b.clear_color_buffer=be_clear;b.draw_show_image=be_show;b.finish=be_finish;b.close=be_close; return b; }
