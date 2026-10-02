#include "internal.h"
#include "../save_fields.h"
#include "qa/render_gl_save.h"
#include "qa/display_settings.h"
#include "qa/q3_source_scene_bank.h"

#include <SDL_video.h>
#include <SDL_loadso.h>
#include <SDL_timer.h>
#include <limits.h>
#include <stdio.h>

struct qa_gl_surface_ticket {
    qa_gl_renderer *renderer;
    qa_gl_renderer targets,original;
    qa_display *original_display,*candidate_display;
    gl_presentation_snapshot *native;
    SDL_GLContext context;
    GLuint reader;
    uint32_t original_width,original_height,width,height,window_id;
    float gamma,original_gamma;
    uint64_t original_owner,original_sequence;
    gl_output_target original_output,ready_output;
    gl_opacity_target ready_opacity;
    qa_output_domains retired_domains;
    qa_display_endpoint ready_original_endpoint,ready_candidate_endpoint;
    GLuint ready_reader;
    bool captured,attempted,replace_output,replace_opacity,prepared,published,native_restored,native_ready;
};

static bool gl_surface_idle(const qa_gl_renderer *renderer,qa_error *error)
{
    if (renderer && !renderer->surface_ticket && !renderer->controls.ticket && !renderer->controls.image_ticket && !renderer->controls.source.entered) return true;
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"OpenGL renderer is absent or retains a settings ticket");
    return false;
}

qa_render_controls *qa_gl_render_controls(qa_gl_renderer *renderer)
{ return renderer ? &renderer->controls : NULL; }
bool qa_gl_render_controls_current(const qa_render_controls *controls)
{
    const qa_gl_renderer *renderer = controls ? controls->owner.gl : NULL;
    return renderer && controls->backend == QA_RENDER_CONTROLS_GL &&
        &renderer->controls == controls && !renderer->closed && !renderer->detached &&
        !renderer->destroy_pending && !renderer->executing && !renderer->capturing &&
        (!renderer->preparing || renderer->surface_ticket) && !renderer->opacity.active;
}
void qa_gl_render_controls_close(qa_render_controls *controls)
{
    qa_gl_renderer *renderer = controls->owner.gl;
    if (renderer->destroy_pending) qa_gl_destroy(renderer);
}
bool qa_gl_source_scratch_current(const qa_render_controls *controls)
{
    const qa_gl_renderer *renderer = controls ? controls->owner.gl : NULL;
    return renderer && controls->backend == QA_RENDER_CONTROLS_GL &&
        &renderer->controls == controls && !renderer->closed && !renderer->detached &&
        !renderer->destroy_pending && !renderer->executing && !renderer->capturing &&
        !renderer->surface_ticket && !renderer->preparing && (!renderer->opacity.active ||
        (controls->source.entered && controls->source.issuing));
}

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

static bool gl_extension(const char *extensions,const char *name)
{
    if (!extensions) return false;
    size_t length=strlen(name);
    for (const char *at=extensions;(at=strstr(at,name))!=NULL;at+=length)
        if ((at==extensions || at[-1]==' ') && (!at[length] || at[length]==' ')) return true;
    return false;
}

#ifdef _WIN32
static bool gl_wgl_native_depth(bool *floating,qa_error *error)
{
    typedef void *(APIENTRY *current_dc_fn)(void);
    typedef const char *(APIENTRY *extensions_fn)(void);
    typedef int (APIENTRY *pixel_attribute_fn)(void *,int,int,unsigned,const int *,int *);
    typedef int (APIENTRY *pixel_format_fn)(void *);
    current_dc_fn current_dc=NULL; extensions_fn extensions_string=NULL;
    pixel_attribute_fn attribute=NULL; pixel_format_fn pixel_format=NULL;
    _Static_assert(sizeof(current_dc)==sizeof(void *) && sizeof(extensions_string)==sizeof(void *) &&
        sizeof(attribute)==sizeof(void *) && sizeof(pixel_format)==sizeof(void *),"WGL procedure pointers must fit in void pointers");
    void *address=SDL_GL_GetProcAddress("wglGetCurrentDC"); memcpy(&current_dc,&address,sizeof(current_dc));
    if (!current_dc || (uintptr_t)address<=3 || (uintptr_t)address==UINTPTR_MAX) {
        qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"Legacy native depth has no current WGL device witness"); return false;
    }
    void *dc=current_dc();
    if (!dc) { qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"Legacy native depth has no current WGL drawable"); return false; }
    address=SDL_GL_GetProcAddress("wglGetExtensionsStringEXT");
    if ((uintptr_t)address>3 && (uintptr_t)address!=UINTPTR_MAX) memcpy(&extensions_string,&address,sizeof(extensions_string));
    /* WGL_EXT_depth_float requires this exact extension-string query. */
    if (!extensions_string) {
        *floating=false; return true;
    }
    const char *extensions=extensions_string();
    if (!extensions) { qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"Reading actual current WGL depth extensions"); return false; }
    if (!gl_extension(extensions,"WGL_EXT_depth_float")) { *floating=false; return true; }
    address=SDL_GL_GetProcAddress("wglGetPixelFormatAttribivEXT");
    if ((uintptr_t)address>3 && (uintptr_t)address!=UINTPTR_MAX) memcpy(&attribute,&address,sizeof(attribute));
    if (!attribute) { qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"Native WGL floating depth has no pixel-format query"); return false; }
    void *library=SDL_LoadObject("gdi32.dll");
    if (!library) { qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"Loading the actual native WGL pixel-format getter: %s",SDL_GetError()); return false; }
    address=SDL_LoadFunction(library,"GetPixelFormat"); memcpy(&pixel_format,&address,sizeof(pixel_format));
    int format=pixel_format?pixel_format(dc):0,value=-1;
    const int depth_float=0x2040; /* WGL_DEPTH_FLOAT_EXT */
    bool ok=format>0 && attribute(dc,format,0,1,&depth_float,&value) && (value==0 || value==1);
    SDL_UnloadObject(library);
    if (!ok) { qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"Reading the actual native WGL depth representation"); return false; }
    *floating=value!=0; return true;
}
#endif

static bool gl_native_depth(qa_gl_renderer *renderer,bool *floating,qa_error *error)
{
    gl_api *gl=&renderer->gl;
    const char *version=(const char *)gl->GetString(GL_VERSION);
    unsigned major=0;
    if (!version || *version<'1' || *version>'9') {
        qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"Native depth requires an actual desktop OpenGL version"); return false;
    }
    for (;*version>='0' && *version<='9';++version) {
        if (major>(UINT_MAX-(unsigned)(*version-'0'))/10) {
            qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"Actual OpenGL version exceeds native depth discovery"); return false;
        }
        major=major*10+(unsigned)(*version-'0');
    }
    if (*version!='.') { qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"Actual desktop OpenGL version is malformed"); return false; }
    const char *extensions=major>=3?NULL:(const char *)gl->GetString(GL_EXTENSIONS);
    if (major<3 && !extensions) { qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"Reading actual legacy OpenGL extensions"); return false; }
    bool standard_fbo=major>=3 || gl_extension(extensions,"GL_ARB_framebuffer_object");
    GLint draw=0,read=0,value=-1;
    gl->GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw); gl->GetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
    if (!gl_check(renderer,"Reading bindings for native depth discovery",error)) return false;
    gl->BindFramebuffer(GL_FRAMEBUFFER,0);
    bool ok=true;
    if (standard_fbo) {
        gl->GetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_DEPTH,GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE,&value);
        if (value!=GL_FLOAT && value!=GL_UNSIGNED_NORMALIZED) {
            qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"Actual native depth component type is unsupported"); ok=false;
        } else *floating=value==GL_FLOAT;
    } else if (gl_extension(extensions,"GL_NV_depth_buffer_float")) {
        gl->GetIntegerv(0x8DAF,&value); /* DEPTH_BUFFER_FLOAT_MODE_NV */
        if (value!=0 && value!=1) { qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"Actual NV native depth mode is invalid"); ok=false; }
        else *floating=value!=0;
    } else {
#ifdef _WIN32
        ok=gl_wgl_native_depth(floating,error);
#else
        /* Legacy desktop GL specifies unsigned fixed-point default depth.
         * NV supplies its mode query; WGL separately extends native visuals. */
        *floating=false;
#endif
    }
    gl->BindFramebuffer(GL_DRAW_FRAMEBUFFER,(GLuint)draw); gl->BindFramebuffer(GL_READ_FRAMEBUFFER,(GLuint)read);
    if (!gl_check(renderer,"Discovering actual native depth and restoring bindings",error)) return false;
    if (ok && *floating && major<3 && !gl_extension(extensions,"GL_ARB_depth_buffer_float")) {
        qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"Actual native floating depth lacks the renderer's standard retained formats"); return false;
    }
    return ok;
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
    bool floating_depth=false;
    if (!gl_native_depth(renderer,&floating_depth,error)) return false;
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
    caps->floating_depth = floating_depth;
    caps->compiled_vertex_arrays = gl->LockArraysEXT && gl->UnlockArraysEXT &&
        gl_extension((const char *)gl->GetString(GL_EXTENSIONS), "GL_EXT_compiled_vertex_array");
    caps->s3tc=gl_extension((const char *)gl->GetString(GL_EXTENSIONS),"GL_S3_s3tc");
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
    qa_render_controls_init_gl(&renderer->controls, renderer);
    qa_scene_state_default(&renderer->pipeline);
    renderer->pipeline.depth_test=QA_DEPTH_LESS;
    renderer->draw_buffer = QA_DRAW_BACK;
    renderer->gamma = 1;
    renderer->clear_depth = 1;
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
    renderer->gl.ClearDepth(renderer->clear_depth);
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
    if (renderer->surface_ticket || renderer->controls.ticket || renderer->controls.image_ticket || renderer->controls.source.entered) { renderer->destroy_pending=true; return; }
    material_source_release(&renderer->controls.source);
    qa_render_source_texture_release(&renderer->controls.zero_texture);
    renderer->closed = true;
    qa_output_domains_destroy(&renderer->output_domains);
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
            qa_scene_resources_destroy(entry->source_owner);
            qa_render_source_texture_release(&entry->source_texture);
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

