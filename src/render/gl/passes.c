#include "internal.h"

#include <limits.h>

static void texture_storage(qa_gl_renderer *renderer, GLuint texture,
                            GLint internal, uint32_t width, uint32_t height,
                            GLenum format, GLenum type)
{
    gl_api *gl = &renderer->gl;
    gl->ActiveTexture(GL_TEXTURE0);
    gl->BindTexture(GL_TEXTURE_2D, texture);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    gl->TexImage2D(GL_TEXTURE_2D, 0, internal, (GLsizei)width,
                   (GLsizei)height, 0, format, type, NULL);
}

static GLenum depth_internal(const qa_gl_renderer *renderer)
{
    if (renderer->capabilities.stencil_bits != 0)
        return renderer->capabilities.floating_depth ? GL_DEPTH32F_STENCIL8 :
                                                       GL_DEPTH24_STENCIL8;
    if (renderer->capabilities.floating_depth) return GL_DEPTH_COMPONENT32F;
    return renderer->capabilities.depth_bits <= 16 ? GL_DEPTH_COMPONENT16 :
           renderer->capabilities.depth_bits <= 24 ? GL_DEPTH_COMPONENT24 :
                                                     GL_DEPTH_COMPONENT32;
}

static void attach_depth(qa_gl_renderer *renderer, GLuint framebuffer,
                         GLuint renderbuffer)
{
    gl_api *gl = &renderer->gl;
    gl->BindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    gl->FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                GL_RENDERBUFFER, renderbuffer);
    if (renderer->capabilities.stencil_bits != 0)
        gl->FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                                    GL_RENDERBUFFER, renderbuffer);
}

static bool framebuffer_complete(qa_gl_renderer *renderer,
                                 const char *operation, qa_error *error)
{
    GLenum status = renderer->gl.CheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status == GL_FRAMEBUFFER_COMPLETE) return true;
    qa_error_set(error, QA_ERROR_UNSUPPORTED, 0,
                 "%s is incomplete (0x%x)", operation, (unsigned)status);
    return false;
}

static GLbitfield depth_mask(const qa_gl_renderer *renderer)
{
    return GL_DEPTH_BUFFER_BIT |
           (renderer->capabilities.stencil_bits == 0 ? 0 :
                                                       GL_STENCIL_BUFFER_BIT);
}

static bool output_objects(qa_gl_renderer *renderer, qa_error *error)
{
    gl_output_target *output = &renderer->output;
    if (output->framebuffer != 0) return true;
    renderer->gl.GenFramebuffers(1, &output->framebuffer);
    renderer->gl.GenRenderbuffers(1, &output->depth_stencil);
    if (output->framebuffer == 0 || output->depth_stencil == 0) {
        if (output->depth_stencil != 0)
            renderer->gl.DeleteRenderbuffers(1, &output->depth_stencil);
        if (output->framebuffer != 0)
            renderer->gl.DeleteFramebuffers(1, &output->framebuffer);
        output->framebuffer = output->depth_stencil = 0;
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "OpenGL could not allocate output-gamma storage");
        return false;
    }
    return true;
}

static bool output_resize(qa_gl_renderer *renderer, uint32_t width,
                          uint32_t height, qa_error *error)
{
    gl_output_target *output = &renderer->output;
    if (width > renderer->capabilities.maximum_texture_size ||
        height > renderer->capabilities.maximum_texture_size) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0,
                     "OpenGL output target exceeds maximum texture size");
        return false;
    }
    if (output->width == width && output->height == height) return true;
    for (size_t i = 0; i < GL_DRAW_BUFFER_COUNT_QA; ++i) {
        if (output->color[i] != 0)
            renderer->gl.DeleteTextures(1, &output->color[i]);
        output->color[i] = 0;
        output->color_ready[i] = false;
        output->dirty[i] = false;
    }
    renderer->gl.BindRenderbuffer(GL_RENDERBUFFER, output->depth_stencil);
    renderer->gl.RenderbufferStorage(GL_RENDERBUFFER,
                                     depth_internal(renderer),
                                     (GLsizei)width, (GLsizei)height);
    if (!gl_check(renderer, "OpenGL output target resize", error)) return false;
    output->width = width;
    output->height = height;
    qa_output_domains_extent(&renderer->output_domains,width,height);
    return true;
}

static bool output_bind_slot(qa_gl_renderer *renderer, unsigned slot,
                             bool copy_default, qa_error *error)
{
    gl_output_target *output = &renderer->output;
    uint32_t width, height;
    bool created = false;
    if (!gl_dimensions(renderer, &width, &height, error) ||
        !output_objects(renderer, error) ||
        !output_resize(renderer, width, height, error)) return false;
    gl_api *gl = &renderer->gl;
    if (!output->color_ready[slot]) {
        gl->GenTextures(1, &output->color[slot]);
        if (output->color[slot] == 0) {
            qa_error_set(error, QA_ERROR_MEMORY, 0,
                         "OpenGL could not allocate an output color buffer");
            return false;
        }
        GLint color = renderer->capabilities.alpha_bits != 0 ? GL_RGBA8 :
                      renderer->capabilities.color_bits <= 16 ? GL_RGB565 : GL_RGB8;
        texture_storage(renderer, output->color[slot], color, width, height,
                        GL_RGBA, GL_UNSIGNED_BYTE);
        gl->BindFramebuffer(GL_FRAMEBUFFER, output->framebuffer);
        gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                 GL_TEXTURE_2D, output->color[slot], 0);
        attach_depth(renderer, output->framebuffer, output->depth_stencil);
        gl->DrawBuffer(GL_COLOR_ATTACHMENT0);
        gl->ReadBuffer(GL_COLOR_ATTACHMENT0);
        if (!framebuffer_complete(renderer, "OpenGL output framebuffer", error))
            goto fail_created;
        bool first = true;
        for (size_t i = 0; i < GL_DRAW_BUFFER_COUNT_QA; ++i)
            first = first && !output->color_ready[i];
        if (copy_default) {
            bool scissor = gl->IsEnabled(GL_SCISSOR_TEST) != GL_FALSE;
            gl->Disable(GL_SCISSOR_TEST);
            gl->BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            gl->ReadBuffer(gl_draw_buffer_name(renderer->draw_buffer));
            gl->BindFramebuffer(GL_DRAW_FRAMEBUFFER, output->framebuffer);
            gl->DrawBuffer(GL_COLOR_ATTACHMENT0);
            gl->BlitFramebuffer(0, 0, (GLint)width, (GLint)height,
                                0, 0, (GLint)width, (GLint)height,
                                GL_COLOR_BUFFER_BIT |
                                    (first ? depth_mask(renderer) : 0),
                                GL_NEAREST);
            if (scissor) gl->Enable(GL_SCISSOR_TEST);
        }
        output->color_ready[slot] = true;
        created = true;
    }
    gl->BindFramebuffer(GL_FRAMEBUFFER, output->framebuffer);
    gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, output->color[slot], 0);
    gl->DrawBuffer(GL_COLOR_ATTACHMENT0);
    gl->ReadBuffer(GL_COLOR_ATTACHMENT0);
    output->dirty[slot] = true;
    if (gl_check(renderer, "OpenGL output framebuffer binding", error))
        return true;
    if (!created) return false;
