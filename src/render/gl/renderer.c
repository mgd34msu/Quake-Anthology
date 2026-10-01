#include "internal.h"
#include "../save_fields.h"
#include "qa/render_gl_save.h"

#include <limits.h>
#include <stdio.h>

static bool finite3(qa_vec3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

static bool finite4(qa_scene_vec4 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z) &&
           isfinite(value.w);
}

static void copy_gl_string(char *destination, size_t capacity,
                           const GLubyte *value)
{
    snprintf(destination, capacity, "%s",
             value == NULL ? "unknown" : (const char *)value);
}

void qa_gl_options_default(qa_gl_options *options)
{
    if (options != NULL) *options = (qa_gl_options){0};
}

static bool capabilities(qa_gl_renderer *renderer, qa_error *error)
{
    gl_api *gl = &renderer->gl;
    qa_gl_capabilities *caps = &renderer->capabilities;
    copy_gl_string(caps->vendor, sizeof(caps->vendor),
                   gl->GetString(GL_VENDOR));
    copy_gl_string(caps->renderer, sizeof(caps->renderer),
                   gl->GetString(GL_RENDERER));
    copy_gl_string(caps->version, sizeof(caps->version),
                   gl->GetString(GL_VERSION));
    copy_gl_string(caps->shading_language, sizeof(caps->shading_language),
                   gl->GetString(GL_SHADING_LANGUAGE_VERSION));
    GLint red = 0, green = 0, blue = 0, alpha = 0, depth = 0, stencil = 0;
    GLint maximum = 0, units = 0, attributes = 0, stereo = 0;
    gl->GetIntegerv(GL_RED_BITS, &red);
    gl->GetIntegerv(GL_GREEN_BITS, &green);
    gl->GetIntegerv(GL_BLUE_BITS, &blue);
    gl->GetIntegerv(GL_ALPHA_BITS, &alpha);
    gl->GetIntegerv(GL_DEPTH_BITS, &depth);
    gl->GetIntegerv(GL_STENCIL_BITS, &stencil);
    gl->GetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
    gl->GetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
    gl->GetIntegerv(GL_MAX_VERTEX_ATTRIBS, &attributes);
    gl->GetIntegerv(GL_STEREO, &stereo);
    GLint depth_component = 0;
    gl->GetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER, GL_DEPTH, GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE,
        &depth_component);
    if (red < 0 || green < 0 || blue < 0 || alpha < 0 || depth < 1 ||
        stencil < 0 || maximum < 1 || units < 3 || attributes < 5) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0,
                     "OpenGL requires depth, three texture units, and five vertex attributes");
        return false;
    }
    caps->color_bits = (unsigned)(red + green + blue);
    caps->alpha_bits = (unsigned)alpha;
    caps->depth_bits = (unsigned)depth;
    caps->stencil_bits = (unsigned)stencil;
    caps->maximum_texture_size = (uint32_t)maximum;
    caps->texture_units = (uint32_t)units;
    caps->vertex_attributes = (uint32_t)attributes;
    caps->stereo = stereo != 0;
    caps->floating_depth = depth_component == GL_FLOAT;
    return gl_check(renderer, "OpenGL capability query", error);
}

qa_gl_renderer *qa_gl_create(const qa_gl_options *input, qa_error *error)
{
    qa_gl_options defaults;
    qa_gl_options_default(&defaults);
    const qa_gl_options *options = input == NULL ? &defaults : input;
    qa_display_info info = {0};
    if (options->display == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "OpenGL renderer requires an OpenGL display");
        return NULL;
    }
    if (!qa_display_info_get(options->display, &info, error)) return NULL;
    if (info.backend != QA_DISPLAY_OPENGL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "OpenGL renderer requires an OpenGL display");
        return NULL;
    }
    qa_gl_renderer *renderer = calloc(1, sizeof(*renderer));
    if (renderer == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating OpenGL renderer");
        return NULL;
    }
    renderer->options = *options;
    renderer->draw_buffer = QA_DRAW_BACK;
    renderer->gamma = 1;
    renderer->view.viewport = (qa_scene_rect){0, 0, info.drawable_width,
                                              info.drawable_height};
    renderer->view.depth = 1;
    if (!gl_api_load(renderer, error)) {
        free(renderer);
        return NULL;
    }
    while (renderer->gl.GetError() != GL_NO_ERROR) {}
    if (!capabilities(renderer, error) ||
        !gl_programs_create(renderer, error) ||
        !gl_resources_create(renderer, error)) {
        gl_resources_destroy(renderer);
        gl_programs_destroy(renderer);
        free(renderer);
        return NULL;
    }
    renderer->gl.FrontFace(GL_CW);
    renderer->gl.Disable(GL_BLEND);
    renderer->gl.Disable(GL_CULL_FACE);
    renderer->gl.Disable(GL_STENCIL_TEST);
    renderer->gl.Disable(GL_POLYGON_OFFSET_FILL);
    renderer->gl.Enable(GL_DEPTH_TEST);
    renderer->gl.DepthMask(GL_TRUE);
    renderer->gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    renderer->gl.ClearStencil(0);
    renderer->gl.StencilMask(UINT_MAX);
    renderer->gl.PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    renderer->gl.LineWidth(1);
    if (!gl_bind_destination(renderer, error) ||
        !gl_check(renderer, "OpenGL renderer initialization", error)) {
        qa_gl_destroy(renderer);
        return NULL;
    }
    return renderer;
}

