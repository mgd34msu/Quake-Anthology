/* Port of src/render/gl/programs.ts. GPL-2.0-or-later. */
#include "qa/render/gl/programs.h"
#include "qa/render/gl/shadow_shader.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
const char *const QA_GL_STAGE_VERTEX_SHADER =
"#version 120\n"
"varying vec4 vertexColor;\n"
"varying vec2 coordinates0;\n"
"varying vec2 coordinates1;\n"
"varying vec3 worldPosition;\n"
"varying vec3 worldNormal;\n"
"void main() {\n"
"  gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
"  gl_ClipVertex = gl_ModelViewMatrix * gl_Vertex;\n"
"  vertexColor = clamp(gl_Color, 0.0, 1.0);\n"
"  coordinates0 = gl_MultiTexCoord0.xy;\n"
"  coordinates1 = gl_MultiTexCoord1.xy;\n"
"  worldPosition = gl_MultiTexCoord2.xyz;\n"
"  worldNormal = gl_MultiTexCoord3.xyz;\n"
"}\n";
static const char *const FRAG_HEAD =
"#version 120\n"
"uniform sampler2D primaryTexture;\n"
"uniform sampler2D secondaryTexture;\n"
"uniform int secondaryMode;\n"
"uniform int alphaMode;\n"
"uniform int u_fog_mode;\n"
"uniform vec3 u_fog_color;\n"
"uniform float u_fog_amount;\n"
"varying vec4 vertexColor;\n"
"varying vec2 coordinates0;\n"
"varying vec2 coordinates1;\n"
"varying vec3 worldPosition;\n"
"varying vec3 worldNormal;\n"
"uniform int u_lighting_mode;\n"
"uniform int u_luminance_alpha;\n"
"uniform int u_light_count;\n"
"uniform sampler2D u_shadow_map;\n"
"uniform float u_shadow_texel;\n"
"uniform float u_shadow_near;\n"
"uniform vec3 u_light_pos[8];\n"
"uniform float u_light_radius[8];\n"
"uniform vec3 u_light_color[8];\n"
"uniform float u_light_scale[8];\n"
"uniform vec3 u_light_cone_dir[8];\n"
"uniform float u_light_cone_cos[8];\n"
"uniform mat4 u_light_matrix[8];\n"
"uniform vec4 u_light_atlas[8];\n"
"uniform float u_light_shadow[8];\n"
"uniform vec3 u_light_frac[8];\n"
"uniform float u_shade_scale;\n"
"#define SHADOW_NEAR u_shadow_near\n"
"#define SHADOW_CUBE_COLS 3.0\n"
"#define SHADOW_CUBE_ROWS 2.0\n"
"vec3 dynamicLights() {\n"
"  vec3 shade = vec3(0.0);\n"
"  for (int i = 0; i < 8; i++) {\n"
"    if (i >= u_light_count) break;\n"
"    if (u_light_scale[i] == 0.0 || all(equal(u_light_color[i], vec3(0.0)))) continue;\n"
"    vec3 lightPosition = u_light_pos[i];\n"
"    float cone = u_light_cone_cos[i];\n"
"    if (cone == 0.0) lightPosition += worldNormal * 16.0;\n"
"    vec3 towardLight = lightPosition - worldPosition;\n"
"    float distance = length(towardLight);\n"
"    float radius = u_light_radius[i] + 64.0;\n"
"    float falloff = max(radius - distance - 64.0, 0.0) / radius;\n"
"    vec3 direction = towardLight / max(distance, 1.0);\n"
"    float lambert = u_light_color[i].r < 0.0 ? 1.0 : max(dot(worldNormal, direction), 0.0);\n"
"    vec3 result = u_light_color[i] * u_light_scale[i] * falloff * lambert;\n"
"    if (cone != 0.0) {\n"
"      float magnitude = -dot(direction, u_light_cone_dir[i]);\n"
"      result *= cone >= 1.0 ? 0.0 : max(1.0 - (1.0 - magnitude) / (1.0 - cone), 0.0);\n"
"    }\n"
"    if (u_light_shadow[i] != 0.0) {\n";
static const char *const FRAG_MID =
"      result *= lit;\n"
"    }\n"
"    shade += result;\n"
"  }\n"
"  return shade;\n"
"}\n"
"vec4 modelShadow(vec4 texel) {\n"
"  vec3 shade = vertexColor.rgb * u_shade_scale;\n"
"  vec3 keep = vec3(1.0);\n"
"  for (int i = 0; i < 8; i++) {\n"
"    if (i >= u_light_count) break;\n"
"    if (all(equal(u_light_frac[i], vec3(0.0)))) continue;\n"
"    if (u_light_shadow[i] != 0.0) {\n";
static const char *const FRAG_TAIL =
"      keep -= u_light_frac[i] * (1.0 - lit);\n"
"    }\n"
"  }\n"
"  shade *= max(keep, vec3(0.0));\n"
"  return vec4(texel.rgb * min(shade, vec3(1.0)), texel.a * vertexColor.a);\n"
"}\n"
"void main() {\n"
"  vec4 texel = texture2D(primaryTexture, coordinates0);\n"
"  if (u_luminance_alpha != 0) texel.rgb *= (texel.r + texel.g + texel.b) / 3.0 * vertexColor.a;\n"
"  vec4 color = clamp(texel * vertexColor, 0.0, 1.0);\n"
"  if (u_lighting_mode == 1) color = vec4(texel.rgb + dynamicLights(), 1.0);\n"
"  else if (u_lighting_mode == 2) color.rgb += dynamicLights();\n"
"  else if (u_lighting_mode == 3) color = modelShadow(texel);\n"
"  else if (u_lighting_mode == 4) color = vec4((texel.rgb + dynamicLights()) * vertexColor.rgb, texel.a * vertexColor.a);\n"
"  else if (u_lighting_mode == 5) {\n"
"    color = u_shade_scale > 0.0 ? modelShadow(texel) : texel * vertexColor;\n"
"    color.rgb += texel.rgb * dynamicLights();\n"
"  }\n"
"  if (secondaryMode != 0) {\n"
"    vec4 second = texture2D(secondaryTexture, coordinates1);\n"
"    if (secondaryMode == 1) color *= second;\n"
"    else if (secondaryMode == 2) color = vec4(color.rgb + second.rgb, color.a * second.a);\n"
"    else color = second;\n"
"    color = clamp(color, 0.0, 1.0);\n"
"  }\n"
"  float fogAmount = u_fog_mode == 2 ? u_fog_amount : 0.0;\n"
"  if (u_fog_mode != 0 && u_fog_mode != 2) {\n"
"    float distance = u_fog_amount / (64.0 * gl_FragCoord.w);\n"
"    fogAmount = 1.0 - exp(-distance * distance);\n"
"  }\n"
"  if (u_fog_mode != 0) color.rgb = clamp(color.rgb, 0.0, 1.0);\n"
"  if (u_fog_mode == 1 || u_fog_mode == 2) color.rgb = mix(color.rgb, u_fog_color, fogAmount);\n"
"  if (u_fog_mode == 3 || u_fog_mode == 5) color.rgb *= 1.0 - fogAmount;\n"
"  if (u_fog_mode == 4 || u_fog_mode == 5) color.a *= 1.0 - fogAmount;\n"
"  if (u_fog_mode == 6) color = vec4(u_fog_color, color.a * fogAmount);\n"
"  if (alphaMode == 1 && color.a <= 0.0) discard;\n"
"  if (alphaMode == 2 && color.a >= 0.5) discard;\n"
"  if (alphaMode == 3 && color.a < 0.5) discard;\n"
"  gl_FragColor = color;\n"
"}\n";
static void append_shadow(char *out,size_t *off,const char *receiver){
  const char *lines[96]; char dyn[3][256];
  size_t n=qa_shadow_factor_lines(receiver,lines,96,dyn);
  for(size_t i=0;i<n;i++){ size_t l=strlen(lines[i]); memcpy(out+*off,lines[i],l); *off+=l; out[(*off)++]='\n'; }
}
char *qa_gl_stage_fragment_shader(void){
  size_t cap=strlen(FRAG_HEAD)+strlen(FRAG_MID)+strlen(FRAG_TAIL)+16384;
  char *out=(char*)malloc(cap); if(!out)qa_range_error("stage shader allocation failed");
  size_t off=0; memcpy(out+off,FRAG_HEAD,strlen(FRAG_HEAD)); off+=strlen(FRAG_HEAD);
  append_shadow(out,&off,"world");
  memcpy(out+off,FRAG_MID,strlen(FRAG_MID)); off+=strlen(FRAG_MID);
  append_shadow(out,&off,"model");
  memcpy(out+off,FRAG_TAIL,strlen(FRAG_TAIL)); off+=strlen(FRAG_TAIL);
  out[off]=0; return out;
}
uint32_t qa_gl_compile_program(qa_gl_programs *lib,const char *vertex,const char *fragment){
  uint32_t shaders[2]={0,0}; uint32_t program=0;
  const char *sources[2]={vertex,fragment}; const uint32_t kinds[2]={0x8b31,0x8b30};
  for(int i=0;i<2;i++){
    uint32_t shader=lib->glCreateShader(kinds[i]);
    if(shader==0){ for(int j=0;j<i;j++)lib->glDeleteShader(shaders[j]); qa_range_error("OpenGL could not allocate a shader"); }
    shaders[i]=shader;
    qa_gl_programs_shader_source(lib,shader,sources[i]);
    lib->glCompileShader(shader);
    int32_t status=0; lib->glGetShaderiv(shader,0x8b81,&status);
    if(status!=1){ char log[8192]; int32_t size=0; lib->glGetShaderInfoLog(shader,8192,&size,log); log[8191]=0;
      for(int j=0;j<2;j++)if(shaders[j])lib->glDeleteShader(shaders[j]);
      qa_range_error("OpenGL shader compilation failed: %s",log); }
  }
  program=lib->glCreateProgram();
  if(program==0){ lib->glDeleteShader(shaders[0]);lib->glDeleteShader(shaders[1]); qa_range_error("OpenGL could not allocate a program"); }
  lib->glAttachShader(program,shaders[0]); lib->glAttachShader(program,shaders[1]);
  lib->glLinkProgram(program);
  int32_t status=0; lib->glGetProgramiv(program,0x8b82,&status);
  lib->glDeleteShader(shaders[0]); lib->glDeleteShader(shaders[1]);
  if(status!=1){ char log[8192]; int32_t size=0; lib->glGetProgramInfoLog(program,8192,&size,log); log[8191]=0;
    lib->glDeleteProgram(program); qa_range_error("OpenGL program linking failed: %s",log); }
  return program;
}
typedef struct simpl { qa_gl_programs library; uint32_t program, depth_program; bool closed; } simpl;
static int32_t uniform_loc(simpl *p,const char *name){
  int32_t loc=p->library.glGetUniformLocation(p->program,name);
  if(loc<0) qa_range_error("OpenGL stage uniform is missing: %s",name);
  return loc;
}
static void finite4u(double a,double b,double c,double d){ double w=qa_js_max(qa_js_max(fabs(a),fabs(b)),qa_js_max(fabs(c),fabs(d))); if(!(w<3.4028235677973366e38))qa_range_error("OpenGL lighting uniforms must be finite float32 values"); }
static void finite_arr(const double *v,size_t n){ double w=0; for(size_t i=0;i<n;i++)w=qa_js_max(w,fabs(v[i])); if(!(w<3.4028235677973366e38))qa_range_error("OpenGL lighting uniforms must be finite float32 values"); }
bool qa_gl_stage_program_init(qa_gl_stage_program *program,const qa_gl_context *context){
  simpl *p=(simpl*)calloc(1,sizeof *p); if(!p)return false; program->impl=p;
  if(!qa_load_gl_programs(context,&p->library)){ free(p); program->impl=NULL; return false; }
  char *frag=qa_gl_stage_fragment_shader();
  p->program=qa_gl_compile_program(&p->library,QA_GL_STAGE_VERTEX_SHADER,frag);
  free(frag);
  p->depth_program=qa_gl_compile_program(&p->library,"#version 120\nvoid main() { gl_Position = gl_Vertex; }\n","#version 120\nvoid main() { gl_FragColor = vec4(1.0); }\n");
  p->library.glUseProgram(p->program);
  p->library.glUniform1i(uniform_loc(p,"primaryTexture"),0);
  p->library.glUniform1i(uniform_loc(p,"secondaryTexture"),1);
  p->library.glUniform1i(uniform_loc(p,"u_shadow_map"),2);
  p->library.glUseProgram(0);
  return true;
}
static double alpha_mode(const char *t){ if(!strcmp(t,"gt0"))return 1; if(!strcmp(t,"lt128"))return 2; if(!strcmp(t,"ge128"))return 3; return 0; }
static double secondary_mode(const char *e){ if(!strcmp(e,"modulate"))return 1; if(!strcmp(e,"add"))return 2; return 3; }
void qa_gl_stage_program_use(qa_gl_stage_program *program,const char *environment,const char *alpha_test,qa_batch_lighting lighting,bool luminance_alpha,const qa_batch_fog *fog){
  simpl *p=(simpl*)program->impl;
  if(p->closed) qa_range_error("OpenGL stage program is closed");
  qa_gl_programs *gl=&p->library; gl->glUseProgram(p->program);
  double fog_mode=fog==NULL?0:!strcmp(fog->kind,"constant")?2:(fog->effect&&!strcmp(fog->effect,"none"))?0:(fog->effect&&!strcmp(fog->effect,"rgb"))?3:(fog->effect&&!strcmp(fog->effect,"alpha"))?4:(fog->effect&&!strcmp(fog->effect,"rgba"))?5:(fog->effect&&!strcmp(fog->effect,"overlay"))?6:1;
  gl->glUniform1i(uniform_loc(p,"u_fog_mode"),(int32_t)fog_mode);
  gl->glUniform3f(uniform_loc(p,"u_fog_color"),(float)(fog?fog->color.x:0),(float)(fog?fog->color.y:0),(float)(fog?fog->color.z:0));
  int32_t value7 = uniform_loc(p,"u_fog_amount");
  float value8 = (float)(fog==NULL?0:!strcmp(fog->kind,"exp2")?fog->density:fog->amount);
  gl->glUniform1f(value7,value8);
  int32_t value5 = uniform_loc(p,"secondaryMode");
  int value6 = environment==NULL?0:(int32_t)secondary_mode(environment);
  gl->glUniform1i(value5,value6);
  int32_t value3 = uniform_loc(p,"alphaMode");
  int32_t value4 = (int32_t)alpha_mode(alpha_test);
  gl->glUniform1i(value3,value4);
  gl->glUniform1i(uniform_loc(p,"u_luminance_alpha"),luminance_alpha?1:0);
  double lmode=!strcmp(lighting.kind,"vertex")?0:!strcmp(lighting.kind,"q2-model-shadow")?3:(lighting.pass&&!strcmp(lighting.pass,"lightmap"))?1:(lighting.pass&&!strcmp(lighting.pass,"material-lightmap"))?4:(lighting.pass&&!strcmp(lighting.pass,"model"))?5:2;
  gl->glUniform1i(uniform_loc(p,"u_lighting_mode"),(int32_t)lmode);
  bool is_model_shadow=!strcmp(lighting.kind,"q2-model-shadow");
  size_t light_count=is_model_shadow?lighting.model_shadow_light_count:lighting.light_count;
  const qa_q2_shadow_atlas *atlas=is_model_shadow?&lighting.atlas_value:lighting.atlas;
  if(!strcmp(lighting.kind,"vertex")){ gl->glUniform1i(uniform_loc(p,"u_light_count"),0); return; }
  if(light_count>8) qa_range_error("Q2 fragment lighting accepts at most eight selected lights per draw");
  gl->glUniform1i(uniform_loc(p,"u_light_count"),(int32_t)light_count);
  if(atlas!=NULL&&(light_count>0||is_model_shadow)&&!(!is_model_shadow&&lighting.atlas==NULL)){
    finite4u(atlas->texel_size,atlas->near_plane,0,0);
    if(!isfinite(atlas->texel_size)||atlas->texel_size<=0||!isfinite(atlas->near_plane)||atlas->near_plane<=0) qa_range_error("Q2 shadow atlas texel size and near plane must be positive");
    gl->glUniform1f(uniform_loc(p,"u_shadow_texel"),(float)atlas->texel_size);
    gl->glUniform1f(uniform_loc(p,"u_shadow_near"),(float)atlas->near_plane);
  }
  if(is_model_shadow||(lighting.pass&&!strcmp(lighting.pass,"model"))){
    double scale=lighting.has_shade_scale?lighting.shade_scale:0; finite4u(scale,0,0,0);
    gl->glUniform1f(uniform_loc(p,"u_shade_scale"),(float)scale);
  }
  for(size_t i=0;i<light_count;i++){
    char name[64];
    qa_vec3 origin; double radius; qa_q2_shadow_projection shadow;
    const qa_q2_fragment_light *fl=NULL; const qa_q2_model_shadow_light *ml=NULL;
    if(is_model_shadow){ ml=&lighting.model_shadow_lights[i]; origin=ml->origin;radius=ml->radius;shadow=ml->shadow; }
    else { fl=&lighting.lights[i]; origin=fl->origin;radius=fl->radius;shadow=fl->shadow; }
    finite4u(origin.x,origin.y,origin.z,radius);
    if(radius<=0) qa_range_error("Q2 fragment light radius must be positive");
    (void)snprintf(name,sizeof name,"u_light_pos[%zu]",i); gl->glUniform3f(uniform_loc(p,name),(float)origin.x,(float)origin.y,(float)origin.z);
    (void)snprintf(name,sizeof name,"u_light_radius[%zu]",i); gl->glUniform1f(uniform_loc(p,name),(float)radius);
    if(fl){ finite4u(fl->color.x,fl->color.y,fl->color.z,fl->scale);
      if(fl->cone)finite4u(fl->cone->direction.x,fl->cone->direction.y,fl->cone->direction.z,fl->cone->cos_half_angle);
      (void)snprintf(name,sizeof name,"u_light_color[%zu]",i); gl->glUniform3f(uniform_loc(p,name),(float)fl->color.x,(float)fl->color.y,(float)fl->color.z);
      (void)snprintf(name,sizeof name,"u_light_scale[%zu]",i); gl->glUniform1f(uniform_loc(p,name),(float)fl->scale);
      (void)snprintf(name,sizeof name,"u_light_cone_cos[%zu]",i); gl->glUniform1f(uniform_loc(p,name),(float)(fl->cone?fl->cone->cos_half_angle:0));
      (void)snprintf(name,sizeof name,"u_light_cone_dir[%zu]",i); gl->glUniform3f(uniform_loc(p,name),(float)(fl->cone?fl->cone->direction.x:0),(float)(fl->cone?fl->cone->direction.y:0),(float)(fl->cone?fl->cone->direction.z:0));
      qa_vec3 frac=fl->fraction; finite4u(frac.x,frac.y,frac.z,0);
      (void)snprintf(name,sizeof name,"u_light_frac[%zu]",i);
      if(lighting.pass&&!strcmp(lighting.pass,"model")) gl->glUniform3f(uniform_loc(p,name),(float)frac.x,(float)frac.y,(float)frac.z);
      else gl->glUniform3f(uniform_loc(p,name),(float)frac.x,(float)frac.y,(float)frac.z);
    } else if(ml){ finite4u(ml->fraction.x,ml->fraction.y,ml->fraction.z,0);
      (void)snprintf(name,sizeof name,"u_light_frac[%zu]",i); gl->glUniform3f(uniform_loc(p,name),(float)ml->fraction.x,(float)ml->fraction.y,(float)ml->fraction.z);
    }
    if(strcmp(shadow.kind,"none")&&atlas==NULL) qa_range_error("Q2 shadow receiver is missing its atlas");
    (void)snprintf(name,sizeof name,"u_light_shadow[%zu]",i); int32_t value = uniform_loc(p,name); float value2 = (float)(!strcmp(shadow.kind,"none")?0:!strcmp(shadow.kind,"cone")?1:2); gl->glUniform1f(value,value2);
    if(strcmp(shadow.kind,"none")){
      qa_vec4 rect=shadow.atlas_rect; finite4u(rect.x,rect.y,rect.z,rect.w);
      (void)snprintf(name,sizeof name,"u_light_atlas[%zu]",i); gl->glUniform4f(uniform_loc(p,name),(float)rect.x,(float)rect.y,(float)rect.z,(float)rect.w);
      if(!strcmp(shadow.kind,"cone")){ finite_arr(shadow.matrix.values,16);
        float m[16]; for(int k=0;k<16;k++)m[k]=(float)shadow.matrix.values[k];
        (void)snprintf(name,sizeof name,"u_light_matrix[%zu]",i); gl->glUniformMatrix4fv(uniform_loc(p,name),1,0,m); }
    }
  }
}
void qa_gl_stage_program_use_depth(qa_gl_stage_program *program){ simpl *p=(simpl*)program->impl; p->library.glUseProgram(p->depth_program); }
void qa_gl_stage_program_restore(qa_gl_stage_program *program,double gl_program){ simpl *p=(simpl*)program->impl; p->library.glUseProgram((uint32_t)gl_program); }
void qa_gl_stage_program_close(qa_gl_stage_program *program){ if(!program->impl)return; simpl *p=(simpl*)program->impl; if(p->closed)return;
  p->library.glUseProgram(0); p->library.glDeleteProgram(p->program); p->library.glDeleteProgram(p->depth_program);
  qa_gl_programs_close(&p->library); p->closed=true; free(p); program->impl=NULL; }
