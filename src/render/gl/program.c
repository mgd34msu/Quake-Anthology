#include "internal.h"
#include "../normal_matrix.h"

#include <limits.h>
#include <stdio.h>

enum {
    ATTR_POSITION,
    ATTR_NORMAL,
    ATTR_TEXCOORD,
    ATTR_LIGHTMAP,
    ATTR_COLOR
};

static const char stage_vertex[] =
    "#version 120\n"
    "attribute vec3 a_position;\n"
    "attribute vec3 a_normal;\n"
    "attribute vec2 a_texcoord;\n"
    "attribute vec2 a_lightmap;\n"
    "attribute vec4 a_color;\n"
    "uniform mat4 u_mvp;\n"
    "uniform mat4 u_model;\n"
    "uniform mat3 u_normal_matrix;\n"
    "uniform int u_clip_enabled;\n"
    "uniform vec4 u_clip_plane;\n"
    "varying vec4 vertexColor;\n"
    "varying vec2 coordinates0;\n"
    "varying vec2 coordinates1;\n"
    "varying vec3 worldPosition;\n"
    "varying vec3 worldNormal;\n"
    "varying float clipDistance;\n"
    "void main() {\n"
    "  vec4 world = u_model * vec4(a_position, 1.0);\n"
    "  gl_Position = u_mvp * vec4(a_position, 1.0);\n"
    "  vertexColor = clamp(a_color, 0.0, 1.0);\n"
    "  coordinates0 = a_texcoord;\n"
    "  coordinates1 = a_lightmap;\n"
    "  worldPosition = world.xyz;\n"
    "  worldNormal = u_normal_matrix * a_normal;\n"
    "  clipDistance = u_clip_enabled == 0 ? 1.0 : dot(world, u_clip_plane);\n"
    "}\n";

/* Q2 rerelease receiver equations retain the donor's bottom-left atlas UVs,
 * cone projection bias, 3x2 point-light faces, and 2x2 PCF. */