static void draw_state(qa_gl_renderer *,const qa_scene_state *,qa_scene_primitive);
static bool begin_view(qa_gl_renderer *renderer, const qa_scene_view *view, bool source_backend,
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
    if (source_backend && view->clear_depth) {
        if (renderer->controls.frame_values.finish==1 && !renderer->controls.finish_called) {
            gl->Finish(); renderer->controls.finish_called=true;
        }
        if (renderer->controls.frame_values.finish==0) renderer->controls.finish_called=true;
    }
    if (source_backend && view->clear_depth) {
        qa_scene_state initial=renderer->pipeline;
        qa_render_source_state_bits(&initial,true,false);
        draw_state(renderer,&initial,QA_SCENE_TRIANGLES);
    }
    gl->Viewport(view->viewport.x, (GLint)bottom,
                 (GLsizei)view->viewport.width,
                 (GLsizei)view->viewport.height);
    gl->Enable(GL_SCISSOR_TEST);
    gl->Scissor(view->viewport.x, (GLint)bottom,
                (GLsizei)view->viewport.width,
                (GLsizei)view->viewport.height);
    GLbitfield clear = 0;
    if (view->clear_color) {
        renderer->pipeline.color_write=true;
        gl->ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        gl->ClearColor(view->color.x, view->color.y, view->color.z,
                       view->color.w);
        clear |= GL_COLOR_BUFFER_BIT;
    }
    if (view->clear_depth) {
        renderer->pipeline.depth_write=true;
        gl->DepthMask(GL_TRUE);
        renderer->clear_depth=fminf(1,fmaxf(0,view->depth));
        gl->ClearDepth(renderer->clear_depth);
        clear |= GL_DEPTH_BUFFER_BIT;
    }
    if (view->clear_stencil || (source_backend && view->clear_depth && renderer->overdraw)) {
        if (renderer->target != NULL || (!source_backend && renderer->capabilities.stencil_bits == 0)) {
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
static void stage_state_bits(qa_gl_renderer *renderer,const qa_scene_state *state)
{
    gl_api *gl=&renderer->gl;
    if (state->depth_test==QA_DEPTH_DISABLED) gl->Disable(GL_DEPTH_TEST); else gl->Enable(GL_DEPTH_TEST);
    gl->DepthFunc(state->depth_test==QA_DEPTH_ALWAYS?GL_ALWAYS:
        state->depth_test==QA_DEPTH_LEQUAL || state->depth_test==QA_DEPTH_DISABLED?GL_LEQUAL:
        state->depth_test==QA_DEPTH_EQUAL?GL_EQUAL:state->depth_test==QA_DEPTH_GEQUAL?GL_GEQUAL:GL_LESS);
    gl->DepthMask(state->depth_write?GL_TRUE:GL_FALSE);
    if (state->blend_source==QA_BLEND_ONE && state->blend_destination==QA_BLEND_ZERO) gl->Disable(GL_BLEND);
    else { gl->Enable(GL_BLEND); gl->BlendFunc(blend_factor(state->blend_source),blend_factor(state->blend_destination)); }
    gl->PolygonMode(GL_FRONT_AND_BACK,state->wireframe?GL_LINE:GL_FILL);
    if (state->alpha_test==QA_ALPHA_NONE) gl->Disable(GL_ALPHA_TEST);
    else {
        gl->Enable(GL_ALPHA_TEST);
        gl->AlphaFunc(state->alpha_test==QA_ALPHA_GT0?GL_GREATER:state->alpha_test==QA_ALPHA_LT128?GL_LESS:GL_GEQUAL,
            state->alpha_test==QA_ALPHA_GT0?0:.5f);
    }
}

static void draw_state(qa_gl_renderer *renderer, const qa_scene_state *state,
                       qa_scene_primitive primitive)
{
    renderer->pipeline=*state;
    gl_api *gl = &renderer->gl;
    stage_state_bits(renderer,state);
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

void gl_source_pipeline_restore(qa_gl_renderer *renderer)
{
    gl_api *gl=&renderer->gl;
    draw_state(renderer,&renderer->pipeline,QA_SCENE_TRIANGLES);
    gl->LineWidth(renderer->pipeline.line_width);
    gl->ClearDepth(renderer->clear_depth);
    qa_render_source_attributes *attributes=&renderer->controls.attributes;
    for (uint32_t unit=0;unit<2;++unit) {
        GLuint name=0;
        if (!attributes->actual_empty[unit] && renderer->bound[unit])
            for (gl_texture_entry *entry=renderer->textures;entry;entry=entry->next)
                if (entry->image==renderer->bound[unit]) { name=entry->name; break; }
        gl->ActiveTexture(GL_TEXTURE0+unit);
        gl->BindTexture(GL_TEXTURE_2D,name);
        if (!name) {
            const qa_scene_vec4 *border=&attributes->zero_border;
            GLfloat values[4]={border->x,border->y,border->z,border->w};
            gl->TexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_BORDER_COLOR,values);
        }
        if (attributes->texture_enabled[unit]) gl->Enable(GL_TEXTURE_2D); else gl->Disable(GL_TEXTURE_2D);
        gl->TexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,
            attributes->environment[unit]==QA_TEXTURE_ADD?GL_ADD:
            attributes->environment[unit]==QA_TEXTURE_REPLACE?GL_REPLACE:GL_MODULATE);
        if (attributes->coordinate_array[unit]) gl->EnableVertexAttribArray(2+unit);
        else gl->DisableVertexAttribArray(2+unit);
        if (attributes->coordinates_known[unit]) {
            qa_scene_vec2 uv=attributes->coordinates[unit];
            gl->VertexAttrib4f(2+unit,uv.x,uv.y,0,1);
        }
    }
    if (attributes->color_array) gl->EnableVertexAttribArray(4); else gl->DisableVertexAttribArray(4);
    if (attributes->color_known) {
        qa_scene_vec4 color=attributes->color;
        gl->Color4f(color.x,color.y,color.z,color.w);
        gl->VertexAttrib4f(4,color.x,color.y,color.z,color.w);
    }
    gl->ActiveTexture(GL_TEXTURE0+attributes->texture_unit);
    gl->ClientActiveTexture(GL_TEXTURE0+attributes->texture_unit);
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
        (unsigned)state->depth_test > QA_DEPTH_DISABLED ||
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
        (draw->source_vertex_storage && (!draw->source_arrays ||
         draw->source_vertex_storage != QA_SOURCE_TESS_VERTICES ||
         draw->mesh.vertex_count > draw->source_vertex_storage)) ||
        (draw->mesh.identity != 0 && draw->mesh.geometry == NULL) ||
        (draw->mesh.vertex_count != 0 && draw->mesh.vertices == NULL) ||
        (draw->source_vertex_storage && draw->mesh.vertices == NULL) ||
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
        size_t storage=draw->source_vertex_storage?draw->source_vertex_storage:draw->mesh.vertex_count;
        bool referenced[QA_SOURCE_TESS_VERTICES]={0};
        for (size_t i=0;i<draw->mesh.index_count;++i) {
            size_t index=draw->mesh.indices[i];
            if (index>=storage) {
                qa_error_set(error,QA_ERROR_ARGUMENT,i,"OpenGL mesh index is outside vertex storage");
                return false;
            }
            if (draw->source_vertex_storage) referenced[index]=true;
        }
        for (size_t i = 0; i < storage; ++i) {
            if (draw->source_vertex_storage && i>=draw->mesh.vertex_count && !referenced[i]) continue;
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

static GLfloat source_color(float value)
{
    return floorf(fminf(1, fmaxf(0, value)) * 255 + .5f) / 255;
}
static void draw_source_strips(qa_gl_renderer *renderer, const qa_scene_draw *draw,
    bool discrete)
{
    gl_api *gl = &renderer->gl;
    if (discrete) gl_mesh_unbind(renderer);
    size_t cursor = 0; qa_render_strip strip;
    while (qa_render_strip_next(draw->mesh.indices, draw->mesh.index_count, &cursor, &strip)) {
        gl->Begin(GL_TRIANGLE_STRIP);
        for (size_t ordinal = 0; ordinal < strip.triangles + 2; ++ordinal) {
            uint32_t index = qa_render_strip_vertex(&strip, ordinal);
            if (!discrete) gl->ArrayElement((GLint)index);
            else {
                const qa_scene_vertex *v = draw->mesh.vertices + index;
                qa_scene_vec4 color; qa_scene_vec2 uv[2];
                qa_render_source_attributes_vertex(&renderer->controls,draw,QA_RENDER_PRIMITIVES_DISCRETE_STRIPS,index,v,&color,uv);
                gl->VertexAttrib4f(1, v->normal.x, v->normal.y, v->normal.z, 1);
                gl->VertexAttrib4f(2, uv[0].x, uv[0].y, 0, 1);
                gl->VertexAttrib4f(3, uv[1].x, uv[1].y, 0, 1);
                gl->VertexAttrib4f(4, source_color(color.x), source_color(color.y),
                    source_color(color.z), source_color(color.w));
                gl->VertexAttrib4f(0, v->position.x, v->position.y, v->position.z, 1);
            }
        }
        gl->End();
    }
}

static bool draw_scene(qa_gl_renderer *renderer, const qa_scene_draw *source,
                       qa_error *error)
{
    qa_scene_draw draw = *source;
    qa_render_source_direct_state(&draw.state,&renderer->pipeline,source);
    if ((unsigned)draw.source_direct>QA_SOURCE_DIRECT_IMAGE_GRID || !draw_valid(renderer,&draw,error)) {
        if (!error || error->code==QA_OK)
            qa_error_set(error,QA_ERROR_ARGUMENT,0,"Invalid Source direct draw provenance");
        return false;
    }
    if (draw.source_direct==QA_SOURCE_DIRECT_SHADOW_FINISH) renderer->view.clip_enabled=false;
    bool source_pipeline=draw.source_arrays || draw.source_retain_depth_range || draw.source_direct!=QA_SOURCE_DIRECT_NONE;
    bool compiled_arrays = renderer->capabilities.compiled_vertex_arrays &&
        renderer->controls.values.compiled_vertex_arrays;
    qa_render_primitive_mode mode = draw.source_primitives && draw.mesh.primitive == QA_SCENE_TRIANGLES
        ? qa_render_primitives_mode(renderer->controls.values.primitives, compiled_arrays)
        : QA_RENDER_PRIMITIVES_INDEXED;
    for (size_t unit=0;unit<source->texture_count;++unit) {
        size_t destination=source_pipeline && !draw.source_arrays && unit==0?renderer->controls.attributes.texture_unit:unit;
        if (!draw.source_arrays && !source->retain_texture[unit] && source->textures[unit]) {
            if (source_pipeline) {
                if (!gl_source_texture_bind(renderer,source->textures[unit],error)) return false;
            } else {
                retain_binding(renderer,destination,source->textures[unit]);
                renderer->controls.attributes.actual_empty[destination]=false;
            }
        }
        if (draw.retain_texture[unit]) draw.textures[unit]=renderer->bound[destination];
    }
    draw_state(renderer,&draw.state,draw.mesh.primitive);
    if (draw.source_stage_state) renderer->gl.LineWidth(draw.state.line_width);
    if (!qa_render_source_attributes_resolve(&renderer->controls,&draw,renderer->bound,mode,error)) {
        if (mode==QA_RENDER_PRIMITIVES_DISCRETE_STRIPS && renderer->controls.attributes.texture_unit!=0 &&
            renderer->controls.attributes.color_known) {
            qa_scene_vec4 color=renderer->controls.attributes.color;
            renderer->gl.Color4f(color.x,color.y,color.z,color.w);
            renderer->gl.VertexAttrib4f(4,color.x,color.y,color.z,color.w);
        }
        return false;
    }
    gl_texture_entry *textures[2] = {NULL, NULL}, *shadow = NULL;
    for (size_t unit = 0; unit < draw.texture_count; ++unit) {
        if (!source_pipeline && draw.retain_texture[unit]) draw.textures[unit] = renderer->bound[unit];
        if (!source_pipeline && draw.textures[unit] != NULL &&
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
    if (!gl_bind_destination(renderer, error) ||
        !gl_program_stage(renderer, &draw, error)) return false;
    for (unsigned unit = 0; unit < 2; ++unit) {
        if (!source_pipeline) {
            renderer->gl.ActiveTexture(GL_TEXTURE0 + unit);
            renderer->gl.BindTexture(GL_TEXTURE_2D,
                unit < draw.texture_count && textures[unit] != NULL
                    ? textures[unit]->name : renderer->white_texture);
        }
    }
    renderer->gl.ActiveTexture(GL_TEXTURE2);
    renderer->gl.BindTexture(GL_TEXTURE_2D,
                             renderer->preblend_gamma && renderer->gamma!=1 &&
                               !qa_display_gamma_applied_is(renderer->options.display) ? renderer->output.table :
                             shadow == NULL ? renderer->white_texture :
                                              shadow->name);
    renderer->gl.ActiveTexture(GL_TEXTURE0+renderer->controls.attributes.texture_unit);
    size_t vertex_storage=draw.source_vertex_storage?draw.source_vertex_storage:draw.mesh.vertex_count;
    if (draw.source_arrays && vertex_storage<=QA_SOURCE_TESS_VERTICES) {
        for (size_t i=0;i<vertex_storage;++i) {
            qa_scene_vec4 color; qa_scene_vec2 uv[2];
            qa_render_source_attributes_vertex(&renderer->controls,&draw,mode,i,draw.mesh.vertices+i,&color,uv);
            renderer->source_vertices[i]=draw.mesh.vertices[i];
            renderer->source_vertices[i].color=color; renderer->source_vertices[i].texcoord=uv[0];
            renderer->source_vertices[i].lightmap=uv[1];
        }
        draw.mesh.vertices=renderer->source_vertices;
        draw.mesh.identity=draw.mesh.revision=0;
        draw.mesh.geometry=NULL;
    }
    qa_scene_mesh uploaded=draw.mesh;
    if (draw.source_vertex_storage) uploaded.vertex_count=draw.source_vertex_storage;
    if (!gl_mesh_bind(renderer, &uploaded, error)) return false;
    if (source_pipeline && draw.mesh.vertex_count) {
        qa_scene_vec4 color; qa_scene_vec2 uv[2];
        qa_render_source_attributes_vertex(&renderer->controls,&draw,mode,0,draw.mesh.vertices,&color,uv);
        if (draw.source_arrays && !renderer->controls.attributes.color_array) {
            renderer->gl.DisableVertexAttribArray(4);
            renderer->gl.VertexAttrib4f(4,color.x,color.y,color.z,color.w);
        }
        for (uint32_t unit=0;unit<2;++unit)
            if (((draw.source_direct!=QA_SOURCE_DIRECT_SKY &&
                  draw.source_direct!=QA_SOURCE_DIRECT_RAW &&
                  draw.source_direct!=QA_SOURCE_DIRECT_IMAGE_GRID) || unit!=0) &&
                (!draw.source_arrays || !renderer->controls.attributes.coordinate_array[unit])) {
                renderer->gl.DisableVertexAttribArray(2+unit);
                renderer->gl.VertexAttrib4f(2+unit,uv[unit].x,uv[unit].y,0,1);
            }
    }
    bool locked = draw.source_primitives && mode != QA_RENDER_PRIMITIVES_DISCRETE_STRIPS &&
        compiled_arrays && draw.mesh.vertex_count <= INT_MAX;
    if (mode == QA_RENDER_PRIMITIVES_ARRAY_STRIPS)
        for (size_t i = 0; i < draw.mesh.index_count; ++i)
            if (draw.mesh.indices[i] > INT_MAX) {
                gl_mesh_unbind(renderer);
                qa_error_set(error, QA_ERROR_ARGUMENT, i, "Source array-element index exceeds native GLint");
                return false;
            }
    if (locked) renderer->gl.LockArraysEXT(0, (GLsizei)draw.mesh.vertex_count);
    if (mode == QA_RENDER_PRIMITIVES_INDEXED && draw.mesh.index_count != 0)
        renderer->gl.DrawElements(draw.mesh.primitive == QA_SCENE_LINES
                                      ? GL_LINES : GL_TRIANGLES,
                                  (GLsizei)draw.mesh.index_count,
                                  GL_UNSIGNED_INT, NULL);
    else if (mode == QA_RENDER_PRIMITIVES_ARRAY_STRIPS || mode == QA_RENDER_PRIMITIVES_DISCRETE_STRIPS)
        draw_source_strips(renderer, &draw, mode == QA_RENDER_PRIMITIVES_DISCRETE_STRIPS);
    if (draw.source_arrays && draw.mesh.primitive==QA_SCENE_TRIANGLES && !draw.state.wireframe)
        renderer->controls.counters.total_indexes+=draw.mesh.index_count;
    if (locked) renderer->gl.UnlockArraysEXT();
    qa_render_source_attributes_finish(&renderer->controls,&draw,mode);
    if (source_pipeline && renderer->controls.attributes.color_known) {
        qa_scene_vec4 color=renderer->controls.attributes.color;
        renderer->gl.Color4f(color.x,color.y,color.z,color.w);
    }
    gl_mesh_unbind(renderer);
    if (renderer->controls.attributes.color_array) renderer->gl.EnableVertexAttribArray(4);
    for (uint32_t unit=0;unit<2;++unit)
        if (renderer->controls.attributes.coordinate_array[unit]) renderer->gl.EnableVertexAttribArray(2+unit);
    if (draw.source_direct==QA_SOURCE_DIRECT_AXIS) {
        renderer->gl.LineWidth(1);
        renderer->pipeline.line_width=1;
    }
    if (draw.source_direct==QA_SOURCE_DIRECT_SHADOW_FINISH) {
        renderer->gl.Disable(GL_STENCIL_TEST);
        renderer->pipeline.stencil_enabled=false;
    }
    if (draw.source_direct==QA_SOURCE_DIRECT_SHADOW_VOLUME_END) {
        renderer->gl.ColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        renderer->pipeline.color_write=true;
    }
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
        renderer->pipeline.color_write=renderer->pipeline.depth_write=true;
        renderer->gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        renderer->gl.DepthMask(GL_TRUE);
        renderer->gl.ClearColor(1, 0, 0.5f, 1);
        renderer->clear_depth=1;
        renderer->gl.ClearDepth(renderer->clear_depth);
        GLbitfield mask = GL_DEPTH_BUFFER_BIT;
        if (renderer->target == NULL) mask |= GL_COLOR_BUFFER_BIT;
        renderer->gl.Clear(mask);
    }
    return gl_check(renderer, "OpenGL draw-buffer selection", error);
}
static bool source_draw_buffer_clear(qa_gl_renderer *renderer, qa_error *error)
{
    renderer->gl.ClearColor(1, 0, 0.5f, 1);
    renderer->gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    return gl_check(renderer,"Source draw-buffer clear",error);
}
static bool pack_state(qa_gl_renderer *,GLint,qa_error *);
static bool gl_swap(qa_gl_renderer *renderer, bool source_front_buffer, qa_error *error)
{
    if ((renderer->opacity.active && renderer->opacity.value != 1) || renderer->target) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Cannot swap an OpenGL opacity or depth target");
        return false;
    }
    if (renderer->source_frame) {
        qa_render_controls *controls=&renderer->controls;
        uint32_t width=0,height=0;
        if (!gl_dimensions(renderer,&width,&height,error)) return false;
        if (controls->frame_values.show_images && !qa_gl_source_image_grid(controls,controls->frame_values.show_images,error)) return false;
        if (renderer->overdraw) {
            size_t stride=((size_t)width+3)&~(size_t)3;
            if (!height || stride>SIZE_MAX/height) return false;
            uint8_t *pixels=malloc(stride*height);
            if (!pixels) { qa_error_set(error,QA_ERROR_MEMORY,0,"Reading Source overdraw stencil"); return false; }
            bool ok=pack_state(renderer,4,error);
            if (ok) {
                renderer->gl.ReadPixels(0,0,(GLsizei)width,(GLsizei)height,GL_STENCIL_INDEX,GL_UNSIGNED_BYTE,pixels);
                ok=gl_check(renderer,"Source overdraw readback",error);
            }
            if (ok) for (size_t y=0;y<height;++y) for (size_t x=0;x<width;++x) controls->counters.overdraw+=pixels[y*stride+x];
            free(pixels); if (!ok) return false;
        }
        if (!controls->finish_called) renderer->gl.Finish();
        qa_render_source_report(controls,width,height);
    }
    if (!gl_output_resolve(renderer,error) ||
        !gl_dimensions(renderer,&renderer->presented_width,&renderer->presented_height,error) ||
        (!source_front_buffer && !qa_display_swap(renderer->options.display,error))) return false;
    renderer->presented=true;
    renderer->source_frame=false;
    renderer->controls.source.projection_2d=false;
    renderer->controls.source.entity_count=renderer->controls.source.first_scene_entity=0;
    renderer->controls.source.submitted_light_count=renderer->controls.source.first_scene_light=0;
    qa_q3_source_scene_bank_frame(renderer->controls.source.scene_bank);
    return true;
}

static bool gl_execute_range(qa_gl_renderer *renderer, const qa_scene_frame *frame,
                   size_t first, bool begin, bool finish, qa_error *error)
{
    if (renderer == NULL || renderer->closed || frame == NULL ||
        frame->owner != renderer->options.owner ||
        (frame->command_count != 0 && frame->commands == NULL) ||
        first > frame->command_count || (begin && renderer->opacity.active)) {
        if (renderer == NULL || renderer->closed || frame == NULL ||
            (renderer != NULL && frame != NULL &&
             frame->owner != renderer->options.owner) ||
            (frame != NULL && frame->command_count != 0 &&
             frame->commands == NULL) ||
            (frame != NULL && first > frame->command_count) ||
            (renderer != NULL && begin && renderer->opacity.active))
            qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                         "Invalid OpenGL frame or renderer owner");
        return false;
    }
    if (frame->source_backend) renderer->source_frame=true;
    if (frame->source_backend && frame->source_skip_backend) return true;
    if (!qa_display_make_current(renderer->options.display, error)) return false;
    if (begin && frame->source_backend && frame->source_clear_draw_buffer) {
        if (!select_draw_buffer(renderer, renderer->draw_buffer, false, error)) return false;
        if (!source_draw_buffer_clear(renderer,error)) return false;
        if (renderer->controls.source.issuing && renderer->controls.source.frame == frame)
            renderer->controls.source.frame->source_clear_draw_buffer = false;
    }
    if (begin) {
        renderer->presented = false;
        gl_textures_prune(renderer);
        gl_meshes_prune(renderer);
        renderer->sequence = frame->sequence;
    }
    for (size_t i = first; i < frame->command_count; ++i) {
        const qa_scene_command *command = &frame->commands[i];
        if (renderer->opacity.skip &&
            command->kind != QA_SCENE_COMMAND_OPACITY_BEGIN &&
            command->kind != QA_SCENE_COMMAND_OPACITY_END) continue;
        bool ok;
        switch (command->kind) {
        case QA_SCENE_COMMAND_VIEW:
            ok = begin_view(renderer, &command->data.view, frame->source_backend, error);
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
                                    command->data.draw_buffer.clear && !frame->source_backend, error);
            if (ok && command->data.draw_buffer.clear && frame->source_backend)
                ok=source_draw_buffer_clear(renderer,error);
            break;
        case QA_SCENE_COMMAND_SWAP:
            ok=gl_swap(renderer,frame->source_backend && frame->source_front_buffer,error);
            break;
        case QA_SCENE_COMMAND_IMAGE:
            ok = gl_image_update(renderer, command->data.image, error);
            break;
        case QA_SCENE_COMMAND_OUTPUT_DOMAIN: {
            uint32_t width=0,height=0;
            ok=renderer->target==NULL && !renderer->opacity.active;
            if (!ok) qa_error_set(error,QA_ERROR_ARGUMENT,i,"Output color domain requires the actual GL display target");
            else ok=gl_dimensions(renderer,&width,&height,error) &&
                qa_output_domains_assign(&renderer->output_domains,command->data.output_domain.rect,
                    renderer->draw_buffer,command->data.output_domain.source,width,height,error);
            if (ok) renderer->preblend_gamma=false;
            break;
        }
        case QA_SCENE_COMMAND_PREBLEND_GAMMA:
            ok=renderer->target==NULL && !renderer->opacity.active;
            if (!ok) qa_error_set(error,QA_ERROR_ARGUMENT,i,"Generic overlay gamma requires the actual GL display target");
            else renderer->preblend_gamma=command->data.preblend_gamma.enabled;
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
    if (finish && renderer->opacity.active) {
        gl_opacity_abort(renderer);
        qa_error_set(error, QA_ERROR_ARGUMENT, frame->command_count,
                     "OpenGL frame ended inside an opacity scope");
        return false;
    }
    return true;
}
bool qa_gl_source_execute_prefix(qa_render_controls *controls, const qa_scene_frame *frame,
    size_t first, bool begin, bool finish, qa_error *error)
{
    if (!qa_gl_source_scratch_current(controls) || controls->ticket ||
        !controls->source.entered || !controls->source.issuing || controls->source.frame != frame) {
        qa_error_set(error, QA_ERROR_ARGUMENT, first, "OpenGL Source issue lost its actual renderer/frame owner");
        return false;
    }
    qa_gl_renderer *renderer = controls->owner.gl;
    renderer->executing = true;
    bool ok = gl_execute_range(renderer, frame, first, begin, finish, error);
    renderer->executing = false;
    return ok;
}
bool qa_gl_source_depth_range(qa_render_controls *controls,float near_depth,float far_depth,
    qa_error *error)
{
    if (!qa_gl_source_scratch_current(controls) || controls->ticket || !controls->source.entered ||
        !controls->source.issuing || !isfinite(near_depth) || !isfinite(far_depth) ||
        near_depth<0 || near_depth>1 || far_depth<0 || far_depth>1) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source depth range lost its actual OpenGL issue owner");
        return false;
    }
    qa_gl_renderer *renderer=controls->owner.gl;
    if (!qa_display_make_current(renderer->options.display,error)) return false;
    renderer->pipeline.depth_near=near_depth;
    renderer->pipeline.depth_far=far_depth;
    renderer->gl.DepthRange(near_depth,far_depth);
    return gl_check(renderer,"Source depth range",error);
}
bool qa_gl_source_polygon_offset(qa_render_controls *controls,bool enabled,float factor,float units,
    qa_error *error)
{
    if (!qa_gl_source_scratch_current(controls) || controls->ticket || !controls->source.entered ||
        !controls->source.issuing || !isfinite(factor) || !isfinite(units)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source polygon offset lost its actual OpenGL issue owner");
        return false;
    }
    qa_gl_renderer *renderer=controls->owner.gl;
    if (!qa_display_make_current(renderer->options.display,error)) return false;
    renderer->pipeline.polygon_offset=enabled;
    if (enabled) {
        renderer->pipeline.offset_factor=factor;
        renderer->pipeline.offset_units=units;
        renderer->gl.Enable(GL_POLYGON_OFFSET_FILL);
        renderer->gl.PolygonOffset(factor,units);
    } else renderer->gl.Disable(GL_POLYGON_OFFSET_FILL);
    return gl_check(renderer,"Source polygon offset",error);
}
bool qa_gl_source_cull(qa_render_controls *controls,qa_scene_cull cull,qa_error *error)
{
    if (!qa_gl_source_scratch_current(controls) || controls->ticket || !controls->source.entered ||
        !controls->source.issuing || (unsigned)cull>QA_CULL_BACK) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source cull lost its actual OpenGL issue owner");
        return false;
    }
    qa_gl_renderer *renderer=controls->owner.gl;
    if (!qa_display_make_current(renderer->options.display,error)) return false;
    renderer->pipeline.cull=cull;
    if (cull==QA_CULL_NONE) renderer->gl.Disable(GL_CULL_FACE);
    else {
        renderer->gl.Enable(GL_CULL_FACE);
        renderer->gl.CullFace(cull==QA_CULL_FRONT?GL_FRONT:GL_BACK);
    }
    return gl_check(renderer,"Source cull",error);
}
static bool source_texture_owner(qa_render_controls *controls,qa_error *error)
{
    if (!qa_gl_source_scratch_current(controls) || controls->ticket || !controls->source.entered ||
        !controls->source.issuing || controls->attributes.texture_unit>1) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source texture state lost its actual OpenGL issue owner");
        return false;
    }
    return qa_display_make_current(controls->owner.gl->options.display,error);
}
bool qa_gl_source_color(qa_render_controls *controls, qa_scene_vec4 color, qa_error *error)
{
    if (!source_texture_owner(controls, error)) return false;
    qa_gl_renderer *renderer = controls->owner.gl;
    controls->attributes.color = color;
    controls->attributes.color_known = true;
    renderer->gl.Color4f(color.x, color.y, color.z, color.w);
    renderer->gl.VertexAttrib4f(4, color.x, color.y, color.z, color.w);
    return gl_check(renderer, "Source current color", error);
}
bool qa_gl_source_texture_select(qa_render_controls *controls,uint32_t unit,qa_error *error)
{
    if (!source_texture_owner(controls,error) || unit>1) return false;
    qa_gl_renderer *renderer=controls->owner.gl;
    renderer->gl.ActiveTexture(GL_TEXTURE0+unit);
    renderer->gl.ClientActiveTexture(GL_TEXTURE0+unit);
    controls->attributes.texture_unit=unit;
    return gl_check(renderer,"Source texture-unit selection",error);
}
bool qa_gl_source_texture_enable(qa_render_controls *controls,bool enabled,qa_error *error)
{
    if (!source_texture_owner(controls,error)) return false;
    qa_gl_renderer *renderer=controls->owner.gl;
    renderer->gl.ActiveTexture(GL_TEXTURE0+controls->attributes.texture_unit);
    if (enabled) renderer->gl.Enable(GL_TEXTURE_2D); else renderer->gl.Disable(GL_TEXTURE_2D);
    controls->attributes.texture_enabled[controls->attributes.texture_unit]=enabled;
    return gl_check(renderer,"Source texture-unit enable",error);
}
bool qa_gl_source_texture_environment(qa_render_controls *controls,qa_scene_texture_environment environment,qa_error *error)
{
    if (!source_texture_owner(controls,error) || (unsigned)environment>QA_TEXTURE_REPLACE) return false;
    qa_gl_renderer *renderer=controls->owner.gl;
    renderer->gl.ActiveTexture(GL_TEXTURE0+controls->attributes.texture_unit);
    renderer->gl.TexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,
        environment==QA_TEXTURE_ADD?GL_ADD:environment==QA_TEXTURE_REPLACE?GL_REPLACE:GL_MODULATE);
    controls->attributes.environment[controls->attributes.texture_unit]=environment;
    return gl_check(renderer,"Source texture environment",error);
}
bool qa_gl_source_client_arrays(qa_render_controls *controls,bool color,bool uv,qa_error *error)
{
    if (!source_texture_owner(controls,error)) return false;
    qa_gl_renderer *renderer=controls->owner.gl;
    uint32_t unit=controls->attributes.texture_unit;
    if (color) renderer->gl.EnableVertexAttribArray(4); else renderer->gl.DisableVertexAttribArray(4);
    if (uv) renderer->gl.EnableVertexAttribArray(2+unit); else renderer->gl.DisableVertexAttribArray(2+unit);
    controls->attributes.color_array=color;
    controls->attributes.coordinate_array[unit]=uv;
    return gl_check(renderer,"Source client-array enable",error);
}
bool qa_gl_source_texture_bind(qa_render_controls *controls,const qa_scene_image *image,qa_error *error)
{
    if (!source_texture_owner(controls,error)) return false;
    return gl_source_texture_bind(controls->owner.gl,image,error);
}
bool qa_gl_source_stage_state(qa_render_controls *controls,const qa_scene_state *state,qa_error *error)
{
    if (!source_texture_owner(controls,error) || !state) return false;
    qa_gl_renderer *renderer=controls->owner.gl;
    qa_render_source_stage_state(&renderer->pipeline,state);
    stage_state_bits(renderer,&renderer->pipeline);
    return gl_check(renderer,"Source reached GL_State",error);
}
bool qa_gl_source_view_read(qa_render_controls *controls,qa_scene_view *out,qa_error *error)
{
    if (!out || !qa_gl_source_scratch_current(controls) || controls->ticket) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source view lost its actual OpenGL owner"); return false;
    }
    *out=controls->owner.gl->view; return true;
}
bool qa_gl_execute(qa_gl_renderer *renderer,const qa_scene_frame *frame,qa_error *error)
{
    if (renderer && renderer->controls.source.entered)
        return qa_material_source_frame_end(&renderer->controls.source, (qa_scene_frame *)frame, true, error);
    if (!gl_surface_idle(renderer,error)) return false;
    if (!renderer || renderer->detached || renderer->executing || renderer->capturing || renderer->preparing) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"OpenGL renderer is absent or executing"); return false;
    }
    renderer->executing=true; bool ok=gl_execute_range(renderer,frame,0,true,true,error);
    renderer->executing=false; return ok;
}
bool qa_gl_checkpoint_resources(const qa_gl_renderer *renderer,qa_render_resource_visit_fn visit,void *context,qa_error *error)
{
    if (!gl_surface_idle(renderer,error)) return false;
    if (!renderer || !visit || renderer->closed || renderer->executing || renderer->capturing || renderer->preparing || renderer->opacity.active) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"OpenGL resource observation requires its idle actual renderer owner"); return false;
    }
    size_t ordinal=0;
    for (const gl_texture_entry *entry=renderer->textures;entry;entry=entry->next,++ordinal)
        if (!visit(context,entry->image,NULL,ordinal,error)) return false;
    for (size_t i=0;i<2;++i,++ordinal)
        if (renderer->bound[i] && !visit(context,renderer->bound[i],NULL,ordinal,error)) return false;
    if (renderer->target && !visit(context,renderer->target,NULL,ordinal,error)) return false;
    ++ordinal;
    if (renderer->controls.source.lightmap &&
        !visit(context,renderer->controls.source.lightmap,NULL,ordinal,error)) return false;
    ++ordinal;
    size_t texture_levels=qa_gl_source_texture_metadata_count(&renderer->controls);
    for (size_t i=0;i<texture_levels;++i,++ordinal)
        if (!visit(context,qa_gl_source_texture_metadata_at(&renderer->controls,i),NULL,ordinal,error)) return false;
    ordinal=0;
    for (const gl_mesh_entry *entry=renderer->meshes;entry;entry=entry->next,++ordinal)
        if (!entry->geometry || (qa_scene_geometry_active(entry->geometry) &&
            !visit(context,NULL,entry->geometry,ordinal,error))) return false;
    return true;
}
size_t qa_gl_source_images_metadata_count(const qa_render_controls *controls)
{ return controls->owner.gl->source_image_count; }
const qa_scene_image *qa_gl_source_image_metadata_at(const qa_render_controls *controls,size_t ordinal)
{ return controls->owner.gl->source_images[ordinal]->image; }
bool qa_gl_checkpoint_meshes(const qa_gl_renderer *renderer,qa_gl_mesh_visit_fn visit,void *context,qa_error *error)
{
    if (!gl_surface_idle(renderer,error)) return false;
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
    if (!gl_surface_idle(renderer,error)) return false;
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
bool qa_gl_swap(qa_gl_renderer *renderer, qa_error *error)
{
    if (!gl_surface_idle(renderer,error)) return false;
    if (renderer->closed || renderer->detached || renderer->destroy_pending ||
        renderer->executing || renderer->capturing || renderer->preparing ||
        !qa_display_make_current(renderer->options.display,error)) {
        if (!error || error->code==QA_OK)
            qa_error_set(error,QA_ERROR_ARGUMENT,0,"OpenGL swap requires its actual idle renderer/display");
        return false;
    }
    qa_scene_frame frame;
    qa_scene_frame_init(&frame,renderer->options.owner);
    frame.source_backend=renderer->source_frame;
    bool ok=qa_material_source_swap_end(&renderer->controls.source,&frame,error);
    bool skip=frame.source_backend && frame.source_skip_backend;
    bool front=frame.source_backend && frame.source_front_buffer;
    qa_scene_frame_destroy(&frame);
    return ok && (skip || gl_swap(renderer,front,error));
}

bool qa_gl_set_gamma(qa_gl_renderer *renderer, float gamma, qa_error *error)
{
    if (!gl_surface_idle(renderer,error)) return false;
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

bool qa_gl_gamma_read(const qa_gl_renderer *renderer,float *out,qa_error *error)
{
    if (!renderer || !out || renderer->closed || renderer->destroy_pending ||
        !isfinite(renderer->gamma) || renderer->gamma<.5f || renderer->gamma>3) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"OpenGL brightness observation requires its actual live renderer"); return false;
    }
    *out=renderer->gamma; return true;
}
bool qa_gl_output_domain_read(const qa_gl_renderer *renderer,qa_scene_rect rect,bool *out,qa_error *error)
{
    if (!gl_surface_idle(renderer,error)) return false;
    if (!qa_gl_render_controls_current(&renderer->controls)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Output domain read requires its returned GL renderer"); return false;
    }
    if (renderer->target) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Output domain read requires its actual GL display target"); return false;
    }
    return qa_output_domains_rect_read(&renderer->output_domains,rect,renderer->draw_buffer,out,error);
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
    if (!gl_surface_idle(renderer,error)) return false;
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
    if (!gl_surface_idle(renderer,error)) return false;
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
    if (!gl_surface_idle(renderer,error)) return false;
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
    if (!gl_surface_idle(renderer,error)) return false;
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
    if (!gl_surface_idle(renderer,error)) return false;
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
    if (renderer && !gl_surface_idle(*renderer,error)) return false;
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
    replacement_renderer->controls.values = (*renderer)->controls.values;
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