void qa_gl_destroy(qa_gl_renderer *renderer)
{
    if (renderer == NULL || renderer->closed) return;
    renderer->closed = true;
    qa_error ignored = {0};
    if (!renderer->gl.DeleteTextures || !qa_display_make_current(renderer->options.display, &ignored)) {
        /* The context is unavailable, so native objects retire with it. */
        if (renderer->restore) {
            gl_api native=renderer->gl; renderer->gl.DeleteTextures=NULL;
            gl_restore_storage_destroy(renderer); renderer->gl=native;
        }
        qa_scene_image_release(renderer->target);
        for (size_t unit = 0; unit < 2; ++unit)
            qa_scene_image_release(renderer->bound[unit]);
        while (renderer->textures != NULL) {
            gl_texture_entry *entry = renderer->textures;
            renderer->textures = entry->next;
            qa_scene_image_release(entry->image);
            free(entry);
        }
        while (renderer->meshes != NULL) {
            gl_mesh_entry *entry = renderer->meshes;
            renderer->meshes = entry->next;
            qa_scene_geometry_cache_release(entry->geometry);
            free(entry);
        }
        free(renderer);
        return;
    }
    gl_restore_storage_destroy(renderer);
    if (!renderer->detached) gl_mesh_unbind(renderer);
    gl_opacity_destroy(renderer);
    gl_output_destroy(renderer);
    if (renderer->target_framebuffer != 0)
        renderer->gl.DeleteFramebuffers(1, &renderer->target_framebuffer);
    if (renderer->fog_depth != 0)
        renderer->gl.DeleteTextures(1, &renderer->fog_depth);
    gl_resources_destroy(renderer);
    gl_programs_destroy(renderer);
    free(renderer);
}

const qa_gl_capabilities *qa_gl_capabilities_get(const qa_gl_renderer *renderer)
{
    return renderer == NULL ? NULL : &renderer->capabilities;
}

static bool view_dimensions(qa_gl_renderer *renderer, uint32_t *width,
                            uint32_t *height, qa_error *error)
{
    if (renderer->target == NULL)
        return gl_dimensions(renderer, width, height, error);
    const qa_scene_image_level *level = &renderer->target->levels[0];
    *width = level->width;
    *height = level->height;
    return true;
}

static bool begin_view(qa_gl_renderer *renderer, const qa_scene_view *view,
                       qa_error *error)
{
    if (view->viewport.width == 0 || view->viewport.height == 0 ||
        view->viewport.width > INT_MAX || view->viewport.height > INT_MAX ||
        !isfinite(view->depth) ||
        (view->clear_color && !finite4(view->color)) ||
        (view->clip_enabled &&
         (!finite3(view->clip_plane.normal) ||
          !isfinite(view->clip_plane.distance)))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid OpenGL view");
        return false;
    }
    uint32_t width = 0, height = 0;
    if (!view_dimensions(renderer, &width, &height, error) ||
        !gl_bind_destination(renderer, error)) return false;
    int64_t bottom = (int64_t)height - view->viewport.y -
                     view->viewport.height;
    if (bottom < INT_MIN || bottom > INT_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "OpenGL viewport exceeds signed coordinates");
        return false;
    }
    renderer->view = *view;
    if (renderer->opacity.skip) return true;
    gl_api *gl = &renderer->gl;
    gl->Viewport(view->viewport.x, (GLint)bottom,
                 (GLsizei)view->viewport.width,
                 (GLsizei)view->viewport.height);
    gl->Enable(GL_SCISSOR_TEST);
    gl->Scissor(view->viewport.x, (GLint)bottom,
                (GLsizei)view->viewport.width,
                (GLsizei)view->viewport.height);
    GLbitfield clear = 0;
    if (view->clear_color) {
        gl->ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        gl->ClearColor(view->color.x, view->color.y, view->color.z,
                       view->color.w);
        clear |= GL_COLOR_BUFFER_BIT;
    }
    if (view->clear_depth) {
        gl->DepthMask(GL_TRUE);
        gl->ClearDepth(view->depth);
        clear |= GL_DEPTH_BUFFER_BIT;
    }
    if (view->clear_stencil) {
        if (renderer->target != NULL || renderer->capabilities.stencil_bits == 0) {
            qa_error_set(error, QA_ERROR_UNSUPPORTED, 0,
                         "OpenGL view requested unavailable stencil storage");
            return false;
        }
        gl->StencilMask(UINT_MAX);
        gl->ClearStencil(0);
        clear |= GL_STENCIL_BUFFER_BIT;
    }
    if (clear != 0) gl->Clear(clear);
    return gl_check(renderer, "OpenGL view begin", error);
}

static GLenum blend_factor(qa_scene_blend factor)
{
    static const GLenum names[] = {
        GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR,
        GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA,
        GL_ONE_MINUS_DST_ALPHA, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR,
        GL_SRC_ALPHA_SATURATE
    };
    return names[factor];
}

static GLenum stencil_operation(qa_scene_stencil_op operation)
{
    static const GLenum names[] = {GL_KEEP, GL_ZERO, GL_REPLACE, GL_INCR,
                                   GL_DECR, GL_INVERT};
    return names[operation];
}