static const char *const stage_fragment[] = {
    "#version 120\n"
    "uniform sampler2D primaryTexture;\n"
    "uniform int primaryEnabled;\n"
    "uniform sampler2D secondaryTexture;\n"
    "uniform int secondaryMode;\n"
    "uniform int secondaryAlpha;\n"
    "uniform int alphaMode;\n"
    "uniform int u_preblend_gamma;\n"
    "uniform sampler2D u_preblend_table;\n"
    "uniform int u_fog_mode;\n"
    "uniform vec3 u_fog_color;\n"
    "uniform float u_fog_amount;\n"
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
    "uniform int u_light_spot[8];\n"
    "uniform vec3 u_light_cone_dir[8];\n"
    "uniform float u_light_cone_cos[8];\n"
    "uniform mat4 u_light_matrix[8];\n"
    "uniform vec4 u_light_atlas[8];\n"
    "uniform float u_light_shadow[8];\n"
    "uniform vec3 u_light_frac[8];\n"
    "uniform float u_shade_scale;\n"
    "uniform int u_model_shade_enabled;\n"
    "varying vec4 vertexColor;\n"
    "varying vec2 coordinates0;\n"
    "varying vec2 coordinates1;\n"
    "varying vec3 worldPosition;\n"
    "varying vec3 worldNormal;\n"
    "varying float clipDistance;\n",
    "float shadowFactor(int i, bool model) {\n"
    "  if (u_light_shadow[i] == 0.0) return 1.0;\n"
    "  vec2 rect_lo = u_light_atlas[i].xy;\n"
    "  vec2 rect_size = u_light_atlas[i].zw;\n"
    "  vec2 base; vec2 tap_lo; vec2 tap_hi; float compare_depth;\n"
    "  bool point = u_light_shadow[i] >= 1.5;\n"
    "  float axial = 0.0; float pa = 0.0; float pb = 0.0; float bias = 0.0;\n"
    "  if (!point) {\n"
    "    vec4 lpos = u_light_matrix[i] * vec4(worldPosition, 1.0);\n"
    "    if (lpos.w <= 0.0) return 1.0;\n"
    "    vec3 p = lpos.xyz / lpos.w;\n"
    "    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) return 1.0;\n"
    "    base = rect_lo + p.xy * rect_size;\n"
    "    tap_lo = rect_lo + vec2(u_shadow_texel);\n"
    "    tap_hi = rect_lo + rect_size - vec2(u_shadow_texel);\n"
    "    compare_depth = p.z; bias = model ? 0.0025 : 0.0005;\n"
    "  } else {\n"
    "    vec3 lvec = worldPosition - u_light_pos[i];\n"
    "    vec3 mag = abs(lvec); vec3 face_f; vec3 face_r; vec3 face_u; float face;\n"
    "    if (mag.x >= mag.y && mag.x >= mag.z) {\n"
    "      if (lvec.x >= 0.0) { face_f=vec3(1,0,0); face_r=vec3(0,-1,0); face_u=vec3(0,0,1); face=0.0; }\n"
    "      else { face_f=vec3(-1,0,0); face_r=vec3(0,1,0); face_u=vec3(0,0,1); face=1.0; }\n"
    "    } else if (mag.y >= mag.z) {\n"
    "      if (lvec.y >= 0.0) { face_f=vec3(0,1,0); face_r=vec3(1,0,0); face_u=vec3(0,0,1); face=2.0; }\n"
    "      else { face_f=vec3(0,-1,0); face_r=vec3(-1,0,0); face_u=vec3(0,0,1); face=3.0; }\n"
    "    } else {\n"
    "      if (lvec.z >= 0.0) { face_f=vec3(0,0,1); face_r=vec3(0,1,0); face_u=vec3(1,0,0); face=4.0; }\n"
    "      else { face_f=vec3(0,0,-1); face_r=vec3(0,-1,0); face_u=vec3(1,0,0); face=5.0; }\n"
    "    }\n"
    "    axial = dot(lvec, face_f); if (axial <= u_shadow_near) return 1.0;\n"
    "    float zfar=max(u_light_radius[i],u_shadow_near*2.0);\n"
    "    pa=(zfar+u_shadow_near)/(u_shadow_near-zfar);\n"
    "    pb=(2.0*zfar*u_shadow_near)/(u_shadow_near-zfar);\n"
    "    vec2 cell=rect_size/vec2(3.0,2.0);\n"
    "    vec2 cell_lo=rect_lo+vec2(mod(face,3.0),floor(face/3.0))*cell;\n"
    "    vec2 uv=vec2(dot(lvec,face_r),dot(lvec,face_u))/axial*0.5+0.5;\n"
    "    base=cell_lo+uv*cell; tap_lo=cell_lo+vec2(u_shadow_texel);\n"
    "    tap_hi=cell_lo+cell-vec2(u_shadow_texel);\n"
    "    float face_texels=cell.x/u_shadow_texel;\n"
    "    bias=(model?5.0:1.0)+axial*(2.0/face_texels)*(model?6.0:2.0);\n"
    "  }\n"
    "  float lit=0.0;\n"
    "  for (int sy=0; sy<2; ++sy) for (int sx=0; sx<2; ++sx) {\n"
    "    vec2 off=(vec2(float(sx),float(sy))-0.5)*u_shadow_texel;\n"
    "    float d=texture2D(u_shadow_map,clamp(base+off,tap_lo,tap_hi)).r;\n"
    "    float stored=point ? pb/((2.0*d-1.0)+pa) : d;\n"
    "    float receiver=point ? axial : compare_depth;\n"
    "    lit += receiver-bias > stored ? 0.0 : 1.0;\n"
    "  }\n"
    "  return lit*0.25;\n"
    "}\n"
    , "vec3 dynamicLights() {\n"
    "  vec3 shade=vec3(0.0);\n"
    "  for (int i=0; i<8; ++i) {\n"
    "    if (i>=u_light_count) break;\n"
    "    if (u_light_scale[i]==0.0 || all(equal(u_light_color[i],vec3(0.0)))) continue;\n"
    "    vec3 light_position=u_light_pos[i];\n"
    "    if (u_light_spot[i]==0) light_position += worldNormal*16.0;\n"
    "    vec3 toward=light_position-worldPosition; float distance=length(toward);\n"
    "    float falloff=max(u_light_radius[i]-distance,0.0)/(u_light_radius[i]+64.0);\n"
    "    vec3 direction=toward/max(distance,1.0);\n"
    "    float lambert=u_light_color[i].r<0.0?1.0:max(dot(worldNormal,direction),0.0);\n"
    "    float scale=u_light_scale[i]*falloff*lambert;\n"
    "    if (u_light_spot[i]!=0) {\n"
    "      float cone=u_light_cone_cos[i]; float magnitude=-dot(direction,u_light_cone_dir[i]);\n"
    "      scale*=cone>=1.0?0.0:max(1.0-(1.0-magnitude)/(1.0-cone),0.0);\n"
    "    }\n"
    "    scale*=shadowFactor(i,false); shade+=u_light_color[i]*scale;\n"
    "  } return shade;\n"
    "}\n"
    "vec4 modelShadow(vec4 texel) {\n"
    "  vec3 keep=vec3(1.0);\n"
    "  for (int i=0;i<8;++i) { if(i>=u_light_count) break;\n"
    "    if(!all(equal(u_light_frac[i],vec3(0.0)))) keep-=u_light_frac[i]*(1.0-shadowFactor(i,true)); }\n"
    "  vec3 shade=vertexColor.rgb*u_shade_scale*max(keep,vec3(0.0));\n"
    "  return vec4(texel.rgb*min(shade,vec3(1.0)),texel.a*vertexColor.a);\n"
    "}\n"
    "float preblendCorrect(float v) { return texture2D(u_preblend_table,vec2((floor(clamp(v,0.0,1.0)*255.0+0.5)+0.5)/256.0,0.5)).r; }\n"
    "void main() {\n"
    "  if (clipDistance < 0.0) discard;\n"
    "  vec4 texel=primaryEnabled!=0?texture2D(primaryTexture,coordinates0):vec4(1.0);\n"
    "  if(u_luminance_alpha!=0) texel.rgb*=(texel.r+texel.g+texel.b)/3.0*vertexColor.a;\n"
    "  vec4 color=clamp(texel*vertexColor,0.0,1.0);\n"
    "  if(u_lighting_mode==1) color=vec4(texel.rgb+dynamicLights(),1.0);\n"
    "  else if(u_lighting_mode==2) color.rgb+=dynamicLights();\n"
    "  else if(u_lighting_mode==3) color=modelShadow(texel);\n"
    "  else if(u_lighting_mode==4) color=vec4((texel.rgb+dynamicLights())*vertexColor.rgb,texel.a*vertexColor.a);\n"
    "  else if(u_lighting_mode==5) {\n"
    "    color=u_model_shade_enabled!=0?modelShadow(texel):texel*vertexColor;\n"
    "    color.rgb+=texel.rgb*dynamicLights();\n"
    "  }\n"
    "  if(secondaryMode!=0) { vec4 second=texture2D(secondaryTexture,coordinates1);\n"
    "    if(secondaryMode==1) color*=second;\n"
    "    else if(secondaryMode==2) color=vec4(color.rgb+second.rgb,color.a*second.a);\n"
    "    else color=vec4(second.rgb,secondaryAlpha!=0?second.a:color.a); color=clamp(color,0.0,1.0); }\n"
    "  float fogAmount=u_fog_mode==2?u_fog_amount:0.0;\n"
    "  if(u_fog_mode!=0&&u_fog_mode!=2) { float d=u_fog_amount/(64.0*gl_FragCoord.w); fogAmount=1.0-exp(-(d*d)); }\n"
    "  if(u_fog_mode!=0) color.rgb=clamp(color.rgb,0.0,1.0);\n"
    "  if(u_fog_mode==1||u_fog_mode==2) color.rgb=mix(color.rgb,u_fog_color,fogAmount);\n"
    "  if(u_fog_mode==3||u_fog_mode==5) color.rgb*=1.0-fogAmount;\n"
    "  if(u_fog_mode==4||u_fog_mode==5) color.a*=1.0-fogAmount;\n"
    "  if(u_fog_mode==6) color=vec4(u_fog_color,color.a*fogAmount);\n"
    "  if(alphaMode==1&&color.a<=0.0) discard;\n"
    "  if(alphaMode==2&&color.a>=0.5) discard;\n"
    "  if(alphaMode==3&&color.a<0.5) discard;\n"
    "  if(alphaMode==4&&color.a<=0.666) discard;\n"
    "  if(u_preblend_gamma!=0) color.rgb=vec3(preblendCorrect(color.r),preblendCorrect(color.g),preblendCorrect(color.b));\n"
    "  gl_FragColor=color;\n"
    "}\n"};

