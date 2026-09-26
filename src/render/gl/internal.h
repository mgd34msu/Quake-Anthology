#ifndef QA_RENDER_GL_INTERNAL_H
#define QA_RENDER_GL_INTERNAL_H

#include "qa/render_gl.h"

#include <SDL_opengl.h>

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define GL_MAX_LIGHTS_QA 8
#define GL_DRAW_BUFFER_COUNT_QA 4

typedef struct gl_api {
    const GLubyte *(APIENTRY *GetString)(GLenum);
    void (APIENTRY *GetIntegerv)(GLenum, GLint *);
    void (APIENTRY *GetFloatv)(GLenum, GLfloat *);
    GLenum (APIENTRY *GetError)(void);
    GLboolean (APIENTRY *IsEnabled)(GLenum);
    void (APIENTRY *Enable)(GLenum);
    void (APIENTRY *Disable)(GLenum);
    void (APIENTRY *Viewport)(GLint, GLint, GLsizei, GLsizei);
    void (APIENTRY *Scissor)(GLint, GLint, GLsizei, GLsizei);
    void (APIENTRY *ClearColor)(GLclampf, GLclampf, GLclampf, GLclampf);
    void (APIENTRY *ClearDepth)(GLclampd);
    void (APIENTRY *ClearStencil)(GLint);
    void (APIENTRY *Clear)(GLbitfield);
    void (APIENTRY *DepthFunc)(GLenum);
    void (APIENTRY *DepthMask)(GLboolean);
    void (APIENTRY *ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
    void (APIENTRY *StencilFunc)(GLenum, GLint, GLuint);
    void (APIENTRY *StencilOp)(GLenum, GLenum, GLenum);
    void (APIENTRY *StencilMask)(GLuint);
    void (APIENTRY *DepthRange)(GLclampd, GLclampd);
    void (APIENTRY *PolygonMode)(GLenum, GLenum);
    void (APIENTRY *PolygonOffset)(GLfloat, GLfloat);
    void (APIENTRY *LineWidth)(GLfloat);
    void (APIENTRY *BlendFunc)(GLenum, GLenum);
    void (APIENTRY *CullFace)(GLenum);
    void (APIENTRY *FrontFace)(GLenum);
    void (APIENTRY *DrawBuffer)(GLenum);
    void (APIENTRY *ReadBuffer)(GLenum);
    void (APIENTRY *ActiveTexture)(GLenum);
    void (APIENTRY *GenTextures)(GLsizei, GLuint *);
    void (APIENTRY *DeleteTextures)(GLsizei, const GLuint *);
    void (APIENTRY *BindTexture)(GLenum, GLuint);
    void (APIENTRY *TexParameteri)(GLenum, GLenum, GLint);
    void (APIENTRY *TexParameterfv)(GLenum, GLenum, const GLfloat *);
    void (APIENTRY *TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint,
                                GLenum, GLenum, const void *);
    void (APIENTRY *TexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei,
                                   GLsizei, GLenum, GLenum, const void *);
    void (APIENTRY *CopyTexImage2D)(GLenum, GLint, GLenum, GLint, GLint,
                                    GLsizei, GLsizei, GLint);
    void (APIENTRY *GetTexImage)(GLenum, GLint, GLenum, GLenum, void *);
    void (APIENTRY *PixelStorei)(GLenum, GLint);
    void (APIENTRY *ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
                                void *);
    void (APIENTRY *GenBuffers)(GLsizei, GLuint *);
    void (APIENTRY *DeleteBuffers)(GLsizei, const GLuint *);
    void (APIENTRY *BindBuffer)(GLenum, GLuint);
    void (APIENTRY *BufferData)(GLenum, GLsizeiptr, const void *, GLenum);
    void (APIENTRY *BufferSubData)(GLenum, GLintptr, GLsizeiptr, const void *);
    void (APIENTRY *EnableVertexAttribArray)(GLuint);
    void (APIENTRY *DisableVertexAttribArray)(GLuint);
    void (APIENTRY *VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean,
                                         GLsizei, const void *);
    void (APIENTRY *DrawElements)(GLenum, GLsizei, GLenum, const void *);
    GLuint (APIENTRY *CreateShader)(GLenum);
    void (APIENTRY *ShaderSource)(GLuint, GLsizei, const GLchar *const *,
                                  const GLint *);
    void (APIENTRY *CompileShader)(GLuint);
    void (APIENTRY *GetShaderiv)(GLuint, GLenum, GLint *);
    void (APIENTRY *GetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
    void (APIENTRY *DeleteShader)(GLuint);
    GLuint (APIENTRY *CreateProgram)(void);
    void (APIENTRY *AttachShader)(GLuint, GLuint);
    void (APIENTRY *BindAttribLocation)(GLuint, GLuint, const GLchar *);
    void (APIENTRY *LinkProgram)(GLuint);
    void (APIENTRY *GetProgramiv)(GLuint, GLenum, GLint *);
    void (APIENTRY *GetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
    void (APIENTRY *DeleteProgram)(GLuint);
    void (APIENTRY *UseProgram)(GLuint);
    GLint (APIENTRY *GetUniformLocation)(GLuint, const GLchar *);
    void (APIENTRY *Uniform1i)(GLint, GLint);
    void (APIENTRY *Uniform1f)(GLint, GLfloat);
    void (APIENTRY *Uniform3f)(GLint, GLfloat, GLfloat, GLfloat);
    void (APIENTRY *Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
    void (APIENTRY *UniformMatrix3fv)(GLint, GLsizei, GLboolean,
                                      const GLfloat *);
    void (APIENTRY *UniformMatrix4fv)(GLint, GLsizei, GLboolean,
                                      const GLfloat *);
    void (APIENTRY *GenFramebuffers)(GLsizei, GLuint *);
    void (APIENTRY *DeleteFramebuffers)(GLsizei, const GLuint *);
    void (APIENTRY *BindFramebuffer)(GLenum, GLuint);
    void (APIENTRY *FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint,
                                          GLint);
    GLenum (APIENTRY *CheckFramebufferStatus)(GLenum);
    void (APIENTRY *GetFramebufferAttachmentParameteriv)(GLenum, GLenum,
                                                          GLenum, GLint *);
    void (APIENTRY *BlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint,
                                     GLint, GLint, GLbitfield, GLenum);
    void (APIENTRY *GenRenderbuffers)(GLsizei, GLuint *);
    void (APIENTRY *DeleteRenderbuffers)(GLsizei, const GLuint *);
    void (APIENTRY *BindRenderbuffer)(GLenum, GLuint);
    void (APIENTRY *RenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
    void (APIENTRY *FramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
    void (APIENTRY *Begin)(GLenum);
    void (APIENTRY *End)(void);
    void (APIENTRY *TexCoord2f)(GLfloat, GLfloat);
    void (APIENTRY *Vertex2f)(GLfloat, GLfloat);
    void (APIENTRY *Vertex4f)(GLfloat, GLfloat, GLfloat, GLfloat);
    void (APIENTRY *Color4f)(GLfloat, GLfloat, GLfloat, GLfloat);
    void (APIENTRY *Finish)(void);
} gl_api;

typedef struct gl_stage_uniforms {
    GLint mvp, model, normal_matrix;
    GLint clip_enabled, clip_plane;
    GLint primary, secondary, secondary_mode, alpha_mode;
    GLint fog_mode, fog_color, fog_amount;
    GLint lighting_mode, luminance_alpha, light_count;
    GLint shadow_map, shadow_texel, shadow_near;
    GLint shade_scale, model_shade_enabled;
    GLint light_position[GL_MAX_LIGHTS_QA];
    GLint light_radius[GL_MAX_LIGHTS_QA];
    GLint light_color[GL_MAX_LIGHTS_QA];
    GLint light_scale[GL_MAX_LIGHTS_QA];
    GLint light_spot[GL_MAX_LIGHTS_QA];
    GLint light_direction[GL_MAX_LIGHTS_QA];
    GLint light_cone[GL_MAX_LIGHTS_QA];
    GLint light_matrix[GL_MAX_LIGHTS_QA];
    GLint light_atlas[GL_MAX_LIGHTS_QA];
    GLint light_shadow[GL_MAX_LIGHTS_QA];
    GLint light_fraction[GL_MAX_LIGHTS_QA];
} gl_stage_uniforms;

typedef struct gl_fog_uniforms {
    GLint depth, far_depth, projection, fog_color;
    GLint view_origin, forward, right, up, tangent;
    GLint height_start, height_end, height_density, height_falloff;
} gl_fog_uniforms;

typedef struct gl_programs {
    GLuint stage, opacity, gamma, fog[3];
    gl_stage_uniforms stage_uniform;
    struct { GLint backdrop, result, opacity; } opacity_uniform;
    struct { GLint raw, table; } gamma_uniform;
    gl_fog_uniforms fog_uniform[3];
} gl_programs;

typedef struct gl_texture_entry {
    const qa_scene_image *image;
    GLuint name;
    struct gl_texture_entry *next;
} gl_texture_entry;

typedef struct gl_mesh_entry {
    uint64_t identity, revision;
    size_t vertex_count, index_count;
    GLuint vertex_buffer, index_buffer;
    struct gl_mesh_entry *next;
} gl_mesh_entry;

typedef struct gl_stream_buffers {
    GLuint vertex_buffer, index_buffer;
    size_t vertex_bytes, index_bytes;
} gl_stream_buffers;

typedef struct gl_output_target {
    GLuint framebuffer, depth_stencil, table;
    GLuint color[GL_DRAW_BUFFER_COUNT_QA];
    bool color_ready[GL_DRAW_BUFFER_COUNT_QA];
    bool dirty[GL_DRAW_BUFFER_COUNT_QA];
    uint32_t width, height;
    bool enabled;
} gl_output_target;

typedef struct gl_opacity_target {
    GLuint framebuffer[2], color[2], depth_stencil;
    uint32_t width, height;
    GLuint parent_draw_framebuffer, parent_read_framebuffer;
    GLenum parent_draw_buffer, parent_read_buffer;
    GLint parent_viewport[4], parent_scissor[4];
    float value;
    bool parent_scissor_enabled, allocated, active, skip;
} gl_opacity_target;

struct qa_gl_renderer {
    qa_gl_options options;
    gl_api gl;
    qa_gl_capabilities capabilities;
    gl_programs programs;
    gl_texture_entry *textures;
    gl_mesh_entry *meshes;
    gl_stream_buffers stream;
    gl_output_target output;
    gl_opacity_target opacity;
    GLuint target_framebuffer, fog_depth, white_texture;
    const qa_scene_image *target;
    const qa_scene_image *bound[2];
    qa_scene_view view;
    qa_scene_draw_buffer draw_buffer;
    float gamma;
    uint64_t sequence;
    bool overdraw, closed;
};

bool gl_api_load(qa_gl_renderer *renderer, qa_error *error);
bool gl_programs_create(qa_gl_renderer *renderer, qa_error *error);
void gl_programs_destroy(qa_gl_renderer *renderer);
bool gl_program_stage(qa_gl_renderer *renderer, const qa_scene_draw *draw,
                      qa_error *error);
bool gl_program_fog(qa_gl_renderer *renderer, unsigned pass,
                    const qa_scene_fog *fog, const qa_scene_view *view,
                    qa_error *error);
void gl_draw_quad(qa_gl_renderer *renderer);

bool gl_texture_get(qa_gl_renderer *renderer, const qa_scene_image *image,
                    gl_texture_entry **out, qa_error *error);
bool gl_resources_create(qa_gl_renderer *renderer, qa_error *error);
bool gl_image_update(qa_gl_renderer *renderer, const qa_scene_image *image,
                     qa_error *error);
void gl_textures_prune(qa_gl_renderer *renderer);
void gl_resources_destroy(qa_gl_renderer *renderer);
bool gl_mesh_bind(qa_gl_renderer *renderer, const qa_scene_mesh *mesh,
                  qa_error *error);
void gl_mesh_unbind(qa_gl_renderer *renderer);

bool gl_bind_destination(qa_gl_renderer *renderer, qa_error *error);
bool gl_select_target(qa_gl_renderer *renderer, const qa_scene_image *image,
                      qa_error *error);
bool gl_output_set_gamma(qa_gl_renderer *renderer, float gamma,
                         qa_error *error);
bool gl_output_resolve(qa_gl_renderer *renderer, qa_error *error);
void gl_output_destroy(qa_gl_renderer *renderer);
bool gl_opacity_begin(qa_gl_renderer *renderer, float opacity,
                      qa_error *error);
bool gl_opacity_end(qa_gl_renderer *renderer, qa_error *error);
void gl_opacity_abort(qa_gl_renderer *renderer);
void gl_opacity_destroy(qa_gl_renderer *renderer);
bool gl_depth_fog(qa_gl_renderer *renderer, const qa_scene_fog *fog,
                  const qa_scene_view *view, qa_error *error);

bool gl_dimensions(qa_gl_renderer *renderer, uint32_t *width, uint32_t *height,
                   qa_error *error);
GLenum gl_draw_buffer_name(qa_scene_draw_buffer buffer);
unsigned gl_draw_buffer_index(qa_scene_draw_buffer buffer);
bool gl_check(qa_gl_renderer *renderer, const char *operation,
              qa_error *error);

#endif