static void draw_state(qa_gl_renderer *renderer, const qa_scene_state *state,
                       qa_scene_primitive primitive)
{
    gl_api *gl = &renderer->gl;
    gl->Enable(GL_DEPTH_TEST);
    gl->DepthFunc(state->depth_test == QA_DEPTH_ALWAYS ? GL_ALWAYS :
                  state->depth_test == QA_DEPTH_LEQUAL ? GL_LEQUAL :
                  state->depth_test == QA_DEPTH_EQUAL ? GL_EQUAL : GL_LESS);
    gl->DepthMask(state->depth_write ? GL_TRUE : GL_FALSE);
    gl->ColorMask(state->color_write ? GL_TRUE : GL_FALSE,
                  state->color_write ? GL_TRUE : GL_FALSE,
                  state->color_write ? GL_TRUE : GL_FALSE,
                  state->color_write ? GL_TRUE : GL_FALSE);
    if (state->blend_source == QA_BLEND_ONE &&
        state->blend_destination == QA_BLEND_ZERO) gl->Disable(GL_BLEND);
    else {
        gl->Enable(GL_BLEND);
        gl->BlendFunc(blend_factor(state->blend_source),
                      blend_factor(state->blend_destination));
    }
    if (state->cull == QA_CULL_NONE) gl->Disable(GL_CULL_FACE);
    else {
        gl->Enable(GL_CULL_FACE);
        gl->CullFace(state->cull == QA_CULL_FRONT ? GL_FRONT : GL_BACK);
    }
    gl->DepthRange(state->depth_near, state->depth_far);
    if (state->polygon_offset) {
        gl->Enable(GL_POLYGON_OFFSET_FILL);
        gl->PolygonOffset(state->offset_factor, state->offset_units);
    } else gl->Disable(GL_POLYGON_OFFSET_FILL);
    gl->PolygonMode(GL_FRONT_AND_BACK, state->wireframe ? GL_LINE : GL_FILL);
    gl->LineWidth(primitive == QA_SCENE_LINES || state->wireframe
                      ? state->line_width : 1);
    if (renderer->overdraw) {
        gl->Enable(GL_STENCIL_TEST);
        gl->StencilMask(UINT_MAX);
        gl->StencilFunc(GL_ALWAYS, 0, UINT_MAX);
        gl->StencilOp(GL_KEEP, GL_INCR, GL_INCR);
    } else if (state->stencil_enabled) {
        gl->Enable(GL_STENCIL_TEST);
        GLenum test = state->stencil_test == QA_STENCIL_ALWAYS ? GL_ALWAYS :
                      state->stencil_test == QA_STENCIL_EQUAL ? GL_EQUAL :
                                                                GL_NOTEQUAL;
        gl->StencilMask(state->stencil_write_mask);
        gl->StencilFunc(test, (GLint)state->stencil_reference,
                        state->stencil_compare_mask);
        gl->StencilOp(stencil_operation(state->stencil_fail),
                      stencil_operation(state->stencil_depth_fail),
                      stencil_operation(state->stencil_depth_pass));
    } else gl->Disable(GL_STENCIL_TEST);
}

static bool mesh_resident(const qa_gl_renderer *renderer,
                          const qa_scene_mesh *mesh)
{
    if (mesh->identity == 0) return false;
    for (const gl_mesh_entry *entry = renderer->meshes; entry;
         entry = entry->next)
        if (entry->identity == mesh->identity &&
            entry->revision == mesh->revision &&
            entry->geometry == mesh->geometry &&
            entry->vertex_count == mesh->vertex_count &&
            entry->index_count == mesh->index_count) return true;
    return false;
}