static const char quad_vertex[] =
    "#version 120\n"
    "varying vec2 tc;\n"
    "void main(){tc=gl_MultiTexCoord0.xy;gl_Position=gl_Vertex;}\n";
static const char opacity_fragment[] =
    "#version 120\n"
    "uniform sampler2D backdrop;uniform sampler2D result;uniform float opacity;\n"
    "varying vec2 tc;void main(){vec4 b=texture2D(backdrop,tc);"
    "gl_FragColor=b+opacity*(texture2D(result,tc)-b);}\n";
static const char gamma_fragment[] =
    "#version 120\n"
    "uniform sampler2D rawColor;uniform sampler2D gammaTable;uniform int applyGamma;varying vec2 tc;\n"
    "float corrected(float v){return texture2D(gammaTable,vec2((floor(clamp(v,0.0,1.0)*255.0+0.5)+0.5)/256.0,0.5)).r;}\n"
    "void main(){vec4 c=texture2D(rawColor,tc);gl_FragColor=applyGamma==0?c:vec4(corrected(c.r),corrected(c.g),corrected(c.b),c.a);}\n";

static const char fog_vertex[] =
    "#version 120\n"
    "uniform int u_height;uniform vec3 u_forward;uniform vec3 u_right;uniform vec3 u_up;uniform vec4 u_tan;\n"
    "varying vec2 tc;varying vec3 ray;void main(){tc=gl_MultiTexCoord0.xy;"
    "ray=u_forward+u_right*(gl_Vertex.x*u_tan.x)+u_up*(gl_Vertex.y*u_tan.y);gl_Position=gl_Vertex;}\n";
static const char fog_global_fragment[] =
    "#version 120\n"
    "varying vec2 tc;uniform sampler2D u_depth;uniform float u_far_depth;uniform vec4 u_proj;uniform vec4 u_fog_color;\n"
    "void main(){float d=texture2D(u_depth,tc).r;if(d>=u_far_depth)discard;float z=2.0*d-1.0;"
    "float w=u_proj.y/(u_proj.x-z);float fd=d*w;float dd=u_fog_color.a*fd;"
    "float fog=1.0-exp(-(dd*dd));gl_FragColor=vec4(u_fog_color.rgb,fog);}\n";