fail_created:
    if (output->color[slot] != 0)
        gl->DeleteTextures(1, &output->color[slot]);
    output->color[slot] = 0;
    output->color_ready[slot] = false;
    output->dirty[slot] = false;
    return false;
}

bool gl_bind_destination(qa_gl_renderer *renderer, qa_error *error)
{
    gl_api *gl = &renderer->gl;
    if (renderer->opacity.active && renderer->opacity.value > 0 &&
        renderer->opacity.value < 1) {
        gl->BindFramebuffer(GL_FRAMEBUFFER, renderer->opacity.framebuffer[1]);
        gl->DrawBuffer(GL_COLOR_ATTACHMENT0);
        gl->ReadBuffer(GL_COLOR_ATTACHMENT0);
        return true;
    }
    if (renderer->target != NULL) {
        gl_texture_entry *texture;
        if (!gl_texture_get(renderer, renderer->target, &texture, error))
            return false;
        if (renderer->target_framebuffer == 0)
            gl->GenFramebuffers(1, &renderer->target_framebuffer);
        if (renderer->target_framebuffer == 0) {
            qa_error_set(error, QA_ERROR_MEMORY, 0,
                         "OpenGL could not allocate a target framebuffer");
            return false;
        }
        gl->BindFramebuffer(GL_FRAMEBUFFER, renderer->target_framebuffer);
        gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                 GL_TEXTURE_2D, texture->name, 0);
        gl->DrawBuffer(GL_NONE);
        gl->ReadBuffer(GL_NONE);
        return framebuffer_complete(renderer, "OpenGL depth target", error);
    }
    if (renderer->output.enabled)
        return output_bind_slot(renderer,
                                gl_draw_buffer_index(renderer->draw_buffer),
                                true, error);
    gl->BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->DrawBuffer(gl_draw_buffer_name(renderer->draw_buffer));
    gl->ReadBuffer(gl_draw_buffer_name(renderer->draw_buffer));
    return true;
}

bool gl_select_target(qa_gl_renderer *renderer, const qa_scene_image *image,
                      qa_error *error)
{
    if (renderer->opacity.active && renderer->opacity.value != 1) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "OpenGL render target changed inside opacity scope");
        return false;
    }
    if (image != NULL) {
        if (image->kind != QA_SCENE_DEPTH32F || image->levels == NULL ||
            image->level_count == 0) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                         "OpenGL render target requires a depth32f image");
            return false;
        }
        gl_texture_entry *texture;
        if (!gl_texture_get(renderer, image, &texture, error)) return false;
        (void)texture;
    }
    const qa_scene_image *previous = renderer->target;
    if (image != previous) qa_scene_image_retain(image);
    renderer->target = image;
    if (!gl_bind_destination(renderer, error)) {
        renderer->target = previous;
        if (image != previous) qa_scene_image_release(image);
        qa_error ignored = {0};
        (void)gl_bind_destination(renderer, &ignored);
        return false;
    }
    if (image != previous) qa_scene_image_release(previous);
    return true;
}

static void output_delete(qa_gl_renderer *renderer)
{
    gl_output_target *output = &renderer->output;
    for (size_t i = 0; i < GL_DRAW_BUFFER_COUNT_QA; ++i)
        if (output->color[i] != 0)
            renderer->gl.DeleteTextures(1, &output->color[i]);
    if (output->table != 0) renderer->gl.DeleteTextures(1, &output->table);
    if (output->depth_stencil != 0)
        renderer->gl.DeleteRenderbuffers(1, &output->depth_stencil);
    if (output->framebuffer != 0)
        renderer->gl.DeleteFramebuffers(1, &output->framebuffer);
    memset(output, 0, sizeof(*output));
}

static bool output_restore(qa_gl_renderer *renderer, qa_error *error)
{
    gl_output_target *output = &renderer->output;
    gl_api *gl = &renderer->gl;
    bool scissor = gl->IsEnabled(GL_SCISSOR_TEST) != GL_FALSE;
    gl->Disable(GL_SCISSOR_TEST);
    for (unsigned slot = 0; slot < GL_DRAW_BUFFER_COUNT_QA; ++slot) {
        if (!output->color_ready[slot]) continue;
        gl->BindFramebuffer(GL_READ_FRAMEBUFFER, output->framebuffer);
        gl->FramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                 GL_TEXTURE_2D, output->color[slot], 0);
        gl->ReadBuffer(GL_COLOR_ATTACHMENT0);
        gl->BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        qa_scene_draw_buffer buffer = slot == 0 ? QA_DRAW_FRONT :
                                      slot == 1 ? QA_DRAW_BACK :
                                      slot == 2 ? QA_DRAW_BACK_LEFT :
                                                  QA_DRAW_BACK_RIGHT;
        gl->DrawBuffer(gl_draw_buffer_name(buffer));
        gl->BlitFramebuffer(0, 0, (GLint)output->width, (GLint)output->height,
                            0, 0, (GLint)output->width, (GLint)output->height,
                            GL_COLOR_BUFFER_BIT | depth_mask(renderer),
                            GL_NEAREST);
    }
    gl->BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->DrawBuffer(gl_draw_buffer_name(renderer->draw_buffer));
    gl->ReadBuffer(gl_draw_buffer_name(renderer->draw_buffer));
    if (scissor) gl->Enable(GL_SCISSOR_TEST);
    return gl_check(renderer, "OpenGL output target restore", error);
}