bool qa_gl_surface_begin(qa_gl_renderer *renderer,qa_gl_surface_ticket **out,qa_error *error)
{
    if (!out || *out) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Surface capture requires empty retained ticket output"); return false;
    }
    if (!gl_surface_idle(renderer,error)) return false;
    if (renderer->closed || renderer->detached ||
        renderer->destroy_pending || renderer->executing || renderer->capturing || renderer->preparing ||
        renderer->opacity.active || renderer->target) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Surface capture requires its actual completed OpenGL display renderer");
        return false;
    }
    if (!qa_display_make_current(renderer->options.display,error)) return false;
    qa_display_info info={0};
    if (!qa_display_info_get(renderer->options.display,&info,error)) return false;
    if (info.backend!=QA_DISPLAY_OPENGL ||
        !info.drawable_width || !info.drawable_height || !SDL_GL_GetCurrentContext()) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Surface capture requires its actual OpenGL drawable and context"); return false;
    }
    if (info.drawable_width>renderer->capabilities.maximum_texture_size ||
        info.drawable_height>renderer->capabilities.maximum_texture_size) {
        qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"Actual drawable exceeds retained native texture limits"); return false;
    }
    qa_gl_surface_ticket *ticket=calloc(1,sizeof(*ticket));
    if (!ticket) { qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining OpenGL surface settings ticket"); return false; }
    ticket->renderer=renderer; ticket->original_display=renderer->options.display;
    ticket->original=*renderer;
    ticket->context=SDL_GL_GetCurrentContext(); ticket->original_width=info.drawable_width;
    ticket->original_height=info.drawable_height; ticket->targets.gl=renderer->gl;
    ticket->original_gamma=renderer->gamma; ticket->original_output=renderer->output;
    ticket->original_owner=renderer->options.owner; ticket->original_sequence=renderer->sequence;
    renderer->surface_ticket=ticket; *out=ticket;
    ticket->captured=gl_presentation_capture(renderer,&ticket->native,error);
    renderer->preparing=true;
    return ticket->captured;
}