static const char fog_height_fragment[] =
    "#version 120\n"
    "varying vec2 tc;varying vec3 ray;uniform sampler2D u_depth;uniform float u_far_depth;uniform vec4 u_proj;"
    "uniform vec3 u_vieworg;uniform vec4 u_hf_start;uniform vec4 u_hf_end;uniform float u_hf_density;uniform float u_hf_falloff;\n"
    "void main(){float d=texture2D(u_depth,tc).r;if(d>=u_far_depth)discard;float z=2.0*d-1.0;"
    "float w=u_proj.y/(u_proj.x-z);float fd=d*w;vec3 wp=u_vieworg+ray*w;float dz=normalize(wp-u_vieworg).z;"
    "float s=sign(dz);dz+=0.00001*(1.0-s*s);float eye=u_vieworg.z-u_hf_start.w;float pos=wp.z-u_hf_start.w;"
    "float den=(exp(-u_hf_falloff*eye)-exp(-u_hf_falloff*pos))/(u_hf_falloff*dz);"
    "float ext=1.0-clamp(exp(-den),0.0,1.0);float span=u_hf_end.w-u_hf_start.w;"
    "float fraction=span==0.0?0.0:clamp((pos-u_hf_start.w)/span,0.0,1.0);"
    "vec3 fc=mix(u_hf_start.rgb,u_hf_end.rgb,fraction)*ext;float fog=(1.0-exp(-(u_hf_density*fd)))*ext;"
    "gl_FragColor=vec4(fc,fog);}\n";
static const char fog_sky_fragment[] =
    "#version 120\n"
    "varying vec2 tc;uniform sampler2D u_depth;uniform float u_far_depth;uniform vec4 u_fog_color;\n"
    "void main(){float d=texture2D(u_depth,tc).r;if(d<u_far_depth)discard;gl_FragColor=u_fog_color;}\n";

static bool shader_status(qa_gl_renderer *renderer, GLuint shader,
                          qa_error *error)
{
    GLint status = 0;
    renderer->gl.GetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_TRUE) return true;
    GLchar log[4096] = {0};
    GLsizei length = 0;
    renderer->gl.GetShaderInfoLog(shader, (GLsizei)sizeof(log), &length, log);
    qa_error_set(error, QA_ERROR_UNSUPPORTED, 0,
                 "OpenGL shader compilation failed: %s", log);
    return false;
}

static bool program_status(qa_gl_renderer *renderer, GLuint program,
                           qa_error *error)
{
    GLint status = 0;
    renderer->gl.GetProgramiv(program, GL_LINK_STATUS, &status);
    if (status == GL_TRUE) return true;
    GLchar log[4096] = {0};
    GLsizei length = 0;
    renderer->gl.GetProgramInfoLog(program, (GLsizei)sizeof(log), &length, log);
    qa_error_set(error, QA_ERROR_UNSUPPORTED, 0,
                 "OpenGL program link failed: %s", log);
    return false;
}

static bool compile_program(qa_gl_renderer *renderer, const char *vertex,
                            const char *const *fragments, GLsizei fragment_count, bool attributes, GLuint *out,
                            qa_error *error)
{
    GLuint shaders[2] = {0, 0}, program = 0;
    const GLenum kinds[2] = {GL_VERTEX_SHADER, GL_FRAGMENT_SHADER};
    bool ok = false;
    for (size_t i = 0; i < 2; ++i) {
        const char *const *sources=i?fragments:&vertex;
        GLsizei count=i?fragment_count:1;
        if (count<=0) {
            qa_error_set(error, QA_ERROR_MEMORY, 0,
                         "Embedded OpenGL shader requires its source segments");
            goto done;
        }
        shaders[i] = renderer->gl.CreateShader(kinds[i]);
        if (shaders[i] == 0) {
            qa_error_set(error, QA_ERROR_MEMORY, 0,
                         "OpenGL could not allocate a shader");
            goto done;
        }
        renderer->gl.ShaderSource(shaders[i], count, sources, NULL);
        renderer->gl.CompileShader(shaders[i]);
        if (!shader_status(renderer, shaders[i], error)) goto done;
    }
    program = renderer->gl.CreateProgram();
    if (program == 0) {
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "OpenGL could not allocate a program");
        goto done;
    }
    renderer->gl.AttachShader(program, shaders[0]);
    renderer->gl.AttachShader(program, shaders[1]);
    if (attributes) {
        renderer->gl.BindAttribLocation(program, ATTR_POSITION, "a_position");
        renderer->gl.BindAttribLocation(program, ATTR_NORMAL, "a_normal");
        renderer->gl.BindAttribLocation(program, ATTR_TEXCOORD, "a_texcoord");
        renderer->gl.BindAttribLocation(program, ATTR_LIGHTMAP, "a_lightmap");
        renderer->gl.BindAttribLocation(program, ATTR_COLOR, "a_color");
    }
    renderer->gl.LinkProgram(program);
    if (!program_status(renderer, program, error)) goto done;
    *out = program;
    program = 0;
    ok = true;