void gl_gamma_table(float gamma, uint8_t table[256])
{
    for (size_t i = 0; i < 256; ++i) {
        float value = powf((float)i / 255.0f, 1.0f / gamma);
        table[i] = (uint8_t)floorf(fminf(fmaxf(value, 0), 1) * 255 + 0.5f);
    }
}

bool gl_output_gamma_prepare(qa_gl_renderer *renderer, float gamma,
                             bool copy_default, qa_error *error)
{
    if (!isfinite(gamma) || gamma < 0.5f || gamma > 3) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "OpenGL gamma must be within 0.5..3");
        return false;
    }
    if ((renderer->opacity.active && renderer->opacity.value != 1) ||
        renderer->target != NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "OpenGL gamma cannot change inside an offscreen scope");
        return false;
    }
    if (gamma == 1) {
        if (renderer->output.enabled &&
            !output_restore(renderer, error)) return false;
        output_delete(renderer);
        renderer->gamma = gamma;
        return gl_bind_destination(renderer, error);
    }
    if (!output_objects(renderer, error)) return false;
    uint8_t table[256];
    gl_gamma_table(gamma, table);
    gl_api *gl = &renderer->gl;
    GLuint table_texture = 0;
    gl->GenTextures(1, &table_texture);
    if (table_texture == 0) {
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "OpenGL could not allocate a gamma table");
        return false;
    }
    gl->ActiveTexture(GL_TEXTURE1);
    gl->BindTexture(GL_TEXTURE_2D, table_texture);
    gl->PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->TexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE8, 256, 1, 0,
                   GL_LUMINANCE, GL_UNSIGNED_BYTE, table);
    if (!gl_check(renderer, "OpenGL gamma table upload", error)) {
        gl->DeleteTextures(1, &table_texture);
        return false;
    }
    bool was_enabled = renderer->output.enabled;
    renderer->output.enabled = true;
    if (!output_bind_slot(renderer, gl_draw_buffer_index(renderer->draw_buffer), copy_default, error)) {
        renderer->output.enabled = was_enabled;
        gl->DeleteTextures(1, &table_texture);
        qa_error ignored = {0};
        (void)gl_bind_destination(renderer, &ignored);
        return false;
    }
    if (renderer->output.table != 0)
        gl->DeleteTextures(1, &renderer->output.table);
    renderer->output.table = table_texture;
    renderer->gamma = gamma;
    return true;
}

bool gl_output_set_gamma(qa_gl_renderer *renderer, float gamma,
                         qa_error *error)
{ return gl_output_gamma_prepare(renderer, gamma, true, error); }