static bool gl_surface_prepare(qa_gl_surface_ticket *ticket,qa_display *display,float gamma,
    bool same_endpoint,qa_error *error)
{
    qa_gl_renderer *renderer=ticket?ticket->renderer:NULL;
    if (!renderer || renderer->surface_ticket!=ticket || renderer->destroy_pending || !renderer->preparing ||
        !ticket->captured || ticket->attempted || ticket->published || !display ||
        (same_endpoint?(display!=ticket->original_display):(display==ticket->original_display)) ||
        renderer->options.display!=ticket->original_display || !isfinite(gamma) || gamma<0.5f || gamma>3 ||
        SDL_GL_GetCurrentContext()!=ticket->context) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"OpenGL surface preparation requires its entered compatible native candidate"); return false;
    }
    qa_display_info info={0}; SDL_Window *window=SDL_GL_GetCurrentWindow();
    if (!qa_display_info_get(display,&info,error)) return false;
    if (info.backend!=QA_DISPLAY_OPENGL || !window ||
        SDL_GetWindowID(window)!=info.window_id || !info.drawable_width || !info.drawable_height ||
        info.drawable_width>INT_MAX || info.drawable_height>INT_MAX ||
        (same_endpoint && (info.drawable_width!=ticket->original_width || info.drawable_height!=ticket->original_height))) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"OpenGL candidate is not the current native drawable"); return false;
    }
    ticket->attempted=true; ticket->candidate_display=display; ticket->window_id=info.window_id;
    ticket->width=info.drawable_width; ticket->height=info.drawable_height; ticket->gamma=gamma;
    bool resized=ticket->width!=ticket->original_width || ticket->height!=ticket->original_height;
    ticket->replace_output=resized || gamma!=renderer->gamma;
    ticket->replace_opacity=resized && renderer->opacity.allocated;
    ticket->targets=*renderer; ticket->targets.options.display=display;
    ticket->targets.surface_ticket=NULL; ticket->targets.destroy_pending=false; ticket->targets.preparing=false;
    if (ticket->replace_output) ticket->targets.output=(gl_output_target){0};
    if (ticket->replace_opacity) {
        memset(ticket->targets.opacity.framebuffer,0,sizeof(ticket->targets.opacity.framebuffer));
        memset(ticket->targets.opacity.color,0,sizeof(ticket->targets.opacity.color));
        ticket->targets.opacity.depth_stencil=0; ticket->targets.opacity.width=0; ticket->targets.opacity.height=0;
        ticket->targets.opacity.allocated=false;
    }
    bool floating_depth=false;
    if (!gl_native_depth(&ticket->targets,&floating_depth,error) || floating_depth!=renderer->capabilities.floating_depth) {
        if (error && error->code==QA_OK) qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"Candidate native depth representation differs from the retained visual");
        return false;
    }
    if (!gl_check(renderer,"Beginning compatible surface target preparation",error) ||
        !gl_presentation_copy(renderer,ticket->native,ticket->width,ticket->height,error) ||
        !gl_surface_targets_prepare(&ticket->targets,renderer,gamma,ticket->replace_output,
            ticket->replace_opacity,&ticket->reader,error) ||
        !gl_presentation_restore_bindings(renderer,ticket->native,error)) return false;
    if (ticket->replace_output && !gl_bind_destination(&ticket->targets,error)) return false;
    if (!gl_check(renderer,"Completing compatible surface target preparation",error)) return false;
    ticket->prepared=true;
    return qa_gl_surface_ready(ticket,error);
}