done:
    if (program != 0) renderer->gl.DeleteProgram(program);
    for (size_t i = 0; i < 2; ++i)
        if (shaders[i] != 0) renderer->gl.DeleteShader(shaders[i]);
    return ok;
}

static bool uniform(qa_gl_renderer *renderer, GLuint program, const char *name,
                    GLint *out, qa_error *error)
{
    *out = renderer->gl.GetUniformLocation(program, name);
    if (*out >= 0) return true;
    qa_error_set(error, QA_ERROR_UNSUPPORTED, 0,
                 "OpenGL program omitted required uniform %s", name);
    return false;
}

#define STAGE_UNIFORM(field, name)                                               \
    do {                                                                         \
        if (!uniform(renderer, p->stage, name, &p->stage_uniform.field, error))  \
            return false;                                                        \
    } while (0)

static bool stage_uniforms(qa_gl_renderer *renderer, qa_error *error)
{
    gl_programs *p = &renderer->programs;
    gl_state_invalidate(renderer);
    STAGE_UNIFORM(mvp, "u_mvp");
    STAGE_UNIFORM(model, "u_model");
    STAGE_UNIFORM(normal_matrix, "u_normal_matrix");
    STAGE_UNIFORM(clip_enabled, "u_clip_enabled");
    STAGE_UNIFORM(clip_plane, "u_clip_plane");
    STAGE_UNIFORM(primary, "primaryTexture");
    STAGE_UNIFORM(primary_enabled, "primaryEnabled");
    STAGE_UNIFORM(secondary, "secondaryTexture");
    STAGE_UNIFORM(secondary_mode, "secondaryMode");
    STAGE_UNIFORM(secondary_alpha, "secondaryAlpha");
    STAGE_UNIFORM(alpha_mode, "alphaMode");
    STAGE_UNIFORM(preblend_gamma, "u_preblend_gamma");
    STAGE_UNIFORM(preblend_table, "u_preblend_table");
    STAGE_UNIFORM(fog_mode, "u_fog_mode");
    STAGE_UNIFORM(fog_color, "u_fog_color");
    STAGE_UNIFORM(fog_amount, "u_fog_amount");
    STAGE_UNIFORM(lighting_mode, "u_lighting_mode");
    STAGE_UNIFORM(luminance_alpha, "u_luminance_alpha");
    STAGE_UNIFORM(light_count, "u_light_count");
    STAGE_UNIFORM(shadow_map, "u_shadow_map");
    STAGE_UNIFORM(shadow_texel, "u_shadow_texel");
    STAGE_UNIFORM(shadow_near, "u_shadow_near");
    STAGE_UNIFORM(shade_scale, "u_shade_scale");
    STAGE_UNIFORM(model_shade_enabled, "u_model_shade_enabled");
    for (unsigned i = 0; i < GL_MAX_LIGHTS_QA; ++i) {
        char name[64];
#define LIGHT_UNIFORM(field, base)                                               \
        do {                                                                     \
            snprintf(name, sizeof(name), base "[%u]", i);                      \
            if (!uniform(renderer, p->stage, name,                              \
                         &p->stage_uniform.field[i], error)) return false;       \
        } while (0)
        LIGHT_UNIFORM(light_position, "u_light_pos");
        LIGHT_UNIFORM(light_radius, "u_light_radius");
        LIGHT_UNIFORM(light_color, "u_light_color");
        LIGHT_UNIFORM(light_scale, "u_light_scale");
        LIGHT_UNIFORM(light_spot, "u_light_spot");
        LIGHT_UNIFORM(light_direction, "u_light_cone_dir");
        LIGHT_UNIFORM(light_cone, "u_light_cone_cos");
        LIGHT_UNIFORM(light_matrix, "u_light_matrix");
        LIGHT_UNIFORM(light_atlas, "u_light_atlas");
        LIGHT_UNIFORM(light_shadow, "u_light_shadow");
        LIGHT_UNIFORM(light_fraction, "u_light_frac");
#undef LIGHT_UNIFORM
    }
    return true;
}

#undef STAGE_UNIFORM

static bool fog_uniforms(qa_gl_renderer *renderer, unsigned pass,
                         qa_error *error)
{
    GLuint program = renderer->programs.fog[pass];
    gl_fog_uniforms *u = &renderer->programs.fog_uniform[pass];
    if (!uniform(renderer, program, "u_depth", &u->depth, error) ||
        !uniform(renderer, program, "u_far_depth", &u->far_depth, error))
        return false;
    if ((pass == 0 || pass == 2) &&
        !uniform(renderer, program, "u_fog_color", &u->fog_color, error))
        return false;
    if (pass == 2) return true;
    if (!uniform(renderer, program, "u_proj", &u->projection, error))
        return false;
    if (pass == 0) return true;
    return uniform(renderer, program, "u_vieworg", &u->view_origin, error) &&
           uniform(renderer, program, "u_forward", &u->forward, error) &&
           uniform(renderer, program, "u_right", &u->right, error) &&
           uniform(renderer, program, "u_up", &u->up, error) &&
           uniform(renderer, program, "u_tan", &u->tangent, error) &&
           uniform(renderer, program, "u_hf_start", &u->height_start, error) &&
           uniform(renderer, program, "u_hf_end", &u->height_end, error) &&
           uniform(renderer, program, "u_hf_density", &u->height_density,
                   error) &&
           uniform(renderer, program, "u_hf_falloff", &u->height_falloff,
                   error);
}