static void composite_state(qa_gl_renderer *renderer, uint32_t width,
                            uint32_t height)
{
    gl_api *gl = &renderer->gl;
    gl_mesh_unbind(renderer);
    gl->Viewport(0, 0, (GLsizei)width, (GLsizei)height);
    gl->Disable(GL_SCISSOR_TEST);
    gl->Disable(GL_DEPTH_TEST);
    gl->Disable(GL_CULL_FACE);
    gl->Disable(GL_STENCIL_TEST);
    gl->Disable(GL_BLEND);
    gl->Disable(GL_ALPHA_TEST);
    gl->Disable(GL_POLYGON_OFFSET_FILL);
    gl->DepthMask(GL_FALSE);
    gl->ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl->PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

typedef struct gl_composite_state {
    GLint depth_mask,color_mask[4],polygon_mode[2],program,active,texture[3];
    GLint blend_source,blend_destination,viewport[4],scissor[4];
    GLfloat color[4],alpha_reference;
    GLint alpha_function;
    bool depth,cull,stencil,blend,alpha,offset,scissor_enabled;
} gl_composite_state;
static void composite_state_read(qa_gl_renderer *renderer,gl_composite_state *state)
{
    gl_api *gl=&renderer->gl;
    gl->GetIntegerv(GL_DEPTH_WRITEMASK,&state->depth_mask);
    gl->GetIntegerv(GL_COLOR_WRITEMASK,state->color_mask);
    gl->GetIntegerv(GL_POLYGON_MODE,state->polygon_mode);
    gl->GetIntegerv(GL_CURRENT_PROGRAM,&state->program);
    gl->GetIntegerv(GL_ACTIVE_TEXTURE,&state->active);
    gl->GetIntegerv(GL_BLEND_SRC,&state->blend_source);
    gl->GetIntegerv(GL_BLEND_DST,&state->blend_destination);
    gl->GetIntegerv(GL_VIEWPORT,state->viewport);
    gl->GetIntegerv(GL_SCISSOR_BOX,state->scissor);
    gl->GetFloatv(GL_CURRENT_COLOR,state->color);
    gl->GetFloatv(GL_ALPHA_TEST_REF,&state->alpha_reference);
    gl->GetIntegerv(GL_ALPHA_TEST_FUNC,&state->alpha_function);
    for (size_t i=0;i<3;++i) {
        gl->ActiveTexture(GL_TEXTURE0+(GLenum)i);
        gl->GetIntegerv(GL_TEXTURE_BINDING_2D,state->texture+i);
    }
    gl->ActiveTexture((GLenum)state->active);
    state->depth=gl->IsEnabled(GL_DEPTH_TEST)!=GL_FALSE;
    state->cull=gl->IsEnabled(GL_CULL_FACE)!=GL_FALSE;
    state->stencil=gl->IsEnabled(GL_STENCIL_TEST)!=GL_FALSE;
    state->blend=gl->IsEnabled(GL_BLEND)!=GL_FALSE;
    state->alpha=gl->IsEnabled(GL_ALPHA_TEST)!=GL_FALSE;
    state->offset=gl->IsEnabled(GL_POLYGON_OFFSET_FILL)!=GL_FALSE;
    state->scissor_enabled=gl->IsEnabled(GL_SCISSOR_TEST)!=GL_FALSE;
}
static void composite_enable(gl_api *gl,GLenum capability,bool enabled)
{ if (enabled) gl->Enable(capability); else gl->Disable(capability); }
static void composite_state_restore(qa_gl_renderer *renderer,const gl_composite_state *state)
{
    gl_api *gl=&renderer->gl;
    gl->DepthMask(state->depth_mask?GL_TRUE:GL_FALSE);
    gl->ColorMask(state->color_mask[0]?GL_TRUE:GL_FALSE,state->color_mask[1]?GL_TRUE:GL_FALSE,
        state->color_mask[2]?GL_TRUE:GL_FALSE,state->color_mask[3]?GL_TRUE:GL_FALSE);
    gl->PolygonMode(GL_FRONT,(GLenum)state->polygon_mode[0]);
    gl->PolygonMode(GL_BACK,(GLenum)state->polygon_mode[1]);
    composite_enable(gl,GL_DEPTH_TEST,state->depth);
    composite_enable(gl,GL_CULL_FACE,state->cull);
    composite_enable(gl,GL_STENCIL_TEST,state->stencil);
    composite_enable(gl,GL_BLEND,state->blend);
    composite_enable(gl,GL_ALPHA_TEST,state->alpha);
    gl->AlphaFunc((GLenum)state->alpha_function,state->alpha_reference);
    composite_enable(gl,GL_POLYGON_OFFSET_FILL,state->offset);
    gl->BlendFunc((GLenum)state->blend_source,(GLenum)state->blend_destination);
    gl->Viewport(state->viewport[0],state->viewport[1],state->viewport[2],state->viewport[3]);
    gl->Scissor(state->scissor[0],state->scissor[1],state->scissor[2],state->scissor[3]);
    composite_enable(gl,GL_SCISSOR_TEST,state->scissor_enabled);
    gl->Color4f(state->color[0],state->color[1],state->color[2],state->color[3]);
    for (size_t i=0;i<3;++i) {
        gl->ActiveTexture(GL_TEXTURE0+(GLenum)i);
        gl->BindTexture(GL_TEXTURE_2D,(GLuint)state->texture[i]);
    }
    gl->ActiveTexture((GLenum)state->active);
    gl->UseProgram((GLuint)state->program);
}

bool gl_output_resolve(qa_gl_renderer *renderer, qa_error *error)
{
    if (!renderer->output.enabled) {
        renderer->gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        renderer->gl.DrawBuffer(gl_draw_buffer_name(renderer->draw_buffer));
        renderer->gl.ReadBuffer(gl_draw_buffer_name(renderer->draw_buffer));
        return true;
    }
    if ((renderer->opacity.active && renderer->opacity.value != 1) ||
        renderer->target != NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "OpenGL output cannot resolve inside an offscreen scope");
        return false;
    }
    gl_output_target *output = &renderer->output;
    gl_api *gl = &renderer->gl;
    gl_composite_state retained;
    composite_state_read(renderer,&retained);
    composite_state(renderer, output->width, output->height);
    gl->BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->UseProgram(renderer->programs.gamma);
    gl->Uniform1i(renderer->programs.gamma_uniform.raw, 0);
    gl->Uniform1i(renderer->programs.gamma_uniform.table, 1);
    bool native_gamma = qa_display_gamma_applied_is(renderer->options.display);
    gl->Uniform1i(renderer->programs.gamma_uniform.apply, native_gamma ? 0 : 1);
    gl->ActiveTexture(GL_TEXTURE1);
    gl->BindTexture(GL_TEXTURE_2D, output->table);
    unsigned resolved = 0;
    for (unsigned slot = 0; slot < GL_DRAW_BUFFER_COUNT_QA; ++slot) {
        if (!output->color_ready[slot] || !output->dirty[slot]) continue;
        qa_scene_draw_buffer buffer = slot == 0 ? QA_DRAW_FRONT :
                                      slot == 1 ? QA_DRAW_BACK :
                                      slot == 2 ? QA_DRAW_BACK_LEFT :
                                                  QA_DRAW_BACK_RIGHT;
        gl->DrawBuffer(gl_draw_buffer_name(buffer));
        gl->ActiveTexture(GL_TEXTURE0);
        gl->BindTexture(GL_TEXTURE_2D, output->color[slot]);
        gl_draw_quad(renderer);
        if (!native_gamma) {
            gl->Enable(GL_SCISSOR_TEST);
            for (size_t i=0;i<renderer->output_domains.count;++i) {
                const qa_output_domain_region *domain=renderer->output_domains.regions+i;
                if (domain->buffer!=buffer) continue;
                gl->Scissor(domain->rect.x,(GLint)(output->height-(uint32_t)domain->rect.y-domain->rect.height),
                    (GLsizei)domain->rect.width,(GLsizei)domain->rect.height);
                gl->Uniform1i(renderer->programs.gamma_uniform.apply,domain->source?0:1);
                gl_draw_quad(renderer);
            }
            gl->Disable(GL_SCISSOR_TEST);
            gl->Uniform1i(renderer->programs.gamma_uniform.apply,1);
        }
        resolved |= 1u << slot;
    }
    gl->UseProgram(0);
    gl->DrawBuffer(gl_draw_buffer_name(renderer->draw_buffer));
    gl->ReadBuffer(gl_draw_buffer_name(renderer->draw_buffer));
    composite_state_restore(renderer,&retained);
    if (!gl_check(renderer, "OpenGL output gamma resolve", error)) return false;
    for (unsigned slot = 0; slot < GL_DRAW_BUFFER_COUNT_QA; ++slot)
        if ((resolved & (1u << slot)) != 0) output->dirty[slot] = false;
    return true;
}

void gl_output_destroy(qa_gl_renderer *renderer)
{
    output_delete(renderer);
}