static bool draw_valid(const qa_gl_renderer *renderer,
                       const qa_scene_draw *draw, qa_error *error)
{
    const qa_scene_state *state = &draw->state;
    bool stencil_shadow = state->stencil_fail == QA_STENCIL_INCREMENT ||
                          state->stencil_fail == QA_STENCIL_DECREMENT ||
                          state->stencil_depth_fail == QA_STENCIL_INCREMENT ||
                          state->stencil_depth_fail == QA_STENCIL_DECREMENT ||
                          state->stencil_depth_pass == QA_STENCIL_INCREMENT ||
                          state->stencil_depth_pass == QA_STENCIL_DECREMENT;
    if (draw->texture_count > 2 ||
        (unsigned)draw->environment > QA_TEXTURE_REPLACE ||
        (unsigned)draw->lighting > QA_LIGHT_Q2_MODEL_SHADOW ||
        (unsigned)draw->light_pass > QA_LIGHT_PASS_MODEL ||
        (unsigned)draw->mesh.primitive > QA_SCENE_LINES ||
        (unsigned)state->blend_source > QA_BLEND_SRC_ALPHA_SATURATE ||
        (unsigned)state->blend_destination > QA_BLEND_SRC_ALPHA_SATURATE ||
        state->blend_destination == QA_BLEND_SRC_ALPHA_SATURATE ||
        (unsigned)state->depth_test > QA_DEPTH_LESS ||
        (unsigned)state->alpha_test > QA_ALPHA_GE128 ||
        (unsigned)state->cull > QA_CULL_BACK ||
        (unsigned)state->stencil_test > QA_STENCIL_NOTEQUAL ||
        (unsigned)state->stencil_fail > QA_STENCIL_INVERT ||
        (unsigned)state->stencil_depth_fail > QA_STENCIL_INVERT ||
        (unsigned)state->stencil_depth_pass > QA_STENCIL_INVERT ||
        !isfinite(state->depth_near) || !isfinite(state->depth_far) ||
        !isfinite(state->offset_factor) || !isfinite(state->offset_units) ||
        !isfinite(state->line_width) ||
        ((draw->mesh.primitive == QA_SCENE_LINES || state->wireframe) &&
         state->line_width <= 0) ||
        (unsigned)draw->fog.kind > QA_FOG_Q2 ||
        (unsigned)draw->fog.effect > QA_FOG_NO_EFFECT ||
        !finite3(draw->fog.color) || !isfinite(draw->fog.density) ||
        !isfinite(draw->fog.amount) || !isfinite(draw->shade_scale) ||
        !isfinite(draw->shadow_near) ||
        (draw->mesh.identity != 0 && draw->mesh.geometry == NULL) ||
        (draw->mesh.vertex_count != 0 && draw->mesh.vertices == NULL) ||
        (draw->mesh.index_count != 0 && draw->mesh.indices == NULL) ||
        (draw->light_count != 0 && draw->lights == NULL) ||
        draw->light_count > GL_MAX_LIGHTS_QA ||
        (state->stencil_enabled && renderer->capabilities.stencil_bits == 0) ||
        (state->stencil_enabled && stencil_shadow &&
         renderer->capabilities.stencil_bits < 4) ||
        (renderer->overdraw && renderer->capabilities.stencil_bits == 0)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "Invalid OpenGL draw state or storage");
        return false;
    }
    for (size_t i = 0; i < 16; ++i)
        if (!isfinite(draw->model.m[i]) || !isfinite(draw->mvp.m[i])) {
            qa_error_set(error, QA_ERROR_ARGUMENT, i,
                         "OpenGL draw matrix contains a nonfinite value");
            return false;
        }
    size_t primitive = draw->mesh.primitive == QA_SCENE_LINES ? 2 : 3;
    if (draw->mesh.index_count % primitive != 0 ||
        draw->mesh.index_count > INT_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "OpenGL primitive index count is incomplete or too large");
        return false;
    }
    if (!mesh_resident(renderer, &draw->mesh)) {
        for (size_t i = 0; i < draw->mesh.vertex_count; ++i) {
            const qa_scene_vertex *vertex = &draw->mesh.vertices[i];
            if (!finite3(vertex->position) || !finite3(vertex->normal) ||
                !finite4(vertex->color) || !isfinite(vertex->texcoord.x) ||
                !isfinite(vertex->texcoord.y) ||
                !isfinite(vertex->lightmap.x) ||
                !isfinite(vertex->lightmap.y)) {
                qa_error_set(error, QA_ERROR_ARGUMENT, i,
                             "OpenGL mesh contains a nonfinite vertex");
                return false;
            }
        }
        for (size_t i = 0; i < draw->mesh.index_count; ++i)
            if (draw->mesh.indices[i] >= draw->mesh.vertex_count) {
                qa_error_set(error, QA_ERROR_ARGUMENT, i,
                             "OpenGL mesh index is outside vertex storage");
                return false;
            }
    }
    if ((draw->lighting == QA_LIGHT_Q2_MODEL_SHADOW ||
         draw->model_shade_scale) && draw->shadow_atlas == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "OpenGL model shadows require a depth atlas");
        return false;
    }
    for (size_t i = 0; i < draw->light_count; ++i) {
        const qa_scene_shadow_light *shadow = &draw->lights[i];
        const qa_scene_light *light = &shadow->light;
        if (!finite3(light->origin) || !finite3(light->direction) ||
            !finite3(light->color) || !finite3(shadow->model_fraction) ||
            !isfinite(light->radius) || light->radius < 0 ||
            !isfinite(light->scale) || !isfinite(light->cos_half_angle) ||
            (shadow->shadow_valid &&
             (draw->shadow_atlas == NULL || !finite4(shadow->atlas_rect) ||
              shadow->atlas_rect.z <= 0 || shadow->atlas_rect.w <= 0 ||
              (shadow->point_shadow && draw->shadow_near <= 0)))) {
            qa_error_set(error, QA_ERROR_ARGUMENT, i,
                         "Invalid OpenGL fragment light");
            return false;
        }
        if (shadow->shadow_valid)
            for (size_t j = 0; j < 16; ++j)
                if (!isfinite(shadow->shadow_matrix.m[j])) {
                    qa_error_set(error, QA_ERROR_ARGUMENT, i,
                                 "OpenGL shadow matrix is nonfinite");
                    return false;
                }
    }
    return true;
}

static void retain_binding(qa_gl_renderer *renderer, size_t unit,
                           const qa_scene_image *image)
{
    if (image == NULL || renderer->bound[unit] == image) return;
    qa_scene_image_retain(image);
    qa_scene_image_release(renderer->bound[unit]);
    renderer->bound[unit] = image;
}