bool gl_programs_create(qa_gl_renderer *renderer, qa_error *error)
{
    gl_programs *p = &renderer->programs;
    gl_state_invalidate(renderer);
    if (!compile_program(renderer, stage_vertex, stage_fragment, 3, true,
                         &p->stage, error) ||
        !stage_uniforms(renderer, error) ||
        !compile_program(renderer, quad_vertex, (const char *[]){opacity_fragment}, 1, false,
                         &p->opacity, error) ||
        !uniform(renderer, p->opacity, "backdrop",
                 &p->opacity_uniform.backdrop, error) ||
        !uniform(renderer, p->opacity, "result",
                 &p->opacity_uniform.result, error) ||
        !uniform(renderer, p->opacity, "opacity",
                 &p->opacity_uniform.opacity, error) ||
        !compile_program(renderer, quad_vertex, (const char *[]){gamma_fragment}, 1, false,
                         &p->gamma, error) ||
        !uniform(renderer, p->gamma, "rawColor", &p->gamma_uniform.raw,
                 error) ||
        !uniform(renderer, p->gamma, "gammaTable", &p->gamma_uniform.table,
                 error) ||
        !uniform(renderer, p->gamma, "applyGamma", &p->gamma_uniform.apply,
                 error)) return false;
    const char *fog_fragments[3] = {fog_global_fragment, fog_height_fragment,
                                    fog_sky_fragment};
    for (unsigned i = 0; i < 3; ++i)
        if (!compile_program(renderer, fog_vertex, fog_fragments+i, 1, false,
                             &p->fog[i], error) ||
            !fog_uniforms(renderer, i, error)) return false;
    gl_state_program(renderer, p->stage);
    renderer->gl.Uniform1i(p->stage_uniform.primary, 0);
    renderer->gl.Uniform1i(p->stage_uniform.secondary, 1);
    renderer->gl.Uniform1i(p->stage_uniform.shadow_map, 2);
    renderer->gl.Uniform1i(p->stage_uniform.preblend_table, 2);
    gl_state_program(renderer, 0);
    return gl_check(renderer, "OpenGL program initialization", error);
}

void gl_programs_destroy(qa_gl_renderer *renderer)
{
    gl_programs *p = &renderer->programs;
    gl_state_invalidate(renderer);
    if (!renderer->detached) gl_state_program(renderer, 0);
    if (p->stage != 0) renderer->gl.DeleteProgram(p->stage);
    if (p->opacity != 0) renderer->gl.DeleteProgram(p->opacity);
    if (p->gamma != 0) renderer->gl.DeleteProgram(p->gamma);
    for (size_t i = 0; i < 3; ++i)
        if (p->fog[i] != 0) renderer->gl.DeleteProgram(p->fog[i]);
    memset(p, 0, sizeof(*p));
}

static void normal_matrix(const qa_scene_matrix *model, GLfloat result[9])
{
    double row[9];
    qa_render_normal_matrix(model, row);
    for (size_t column = 0; column < 3; ++column)
        for (size_t r = 0; r < 3; ++r)
            result[column * 3 + r] = (GLfloat)row[r * 3 + column];
}

static int fog_mode(const qa_scene_fog *fog)
{
    if (fog->kind == QA_FOG_CONSTANT) return 2;
    if (fog->kind != QA_FOG_EXP2 || fog->effect == QA_FOG_NO_EFFECT) return 0;
    switch (fog->effect) {
    case QA_FOG_COLOR: return 1;
    case QA_FOG_RGB: return 3;
    case QA_FOG_ALPHA: return 4;
    case QA_FOG_RGBA: return 5;
    case QA_FOG_OVERLAY: return 6;
    case QA_FOG_NO_EFFECT: return 0;
    }
    return 0;
}