bool qa_gl_surface_prepare(qa_gl_surface_ticket *ticket,qa_display *display,float gamma,qa_error *error)
{ return gl_surface_prepare(ticket,display,gamma,false,error); }

bool qa_gl_gamma_prepare(qa_gl_renderer *renderer,qa_display *display,float gamma,qa_gl_surface_ticket **out,qa_error *error)
{
    if (!renderer || !display || renderer->options.display!=display || !isfinite(gamma) || gamma<.5f || gamma>3) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"OpenGL gamma preparation requires its actual display and brightness within 0.5..3"); return false;
    }
    return qa_gl_surface_begin(renderer,out,error) &&
        gl_surface_prepare(*out,renderer->options.display,gamma,true,error);
}

static bool gl_surface_output_same(const gl_output_target *a,const gl_output_target *b)
{
    if (a->framebuffer!=b->framebuffer || a->depth_stencil!=b->depth_stencil || a->table!=b->table ||
        a->width!=b->width || a->height!=b->height || a->enabled!=b->enabled) return false;
    for (size_t i=0;i<GL_DRAW_BUFFER_COUNT_QA;++i)
        if (a->color[i]!=b->color[i] || a->color_ready[i]!=b->color_ready[i] || a->dirty[i]!=b->dirty[i]) return false;
    return true;
}