static bool draw_scene(qa_gl_renderer *renderer, const qa_scene_draw *source,
                       qa_error *error)
{
    if (!draw_valid(renderer, source, error)) return false;
    qa_scene_draw draw = *source;
    gl_texture_entry *textures[2] = {NULL, NULL}, *shadow = NULL;
    for (size_t unit = 0; unit < draw.texture_count; ++unit) {
        if (draw.retain_texture[unit]) draw.textures[unit] = renderer->bound[unit];
        if (draw.textures[unit] != NULL &&
            !gl_texture_get(renderer, draw.textures[unit], &textures[unit], error))
            return false;
    }
    if (draw.shadow_atlas != NULL) {
        if (draw.shadow_atlas->kind != QA_SCENE_DEPTH32F ||
            !gl_texture_get(renderer, draw.shadow_atlas, &shadow, error)) {
            if (draw.shadow_atlas->kind != QA_SCENE_DEPTH32F)
                qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                             "OpenGL shadow atlas is not depth32f");
            return false;
        }
    }
    for (size_t unit = 0; unit < draw.texture_count; ++unit)
        if (!source->retain_texture[unit] && source->textures[unit] != NULL)
            retain_binding(renderer, unit, source->textures[unit]);
    if (!gl_bind_destination(renderer, error) ||
        !gl_program_stage(renderer, &draw, error)) return false;
    for (unsigned unit = 0; unit < 2; ++unit) {
        renderer->gl.ActiveTexture(GL_TEXTURE0 + unit);
        renderer->gl.BindTexture(GL_TEXTURE_2D,
            unit < draw.texture_count && textures[unit] != NULL
                ? textures[unit]->name : renderer->white_texture);
    }
    renderer->gl.ActiveTexture(GL_TEXTURE2);
    renderer->gl.BindTexture(GL_TEXTURE_2D,
                             shadow == NULL ? renderer->white_texture :
                                              shadow->name);
    renderer->gl.ActiveTexture(GL_TEXTURE0);
    draw_state(renderer, &draw.state, draw.mesh.primitive);
    if (!gl_mesh_bind(renderer, &draw.mesh, error)) return false;
    if (draw.mesh.index_count != 0)
        renderer->gl.DrawElements(draw.mesh.primitive == QA_SCENE_LINES
                                      ? GL_LINES : GL_TRIANGLES,
                                  (GLsizei)draw.mesh.index_count,
                                  GL_UNSIGNED_INT, NULL);
    gl_mesh_unbind(renderer);
    return gl_check(renderer, "OpenGL scene draw", error);
}

static bool select_draw_buffer(qa_gl_renderer *renderer,
                               qa_scene_draw_buffer buffer, bool clear,
                               qa_error *error)
{
    if ((unsigned)buffer > QA_DRAW_BACK_RIGHT ||
        (buffer == QA_DRAW_BACK_RIGHT && !renderer->capabilities.stereo)) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0,
                     "OpenGL draw buffer is unavailable");
        return false;
    }
    renderer->draw_buffer = buffer;
    if (!gl_bind_destination(renderer, error)) return false;
    if (clear && !renderer->opacity.skip) {
        renderer->gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        renderer->gl.DepthMask(GL_TRUE);
        renderer->gl.ClearColor(1, 0, 0.5f, 1);
        renderer->gl.ClearDepth(1);
        GLbitfield mask = GL_DEPTH_BUFFER_BIT;
        if (renderer->target == NULL) mask |= GL_COLOR_BUFFER_BIT;
        renderer->gl.Clear(mask);
    }
    return gl_check(renderer, "OpenGL draw-buffer selection", error);
}