static bool opacity_allocate(qa_gl_renderer *renderer, uint32_t width,
                             uint32_t height, qa_error *error)
{
    gl_opacity_target *opacity = &renderer->opacity;
    gl_api *gl = &renderer->gl;
    if (width > renderer->capabilities.maximum_texture_size ||
        height > renderer->capabilities.maximum_texture_size) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0,
                     "OpenGL opacity target exceeds maximum texture size");
        return false;
    }
    if (!opacity->allocated) {
        gl->GenFramebuffers(2, opacity->framebuffer);
        gl->GenTextures(2, opacity->color);
        gl->GenRenderbuffers(1, &opacity->depth_stencil);
        if (opacity->framebuffer[0] == 0 || opacity->framebuffer[1] == 0 ||
            opacity->color[0] == 0 || opacity->color[1] == 0 ||
            opacity->depth_stencil == 0) {
            if (opacity->framebuffer[0] != 0 || opacity->framebuffer[1] != 0)
                gl->DeleteFramebuffers(2, opacity->framebuffer);
            if (opacity->color[0] != 0 || opacity->color[1] != 0)
                gl->DeleteTextures(2, opacity->color);
            if (opacity->depth_stencil != 0)
                gl->DeleteRenderbuffers(1, &opacity->depth_stencil);
            memset(opacity->framebuffer, 0, sizeof(opacity->framebuffer));
            memset(opacity->color, 0, sizeof(opacity->color));
            opacity->depth_stencil = 0;
            qa_error_set(error, QA_ERROR_MEMORY, 0,
                         "OpenGL could not allocate opacity storage");
            return false;
        }
        opacity->allocated = true;
    }
    if (opacity->width == width && opacity->height == height) return true;
    GLint color = renderer->capabilities.alpha_bits != 0 ? GL_RGBA8 :
                  renderer->capabilities.color_bits <= 16 ? GL_RGB565 : GL_RGB8;
    for (size_t i = 0; i < 2; ++i)
        texture_storage(renderer, opacity->color[i], color, width, height,
                        GL_RGBA, GL_UNSIGNED_BYTE);
    gl->BindRenderbuffer(GL_RENDERBUFFER, opacity->depth_stencil);
    gl->RenderbufferStorage(GL_RENDERBUFFER, depth_internal(renderer),
                            (GLsizei)width, (GLsizei)height);
    for (size_t i = 0; i < 2; ++i) {
        gl->BindFramebuffer(GL_FRAMEBUFFER, opacity->framebuffer[i]);
        gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                 GL_TEXTURE_2D, opacity->color[i], 0);
        if (i == 1)
            attach_depth(renderer, opacity->framebuffer[i],
                         opacity->depth_stencil);
        gl->DrawBuffer(GL_COLOR_ATTACHMENT0);
        gl->ReadBuffer(GL_COLOR_ATTACHMENT0);
        if (!framebuffer_complete(renderer, "OpenGL opacity framebuffer",
                                  error)) return false;
    }
    if (!gl_check(renderer, "OpenGL opacity target allocation", error))
        return false;
    opacity->width = width;
    opacity->height = height;
    return true;
}

static void opacity_restore_parent(qa_gl_renderer *renderer)
{
    gl_opacity_target *opacity = &renderer->opacity;
    gl_api *gl = &renderer->gl;
    gl->BindFramebuffer(GL_DRAW_FRAMEBUFFER, opacity->parent_draw_framebuffer);
    gl->DrawBuffer(opacity->parent_draw_buffer);
    gl->BindFramebuffer(GL_READ_FRAMEBUFFER, opacity->parent_read_framebuffer);
    gl->ReadBuffer(opacity->parent_read_buffer);
}

static void opacity_restore_raster(qa_gl_renderer *renderer)
{
    gl_opacity_target *opacity = &renderer->opacity;
    gl_api *gl = &renderer->gl;
    gl->Viewport(opacity->parent_viewport[0], opacity->parent_viewport[1],
                 opacity->parent_viewport[2], opacity->parent_viewport[3]);
    gl->Scissor(opacity->parent_scissor[0], opacity->parent_scissor[1],
                opacity->parent_scissor[2], opacity->parent_scissor[3]);
    if (opacity->parent_scissor_enabled) gl->Enable(GL_SCISSOR_TEST);
    else gl->Disable(GL_SCISSOR_TEST);
}

bool gl_opacity_begin(qa_gl_renderer *renderer, float value, qa_error *error)
{
    if (renderer->opacity.active || !isfinite(value) || value < 0 || value > 1 ||
        (renderer->target != NULL && value > 0 && value < 1)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "Invalid, nested, or depth-target OpenGL opacity scope");
        return false;
    }
    renderer->opacity.value = value;
    if (value == 0 || value == 1) {
        renderer->opacity.active = true;
        renderer->opacity.skip = value == 0;
        return true;
    }
    uint32_t width, height;
    if (!gl_bind_destination(renderer, error) ||
        !gl_dimensions(renderer, &width, &height, error)) return false;
    gl_api *gl = &renderer->gl;
    GLint value_i = 0;
    gl->GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &value_i);
    renderer->opacity.parent_draw_framebuffer = (GLuint)value_i;
    gl->GetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &value_i);
    renderer->opacity.parent_read_framebuffer = (GLuint)value_i;
    gl->GetIntegerv(GL_DRAW_BUFFER, &value_i);
    renderer->opacity.parent_draw_buffer = (GLenum)value_i;
    gl->GetIntegerv(GL_READ_BUFFER, &value_i);
    renderer->opacity.parent_read_buffer = (GLenum)value_i;
    gl->GetIntegerv(GL_VIEWPORT, renderer->opacity.parent_viewport);
    gl->GetIntegerv(GL_SCISSOR_BOX, renderer->opacity.parent_scissor);
    renderer->opacity.parent_scissor_enabled =
        gl->IsEnabled(GL_SCISSOR_TEST) != GL_FALSE;
    GLint samples = 0;
    gl->GetIntegerv(GL_SAMPLES, &samples);
    if (samples != 0) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0,
                     "OpenGL opacity requires a single-sample framebuffer");
        return false;
    }
    if (!opacity_allocate(renderer, width, height, error)) {
        opacity_restore_parent(renderer);
        return false;
    }
    gl->Disable(GL_SCISSOR_TEST);
    for (size_t i = 0; i < 2; ++i) {
        gl->BindFramebuffer(GL_READ_FRAMEBUFFER,
                            renderer->opacity.parent_read_framebuffer);
        gl->ReadBuffer(renderer->opacity.parent_read_buffer);
        gl->BindFramebuffer(GL_DRAW_FRAMEBUFFER,
                            renderer->opacity.framebuffer[i]);
        gl->DrawBuffer(GL_COLOR_ATTACHMENT0);
        GLbitfield mask = GL_COLOR_BUFFER_BIT |
            (i == 0 ? 0 : depth_mask(renderer));
        gl->BlitFramebuffer(0, 0, (GLint)width, (GLint)height,
                            0, 0, (GLint)width, (GLint)height,
                            mask, GL_NEAREST);
    }
    if (!gl_check(renderer, "OpenGL opacity backdrop copy", error)) {
        opacity_restore_raster(renderer);
        opacity_restore_parent(renderer);
        return false;
    }
    opacity_restore_raster(renderer);
    renderer->opacity.active = true;
    renderer->opacity.skip = false;
    gl->BindFramebuffer(GL_FRAMEBUFFER, renderer->opacity.framebuffer[1]);
    gl->DrawBuffer(GL_COLOR_ATTACHMENT0);
    gl->ReadBuffer(GL_COLOR_ATTACHMENT0);
    return true;
}

