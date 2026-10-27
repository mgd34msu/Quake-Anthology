/* Port of src/render/gl/fog-shader.ts. GPL-2.0-or-later. */
#include "qa/render/gl/fog_shader.h"
#include "qa/foundation/support.h"
#include <stdlib.h>
#include <string.h>
static const char *const SKY_U[]={"u_depth","u_far_depth","u_fog_color"};
static const char *const GLOBAL_U[]={"u_depth","u_far_depth","u_proj","u_fog_color"};
static const char *const HEIGHT_U[]={"u_depth","u_far_depth","u_proj","u_vieworg","u_forward","u_right","u_up","u_tan","u_hf_start","u_hf_end","u_hf_density","u_hf_falloff"};
const char *const *qa_fog_uniforms_for(double kind,size_t *n){
  if(kind==QA_FOG_PASS_SKY){*n=3;return SKY_U;}
  if(kind==QA_FOG_PASS_GLOBAL){*n=4;return GLOBAL_U;}
  *n=12;return HEIGHT_U;
}
static char *join_lines(const char *const *lines,size_t count){
  size_t len=0; for(size_t i=0;i<count;i++)len+=strlen(lines[i])+1;
  char *out=(char*)malloc(len?len:1); if(!out)qa_range_error("fog shader allocation failed");
  size_t off=0; for(size_t i=0;i<count;i++){ size_t l=strlen(lines[i]); memcpy(out+off,lines[i],l); off+=l; if(i+1<count)out[off++]='\n'; }
  out[off]=0; return out;
}
char *qa_build_fog_vertex_shader_source(double kind){
  const char *lines[16]; size_t n=0;
  lines[n++]="#version 110"; lines[n++]="varying vec2 v_tc;";
  if(kind==QA_FOG_PASS_HEIGHT){ lines[n++]="uniform vec3 u_forward;";lines[n++]="uniform vec3 u_right;";lines[n++]="uniform vec3 u_up;";lines[n++]="uniform vec4 u_tan;";lines[n++]="varying vec3 v_ray;"; }
  lines[n++]="void main() {"; lines[n++]="  v_tc = gl_MultiTexCoord0.st;";
  if(kind==QA_FOG_PASS_HEIGHT) lines[n++]="  v_ray = u_forward + u_right * (gl_Vertex.x * u_tan.x) + u_up * (gl_Vertex.y * u_tan.y);";
  lines[n++]="  gl_Position = vec4(gl_Vertex.xy, 0.0, 1.0);"; lines[n++]="}";
  return join_lines(lines,n);
}
char *qa_build_fog_fragment_shader_source(double kind){
  const char *lines[40]; size_t n=0;
  lines[n++]="#version 110";lines[n++]="varying vec2 v_tc;";lines[n++]="uniform sampler2D u_depth;";lines[n++]="uniform float u_far_depth;";
  if(kind==QA_FOG_PASS_SKY){
    lines[n++]="uniform vec4 u_fog_color;";lines[n++]="void main() {";lines[n++]="  float d = texture2D(u_depth, v_tc).r;";
    lines[n++]="  if (d < u_far_depth) discard;";lines[n++]="  gl_FragColor = vec4(u_fog_color.rgb, u_fog_color.a);";lines[n++]="}";
    return join_lines(lines,n);
  }
  lines[n++]="uniform vec4 u_proj;";
  if(kind==QA_FOG_PASS_GLOBAL) lines[n++]="uniform vec4 u_fog_color;";
  else { lines[n++]="varying vec3 v_ray;";lines[n++]="uniform vec3 u_vieworg;";lines[n++]="uniform vec4 u_hf_start;";lines[n++]="uniform vec4 u_hf_end;";lines[n++]="uniform float u_hf_density;";lines[n++]="uniform float u_hf_falloff;"; }
  lines[n++]="void main() {";lines[n++]="  float d = texture2D(u_depth, v_tc).r;";lines[n++]="  if (d >= u_far_depth) discard;";
  lines[n++]="  float ndc_z = 2.0 * d - 1.0;";lines[n++]="  float w = u_proj.y / (u_proj.x - ndc_z);";lines[n++]="  float frag_depth = d * w;";
  if(kind==QA_FOG_PASS_GLOBAL){
    lines[n++]="  float dd = u_fog_color.a * frag_depth;";lines[n++]="  float fog = 1.0 - exp(-(dd * dd));";
    lines[n++]="  gl_FragColor = vec4(u_fog_color.rgb, fog);";lines[n++]="}"; return join_lines(lines,n);
  }
  lines[n++]="  vec3 v_world_pos = u_vieworg + v_ray * w;";
  lines[n++]="  float dir_z = normalize(v_world_pos - u_vieworg).z;";
  lines[n++]="  float s = sign(dir_z);";
  lines[n++]="  dir_z += 0.00001 * (1.0 - s * s);";
  lines[n++]="  float eye = u_vieworg.z - u_hf_start.w;";
  lines[n++]="  float pos = v_world_pos.z - u_hf_start.w;";
  lines[n++]="  float density = (exp(-u_hf_falloff * eye) - exp(-u_hf_falloff * pos)) / (u_hf_falloff * dir_z);";
  lines[n++]="  float extinction = 1.0 - clamp(exp(-density), 0.0, 1.0);";
  lines[n++]="  float fraction = clamp((pos - u_hf_start.w) / (u_hf_end.w - u_hf_start.w), 0.0, 1.0);";
  lines[n++]="  vec3 fog_color = mix(u_hf_start.rgb, u_hf_end.rgb, fraction) * extinction;";
  lines[n++]="  float fog = (1.0 - exp(-(u_hf_density * frag_depth))) * extinction;";
  lines[n++]="  gl_FragColor = vec4(fog_color, fog);";lines[n++]="}";
  return join_lines(lines,n);
}