static bool gl_surface_opacity_same(const gl_opacity_target *a,const gl_opacity_target *b)
{
    if (a->depth_stencil!=b->depth_stencil || a->width!=b->width || a->height!=b->height ||
        a->parent_draw_framebuffer!=b->parent_draw_framebuffer || a->parent_read_framebuffer!=b->parent_read_framebuffer ||
        a->parent_draw_buffer!=b->parent_draw_buffer || a->parent_read_buffer!=b->parent_read_buffer ||
        a->value!=b->value || a->parent_scissor_enabled!=b->parent_scissor_enabled ||
        a->allocated!=b->allocated || a->active!=b->active || a->skip!=b->skip) return false;
    for (size_t i=0;i<2;++i) if (a->framebuffer[i]!=b->framebuffer[i] || a->color[i]!=b->color[i]) return false;
    for (size_t i=0;i<4;++i) if (a->parent_viewport[i]!=b->parent_viewport[i] || a->parent_scissor[i]!=b->parent_scissor[i]) return false;
    return true;
}
static bool gl_surface_current(const qa_gl_surface_ticket *ticket)
{
    const qa_gl_renderer *renderer=ticket?ticket->renderer:NULL;
    if (!renderer || renderer->surface_ticket!=ticket || renderer->closed || renderer->destroy_pending ||
        renderer->detached || !renderer->preparing || renderer->executing || renderer->capturing || renderer->opacity.active || renderer->target ||
        !ticket->captured || !ticket->native || !ticket->prepared || ticket->published || renderer->options.display!=ticket->original_display ||
        renderer->gamma!=ticket->original_gamma || renderer->options.owner!=ticket->original_owner ||
        renderer->sequence!=ticket->original_sequence || !gl_surface_output_same(&renderer->output,&ticket->original_output) ||
        !gl_surface_opacity_same(&renderer->opacity,&ticket->original.opacity) ||
        renderer->textures!=ticket->original.textures || renderer->meshes!=ticket->original.meshes ||
        renderer->bound[0]!=ticket->original.bound[0] || renderer->bound[1]!=ticket->original.bound[1] ||
        renderer->target_framebuffer!=ticket->original.target_framebuffer || renderer->fog_depth!=ticket->original.fog_depth ||
        renderer->white_texture!=ticket->original.white_texture || renderer->restore!=ticket->original.restore ||
        renderer->draw_buffer!=ticket->original.draw_buffer || renderer->overdraw!=ticket->original.overdraw ||
        renderer->stream.vertex_buffer!=ticket->original.stream.vertex_buffer ||
        renderer->stream.index_buffer!=ticket->original.stream.index_buffer ||
        renderer->stream.vertex_bytes!=ticket->original.stream.vertex_bytes ||
        renderer->stream.index_bytes!=ticket->original.stream.index_bytes ||
        renderer->programs.stage!=ticket->original.programs.stage || renderer->programs.gamma!=ticket->original.programs.gamma ||
        renderer->programs.opacity!=ticket->original.programs.opacity ||
        ticket->targets.options.display!=ticket->candidate_display || ticket->targets.gamma!=ticket->gamma ||
        (ticket->targets.output.enabled && (!ticket->targets.output.table || !ticket->targets.output.framebuffer ||
            !ticket->targets.output.color_ready[gl_draw_buffer_index(renderer->draw_buffer)])) ||
        (ticket->replace_opacity && !ticket->targets.opacity.allocated)) return false;
    for (size_t i=0;i<3;++i) if (renderer->programs.fog[i]!=ticket->original.programs.fog[i]) return false;
    return true;
}
bool qa_gl_surface_ready(qa_gl_surface_ticket *ticket,qa_error *error)
{
    if (ticket) ticket->native_ready=false;
    qa_display_info info={0}; SDL_Window *window=SDL_GL_GetCurrentWindow();
    if (!gl_surface_current(ticket) || SDL_GL_GetCurrentContext()!=ticket->context ||
        !window || SDL_GetWindowID(window)!=ticket->window_id) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"OpenGL surface ticket is not prepared at the current actual drawable"); return false;
    }
    if (!qa_display_info_get(ticket->candidate_display,&info,error)) return false;
    if (info.drawable_width!=ticket->width || info.drawable_height!=ticket->height) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"OpenGL surface ticket drawable or target storage changed"); return false;
    }
    if (!qa_display_endpoint_read(ticket->original_display,&ticket->ready_original_endpoint) ||
        !qa_display_endpoint_read(ticket->candidate_display,&ticket->ready_candidate_endpoint) ||
        ticket->ready_original_endpoint.context!=ticket->context ||
        ticket->ready_candidate_endpoint.context!=ticket->context || ticket->ready_candidate_endpoint.window!=window) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"OpenGL readiness lost its actual retained display endpoint"); return false;
    }
    ticket->ready_output=ticket->targets.output; ticket->ready_opacity=ticket->targets.opacity;
    ticket->ready_reader=ticket->reader; ticket->native_ready=true;
    return true;
}
bool qa_gl_surface_ready_is(const qa_gl_surface_ticket *ticket)
{
    return gl_surface_current(ticket) && ticket->native_ready && !ticket->native_restored &&
        qa_display_endpoint_is(ticket->original_display,&ticket->ready_original_endpoint) &&
        qa_display_endpoint_is(ticket->candidate_display,&ticket->ready_candidate_endpoint) &&
        ticket->reader==ticket->ready_reader && gl_surface_output_same(&ticket->targets.output,&ticket->ready_output) &&
        gl_surface_opacity_same(&ticket->targets.opacity,&ticket->ready_opacity);
}