bool gl_opacity_end(qa_gl_renderer *renderer, qa_error *error)
{
    gl_opacity_target *opacity = &renderer->opacity;
    if (!opacity->active) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "OpenGL opacity end has no matching begin");
        return false;
    }
    float value = opacity->value;
    opacity->active = false;
    opacity->skip = false;
    if (value == 0 || value == 1) return gl_bind_destination(renderer, error);
    gl_api *gl = &renderer->gl;
    opacity_restore_parent(renderer);
    gl_composite_state retained;
    composite_state_read(renderer,&retained);
    composite_state(renderer, opacity->width, opacity->height);
    int64_t x0 = opacity->parent_viewport[0] > 0
                     ? opacity->parent_viewport[0] : 0;
    int64_t y0 = opacity->parent_viewport[1] > 0
                     ? opacity->parent_viewport[1] : 0;
    int64_t x1 = (int64_t)opacity->parent_viewport[0] +
                 opacity->parent_viewport[2];
    int64_t y1 = (int64_t)opacity->parent_viewport[1] +
                 opacity->parent_viewport[3];
    if (x1 > opacity->width) x1 = opacity->width;
    if (y1 > opacity->height) y1 = opacity->height;
    if (opacity->parent_scissor_enabled) {
        int64_t clip_x0 = opacity->parent_scissor[0];
        int64_t clip_y0 = opacity->parent_scissor[1];
        int64_t clip_x1 = clip_x0 + opacity->parent_scissor[2];
        int64_t clip_y1 = clip_y0 + opacity->parent_scissor[3];
        if (clip_x0 > x0) x0 = clip_x0;
        if (clip_y0 > y0) y0 = clip_y0;
        if (clip_x1 < x1) x1 = clip_x1;
        if (clip_y1 < y1) y1 = clip_y1;
    }
    gl->Enable(GL_SCISSOR_TEST);
    gl->Scissor((GLint)x0, (GLint)y0,
                (GLsizei)(x1 > x0 ? x1 - x0 : 0),
                (GLsizei)(y1 > y0 ? y1 - y0 : 0));
    gl->UseProgram(renderer->programs.opacity);
    gl->Uniform1i(renderer->programs.opacity_uniform.backdrop, 0);
    gl->Uniform1i(renderer->programs.opacity_uniform.result, 1);
    gl->Uniform1f(renderer->programs.opacity_uniform.opacity, value);
    gl->ActiveTexture(GL_TEXTURE0);
    gl->BindTexture(GL_TEXTURE_2D, opacity->color[0]);
    gl->ActiveTexture(GL_TEXTURE1);
    gl->BindTexture(GL_TEXTURE_2D, opacity->color[1]);
    gl->ActiveTexture(GL_TEXTURE0);
    gl_draw_quad(renderer);
    gl->UseProgram(0);
    opacity_restore_raster(renderer);
    composite_state_restore(renderer,&retained);
    return gl_check(renderer, "OpenGL opacity composite", error);
}

void gl_opacity_abort(qa_gl_renderer *renderer)
{
    gl_opacity_target *opacity = &renderer->opacity;
    if (!opacity->active) return;
    bool scratch = opacity->value > 0 && opacity->value < 1;
    opacity->active = false;
    opacity->skip = false;
    if (scratch) {
        opacity_restore_parent(renderer);
        opacity_restore_raster(renderer);
    }
    else {
        qa_error ignored = {0};
        (void)gl_bind_destination(renderer, &ignored);
    }
}

void gl_opacity_destroy(qa_gl_renderer *renderer)
{
    gl_opacity_target *opacity = &renderer->opacity;
    if (opacity->color[0] != 0 || opacity->color[1] != 0)
        renderer->gl.DeleteTextures(2, opacity->color);
    if (opacity->depth_stencil != 0)
        renderer->gl.DeleteRenderbuffers(1, &opacity->depth_stencil);
    if (opacity->framebuffer[0] != 0 || opacity->framebuffer[1] != 0)
        renderer->gl.DeleteFramebuffers(2, opacity->framebuffer);
    memset(opacity, 0, sizeof(*opacity));
}

static bool symmetric_projection(const qa_scene_matrix *projection)
{
    const float *m = projection->m;
    return m[1] == 0 && m[2] == 0 && m[3] == 0 && m[4] == 0 &&
           m[6] == 0 && m[7] == 0 && m[8] == 0 && m[9] == 0 &&
           m[12] == 0 && m[13] == 0 && m[0] != 0 && m[5] != 0 &&
           m[11] == -1 && m[15] == 0;
}