static bool gl_execute(qa_gl_renderer *renderer, const qa_scene_frame *frame,
                   qa_error *error)
{
    if (renderer == NULL || renderer->closed || frame == NULL ||
        frame->owner != renderer->options.owner ||
        (frame->command_count != 0 && frame->commands == NULL) ||
        renderer->opacity.active ||
        !qa_display_make_current(renderer->options.display, error)) {
        if (renderer == NULL || renderer->closed || frame == NULL ||
            (renderer != NULL && frame != NULL &&
             frame->owner != renderer->options.owner) ||
            (frame != NULL && frame->command_count != 0 &&
             frame->commands == NULL) ||
            (renderer != NULL && renderer->opacity.active))
            qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                         "Invalid OpenGL frame or renderer owner");
        return false;
    }
    renderer->presented = false;
    gl_textures_prune(renderer);
    gl_meshes_prune(renderer);
    renderer->sequence = frame->sequence;
    for (size_t i = 0; i < frame->command_count; ++i) {
        const qa_scene_command *command = &frame->commands[i];
        if (renderer->opacity.skip &&
            command->kind != QA_SCENE_COMMAND_OPACITY_BEGIN &&
            command->kind != QA_SCENE_COMMAND_OPACITY_END) continue;
        bool ok;
        switch (command->kind) {
        case QA_SCENE_COMMAND_VIEW:
            ok = begin_view(renderer, &command->data.view, error);
            break;
        case QA_SCENE_COMMAND_DRAW:
            ok = renderer->opacity.skip ||
                 draw_scene(renderer, &command->data.draw, error);
            break;
        case QA_SCENE_COMMAND_TARGET:
            ok = gl_select_target(renderer, command->data.target.image, error);
            break;
        case QA_SCENE_COMMAND_OPACITY_BEGIN:
            ok = gl_opacity_begin(renderer, command->data.opacity.value, error);
            break;
        case QA_SCENE_COMMAND_OPACITY_END:
            ok = gl_opacity_end(renderer, error);
            break;
        case QA_SCENE_COMMAND_FOG:
            ok = renderer->opacity.skip ||
                 gl_depth_fog(renderer, &command->data.fog.fog,
                              &command->data.fog.view, error);
            break;
        case QA_SCENE_COMMAND_DRAW_BUFFER:
            ok = select_draw_buffer(renderer, command->data.draw_buffer.buffer,
                                    command->data.draw_buffer.clear, error);
            break;
        case QA_SCENE_COMMAND_SWAP:
            ok = (!renderer->opacity.active || renderer->opacity.value == 1) &&
                 renderer->target == NULL &&
                 gl_output_resolve(renderer, error) &&
                 gl_dimensions(renderer, &renderer->presented_width, &renderer->presented_height, error) &&
                 qa_display_swap(renderer->options.display, error);
            if (ok) renderer->presented = true;
            if (!ok && renderer->opacity.active &&
                renderer->opacity.value != 1)
                qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                             "Cannot swap inside an OpenGL opacity scope");
            else if (!ok && renderer->target != NULL)
                qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                             "Cannot swap an OpenGL depth target");
            break;
        case QA_SCENE_COMMAND_IMAGE:
            ok = gl_image_update(renderer, command->data.image, error);
            break;
        default:
            qa_error_set(error, QA_ERROR_ARGUMENT, i,
                         "Unknown OpenGL scene command");
            ok = false;
            break;
        }
        if (!ok) {
            gl_opacity_abort(renderer);
            if (error != NULL) error->offset = i;
            return false;
        }
    }
    if (renderer->opacity.active) {
        gl_opacity_abort(renderer);
        qa_error_set(error, QA_ERROR_ARGUMENT, frame->command_count,
                     "OpenGL frame ended inside an opacity scope");
        return false;
    }
    return true;
}
bool qa_gl_execute(qa_gl_renderer *renderer,const qa_scene_frame *frame,qa_error *error)
{
    if (!renderer || renderer->detached || renderer->executing || renderer->capturing || renderer->preparing) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"OpenGL renderer is absent or executing"); return false;
    }
    renderer->executing=true; bool ok=gl_execute(renderer,frame,error);
    renderer->executing=false; return ok;
}
bool qa_gl_checkpoint_resources(const qa_gl_renderer *renderer,qa_render_resource_visit_fn visit,void *context,qa_error *error)
{
    if (!renderer || !visit || renderer->closed || renderer->executing || renderer->capturing || renderer->preparing || renderer->opacity.active) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"OpenGL resource observation requires its idle actual renderer owner"); return false;
    }
    size_t ordinal=0;
    for (const gl_texture_entry *entry=renderer->textures;entry;entry=entry->next,++ordinal)
        if (!visit(context,entry->image,NULL,ordinal,error)) return false;
    for (size_t i=0;i<2;++i,++ordinal)
        if (renderer->bound[i] && !visit(context,renderer->bound[i],NULL,ordinal,error)) return false;
    if (renderer->target && !visit(context,renderer->target,NULL,ordinal,error)) return false;
    ordinal=0;
    for (const gl_mesh_entry *entry=renderer->meshes;entry;entry=entry->next,++ordinal)
        if (!entry->geometry || (qa_scene_geometry_active(entry->geometry) &&
            !visit(context,NULL,entry->geometry,ordinal,error))) return false;
    return true;
}
bool qa_gl_checkpoint_meshes(const qa_gl_renderer *renderer,qa_gl_mesh_visit_fn visit,void *context,qa_error *error)
{
    if (!renderer || !visit || renderer->closed || renderer->executing || renderer->capturing || renderer->preparing || renderer->opacity.active) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"OpenGL mesh observation requires its idle actual cache owner"); return false;
    }
    size_t ordinal=0;
    for (const gl_mesh_entry *entry=renderer->meshes;entry;entry=entry->next,++ordinal) {
        if (!entry->geometry) { qa_error_set(error,QA_ERROR_FORMAT,0,"OpenGL cache row has no retained geometry descriptor"); return false; }
        if (qa_scene_geometry_active(entry->geometry) &&
            !visit(context,entry->identity,entry->revision,entry->geometry,ordinal,error)) return false;
    }
    return true;
}

bool qa_gl_finish(qa_gl_renderer *renderer, qa_error *error)
{
    if (renderer == NULL || renderer->closed || renderer->detached ||
        !qa_display_make_current(renderer->options.display, error) ||
        !gl_output_resolve(renderer, error)) {
        if (renderer == NULL || renderer->closed)
            qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                         "Invalid OpenGL renderer finish");
        return false;
    }
    renderer->gl.Finish();
    return gl_check(renderer, "OpenGL finish", error);
}

bool qa_gl_set_gamma(qa_gl_renderer *renderer, float gamma, qa_error *error)
{
    if (renderer == NULL || renderer->closed || renderer->detached ||
        !qa_display_make_current(renderer->options.display, error)) {
        if (renderer == NULL || renderer->closed)
            qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                         "Invalid OpenGL gamma renderer");
        return false;
    }
    if (gamma == renderer->gamma) return true;
    return gl_output_set_gamma(renderer, gamma, error);
}

static bool pack_state(qa_gl_renderer *renderer, GLint alignment,
                       qa_error *error)
{
    renderer->gl.BindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    renderer->gl.PixelStorei(GL_PACK_ALIGNMENT, alignment);
    renderer->gl.PixelStorei(GL_PACK_ROW_LENGTH, 0);
    renderer->gl.PixelStorei(GL_PACK_SKIP_ROWS, 0);
    renderer->gl.PixelStorei(GL_PACK_SKIP_PIXELS, 0);
    return gl_check(renderer, "OpenGL pack-state setup", error);
}