static gl_cached_value *stage_value(qa_gl_renderer *renderer, const GLint *location)
{
    size_t index = (size_t)((const unsigned char *)location -
        (const unsigned char *)&renderer->programs.stage_uniform) / sizeof(GLint);
    return &renderer->stage_values[index];
}
static void stage_uniform_1i(qa_gl_renderer *renderer, const GLint *location, GLint v0)
{
    const GLint value[] = {v0};
    if (*location >= 0 && gl_state_changed(stage_value(renderer, location), value, sizeof(value)))
        renderer->gl.Uniform1i(*location, v0);
}
static void stage_uniform_1f(qa_gl_renderer *renderer, const GLint *location, GLfloat v0)
{
    const GLfloat value[] = {v0};
    if (*location >= 0 && gl_state_changed(stage_value(renderer, location), value, sizeof(value)))
        renderer->gl.Uniform1f(*location, v0);
}
static void stage_uniform_3f(qa_gl_renderer *renderer, const GLint *location, GLfloat v0, GLfloat v1, GLfloat v2)
{
    const GLfloat value[] = {v0, v1, v2};
    if (*location >= 0 && gl_state_changed(stage_value(renderer, location), value, sizeof(value)))
        renderer->gl.Uniform3f(*location, v0, v1, v2);
}
static void stage_uniform_4f(qa_gl_renderer *renderer, const GLint *location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)
{
    const GLfloat value[] = {v0, v1, v2, v3};
    if (*location >= 0 && gl_state_changed(stage_value(renderer, location), value, sizeof(value)))
        renderer->gl.Uniform4f(*location, v0, v1, v2, v3);
}
static void stage_uniform_matrix3(qa_gl_renderer *renderer, const GLint *location, const GLfloat *value)
{
    if (*location >= 0 && gl_state_changed(stage_value(renderer, location), value, 9 * sizeof(*value)))
        renderer->gl.UniformMatrix3fv(*location, 1, GL_FALSE, value);
}
static void stage_uniform_matrix4(qa_gl_renderer *renderer, const GLint *location, const GLfloat *value)
{
    if (*location >= 0 && gl_state_changed(stage_value(renderer, location), value, 16 * sizeof(*value)))
        renderer->gl.UniformMatrix4fv(*location, 1, GL_FALSE, value);
}

bool gl_program_stage(qa_gl_renderer *renderer, const qa_scene_draw *draw,
                      qa_error *error)
{
    gl_stage_uniforms *u = &renderer->programs.stage_uniform;
    bool preblend=renderer->preblend_gamma && renderer->gamma!=1 &&
        !qa_display_gamma_applied_is(renderer->options.display);
    if (preblend && (draw->lighting!=QA_LIGHT_VERTEX || draw->shadow_atlas || !renderer->output.table)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Generic overlay gamma requires its retained table and unlit primitive");
        return false;
    }
    gl_state_program(renderer, renderer->programs.stage);
    stage_uniform_1i(renderer, &u->preblend_gamma,preblend?1:0);
    stage_uniform_matrix4(renderer, &u->mvp, draw->mvp.m);
    stage_uniform_matrix4(renderer, &u->model, draw->model.m);
    GLfloat normal[9];
    normal_matrix(&draw->model, normal);
    stage_uniform_matrix3(renderer, &u->normal_matrix, normal);
    stage_uniform_1i(renderer, &u->clip_enabled, renderer->view.clip_enabled ? 1 : 0);
    stage_uniform_4f(renderer, &u->clip_plane, renderer->view.clip_plane.normal.x,
                  renderer->view.clip_plane.normal.y,
                  renderer->view.clip_plane.normal.z,
                  -renderer->view.clip_plane.distance);
    stage_uniform_1i(renderer, &u->secondary_mode,
                  draw->texture_count < 2 || !draw->textures[1] ? 0 :
                  draw->environment == QA_TEXTURE_MODULATE ? 1 :
                  draw->environment == QA_TEXTURE_ADD ? 2 : 3);
    stage_uniform_1i(renderer, &u->primary_enabled,draw->texture_count>0 && draw->textures[0]!=NULL);
    stage_uniform_1i(renderer, &u->secondary_alpha,draw->texture_count>1 && draw->textures[1] &&
        qa_render_source_texture_alpha(draw->textures[1]));
    stage_uniform_1i(renderer, &u->alpha_mode, (GLint)draw->state.alpha_test);
    int mode = fog_mode(&draw->fog);
    stage_uniform_1i(renderer, &u->fog_mode, mode);
    stage_uniform_3f(renderer, &u->fog_color, draw->fog.color.x, draw->fog.color.y,
                  draw->fog.color.z);
    stage_uniform_1f(renderer, &u->fog_amount,
                  draw->fog.kind == QA_FOG_EXP2 ? draw->fog.density :
                                                  draw->fog.amount);
    int lighting = 0;
    if (draw->lighting == QA_LIGHT_Q2_MODEL_SHADOW) lighting = 3;
    else if (draw->lighting == QA_LIGHT_Q2_WORLD)
        lighting = draw->light_pass == QA_LIGHT_PASS_LIGHTMAP ? 1 :
                   draw->light_pass == QA_LIGHT_PASS_MATERIAL_LIGHTMAP ? 4 :
                   draw->light_pass == QA_LIGHT_PASS_MODEL ? 5 : 2;
    stage_uniform_1i(renderer, &u->lighting_mode, lighting);
    stage_uniform_1i(renderer, &u->luminance_alpha, draw->luminance_alpha ? 1 : 0);
    stage_uniform_1i(renderer, &u->model_shade_enabled,
                  draw->model_shade_scale ? 1 : 0);
    stage_uniform_1f(renderer, &u->shade_scale, draw->shade_scale);
    if (draw->light_count > GL_MAX_LIGHTS_QA) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "OpenGL draw exceeds eight selected lights");
        return false;
    }
    stage_uniform_1i(renderer, &u->light_count, (GLint)draw->light_count);
    if (draw->shadow_atlas != NULL) {
        const qa_scene_image_level *level = &draw->shadow_atlas->levels[0];
        stage_uniform_1f(renderer, &u->shadow_texel, 1.0f / (float)level->width);
        stage_uniform_1f(renderer, &u->shadow_near, draw->shadow_near);
    } else {
        stage_uniform_1f(renderer, &u->shadow_texel, 1);
        stage_uniform_1f(renderer, &u->shadow_near, 1);
    }
    for (size_t i = 0; i < draw->light_count; ++i) {
        const qa_scene_shadow_light *shadow = &draw->lights[i];
        const qa_scene_light *light = &shadow->light;
        stage_uniform_3f(renderer, &u->light_position[i], light->origin.x, light->origin.y,
                      light->origin.z);
        stage_uniform_1f(renderer, &u->light_radius[i], light->radius);
        stage_uniform_3f(renderer, &u->light_color[i], light->color.x, light->color.y,
                      light->color.z);
        stage_uniform_1f(renderer, &u->light_scale[i], light->scale);
        stage_uniform_1i(renderer, &u->light_spot[i], light->spot ? 1 : 0);
        stage_uniform_3f(renderer, &u->light_direction[i], light->direction.x,
                      light->direction.y, light->direction.z);
        stage_uniform_1f(renderer, &u->light_cone[i], light->cos_half_angle);
        stage_uniform_3f(renderer, &u->light_fraction[i], shadow->model_fraction.x,
                      shadow->model_fraction.y, shadow->model_fraction.z);
        stage_uniform_1f(renderer, &u->light_shadow[i], !shadow->shadow_valid ? 0 :
                      shadow->point_shadow ? 2 : 1);
        if (shadow->shadow_valid) {
            stage_uniform_4f(renderer, &u->light_atlas[i], shadow->atlas_rect.x,
                          shadow->atlas_rect.y, shadow->atlas_rect.z,
                          shadow->atlas_rect.w);
            if (!shadow->point_shadow)
                stage_uniform_matrix4(renderer, &u->light_matrix[i],
                                     shadow->shadow_matrix.m);
        }
    }
    return true;
}