static bool finite_vec3(qa_vec3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

bool gl_depth_fog(qa_gl_renderer *renderer, const qa_scene_fog *fog,
                  const qa_scene_view *view, qa_error *error)
{
    bool finite_projection = true;
    for (size_t i = 0; i < 16; ++i)
        finite_projection = finite_projection && isfinite(view->projection.m[i]);
    if (renderer->target != NULL ||
        (renderer->opacity.active && renderer->opacity.value != 1) ||
        fog->kind != QA_FOG_Q2 ||
        !symmetric_projection(&view->projection) ||
        !finite_projection || !finite_vec3(fog->color) ||
        !finite_vec3(fog->height_color) ||
        !finite_vec3(fog->height_end_color) || !finite_vec3(view->origin) ||
        !finite_vec3(view->axis[0]) || !finite_vec3(view->axis[1]) ||
        !finite_vec3(view->axis[2]) || !isfinite(fog->density) ||
        !isfinite(fog->sky_factor) || !isfinite(fog->height_density) ||
        !isfinite(fog->height_start) || !isfinite(fog->height_end) ||
        !isfinite(fog->height_falloff) || !isfinite(fog->far_depth) ||
        fog->far_depth < 0 || fog->far_depth > 1) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "Invalid display-target OpenGL Q2 fog pass");
        return false;
    }
    if (fog->density <= 0 &&
        (fog->height_density <= 0 || fog->height_falloff <= 0) &&
        (fog->sky_factor <= 0 || !fog->sky_drawn)) return true;
    uint32_t width, height;
    if (!gl_bind_destination(renderer, error) ||
        !gl_dimensions(renderer, &width, &height, error)) return false;
    qa_scene_rect rect = view->viewport;
    if (rect.x < 0 || rect.y < 0 || rect.width == 0 || rect.height == 0 ||
        (uint64_t)(uint32_t)rect.x + rect.width > width ||
        (uint64_t)(uint32_t)rect.y + rect.height > height ||
        rect.width > renderer->capabilities.maximum_texture_size ||
        rect.height > renderer->capabilities.maximum_texture_size) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "OpenGL fog viewport is outside the drawable");
        return false;
    }
    gl_api *gl = &renderer->gl;
    if (renderer->fog_depth == 0) gl->GenTextures(1, &renderer->fog_depth);
    if (renderer->fog_depth == 0) {
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "OpenGL could not allocate fog depth storage");
        return false;
    }
    GLint bottom = (GLint)height - rect.y - (GLint)rect.height;
    gl_composite_state retained;
    composite_state_read(renderer,&retained);
    gl->ActiveTexture(GL_TEXTURE0);
    gl->BindTexture(GL_TEXTURE_2D, renderer->fog_depth);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->CopyTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                       rect.x, bottom, (GLsizei)rect.width,
                       (GLsizei)rect.height, 0);
    gl_mesh_unbind(renderer);
    gl->Viewport(rect.x, bottom, (GLsizei)rect.width, (GLsizei)rect.height);
    gl->Scissor(rect.x, bottom, (GLsizei)rect.width, (GLsizei)rect.height);
    gl->Enable(GL_SCISSOR_TEST);
    gl->Disable(GL_DEPTH_TEST);
    gl->Disable(GL_CULL_FACE);
    gl->Disable(GL_STENCIL_TEST);
    gl->Disable(GL_POLYGON_OFFSET_FILL);
    gl->Enable(GL_BLEND);
    gl->BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl->DepthMask(GL_FALSE);
    gl->ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl->PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    gl->Color4f(1, 1, 1, 1);
    for (unsigned pass = 0; pass < 3; ++pass) {
        if ((pass == 0 && fog->density <= 0) ||
            (pass == 1 &&
             (fog->height_density <= 0 || fog->height_falloff <= 0)) ||
            (pass == 2 && (fog->sky_factor <= 0 || !fog->sky_drawn)))
            continue;
        if (!gl_program_fog(renderer, pass, fog, view, error)) {
            composite_state_restore(renderer,&retained);
            return false;
        }
        gl_draw_quad(renderer);
    }
    gl->UseProgram(0);
    composite_state_restore(renderer,&retained);
    return gl_check(renderer, "OpenGL Q2 fog pass", error);
}

static bool surface_name(qa_gl_renderer *renderer,GLuint *name,unsigned kind,qa_error *error)
{
    gl_api *gl=&renderer->gl;
    if (!*name) {
        if (!kind) gl->GenFramebuffers(1,name);
        else if (kind==1) gl->GenTextures(1,name);
        else gl->GenRenderbuffers(1,name);
    }
    if (!gl_check(renderer,"Allocating retained surface target storage",error)) return false;
    if (*name) return true;
    qa_error_set(error,QA_ERROR_MEMORY,0,"OpenGL could not allocate retained surface target storage");
    return false;
}

static bool surface_color(qa_gl_renderer *renderer,unsigned slot,uint32_t width,uint32_t height,qa_error *error)
{
    gl_output_target *output=&renderer->output;
    gl_api *gl=&renderer->gl;
    if (!output->color_ready[slot]) {
        if (!surface_name(renderer,output->color+slot,1,error)) return false;
        GLint color=renderer->capabilities.alpha_bits?GL_RGBA8:renderer->capabilities.color_bits<=16?GL_RGB565:GL_RGB8;
        texture_storage(renderer,output->color[slot],color,width,height,GL_RGBA,GL_UNSIGNED_BYTE);
        if (!gl_check(renderer,"Allocating candidate output color samples",error)) return false;
    }
    gl->BindFramebuffer(GL_FRAMEBUFFER,output->framebuffer);
    gl->FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output->color[slot],0);
    attach_depth(renderer,output->framebuffer,output->depth_stencil);
    gl->DrawBuffer(GL_COLOR_ATTACHMENT0); gl->ReadBuffer(GL_COLOR_ATTACHMENT0);
    if (!framebuffer_complete(renderer,"Candidate output framebuffer",error) ||
        !gl_check(renderer,"Preparing candidate output attachment",error)) return false;
    output->color_ready[slot]=true; output->dirty[slot]=true;
    return true;
}