void qa_gl_surface_publish(qa_gl_surface_ticket *ticket)
{
    if (!ticket || !ticket->prepared || ticket->published) return;
    qa_gl_renderer *renderer=ticket->renderer;
    if (ticket->replace_output) {
        gl_output_target retired=renderer->output; renderer->output=ticket->targets.output; ticket->targets.output=retired;
    }
    if (ticket->replace_opacity) {
        gl_opacity_target retired=renderer->opacity; renderer->opacity=ticket->targets.opacity; ticket->targets.opacity=retired;
    }
    renderer->options.display=ticket->candidate_display; renderer->gamma=ticket->gamma;
    if (ticket->width!=ticket->original_width || ticket->height!=ticket->original_height) {
        ticket->retired_domains=renderer->output_domains; renderer->output_domains=(qa_output_domains){0};
    }
    if (ticket->width!=ticket->original_width || ticket->height!=ticket->original_height || ticket->replace_output)
        renderer->presented=false;
    ticket->published=true;
}

static bool gl_surface_objects_release(qa_gl_surface_ticket *ticket,qa_error *error)
{
    qa_gl_renderer *renderer=ticket->renderer;
    if (!gl_surface_targets_delete(&ticket->targets,ticket->replace_output,ticket->replace_opacity,error)) return false;
    if (ticket->reader) {
        if (!gl_check(renderer,"Preparing retained surface reader retirement",error)) return false;
        renderer->gl.DeleteFramebuffers(1,&ticket->reader);
        if (!gl_check(renderer,"Retiring retained surface reader",error)) return false;
        ticket->reader=0;
    }
    return gl_presentation_dispose(renderer,&ticket->native,error);
}