static bool capture(qa_gl_renderer *renderer, bool presented, qa_buffer *out,
                     uint32_t *out_width, uint32_t *out_height, qa_error *error)
{
    if (renderer == NULL || renderer->closed || renderer->detached || out == NULL ||
        renderer->opacity.active || renderer->target != NULL ||
        !qa_display_make_current(renderer->options.display, error) ||
        (presented ? !renderer->presented : !gl_output_resolve(renderer, error))) {
        if (renderer == NULL || renderer->closed || out == NULL ||
            (renderer != NULL &&
             (renderer->opacity.active || renderer->target != NULL || (presented && !renderer->presented))))
            qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                         "Invalid OpenGL capture state");
        return false;
    }
    uint32_t width = 0, height = 0;
    if (!gl_dimensions(renderer, &width, &height, error) ||
        (size_t)width > SIZE_MAX / height ||
        (size_t)width * height > SIZE_MAX / 4) {
        if (width != 0 && height != 0)
            qa_error_set(error, QA_ERROR_MEMORY, 0,
                         "OpenGL capture dimensions overflow");
        return false;
    }
    size_t row_bytes = (size_t)width * 4;
    if (presented && (width != renderer->presented_width || height != renderer->presented_height)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "presented drawable changed before capture"); return false;
    }
    size_t bytes = row_bytes * height;
    uint8_t *pixels = malloc(bytes);
    uint8_t *row = malloc(row_bytes);
    if (pixels == NULL || row == NULL) {
        free(pixels);
        free(row);
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "Allocating OpenGL capture storage");
        return false;
    }
    renderer->gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    GLenum read_buffer = gl_draw_buffer_name(renderer->draw_buffer);
    if (presented) read_buffer = renderer->draw_buffer == QA_DRAW_BACK_LEFT ? GL_FRONT_LEFT : renderer->draw_buffer == QA_DRAW_BACK_RIGHT ? GL_FRONT_RIGHT : GL_FRONT;
    renderer->gl.ReadBuffer(read_buffer);
    if (!pack_state(renderer, 1, error)) {
        free(pixels); free(row); return false;
    }
    renderer->gl.ReadPixels(0, 0, (GLsizei)width, (GLsizei)height, GL_RGBA,
                            GL_UNSIGNED_BYTE, pixels);
    if (!gl_check(renderer, "OpenGL color readback", error)) {
        free(pixels); free(row); return false;
    }
    for (size_t y = 0; y < height / 2; ++y) {
        uint8_t *top = pixels + y * row_bytes;
        uint8_t *bottom = pixels + ((size_t)height - y - 1) * row_bytes;
        memcpy(row, top, row_bytes);
        memcpy(top, bottom, row_bytes);
        memcpy(bottom, row, row_bytes);
    }
    free(row);
    *out = (qa_buffer){pixels, bytes};
    if (out_width != NULL) *out_width = width;
    if (out_height != NULL) *out_height = height;
    return true;
}
bool qa_gl_capture(qa_gl_renderer *renderer, qa_buffer *out,
                   uint32_t *width, uint32_t *height, qa_error *error)
{ return capture(renderer, false, out, width, height, error); }
bool qa_gl_capture_presented(qa_gl_renderer *renderer, qa_buffer *out,
                             uint32_t *width, uint32_t *height, qa_error *error)
{ return capture(renderer, true, out, width, height, error); }

bool qa_gl_read_depth(qa_gl_renderer *renderer, uint32_t x, uint32_t y,
                      float *out, qa_error *error)
{
    uint32_t width = 0, height = 0;
    if (renderer == NULL || renderer->closed || renderer->detached || out == NULL ||
        renderer->target != NULL || renderer->opacity.active ||
        !qa_display_make_current(renderer->options.display, error) ||
        !gl_dimensions(renderer, &width, &height, error) || x >= width ||
        y >= height || !gl_bind_destination(renderer, error) ||
        !pack_state(renderer, 1, error)) {
        if (renderer == NULL || renderer->closed || out == NULL ||
            (renderer != NULL &&
             (renderer->target != NULL || renderer->opacity.active)) ||
            (renderer != NULL && x >= width) ||
            (renderer != NULL && y >= height))
            qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                         "OpenGL depth read is outside the display framebuffer");
        return false;
    }
    renderer->gl.ReadPixels((GLint)x, (GLint)y, 1, 1, GL_DEPTH_COMPONENT,
                            GL_FLOAT, out);
    return gl_check(renderer, "OpenGL depth readback", error);
}