/* Source attachments remain untouched: the ticket owns its separate reader. */
bool gl_surface_targets_prepare(qa_gl_renderer *renderer,const qa_gl_renderer *source,float gamma,
    bool replace_output,bool replace_opacity,GLuint *reader,qa_error *error)
{
    uint32_t width=0,height=0;
    if (!reader || !gl_dimensions(renderer,&width,&height,error)) return false;
    if (width>renderer->capabilities.maximum_texture_size || height>renderer->capabilities.maximum_texture_size) {
        qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"Candidate surface targets exceed actual texture limits"); return false;
    }
    gl_api *gl=&renderer->gl;
    gl->Disable(GL_SCISSOR_TEST);
    if (replace_opacity && source->opacity.allocated) {
        gl_opacity_target *opacity=&renderer->opacity;
        for (size_t i=0;i<2;++i)
            if (!surface_name(renderer,opacity->framebuffer+i,0,error) ||
                !surface_name(renderer,opacity->color+i,1,error)) return false;
        if (!surface_name(renderer,&opacity->depth_stencil,2,error)) return false;
        opacity->allocated=true;
        if (!opacity_allocate(renderer,width,height,error)) return false;
    }
    if (!replace_output) return true;
    gl_output_target *output=&renderer->output;
    output->enabled=gamma!=1; renderer->gamma=gamma;
    if (output->enabled) {
        if (!surface_name(renderer,&output->framebuffer,0,error) ||
            !surface_name(renderer,&output->depth_stencil,2,error) ||
            !surface_name(renderer,&output->table,1,error)) return false;
        gl->BindRenderbuffer(GL_RENDERBUFFER,output->depth_stencil);
        gl->RenderbufferStorage(GL_RENDERBUFFER,depth_internal(renderer),(GLsizei)width,(GLsizei)height);
        output->width=width; output->height=height;
        uint8_t table[256]; gl_gamma_table(gamma,table);
        gl->ActiveTexture(GL_TEXTURE1); gl->BindTexture(GL_TEXTURE_2D,output->table);
        gl->BindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
        gl->PixelStorei(GL_UNPACK_ALIGNMENT,1); gl->PixelStorei(GL_UNPACK_ROW_LENGTH,0);
        gl->PixelStorei(GL_UNPACK_SKIP_ROWS,0); gl->PixelStorei(GL_UNPACK_SKIP_PIXELS,0);
        gl->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        gl->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        gl->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        gl->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        gl->TexImage2D(GL_TEXTURE_2D,0,GL_LUMINANCE8,256,1,0,GL_LUMINANCE,GL_UNSIGNED_BYTE,table);
        if (!gl_check(renderer,"Preparing candidate gamma and depth storage",error)) return false;
    }
    for (unsigned slot=0;slot<GL_DRAW_BUFFER_COUNT_QA;++slot) {
        bool raw=source->output.enabled && source->output.color_ready[slot];
        if (!raw && (!output->enabled || slot!=gl_draw_buffer_index(source->draw_buffer))) continue;
        qa_scene_draw_buffer buffer=slot==0?QA_DRAW_FRONT:slot==1?QA_DRAW_BACK:
            slot==2?QA_DRAW_BACK_LEFT:QA_DRAW_BACK_RIGHT;
        if (output->enabled && !surface_color(renderer,slot,width,height,error)) return false;
        if (raw) {
            if (!surface_name(renderer,reader,0,error)) return false;
            gl->BindFramebuffer(GL_READ_FRAMEBUFFER,*reader);
            gl->FramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,source->output.color[slot],0);
            gl->FramebufferRenderbuffer(GL_READ_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,source->output.depth_stencil);
            if (source->capabilities.stencil_bits)
                gl->FramebufferRenderbuffer(GL_READ_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,GL_RENDERBUFFER,source->output.depth_stencil);
            gl->ReadBuffer(GL_COLOR_ATTACHMENT0);
            if (gl->CheckFramebufferStatus(GL_READ_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) {
                qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"Retained raw output reader is incomplete"); return false;
            }
        } else {
            gl->BindFramebuffer(GL_READ_FRAMEBUFFER,0); gl->ReadBuffer(gl_draw_buffer_name(buffer));
        }
        gl->BindFramebuffer(GL_DRAW_FRAMEBUFFER,output->enabled?output->framebuffer:0);
        gl->DrawBuffer(output->enabled?GL_COLOR_ATTACHMENT0:gl_draw_buffer_name(buffer));
        uint32_t source_width=raw?source->output.width:width,source_height=raw?source->output.height:height;
        gl->BlitFramebuffer(0,0,(GLint)source_width,(GLint)source_height,0,0,(GLint)width,(GLint)height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        uint32_t overlap_width=width<source_width?width:source_width,overlap_height=height<source_height?height:source_height;
        gl->BlitFramebuffer(0,0,(GLint)overlap_width,(GLint)overlap_height,0,0,(GLint)overlap_width,(GLint)overlap_height,
            depth_mask(renderer),GL_NEAREST);
        if (!gl_check(renderer,"Preparing retained linear output samples",error)) return false;
    }
    return !output->enabled || gl_output_resolve(renderer,error);
}

static bool surface_delete(qa_gl_renderer *renderer,GLuint *name,unsigned kind,qa_error *error)
{
    if (!*name) return true;
    if (!gl_check(renderer,"Preparing checked surface target retirement",error)) return false;
    if (!kind) renderer->gl.DeleteFramebuffers(1,name);
    else if (kind==1) renderer->gl.DeleteTextures(1,name);
    else renderer->gl.DeleteRenderbuffers(1,name);
    if (!gl_check(renderer,"Retiring retained surface target storage",error)) return false;
    *name=0; return true;
}

bool gl_surface_targets_delete(qa_gl_renderer *renderer,bool output,bool opacity,qa_error *error)
{
    if (output) {
        gl_output_target *target=&renderer->output;
        for (size_t i=0;i<GL_DRAW_BUFFER_COUNT_QA;++i)
            if (!surface_delete(renderer,target->color+i,1,error)) return false;
        if (!surface_delete(renderer,&target->table,1,error) ||
            !surface_delete(renderer,&target->depth_stencil,2,error) ||
            !surface_delete(renderer,&target->framebuffer,0,error)) return false;
        memset(target,0,sizeof(*target));
    }
    if (opacity) {
        gl_opacity_target *target=&renderer->opacity;
        for (size_t i=0;i<2;++i)
            if (!surface_delete(renderer,target->color+i,1,error) ||
                !surface_delete(renderer,target->framebuffer+i,0,error)) return false;
        if (!surface_delete(renderer,&target->depth_stencil,2,error)) return false;
        memset(target,0,sizeof(*target));
    }
    return true;
}