bool gl_program_fog(qa_gl_renderer *renderer, unsigned pass,
                    const qa_scene_fog *fog, const qa_scene_view *view,
                    qa_error *error)
{
    if (pass > 2) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid OpenGL fog pass");
        return false;
    }
    gl_api *gl = &renderer->gl;
    gl_fog_uniforms *u = &renderer->programs.fog_uniform[pass];
    gl_state_program(renderer, renderer->programs.fog[pass]);
    gl->Uniform1i(u->depth, 0);
    gl->Uniform1f(u->far_depth, fog->far_depth);
    if (pass != 2) {
        float a = -view->projection.m[10];
        float b = -view->projection.m[14];
        gl->Uniform4f(u->projection, a, b, 0, 0);
    }
    if (pass == 0 || pass == 2) {
        float amount = pass == 0 ? fog->density / 64.0f : fog->sky_factor;
        gl->Uniform4f(u->fog_color, fog->color.x, fog->color.y, fog->color.z,
                      amount);
    } else {
        gl->Uniform3f(u->view_origin, view->origin.x, view->origin.y,
                      view->origin.z);
        gl->Uniform3f(u->forward, view->axis[0].x, view->axis[0].y,
                      view->axis[0].z);
        gl->Uniform3f(u->right, -view->axis[1].x, -view->axis[1].y,
                      -view->axis[1].z);
        gl->Uniform3f(u->up, view->axis[2].x, view->axis[2].y,
                      view->axis[2].z);
        gl->Uniform4f(u->tangent, 1.0f / view->projection.m[0],
                      1.0f / view->projection.m[5], 0, 0);
        gl->Uniform4f(u->height_start, fog->height_color.x,
                      fog->height_color.y, fog->height_color.z,
                      fog->height_start);
        gl->Uniform4f(u->height_end, fog->height_end_color.x,
                      fog->height_end_color.y, fog->height_end_color.z,
                      fog->height_end);
        gl->Uniform1f(u->height_density, fog->height_density);
        gl->Uniform1f(u->height_falloff, fog->height_falloff);
    }
    return true;
}

void gl_draw_quad(qa_gl_renderer *renderer)
{
    gl_api *gl = &renderer->gl;
    gl->Begin(GL_QUADS);
    gl->TexCoord2f(0, 0); gl->Vertex4f(-1, -1, 0, 1);
    gl->TexCoord2f(1, 0); gl->Vertex4f(1, -1, 0, 1);
    gl->TexCoord2f(1, 1); gl->Vertex4f(1, 1, 0, 1);
    gl->TexCoord2f(0, 1); gl->Vertex4f(-1, 1, 0, 1);
    gl->End();
}