static void gl_surface_release(qa_gl_surface_ticket **out)
{
    qa_gl_surface_ticket *ticket=*out; qa_gl_renderer *renderer=ticket->renderer;
    qa_output_domains_destroy(&ticket->retired_domains);
    renderer->surface_ticket=NULL; renderer->preparing=false; free(ticket); *out=NULL;
    if (renderer->destroy_pending) qa_gl_destroy(renderer);
}

bool qa_gl_surface_abort(qa_gl_surface_ticket **out,qa_error *error)
{
    if (!out) { qa_error_set(error,QA_ERROR_ARGUMENT,0,"Invalid retained OpenGL surface abort"); return false; }
    qa_gl_surface_ticket *ticket=*out;
    if (!ticket) return true;
    ticket->native_ready=false;
    qa_gl_renderer *renderer=ticket->renderer; qa_display_info info={0};
    if (renderer->surface_ticket!=ticket || ticket->published || renderer->options.display!=ticket->original_display) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"OpenGL surface abort requires its restored original native endpoint"); return false;
    }
    if (!qa_display_make_current(ticket->original_display,error)) return false;
    if (SDL_GL_GetCurrentContext()!=ticket->context) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"OpenGL surface abort lost its retained native context"); return false;
    }
    if (!qa_display_info_get(ticket->original_display,&info,error)) return false;
    if (info.drawable_width!=ticket->original_width || info.drawable_height!=ticket->original_height) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"OpenGL surface abort requires its original native dimensions"); return false;
    }
    if (!ticket->native_restored) {
        if ((ticket->captured && !gl_presentation_copy(renderer,ticket->native,ticket->original_width,ticket->original_height,error)) ||
            !gl_presentation_restore_bindings(renderer,ticket->native,error)) return false;
        ticket->native_restored=true;
    }
    if (!gl_surface_objects_release(ticket,error)) return false;
    gl_surface_release(out); return true;
}

bool qa_gl_surface_retire(qa_gl_surface_ticket **out,qa_error *error)
{
    if (!out) { qa_error_set(error,QA_ERROR_ARGUMENT,0,"Invalid retained OpenGL surface retirement"); return false; }
    qa_gl_surface_ticket *ticket=*out;
    if (!ticket) return true;
    ticket->native_ready=false;
    qa_gl_renderer *renderer=ticket->renderer; qa_display_info info={0};
    if (renderer->surface_ticket!=ticket || !ticket->published || renderer->options.display!=ticket->candidate_display) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"OpenGL surface retirement requires its published actual native endpoint"); return false;
    }
    if (!qa_display_make_current(ticket->candidate_display,error)) return false;
    if (SDL_GL_GetCurrentContext()!=ticket->context) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"OpenGL surface retirement lost its retained native context"); return false;
    }
    if (!qa_display_info_get(ticket->candidate_display,&info,error)) return false;
    if (info.drawable_width!=ticket->width || info.drawable_height!=ticket->height) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"OpenGL surface retirement requires its published native dimensions"); return false;
    }
    if (!gl_surface_objects_release(ticket,error)) return false;
    gl_surface_release(out); return true;
}

bool qa_gl_source_overdraw(qa_render_controls *controls,bool enabled,qa_error *error)
{
    qa_gl_renderer *renderer=controls->owner.gl;
    if (enabled && renderer->capabilities.stencil_bits<4) {
        qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"Source overdraw requires four stencil bits"); return false;
    }
    if (renderer->overdraw==enabled) return true;
    if (!qa_display_make_current(renderer->options.display,error)) return false;
    renderer->overdraw=enabled;
    if (enabled) {
        renderer->gl.Enable(GL_STENCIL_TEST); renderer->gl.StencilMask(UINT_MAX);
        renderer->gl.ClearStencil(0); renderer->gl.StencilFunc(GL_ALWAYS,0,UINT_MAX);
        renderer->gl.StencilOp(GL_KEEP,GL_INCR,GL_INCR);
    } else renderer->gl.Disable(GL_STENCIL_TEST);
    return gl_check(renderer,"Source frame overdraw policy",error);
}
bool qa_gl_source_image_grid(qa_render_controls *controls,int32_t mode,qa_error *error)
{
    qa_gl_renderer *renderer=controls->owner.gl;
    uint32_t width=0,height=0;
    if (renderer->target || renderer->opacity.active || !qa_display_make_current(renderer->options.display,error) ||
        !gl_dimensions(renderer,&width,&height,error) || !gl_bind_destination(renderer,error)) return false;
    qa_scene_rect target={0,0,width,height};
    if (!controls->source.projection_2d) {
        qa_scene_state state=renderer->pipeline;
        qa_render_source_state_bits(&state,false,true);
        state.depth_test=QA_DEPTH_DISABLED; state.cull=QA_CULL_NONE;
        draw_state(renderer,&state,QA_SCENE_TRIANGLES);
        renderer->view=(qa_scene_view){.viewport=target,.depth=1};
        renderer->gl.Viewport(0,0,(GLsizei)width,(GLsizei)height);
        renderer->gl.Scissor(0,0,(GLsizei)width,(GLsizei)height);
        renderer->gl.Enable(GL_SCISSOR_TEST);
        controls->source.projection_2d=true;
    }
    renderer->gl.Clear(GL_COLOR_BUFFER_BIT);
    renderer->gl.Finish();
    uint64_t start=SDL_GetTicks64();
    qa_scene_frame frame; qa_scene_frame_init(&frame,renderer->options.owner);
    bool ok=true;
    for (uint32_t i=0;ok && i<renderer->source_image_count;++i) {
        gl_texture_entry *entry=renderer->source_images[i];
        const qa_scene_image *image=entry->image;
        float w=(float)(width/20),h=(float)(height/15);
        float x=(float)(i%20)*w,y=(float)(i/20)*h;
        if (mode==2 && entry->source_texture.count) {
            w*= (float)entry->source_texture.levels[0].width/512;
            h*= (float)entry->source_texture.levels[0].height/512;
        }
        const qa_scene_image *binding=image;
        if (controls->frame_values.no_bind) {
            const qa_scene_image *dlight=NULL;
            for (uint32_t j=renderer->source_image_count;j>0;--j)
                if (renderer->source_images[j-1]->image->source_dlight) { dlight=renderer->source_images[j-1]->image; break; }
            if (dlight) binding=dlight;
        }
        size_t first=frame.command_count;
        ok=qa_scene_frame_picture_f(&frame,binding,target,(qa_scene_rect_f){x,y,w,h},
            (qa_scene_vec4){0,0,1,1},controls->attributes.color,error);
        for (size_t c=first;ok && c<frame.command_count;++c) {
            if (frame.commands[c].kind!=QA_SCENE_COMMAND_DRAW) continue;
      qa_scene_draw *draw=&frame.commands[c].data.draw;
            draw->source_direct=QA_SOURCE_DIRECT_IMAGE_GRID;
            ok=draw_scene(renderer,draw,error);
        }
    }
    qa_scene_frame_destroy(&frame);
    renderer->gl.Finish();
    if (ok) ok=gl_check(renderer,"Source image grid",error);
    if (ok && controls->source_print) {
        char text[100]; snprintf(text,sizeof(text),"%llu msec to draw all images\n",(unsigned long long)(SDL_GetTicks64()-start));
        controls->source_print(controls->source_print_context,text);
    }
    return ok;
}
