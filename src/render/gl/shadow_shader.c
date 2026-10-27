/* Port of src/render/gl/shadow-shader.ts. GPL-2.0-or-later. */
#include "qa/render/gl/shadow_shader.h"
#include "qa/foundation/support.h"
#include <stdio.h>
#include <string.h>
static const char *const LINES_A[]={
"      float lit = 1.0;",
"      vec2 rect_lo = u_light_atlas[i].xy;",
"      vec2 rect_size = u_light_atlas[i].zw;",
"      if (u_light_shadow[i] < 1.5) {",
"        vec4 lpos = u_light_matrix[i] * vec4(worldPosition, 1.0);",
"        if (lpos.w > 0.0) {",
"          vec3 lproj = lpos.xyz / lpos.w;",
"          if (lproj.x >= 0.0 && lproj.x <= 1.0 && lproj.y >= 0.0 && lproj.y <= 1.0 && lproj.z <= 1.0) {",
"            vec2 base = lproj.xy * rect_size + rect_lo;",
"            vec2 tap_lo = rect_lo + u_shadow_texel;",
"            vec2 tap_hi = rect_lo + rect_size - u_shadow_texel;",
"            lit = 0.0;",
"            for (int sy = 0; sy < 2; sy++) {",
"              for (int sx = 0; sx < 2; sx++) {",
"                vec2 off = (vec2(float(sx), float(sy)) - 0.5) * u_shadow_texel;",
"                float d = texture2D(u_shadow_map, clamp(base + off, tap_lo, tap_hi)).r;",
};
static const char *const LINES_B[]={
"              }",
"            }",
"            lit *= 0.25;",
"          }",
"        }",
"      } else {",
"        vec3 lvec = worldPosition - u_light_pos[i];",
"        vec3 lmag = abs(lvec);",
"        vec3 face_f;",
"        vec3 face_r;",
"        vec3 face_u;",
"        float face;",
"        if (lmag.x >= lmag.y && lmag.x >= lmag.z) {",
"          if (lvec.x >= 0.0) { face_f = vec3(1.0, 0.0, 0.0); face_r = vec3(0.0, -1.0, 0.0); face_u = vec3(0.0, 0.0, 1.0); face = 0.0; }",
"          else { face_f = vec3(-1.0, 0.0, 0.0); face_r = vec3(0.0, 1.0, 0.0); face_u = vec3(0.0, 0.0, 1.0); face = 1.0; }",
"        } else if (lmag.y >= lmag.z) {",
"          if (lvec.y >= 0.0) { face_f = vec3(0.0, 1.0, 0.0); face_r = vec3(1.0, 0.0, 0.0); face_u = vec3(0.0, 0.0, 1.0); face = 2.0; }",
"          else { face_f = vec3(0.0, -1.0, 0.0); face_r = vec3(-1.0, 0.0, 0.0); face_u = vec3(0.0, 0.0, 1.0); face = 3.0; }",
"        } else {",
"          if (lvec.z >= 0.0) { face_f = vec3(0.0, 0.0, 1.0); face_r = vec3(0.0, 1.0, 0.0); face_u = vec3(1.0, 0.0, 0.0); face = 4.0; }",
"          else { face_f = vec3(0.0, 0.0, -1.0); face_r = vec3(0.0, -1.0, 0.0); face_u = vec3(1.0, 0.0, 0.0); face = 5.0; }",
"        }",
"        float axial = dot(lvec, face_f);",
"        if (axial > SHADOW_NEAR) {",
"          float zfar = max(u_light_radius[i], SHADOW_NEAR * 2.0);",
"          float pa = (zfar + SHADOW_NEAR) / (SHADOW_NEAR - zfar);",
"          float pb = (2.0 * zfar * SHADOW_NEAR) / (SHADOW_NEAR - zfar);",
"          vec2 cell_size = rect_size / vec2(SHADOW_CUBE_COLS, SHADOW_CUBE_ROWS);",
"          vec2 cell_lo = rect_lo + vec2(mod(face, SHADOW_CUBE_COLS), floor(face / SHADOW_CUBE_COLS)) * cell_size;",
"          vec2 face_uv = vec2(dot(lvec, face_r), dot(lvec, face_u)) / axial * 0.5 + 0.5;",
"          vec2 base = cell_lo + face_uv * cell_size;",
"          vec2 tap_lo = cell_lo + u_shadow_texel;",
"          vec2 tap_hi = cell_lo + cell_size - u_shadow_texel;",
"          float face_texels = cell_size.x / u_shadow_texel;",
};
static const char *const LINES_C[]={
"          lit = 0.0;",
"          for (int sy = 0; sy < 2; sy++) {",
"            for (int sx = 0; sx < 2; sx++) {",
"              vec2 off = (vec2(float(sx), float(sy)) - 0.5) * u_shadow_texel;",
"              float d = texture2D(u_shadow_map, clamp(base + off, tap_lo, tap_hi)).r;",
"              float stored = pb / ((2.0 * d - 1.0) + pa);",
"              lit += (axial - bias) > stored ? 0.0 : 1.0;",
"            }",
"          }",
"          lit *= 0.25;",
"        }",
"      }",
};
size_t qa_shadow_factor_lines(const char *receiver,const char **out,size_t capacity,char dyn[3][256]){
  int model=!strcmp(receiver,"model");
  (void)snprintf(dyn[0],256,"                lit += (lproj.z - %s) > d ? 0.0 : 1.0;",model?"0.0025":"0.0005");
  (void)snprintf(dyn[1],256,"          float bias = %s + axial * (2.0 / face_texels) * %s;",model?"5.0":"1.0",model?"6.0":"2.0");
  dyn[2][0]=0;
  size_t total=(sizeof LINES_A/sizeof *LINES_A)+1+(sizeof LINES_B/sizeof *LINES_B)+1+(sizeof LINES_C/sizeof *LINES_C);
  if(capacity<total) qa_range_error("shadow factor line storage is too small");
  size_t n=0;
  for(size_t i=0;i<sizeof LINES_A/sizeof *LINES_A;i++)out[n++]=LINES_A[i];
  out[n++]=dyn[0];
  for(size_t i=0;i<sizeof LINES_B/sizeof *LINES_B;i++)out[n++]=LINES_B[i];
  out[n++]=dyn[1];
  for(size_t i=0;i<sizeof LINES_C/sizeof *LINES_C;i++)out[n++]=LINES_C[i];
  return n;
}