bool qa_gl_capture_depth_image(qa_gl_renderer *renderer,
                               const qa_scene_image *image, qa_buffer *out,
                               uint32_t *out_width, uint32_t *out_height,
                               qa_error *error)
{
    if (renderer == NULL || renderer->closed || renderer->detached || image == NULL || out == NULL ||
        image->kind != QA_SCENE_DEPTH32F ||
        !qa_display_make_current(renderer->options.display, error)) {
        if (renderer == NULL || renderer->closed || image == NULL || out == NULL ||
            (image != NULL && image->kind != QA_SCENE_DEPTH32F))
            qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                         "Invalid OpenGL depth-image capture");
        return false;
    }
    gl_texture_entry *texture;
    if (!gl_texture_get(renderer, image, &texture, error)) return false;
    uint32_t width = image->levels[0].width, height = image->levels[0].height;
    if ((size_t)width > SIZE_MAX / height ||
        (size_t)width * height > SIZE_MAX / sizeof(float)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "OpenGL depth-image capture dimensions overflow");
        return false;
    }
    size_t bytes = (size_t)width * height * sizeof(float);
    float *pixels = malloc(bytes);
    if (pixels == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "Allocating OpenGL depth-image capture");
        return false;
    }
    renderer->gl.ActiveTexture(GL_TEXTURE2);
    renderer->gl.BindTexture(GL_TEXTURE_2D, texture->name);
    if (!pack_state(renderer, 1, error)) { free(pixels); return false; }
    renderer->gl.GetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_FLOAT,
                             pixels);
    if (!gl_check(renderer, "OpenGL depth-image readback", error)) {
        free(pixels); return false;
    }
    *out = (qa_buffer){(uint8_t *)pixels, bytes};
    if (out_width != NULL) *out_width = width;
    if (out_height != NULL) *out_height = height;
    return true;
}

bool qa_gl_set_overdraw(qa_gl_renderer *renderer, bool enabled,
                        qa_error *error)
{
    if (renderer == NULL || renderer->closed || renderer->detached ||
        (enabled && renderer->capabilities.stencil_bits == 0)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "OpenGL overdraw requires stencil storage");
        return false;
    }
    renderer->overdraw = enabled;
    return true;
}

bool qa_gl_read_overdraw(qa_gl_renderer *renderer, uint8_t *destination,
                         size_t bytes, qa_error *error)
{
    uint32_t width, height;
    if (renderer == NULL || renderer->closed || renderer->detached || destination == NULL ||
        renderer->capabilities.stencil_bits == 0 || renderer->target != NULL ||
        renderer->opacity.active ||
        !qa_display_make_current(renderer->options.display, error) ||
        !gl_dimensions(renderer, &width, &height, error) ||
        !gl_bind_destination(renderer, error)) {
        if (renderer == NULL || renderer->closed || destination == NULL ||
            (renderer != NULL &&
             (renderer->capabilities.stencil_bits == 0 ||
              renderer->target != NULL || renderer->opacity.active)))
            qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                         "Invalid OpenGL overdraw readback state");
        return false;
    }
    size_t stride = ((size_t)width + 3) & ~(size_t)3;
    if (stride > SIZE_MAX / height ||
        bytes < stride * ((size_t)height - 1) + width) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "OpenGL overdraw destination is truncated");
        return false;
    }
    if (!pack_state(renderer, 4, error)) return false;
    renderer->gl.ReadPixels(0, 0, (GLsizei)width, (GLsizei)height,
                            GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, destination);
    return gl_check(renderer, "OpenGL stencil readback", error);
}

bool qa_gl_restart(qa_gl_renderer **renderer, qa_display **display,
                   const qa_display_options *display_options,
                   const qa_gl_options *renderer_options, qa_error *error)
{
    if (renderer == NULL || display == NULL || *renderer == NULL ||
        *display == NULL || (*renderer)->options.display != *display ||
        (*renderer)->opacity.active || (*renderer)->target != NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "OpenGL restart requires an idle owned display target");
        return false;
    }
    qa_display *replacement_display = qa_display_create(display_options, error);
    if (replacement_display == NULL) return false;
    qa_gl_options options = renderer_options == NULL
                                ? (*renderer)->options : *renderer_options;
    options.display = replacement_display;
    qa_gl_renderer *replacement_renderer = qa_gl_create(&options, error);
    if (replacement_renderer == NULL) {
        qa_display_destroy(replacement_display);
        qa_error ignored = {0};
        (void)qa_display_make_current(*display, &ignored);
        return false;
    }
    if (!qa_gl_set_gamma(replacement_renderer, (*renderer)->gamma, error) ||
        !qa_gl_set_overdraw(replacement_renderer, (*renderer)->overdraw,
                            error) ||
        !select_draw_buffer(replacement_renderer, (*renderer)->draw_buffer,
                            false, error)) {
        qa_gl_destroy(replacement_renderer);
        qa_display_destroy(replacement_display);
        qa_error ignored = {0};
        (void)qa_display_make_current(*display, &ignored);
        return false;
    }
    for (size_t unit = 0; unit < 2; ++unit) {
        const qa_scene_image *image = (*renderer)->bound[unit];
        if (image == NULL) continue;
        gl_texture_entry *texture;
        if (!gl_texture_get(replacement_renderer, image, &texture, error)) {
            qa_gl_destroy(replacement_renderer);
            qa_display_destroy(replacement_display);
            qa_error ignored = {0};
            (void)qa_display_make_current(*display, &ignored);
            return false;
        }
        (void)texture;
        qa_scene_image_retain(image);
        replacement_renderer->bound[unit] = image;
    }
    replacement_renderer->sequence = (*renderer)->sequence;
    qa_gl_renderer *old_renderer = *renderer;
    qa_display *old_display = *display;
    *renderer = replacement_renderer;
    *display = replacement_display;
    qa_gl_destroy(old_renderer);
    qa_display_destroy(old_display);
    qa_error ignored = {0};
    (void)qa_display_make_current(replacement_display, &ignored);
    return true;
}
