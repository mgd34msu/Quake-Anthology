#ifndef QA_RENDER_GL_INTERNAL_H
#define QA_RENDER_GL_INTERNAL_H

#include "qa/render_gl.h"
#include "../controls_private.h"
#include "../output_domain.h"
#include "../resource_index_private.h"

#include <SDL_opengl.h>

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define GL_MAX_LIGHTS_QA 8
#define GL_DRAW_BUFFER_COUNT_QA 4
#define GL_SOURCE_IMAGES_QA 2048

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
    void (APIENTRY *AlphaFunc)(GLenum, GLclampf);
    void (APIENTRY *CullFace)(GLenum);
    void (APIENTRY *FrontFace)(GLenum);
    void (APIENTRY *DrawBuffer)(GLenum);
    void (APIENTRY *ReadBuffer)(GLenum);
    void (APIENTRY *ActiveTexture)(GLenum);
    void (APIENTRY *ClientActiveTexture)(GLenum);
    void (APIENTRY *TexEnvi)(GLenum, GLenum, GLint);
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
    void (APIENTRY *GetTexLevelParameteriv)(GLenum, GLint, GLenum, GLint *);
    void (APIENTRY *GetTexParameteriv)(GLenum, GLenum, GLint *);
    void (APIENTRY *PixelStorei)(GLenum, GLint);
    void (APIENTRY *ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
                                void *);
    void (APIENTRY *GenBuffers)(GLsizei, GLuint *);
    void (APIENTRY *DeleteBuffers)(GLsizei, const GLuint *);
    void (APIENTRY *BindBuffer)(GLenum, GLuint);
    void (APIENTRY *BufferData)(GLenum, GLsizeiptr, const void *, GLenum);
    void (APIENTRY *BufferSubData)(GLenum, GLintptr, GLsizeiptr, const void *);
    void (APIENTRY *GetBufferSubData)(GLenum, GLintptr, GLsizeiptr, void *);
    void (APIENTRY *EnableVertexAttribArray)(GLuint);
    void (APIENTRY *DisableVertexAttribArray)(GLuint);
    void (APIENTRY *VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean,
                                         GLsizei, const void *);
    void (APIENTRY *DrawElements)(GLenum, GLsizei, GLenum, const void *);
    void (APIENTRY *ArrayElement)(GLint);
    void (APIENTRY *VertexAttrib4f)(GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
    void (APIENTRY *LockArraysEXT)(GLint, GLsizei);
    void (APIENTRY *UnlockArraysEXT)(void);
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
    GLint primary, primary_enabled, secondary, secondary_mode, secondary_alpha, alpha_mode;
    GLint preblend_gamma, preblend_table;
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
    struct { GLint raw, table, apply; } gamma_uniform;
    gl_fog_uniforms fog_uniform[3];
} gl_programs;

typedef struct gl_texture_entry {
    uint64_t stream_writes;
    const qa_scene_image *image;
    GLuint name;
    qa_scene_resources *source_owner;
    uint32_t source_ordinal;
    qa_scene_filter source_filter;
    qa_render_source_texture source_texture;
    bool source_admitted;
    struct gl_texture_entry *next;
} gl_texture_entry;

typedef struct gl_mesh_entry {
    uint64_t identity, revision;
    const qa_scene_geometry *geometry;
    size_t vertex_count, index_count;
    GLuint vertex_buffer, index_buffer;
    struct gl_mesh_entry *next;
} gl_mesh_entry;
static inline bool gl_mesh_storage_matches(const gl_mesh_entry *entry,
                                           const qa_scene_mesh *mesh)
{
    return entry && entry->geometry == mesh->geometry &&
        entry->vertex_count == mesh->vertex_count && entry->index_count == mesh->index_count;
}

typedef struct gl_stream_buffers {
    GLuint vertex_buffer, index_buffer;
    size_t vertex_bytes, index_bytes;
    size_t vertex_cursor, index_cursor;
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
typedef struct gl_restore_storage gl_restore_storage;
typedef struct gl_presented_target {
    GLuint framebuffer,color[2];
    uint32_t width,height;
} gl_presented_target;

struct qa_gl_renderer {
    qa_gl_options options;
    qa_render_controls controls;
    gl_api gl;
    qa_gl_capabilities capabilities;
    gl_programs programs;
    gl_texture_entry *textures;
    qa_render_resource_index texture_index, source_image_index;
    gl_texture_entry *source_images[GL_SOURCE_IMAGES_QA];
    uint32_t source_image_count;
    gl_mesh_entry *meshes;
    qa_render_resource_index mesh_index;
    gl_stream_buffers stream;
    gl_output_target output;
    gl_opacity_target opacity;
    gl_presented_target presented_target;
    GLuint target_framebuffer, fog_depth, white_texture;
    const qa_scene_image *target;
    const qa_scene_image *bound[2];
    qa_scene_view view;
    qa_scene_state pipeline;
    float clear_depth;
    qa_scene_vertex source_vertices[QA_SOURCE_TESS_VERTICES];
    qa_scene_draw_buffer draw_buffer;
    float gamma;
    qa_output_domains output_domains;
    uint64_t sequence;
    const char *frame_operation;
    size_t frame_command;
    uint32_t presented_width, presented_height;
    bool overdraw, closed, presented, executing, capturing, preparing, detached;
    bool preblend_gamma, source_frame;
    qa_gl_surface_ticket *surface_ticket;
    bool destroy_pending;
    gl_restore_storage *restore;
};
void gl_restore_storage_destroy(qa_gl_renderer *);
qa_gl_renderer *gl_renderer_allocate(const qa_gl_options *, const qa_display_info *, qa_error *);

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
gl_texture_entry *gl_texture_resident(const qa_gl_renderer *, const qa_scene_image *);
bool gl_source_texture_bind(qa_gl_renderer *,const qa_scene_image *,qa_error *);
bool gl_resources_create(qa_gl_renderer *renderer, qa_error *error);
bool gl_image_update(qa_gl_renderer *renderer, const qa_scene_image *image,
                     qa_error *error);
bool gl_image_region_update(qa_gl_renderer *, const qa_scene_image_region *, qa_error *);
bool gl_image_stream_admit(qa_gl_renderer *, const qa_scene_image_stream *, qa_error *);
void gl_textures_prune(qa_gl_renderer *renderer);
void gl_meshes_prune(qa_gl_renderer *renderer);
void gl_resources_destroy(qa_gl_renderer *renderer);
const gl_mesh_entry *gl_mesh_resident(const qa_gl_renderer *, const qa_scene_mesh *);
bool gl_mesh_bind(qa_gl_renderer *renderer, const qa_scene_mesh *mesh,
                  const gl_mesh_entry *, const qa_scene_vertex_inputs *, size_t *index_offset,
                  qa_error *error);
void gl_mesh_unbind(qa_gl_renderer *renderer);

bool gl_bind_destination(qa_gl_renderer *renderer, qa_error *error);
bool gl_select_target(qa_gl_renderer *renderer, const qa_scene_image *image,
                      qa_error *error);
void gl_gamma_table(float gamma, uint8_t table[256]);
bool gl_output_set_gamma(qa_gl_renderer *renderer, float gamma,
                         qa_error *error);
bool gl_output_gamma_prepare(qa_gl_renderer *, float, bool copy_default, qa_error *);
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
GLenum gl_native_buffer(bool stereo, size_t slot);
unsigned gl_draw_buffer_index(qa_scene_draw_buffer buffer);
bool gl_check(qa_gl_renderer *renderer, const char *operation,
              qa_error *error);
bool gl_frame_check(qa_gl_renderer *, qa_error *);
void gl_source_pipeline_restore(qa_gl_renderer *renderer);

typedef struct gl_presentation_snapshot gl_presentation_snapshot;
bool gl_presentation_capture(qa_gl_renderer *, gl_presentation_snapshot **, qa_error *);
bool gl_presentation_copy(qa_gl_renderer *, gl_presentation_snapshot *, uint32_t, uint32_t, qa_error *);
bool gl_presentation_restore_bindings(qa_gl_renderer *, const gl_presentation_snapshot *, qa_error *);
bool gl_presentation_dispose(qa_gl_renderer *, gl_presentation_snapshot **, qa_error *);
bool gl_surface_targets_prepare(qa_gl_renderer *, const qa_gl_renderer *, float, bool, bool, GLuint *, qa_error *);
bool gl_surface_targets_delete(qa_gl_renderer *, bool, bool, qa_error *);

#endif
