#include "qa/platform_services.h"
#include "internal.h"
#include "qa/render_workers.h"
#include "particles.h"
#include "qa/render_gl_save.h"
#include "qa/display_settings.h"
#include "qa/q3_source_scene_bank.h"

#include <SDL_video.h>
#include <SDL_loadso.h>
#include <SDL_timer.h>
#include <limits.h>
#include <fenv.h>
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
static const qa_gl_renderer *controls_idle_owner(const qa_render_controls *controls)
{
    const qa_gl_renderer *renderer = controls ? controls->owner.gl : NULL;
    return renderer && controls->backend == QA_RENDER_CONTROLS_GL &&
        &renderer->controls == controls && !renderer->closed &&
        !renderer->destroy_pending && !renderer->executing && !renderer->capturing &&
        !renderer->opacity.active ? renderer : NULL;
}
bool qa_gl_render_controls_current(const qa_render_controls *controls)
{
    const qa_gl_renderer *renderer = controls_idle_owner(controls);
    return renderer && !renderer->detached && (!renderer->preparing || renderer->surface_ticket);
}
void qa_gl_render_controls_enter(qa_render_controls *controls)
{ gl_state_invalidate(controls->owner.gl); }

bool qa_gl_render_controls_callback_candidate(const qa_render_controls *controls)
{
    const qa_gl_renderer *renderer = controls_idle_owner(controls);
    return renderer && renderer->detached && renderer->restore && !renderer->preparing &&
        !renderer->surface_ticket && controls->source.owner == controls;
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
    gl_state_framebuffer(renderer, GL_FRAMEBUFFER,0);
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
    gl_state_framebuffer(renderer, GL_DRAW_FRAMEBUFFER,(GLuint)draw); gl_state_framebuffer(renderer, GL_READ_FRAMEBUFFER,(GLuint)read);
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

GLenum gl_native_buffer(bool stereo,size_t slot)
{
    static const GLenum buffers[4]={GL_FRONT_LEFT,GL_FRONT_RIGHT,GL_BACK_LEFT,GL_BACK_RIGHT};
    return stereo?buffers[slot]:(slot==0?GL_FRONT:GL_BACK);
}

static bool gl_native_buffers(qa_gl_renderer *renderer,bool stereo,uint32_t *mask,qa_error *error)
{
    gl_api *gl=&renderer->gl; GLint framebuffer=0,read_buffer=0;
    gl->GetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&framebuffer);
    if (!gl_check(renderer,"Reading native color buffer bindings",error)) return false;
    gl_state_framebuffer(renderer, GL_READ_FRAMEBUFFER,0);
    gl->GetIntegerv(GL_READ_BUFFER,&read_buffer);
    if (!gl_check(renderer,"Beginning native color buffer discovery",error)) return false;
    bool ok=true; *mask=0;
    const char *driver=SDL_GetCurrentVideoDriver();
    bool back_only=driver && !strcmp(driver,"wayland");
    for (size_t i=0;i<4;++i) {
        /* EGL window surfaces expose no preserved front image after swap,
         * even when selecting GL_FRONT succeeds. Retain the completed back image. */
        if (back_only && i<2) continue;
        if (!stereo && (i&1)) continue;
        gl_state_read_buffer(renderer, gl_native_buffer(stereo,i));
        GLenum status=gl->GetError();
        if (status==GL_NO_ERROR) *mask|=1u<<i;
        else if (status!=GL_INVALID_OPERATION) {
            qa_error_set(error,QA_ERROR_IO,0,"Discovering native color buffer 0x%x failed with OpenGL error 0x%x",
                (unsigned)gl_native_buffer(stereo,i),(unsigned)status); ok=false; break;
        }
    }
    gl_state_read_buffer(renderer, (GLenum)read_buffer);
    gl_state_framebuffer(renderer, GL_READ_FRAMEBUFFER,(GLuint)framebuffer);
    if (!gl_check(renderer,"Restoring native color buffer discovery bindings",error)) return false;
    uint32_t required=stereo?12u:4u;
    if (ok && (*mask&required)!=required) {
        qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"Native drawable has no required back color buffer"); return false;
    }
    return ok;
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
    if (!gl_native_buffers(renderer,caps->stereo,&caps->native_buffer_mask,error)) return false;
    caps->floating_depth = floating_depth;
    const char *extensions=(const char *)gl->GetString(GL_EXTENSIONS);
    unsigned major=0,minor=0;
    (void)sscanf(caps->version,"%u.%u",&major,&minor);
    if (!(major>=3 || gl_extension(extensions,"GL_ARB_vertex_array_object")) ||
        !gl->GenVertexArrays || !gl->DeleteVertexArrays || !gl->BindVertexArray) {
        gl->GenVertexArrays=NULL; gl->DeleteVertexArrays=NULL; gl->BindVertexArray=NULL;
    }
    if (!(major>3 || (major==3 && minor>=2) ||
          gl_extension(extensions,"GL_ARB_draw_elements_base_vertex")))
        gl->DrawElementsBaseVertex=NULL;
    caps->compiled_vertex_arrays = gl->LockArraysEXT && gl->UnlockArraysEXT &&
        gl_extension(extensions, "GL_EXT_compiled_vertex_array");
    caps->s3tc=gl_extension(extensions,"GL_S3_s3tc");
    if ((major > 4 || (major == 4 && minor >= 3)) && gl->GetInteger64v &&
        gl->BindBufferBase && gl->BindBufferRange) {
        GLint blocks = 0, bindings = 0, alignment = 0;
        GLint64 limit = 0;
        gl->GetIntegerv(GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS, &blocks);
        gl->GetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &bindings);
        gl->GetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &alignment);
        gl->GetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &limit);
        if (blocks >= 2 && bindings >= 2 && alignment > 0 && limit >= 8192 &&
            (uint64_t)limit <= SIZE_MAX) {
            caps->skeletal_buffer_limit = (size_t)limit;
            caps->skeletal_skinning = true;
            renderer->skin_palette_alignment = (size_t)alignment;
        }
    }
    return gl_check(renderer, "OpenGL capability query", error);
}

qa_gl_renderer *gl_renderer_allocate(const qa_gl_options *options, const qa_display_info *info, qa_error *error)
{
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
    renderer->view.viewport = (qa_scene_rect){0, 0, info->drawable_width,
                                              info->drawable_height};
    renderer->view.depth = 1;
    return renderer;
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
    qa_gl_renderer *renderer = gl_renderer_allocate(options, &info, error);
    if (!renderer) return NULL;
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
    gl_state_front_face(renderer, GL_CCW);
    gl_state_enable(renderer, GL_BLEND, false);
    gl_state_enable(renderer, GL_CULL_FACE, false);
    gl_state_enable(renderer, GL_STENCIL_TEST, false);
    gl_state_enable(renderer, GL_POLYGON_OFFSET_FILL, false);
    gl_state_enable(renderer, GL_DEPTH_TEST, true);
    gl_state_depth_mask(renderer, GL_TRUE);
    gl_state_color_mask(renderer, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    renderer->gl.ClearStencil(0);
    renderer->gl.ClearDepth(renderer->clear_depth);
    gl_state_stencil_mask(renderer, UINT_MAX);
    gl_state_polygon_mode(renderer, GL_FRONT_AND_BACK, GL_FILL);
    gl_state_line_width(renderer, 1);
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
    gl_particles_destroy(renderer);
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
        gl_world_pages_destroy(renderer, false);
        gl_skeletal_destroy(renderer, false);
        render_resource_destroy(&renderer->texture_index);
        render_resource_destroy(&renderer->source_image_index);
        render_resource_destroy(&renderer->mesh_index);
        free(renderer);
        return;
    }
    gl_restore_storage_destroy(renderer);
    if (!renderer->detached) gl_mesh_unbind(renderer);
    gl_opacity_destroy(renderer);
    gl_output_destroy(renderer);
    if (renderer->presented_target.framebuffer)
        renderer->gl.DeleteFramebuffers(1,&renderer->presented_target.framebuffer);
    gl_state_delete_textures(renderer, 2,renderer->presented_target.color);
    if (renderer->target_framebuffer != 0)
        renderer->gl.DeleteFramebuffers(1, &renderer->target_framebuffer);
    if (renderer->fog_depth != 0)
        gl_state_delete_textures(renderer, 1, &renderer->fog_depth);
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
    gl_api *gl = &renderer->gl;
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
    if (view->clear_depth) {
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
    gl_state_enable(renderer, GL_SCISSOR_TEST, true);
    gl->Scissor(view->viewport.x, (GLint)bottom,
                (GLsizei)view->viewport.width,
                (GLsizei)view->viewport.height);
    GLbitfield clear = 0;
    if (view->clear_color) {
        renderer->pipeline.color_write=true;
        gl_state_color_mask(renderer, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        gl->ClearColor(view->color.x, view->color.y, view->color.z,
                       view->color.w);
        clear |= GL_COLOR_BUFFER_BIT;
    }
    if (view->clear_depth) {
        renderer->pipeline.depth_write=true;
        gl_state_depth_mask(renderer, GL_TRUE);
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
        gl_state_stencil_mask(renderer, UINT_MAX);
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
    if (state->depth_test==QA_DEPTH_DISABLED) gl_state_enable(renderer, GL_DEPTH_TEST, false); else gl_state_enable(renderer, GL_DEPTH_TEST, true);
    gl_state_depth_func(renderer, state->depth_test==QA_DEPTH_ALWAYS?GL_ALWAYS:
        state->depth_test==QA_DEPTH_LEQUAL || state->depth_test==QA_DEPTH_DISABLED?GL_LEQUAL:
        state->depth_test==QA_DEPTH_EQUAL?GL_EQUAL:state->depth_test==QA_DEPTH_GEQUAL?GL_GEQUAL:GL_LESS);
    gl_state_depth_mask(renderer, state->depth_write?GL_TRUE:GL_FALSE);
    if (state->blend_source==QA_BLEND_ONE && state->blend_destination==QA_BLEND_ZERO) gl_state_enable(renderer, GL_BLEND, false);
    else { gl_state_enable(renderer, GL_BLEND, true); gl_state_blend_func(renderer, blend_factor(state->blend_source),blend_factor(state->blend_destination)); }
    gl_state_polygon_mode(renderer, GL_FRONT_AND_BACK,state->wireframe?GL_LINE:GL_FILL);
}

static void draw_state(qa_gl_renderer *renderer, const qa_scene_state *state,
                       qa_scene_primitive primitive)
{
    renderer->pipeline=*state;
    stage_state_bits(renderer,state);
    gl_state_color_mask(renderer, state->color_write ? GL_TRUE : GL_FALSE,
                  state->color_write ? GL_TRUE : GL_FALSE,
                  state->color_write ? GL_TRUE : GL_FALSE,
                  state->color_write ? GL_TRUE : GL_FALSE);
    if (state->cull == QA_CULL_NONE) gl_state_enable(renderer, GL_CULL_FACE, false);
    else {
        gl_state_enable(renderer, GL_CULL_FACE, true);
        gl_state_cull_face(renderer, state->cull == QA_CULL_FRONT ? GL_FRONT : GL_BACK);
    }
    gl_state_depth_range(renderer, state->depth_near, state->depth_far);
    if (state->polygon_offset) {
        gl_state_enable(renderer, GL_POLYGON_OFFSET_FILL, true);
        gl_state_polygon_offset(renderer, state->offset_factor, state->offset_units);
    } else gl_state_enable(renderer, GL_POLYGON_OFFSET_FILL, false);
    gl_state_line_width(renderer, primitive == QA_SCENE_LINES || state->wireframe
                      ? state->line_width : 1);
    if (renderer->overdraw) {
        gl_state_enable(renderer, GL_STENCIL_TEST, true);
        gl_state_stencil_mask(renderer, UINT_MAX);
        gl_state_stencil_func(renderer, GL_ALWAYS, 0, UINT_MAX);
        gl_state_stencil_op(renderer, GL_KEEP, GL_INCR, GL_INCR);
    } else if (state->stencil_enabled) {
        gl_state_enable(renderer, GL_STENCIL_TEST, true);
        GLenum test = state->stencil_test == QA_STENCIL_ALWAYS ? GL_ALWAYS :
                      state->stencil_test == QA_STENCIL_EQUAL ? GL_EQUAL :
                                                                GL_NOTEQUAL;
        gl_state_stencil_mask(renderer, state->stencil_write_mask);
        gl_state_stencil_func(renderer, test, (GLint)state->stencil_reference,
                        state->stencil_compare_mask);
        gl_state_stencil_op(renderer, stencil_operation(state->stencil_fail),
                      stencil_operation(state->stencil_depth_fail),
                      stencil_operation(state->stencil_depth_pass));
    } else gl_state_enable(renderer, GL_STENCIL_TEST, false);
}

void gl_source_pipeline_restore(qa_gl_renderer *renderer)
{
    gl_api *gl=&renderer->gl;
    draw_state(renderer,&renderer->pipeline,QA_SCENE_TRIANGLES);
    gl_state_line_width(renderer, renderer->pipeline.line_width);
    gl->ClearDepth(renderer->clear_depth);
    qa_render_source_attributes *attributes=&renderer->controls.attributes;
    for (uint32_t unit=0;unit<2;++unit) {
        GLuint name=0;
        if (!attributes->actual_empty[unit] && renderer->bound[unit]) {
            const gl_texture_entry *entry = gl_texture_resident(renderer, renderer->bound[unit]);
            if (entry && entry->image == renderer->bound[unit]) name = entry->name;
        }
        gl_state_active_texture(renderer, GL_TEXTURE0+unit);
        gl_state_bind_texture(renderer, GL_TEXTURE_2D,name);
        if (!name) {
            const qa_scene_vec4 *border=&attributes->zero_border;
            GLfloat values[4]={border->x,border->y,border->z,border->w};
            gl->TexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_BORDER_COLOR,values);
        }
        if (attributes->texture_enabled[unit]) gl_state_enable(renderer, GL_TEXTURE_2D, true); else gl_state_enable(renderer, GL_TEXTURE_2D, false);
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
    gl_state_active_texture(renderer, GL_TEXTURE0+attributes->texture_unit);
    gl->ClientActiveTexture(GL_TEXTURE0+attributes->texture_unit);
}

static bool draw_valid(const qa_gl_renderer *renderer,
                       const qa_scene_draw *draw, gl_mesh_entry **resident,
                       qa_error *error)
{
    const qa_scene_state *state = &draw->state;
    bool stencil_shadow = state->stencil_fail == QA_STENCIL_INCREMENT ||
                          state->stencil_fail == QA_STENCIL_DECREMENT ||
                          state->stencil_depth_fail == QA_STENCIL_INCREMENT ||
                          state->stencil_depth_fail == QA_STENCIL_DECREMENT ||
                          state->stencil_depth_pass == QA_STENCIL_INCREMENT ||
                          state->stencil_depth_pass == QA_STENCIL_DECREMENT;
    if (draw->texture_count > 2 ||
        (unsigned)draw->environment > QA_TEXTURE_LIGHTMAP_INVERT_ALPHA ||
        (unsigned)draw->lighting > QA_LIGHT_Q2_MODEL_SHADOW ||
        (unsigned)draw->light_pass > QA_LIGHT_PASS_MODEL ||
        (unsigned)draw->mesh.primitive > QA_SCENE_LINES ||
        (unsigned)state->blend_source > QA_BLEND_SRC_ALPHA_SATURATE ||
        (unsigned)state->blend_destination > QA_BLEND_SRC_ALPHA_SATURATE ||
        state->blend_destination == QA_BLEND_SRC_ALPHA_SATURATE ||
        (unsigned)state->depth_test > QA_DEPTH_DISABLED ||
        (unsigned)state->alpha_test > QA_ALPHA_GT666 ||
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
        (draw->vertex_inputs.constant_color && !finite4(draw->vertex_inputs.color)) ||
        !material_source_vertex_storage_valid(draw) ||
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
    *resident = gl_mesh_resident(renderer, &draw->mesh);
    if (!gl_mesh_storage_matches(*resident, &draw->mesh)) {
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

static bool skeletal_draw_prepare(qa_gl_renderer *renderer, qa_scene_draw *draw, qa_error *error)
{
    const qa_scene_skinning *skin = draw->skinning;
    if (!skin) return true;
    qa_scene_geometry_view geometry;
    if (!skin->pose || !skin->pose->joints || !draw->mesh.identity ||
        !qa_scene_geometry_read(draw->mesh.geometry, &geometry) ||
        geometry.skeletal.vertex_count != draw->mesh.vertex_count ||
        skin->pose->count < geometry.skeletal.bone_count ||
        !finite3(skin->shade_direction) || !finite3(skin->light) || !finite4(skin->tint) ||
        !isfinite(skin->shell) || draw->source_primitives || draw->source_arrays ||
        draw->source_direct || draw->source_stage_state || draw->source_vertex_storage) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid skeletal draw descriptor"); return false;
    }
    size_t ordinal = skin->pose->ordinal;
    int rounding = skin->sample ? skin->sample->rounding : fegetround();
    if (rounding == FE_TONEAREST && qa_gl_skeletal_geometry_supported(renderer, draw->mesh.geometry) &&
        ordinal < renderer->skin_palette_count && renderer->skin_palettes[ordinal].pose == skin->pose)
        return true;
    size_t count = draw->mesh.vertex_count;
    if (count > renderer->skin_vertex_capacity) {
        if (count > (size_t)PTRDIFF_MAX / sizeof(*renderer->skin_vertices) ||
            count > (size_t)PTRDIFF_MAX / sizeof(*renderer->skin_sampled)) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Skeletal fallback vertex span overflows"); return false;
        }
        qa_scene_vertex *vertices = malloc(count * sizeof(*vertices));
        qa_model_vertex *sampled = malloc(count * sizeof(*sampled));
        if (!vertices || !sampled) {
            free(vertices); free(sampled); qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating skeletal fallback vertices"); return false;
        }
        free(renderer->skin_vertices); free(renderer->skin_sampled);
        renderer->skin_vertices = vertices; renderer->skin_sampled = sampled;
        renderer->skin_vertex_capacity = count;
    }
    const qa_model_vertex *sampled = renderer->skin_sampled;
    if (skin->sample) {
        qa_scene_skin_sample *sample = skin->sample;
        if (!sample->ready) {
            qa_render_model_job job = {.view = sample->view, .pose = skin->pose,
                .vertices = sample->vertices, .vertex_count = sample->count, .rounding = sample->rounding};
            if (!qa_render_workers_skin_batch(NULL, &job, 1, error)) return false;
            sample->error = job.error;
            sample->ready = job.completed && job.error.code == QA_OK;
        }
        if (!sample->ready || sample->error.code != QA_OK) {
            if (error) *error = sample->error;
            return false;
        }
        if (sample->count < geometry.skeletal.source_vertex_count || (count && !sample->vertices)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Skeletal fallback lost its source sample extent"); return false;
        }
        sampled = sample->vertices;
    } else {
        qa_render_model_job job = {.view = {.vertices = geometry.vertices,
            .vertex_stride = sizeof(qa_scene_vertex), .normal_offset = offsetof(qa_scene_vertex, normal),
            .weights = geometry.skeletal.weights, .ranges = geometry.skeletal.ranges,
            .vertex_count = geometry.skeletal.vertex_count, .weight_count = geometry.skeletal.weight_count,
            .bone_count = geometry.skeletal.bone_count}, .pose = skin->pose,
            .vertices = renderer->skin_sampled, .vertex_count = count, .rounding = rounding};
        if (!qa_render_workers_skin_batch(NULL, &job, 1, error)) return false;
        if (!job.completed || job.error.code != QA_OK) {
            if (error) *error = job.error;
            return false;
        }
    }
    int original_rounding = fegetround();
    if (rounding != original_rounding && fesetround(rounding) != 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Skeletal draw has an unsupported rounding mode"); return false;
    }
    for (size_t i = 0; i < count; ++i) {
        size_t source = skin->sample ? geometry.skeletal.sources[i] : i;
        qa_scene_skin_apply(skin, sampled + source, geometry.vertices + i, renderer->skin_vertices + i);
    }
    if (rounding != original_rounding) (void)fesetround(original_rounding);
    draw->mesh.vertices = renderer->skin_vertices;
    draw->mesh.identity = draw->mesh.revision = 0;
    draw->skinning = NULL;
    return true;
}

static bool draw_scene(qa_gl_renderer *renderer, const qa_scene_draw *source,
                       const gl_world_group *group, qa_error *error)
{
    qa_scene_draw base, lightmap;
    if (!gl_lightmap_combined(source) && qa_scene_draw_lightmap_split(source, &base, &lightmap))
        return draw_scene(renderer, &base, NULL, error) && draw_scene(renderer, &lightmap, NULL, error);
    qa_scene_draw draw = *source;
    if (!skeletal_draw_prepare(renderer, &draw, error)) return false;
    gl_mesh_entry *resident = NULL;
    qa_render_source_direct_state(&draw.state,&renderer->pipeline,source);
    if ((unsigned)draw.source_direct>QA_SOURCE_DIRECT_IMAGE_GRID || !draw_valid(renderer,&draw,&resident,error)) {
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
    if (draw.source_stage_state) gl_state_line_width(renderer, draw.state.line_width);
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
    for (unsigned unit = 0; unit < draw.texture_count; ++unit) {
        if (!source_pipeline && textures[unit] != NULL) {
            gl_state_texture_2d(renderer, GL_TEXTURE0 + unit, textures[unit]->name);
        }
    }
    gl_state_texture_2d(renderer, GL_TEXTURE2,
                             renderer->preblend_gamma && renderer->gamma!=1 &&
                               !qa_display_gamma_applied_is(renderer->options.display) ? renderer->output.table :
                             shadow == NULL ? renderer->white_texture :
                                              shadow->name);
    gl_state_active_texture(renderer, GL_TEXTURE0+renderer->controls.attributes.texture_unit);
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
    size_t index_offset;
    GLint base_vertex = 0;
    size_t index_count = group ? group->index_count : draw.mesh.index_count;
    if (group) {
        if (!gl_world_group_bind(renderer, group, &draw.vertex_inputs, &index_offset, error)) return false;
    } else if (!gl_mesh_bind(renderer, &uploaded, resident, &draw.vertex_inputs,
                      !source_pipeline && !draw.source_primitives,&index_offset,&base_vertex,error)) return false;
    if (draw.skinning && !gl_skeletal_bind(renderer, &draw, error)) return false;
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
    if (mode == QA_RENDER_PRIMITIVES_INDEXED && index_count != 0) {
        GLenum primitive=draw.mesh.primitive==QA_SCENE_LINES?GL_LINES:GL_TRIANGLES;
        if (base_vertex)
            renderer->gl.DrawElementsBaseVertex(primitive,(GLsizei)index_count,
                GL_UNSIGNED_INT,(const void *)(uintptr_t)index_offset,base_vertex);
        else renderer->gl.DrawElements(primitive,(GLsizei)index_count,
                GL_UNSIGNED_INT,(const void *)(uintptr_t)index_offset);
    }
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
    if (source_pipeline || draw.source_primitives || !renderer->gl.GenVertexArrays) {
        gl_mesh_unbind(renderer);
        if (renderer->controls.attributes.color_array) renderer->gl.EnableVertexAttribArray(4);
        for (uint32_t unit=0;unit<2;++unit)
            if (renderer->controls.attributes.coordinate_array[unit]) renderer->gl.EnableVertexAttribArray(2+unit);
    }
    if (draw.source_direct==QA_SOURCE_DIRECT_AXIS) {
        gl_state_line_width(renderer, 1);
        renderer->pipeline.line_width=1;
    }
    if (draw.source_direct==QA_SOURCE_DIRECT_SHADOW_FINISH) {
        gl_state_enable(renderer, GL_STENCIL_TEST, false);
        renderer->pipeline.stencil_enabled=false;
    }
    if (draw.source_direct==QA_SOURCE_DIRECT_SHADOW_VOLUME_END) {
        gl_state_color_mask(renderer, GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        renderer->pipeline.color_write=true;
    }
    return gl_check(renderer, "OpenGL scene draw", error);
}

static bool world_draw(const qa_scene_draw *draw)
{
    return !draw->skinning && draw->light_count <= GL_MAX_LIGHTS_QA && (!draw->light_count || draw->lights) &&
        draw->single_coverage && draw->mesh.identity && draw->mesh.geometry &&
        draw->mesh.vertex_count && draw->mesh.index_count && draw->mesh.primitive == QA_SCENE_TRIANGLES &&
        draw->state.blend_source == QA_BLEND_ONE && draw->state.blend_destination == QA_BLEND_ZERO &&
        draw->state.depth_test == QA_DEPTH_LEQUAL && draw->state.depth_write && draw->state.color_write &&
        !draw->state.stencil_enabled && !draw->state.wireframe && !draw->state.polygon_offset &&
        !draw->source_primitives && !draw->source_arrays && !draw->source_direct && !draw->source_stage_state &&
        !draw->source_retain_depth_range && !draw->source_retain_polygon_offset && !draw->source_vertex_storage &&
        !draw->retain_texture[0] && !draw->retain_texture[1] &&
        (draw->environment <= QA_TEXTURE_REPLACE || gl_lightmap_combined(draw));
}
static bool world_draw_matches(const qa_scene_draw *first, const qa_scene_draw *next)
{
    if (!world_draw(next)) return false;
#define SAME(field) do { if (first->field != next->field) return false; } while (0)
    SAME(texture_count); SAME(environment); SAME(lighting);
    SAME(light_pass); SAME(light_count); SAME(shadow_atlas);
    SAME(shadow_near); SAME(shade_scale); SAME(model_shade_scale);
    SAME(luminance_alpha); SAME(entity); SAME(fog_index);
    SAME(light_mask); SAME(vertex_inputs.constant_color); SAME(vertex_inputs.swap_uv);
    SAME(state.blend_source); SAME(state.blend_destination); SAME(state.depth_test);
    SAME(state.alpha_test); SAME(state.cull); SAME(state.depth_write);
    SAME(state.color_write); SAME(state.polygon_offset); SAME(state.wireframe);
    SAME(state.depth_near); SAME(state.depth_far); SAME(state.offset_factor);
    SAME(state.offset_units); SAME(state.line_width); SAME(state.stencil_enabled);
    SAME(state.stencil_test); SAME(state.stencil_reference); SAME(state.stencil_compare_mask);
    SAME(state.stencil_write_mask); SAME(state.stencil_fail); SAME(state.stencil_depth_fail);
    SAME(state.stencil_depth_pass); SAME(fog.kind); SAME(fog.effect);
    SAME(fog.density); SAME(fog.amount); SAME(fog.sky_factor);
    SAME(fog.height_density); SAME(fog.height_start); SAME(fog.height_end);
    SAME(fog.height_falloff); SAME(fog.far_depth); SAME(fog.sky_drawn);
#undef SAME
    if (first->textures[0] != next->textures[0] || first->textures[1] != next->textures[1] ||
        memcmp(&first->vertex_inputs.color, &next->vertex_inputs.color, sizeof(first->vertex_inputs.color)) ||
        memcmp(first->model.m, next->model.m, sizeof(first->model.m)) ||
        memcmp(first->mvp.m, next->mvp.m, sizeof(first->mvp.m)) ||
        memcmp(&first->fog.color, &next->fog.color, sizeof(first->fog.color)) ||
        memcmp(&first->fog.height_color, &next->fog.height_color, sizeof(first->fog.height_color)) ||
        memcmp(&first->fog.height_end_color, &next->fog.height_end_color, sizeof(first->fog.height_end_color))) return false;
    for (size_t i = 0; i < first->light_count; ++i) {
        const qa_scene_shadow_light *a = first->lights + i, *b = next->lights + i;
#define LIGHT(field) do { if (a->field != b->field) return false; } while (0)
        LIGHT(light.radius); LIGHT(light.minimum); LIGHT(light.scale); LIGHT(light.cos_half_angle);
        LIGHT(light.additive); LIGHT(light.spot); LIGHT(light.casts_shadow); LIGHT(light.identity); LIGHT(light.revision);
        LIGHT(light.shadow_resolution); LIGHT(light.family); LIGHT(point_shadow); LIGHT(shadow_valid);
#undef LIGHT
        if (memcmp(&a->light.origin, &b->light.origin, sizeof(a->light.origin)) ||
            memcmp(&a->light.color, &b->light.color, sizeof(a->light.color)) ||
            memcmp(&a->light.direction, &b->light.direction, sizeof(a->light.direction)) ||
            memcmp(&a->atlas_rect, &b->atlas_rect, sizeof(a->atlas_rect)) ||
            memcmp(a->shadow_matrix.m, b->shadow_matrix.m, sizeof(a->shadow_matrix.m)) ||
            memcmp(&a->model_fraction, &b->model_fraction, sizeof(a->model_fraction))) return false;
    }
    return true;
}

static bool world_draw_run(qa_gl_renderer *renderer, const qa_scene_frame *frame,
                            size_t first, size_t *consumed, qa_error *error)
{
    *consumed = 1;
    const qa_scene_draw *draw = &frame->commands[first].data.draw;
    size_t count = 1;
    if (world_draw(draw))
        while (count < frame->command_count - first &&
               frame->commands[first + count].kind == QA_SCENE_COMMAND_DRAW &&
               world_draw_matches(draw, &frame->commands[first + count].data.draw)) ++count;
    if (count == 1) return draw_scene(renderer, draw, NULL, error);
    for (size_t i = 0; i < count; ++i) {
        gl_mesh_entry *resident;
        if (!draw_valid(renderer, &frame->commands[first + i].data.draw, &resident, error)) {
            renderer->frame_command = first + i;
            if (error) error->offset = first + i;
            return false;
        }
    }
    gl_world_group group;
    if (!gl_world_group_prepare(renderer, frame->commands + first, count, &group, consumed, error)) return false;
    if (*consumed < 2) { *consumed = 1; return draw_scene(renderer, draw, NULL, error); }
    return draw_scene(renderer, draw, &group, error);
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
    if (renderer->draw_buffer != buffer) renderer->native_state.destination_valid = false;
    renderer->draw_buffer = buffer;
    if (!gl_bind_destination(renderer, error)) return false;
    if (clear && !renderer->opacity.skip) {
        renderer->pipeline.color_write=renderer->pipeline.depth_write=true;
        gl_state_color_mask(renderer, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        gl_state_depth_mask(renderer, GL_TRUE);
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
static bool gl_presented_retain(qa_gl_renderer *renderer,qa_error *error)
{
    gl_api *gl=&renderer->gl; gl_presented_target *target=&renderer->presented_target;
    uint32_t front_mask=renderer->capabilities.stereo?3u:1u;
    if ((renderer->capabilities.native_buffer_mask&front_mask)==front_mask) return true;
    GLint read=0,draw=0,buffer=0,active=0,texture=0,unpack=0;
    gl->GetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read); gl->GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
    gl->GetIntegerv(GL_ACTIVE_TEXTURE,&active); gl_state_active_texture(renderer, GL_TEXTURE0);
    gl->GetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
    gl->GetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpack); gl->BindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    gl_state_framebuffer(renderer, GL_READ_FRAMEBUFFER,0); gl->GetIntegerv(GL_READ_BUFFER,&buffer);
    if (!target->framebuffer) gl->GenFramebuffers(1,&target->framebuffer);
    bool ok=target->framebuffer!=0;
    bool resized=target->width!=renderer->presented_width || target->height!=renderer->presented_height;
    gl_state_framebuffer(renderer, GL_DRAW_FRAMEBUFFER,target->framebuffer); gl_state_draw_buffer(renderer, GL_COLOR_ATTACHMENT0);
    GLboolean scissor=gl->IsEnabled(GL_SCISSOR_TEST); gl_state_enable(renderer, GL_SCISSOR_TEST, false);
    for (size_t eye=0;ok && eye<(renderer->capabilities.stereo?2u:1u);++eye) {
        if (!target->color[eye]) { gl->GenTextures(1,target->color+eye); resized=true; }
        if (!target->color[eye]) { ok=false; break; }
        gl_state_bind_texture(renderer, GL_TEXTURE_2D,target->color[eye]);
        if (resized) {
            gl->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            gl->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            gl->TexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,(GLsizei)renderer->presented_width,
                (GLsizei)renderer->presented_height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        }
        gl->FramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,target->color[eye],0);
        ok=gl->CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
        if (ok) {
            gl_state_read_buffer(renderer, gl_native_buffer(renderer->capabilities.stereo,2+eye));
            gl->BlitFramebuffer(0,0,(GLint)renderer->presented_width,(GLint)renderer->presented_height,
                0,0,(GLint)renderer->presented_width,(GLint)renderer->presented_height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        }
    }
    if (scissor) gl_state_enable(renderer, GL_SCISSOR_TEST, true);
    gl_state_read_buffer(renderer, (GLenum)buffer); gl_state_framebuffer(renderer, GL_READ_FRAMEBUFFER,(GLuint)read);
    gl_state_framebuffer(renderer, GL_DRAW_FRAMEBUFFER,(GLuint)draw);
    gl_state_bind_texture(renderer, GL_TEXTURE_2D,(GLuint)texture); gl_state_active_texture(renderer, (GLenum)active);
    gl->BindBuffer(GL_PIXEL_UNPACK_BUFFER,(GLuint)unpack);
    if (!gl_check(renderer,"Retaining presented color before native swap",error)) return false;
    if (!ok) { qa_error_set(error,QA_ERROR_IO,0,"Allocating actual presented color target"); return false; }
    target->width=renderer->presented_width; target->height=renderer->presented_height;
    return true;
}
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
        !gl_presented_retain(renderer,error) ||
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
    /* A fresh frame may follow a guest renderer or a changed drawable.
     * Source prefixes share their entered owner and its tracked state. */
    if (begin || !frame->source_backend) gl_state_invalidate(renderer);
    if (begin && frame->source_backend && frame->source_clear_draw_buffer) {
        if (!select_draw_buffer(renderer, renderer->draw_buffer, false, error)) return false;
        if (!source_draw_buffer_clear(renderer,error)) return false;
        if (renderer->controls.source.issuing && renderer->controls.source.frame == frame)
            renderer->controls.source.frame->source_clear_draw_buffer = false;
    }
    if (begin) {
        if (!frame->source_backend) renderer->controls.finish_called = false;
        renderer->presented = false;
        gl_textures_prune(renderer);
        gl_meshes_prune(renderer);
        renderer->sequence = frame->sequence;
        if (!gl_skeletal_frame(renderer, frame, error)) return false;
    }
    for (size_t i = first; i < frame->command_count; ++i) {
        renderer->frame_command=i;
        const qa_scene_command *command = &frame->commands[i];
        if (renderer->opacity.skip &&
            command->kind != QA_SCENE_COMMAND_OPACITY_BEGIN &&
            command->kind != QA_SCENE_COMMAND_OPACITY_END &&
            command->kind != QA_SCENE_COMMAND_IMAGE_STREAM &&
            command->kind != QA_SCENE_COMMAND_IMAGE_REGION) continue;
        bool ok;
        switch (command->kind) {
        case QA_SCENE_COMMAND_VIEW:
            ok = begin_view(renderer, &command->data.view, frame->source_backend, error);
            break;
        case QA_SCENE_COMMAND_DRAW: {
            size_t consumed = 1;
            ok = renderer->opacity.skip || world_draw_run(renderer, frame, i, &consumed, error);
            if (ok) i += consumed - 1;
            break;
        }
        case QA_SCENE_COMMAND_PARTICLES: {
            qa_scene_draw draw;
            ok = gl_particles_prepare(renderer, &command->data.particles, &draw, error) &&
                draw_scene(renderer, &draw, NULL, error);
            break;
        }
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
        case QA_SCENE_COMMAND_IMAGE_REGION:
            ok = gl_image_region_update(renderer, &command->data.image_region, error);
            break;
        case QA_SCENE_COMMAND_IMAGE_STREAM:
            ok = gl_image_stream_admit(renderer, command->data.image_stream, error);
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
            gl_mesh_unbind(renderer);
            gl_state_invalidate(renderer);
            gl_opacity_abort(renderer);
            if (error != NULL) error->offset = renderer->frame_command;
            return false;
        }
    }
    if (renderer->bound_vertex_array) {
        gl_mesh_unbind(renderer);
        if (renderer->controls.attributes.color_array) renderer->gl.EnableVertexAttribArray(4);
        for (uint32_t unit=0;unit<2;++unit)
            if (renderer->controls.attributes.coordinate_array[unit]) renderer->gl.EnableVertexAttribArray(2+unit);
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
    if (finish || !ok) {
        qa_error ignored={0};
        bool checked=gl_frame_check(renderer,ok?error:&ignored);
        ok=ok && checked;
    }
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
    gl_state_depth_range(renderer, near_depth,far_depth);
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
        gl_state_enable(renderer, GL_POLYGON_OFFSET_FILL, true);
        gl_state_polygon_offset(renderer, factor,units);
    } else gl_state_enable(renderer, GL_POLYGON_OFFSET_FILL, false);
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
    if (cull==QA_CULL_NONE) gl_state_enable(renderer, GL_CULL_FACE, false);
    else {
        gl_state_enable(renderer, GL_CULL_FACE, true);
        gl_state_cull_face(renderer, cull==QA_CULL_FRONT?GL_FRONT:GL_BACK);
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
    gl_state_active_texture(renderer, GL_TEXTURE0+unit);
    renderer->gl.ClientActiveTexture(GL_TEXTURE0+unit);
    controls->attributes.texture_unit=unit;
    return gl_check(renderer,"Source texture-unit selection",error);
}
bool qa_gl_source_texture_enable(qa_render_controls *controls,bool enabled,qa_error *error)
{
    if (!source_texture_owner(controls,error)) return false;
    qa_gl_renderer *renderer=controls->owner.gl;
    gl_state_active_texture(renderer, GL_TEXTURE0+controls->attributes.texture_unit);
    if (enabled) gl_state_enable(renderer, GL_TEXTURE_2D, true); else gl_state_enable(renderer, GL_TEXTURE_2D, false);
    controls->attributes.texture_enabled[controls->attributes.texture_unit]=enabled;
    return gl_check(renderer,"Source texture-unit enable",error);
}
bool qa_gl_source_texture_environment(qa_render_controls *controls,qa_scene_texture_environment environment,qa_error *error)
{
    if (!source_texture_owner(controls,error) || (unsigned)environment>QA_TEXTURE_REPLACE) return false;
    qa_gl_renderer *renderer=controls->owner.gl;
    gl_state_active_texture(renderer, GL_TEXTURE0+controls->attributes.texture_unit);
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
    renderer->executing=false;
    qa_error ignored={0};bool checked=gl_frame_check(renderer,ok?error:&ignored);
    return ok && checked;
}
size_t qa_gl_source_images_metadata_count(const qa_render_controls *controls)
{ return controls->owner.gl->source_image_count; }
const qa_scene_image *qa_gl_source_image_metadata_at(const qa_render_controls *controls,size_t ordinal)
{ return controls->owner.gl->source_images[ordinal]->image; }
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
    if (ok && skip) {
        uint32_t width=0,height=0;
        if (!gl_dimensions(renderer,&width,&height,error)) return false;
        qa_render_source_report(&renderer->controls,width,height);
    }
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
    GLenum read_buffer = gl_draw_buffer_name(renderer->draw_buffer);
    GLuint read_framebuffer=0;
    if (presented) {
        size_t eye=renderer->draw_buffer==QA_DRAW_BACK_RIGHT?1u:0u;
        if (renderer->capabilities.native_buffer_mask&(1u<<eye))
            read_buffer=gl_native_buffer(renderer->capabilities.stereo,eye);
        else {
            gl_presented_target *target=&renderer->presented_target;
            if (!target->color[eye] || target->width!=width || target->height!=height) {
                free(pixels); free(row);
                qa_error_set(error,QA_ERROR_ARGUMENT,0,"Presented color target is unavailable"); return false;
            }
            if (!target->framebuffer) renderer->gl.GenFramebuffers(1,&target->framebuffer);
            read_framebuffer=target->framebuffer;
            read_buffer=GL_COLOR_ATTACHMENT0;
        }
    }
    GLint parent_read=0,native_read=0;
    renderer->gl.GetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&parent_read);
    gl_state_framebuffer(renderer, GL_READ_FRAMEBUFFER,0);
    renderer->gl.GetIntegerv(GL_READ_BUFFER,&native_read);
    gl_state_framebuffer(renderer, GL_READ_FRAMEBUFFER,read_framebuffer);
    if (read_framebuffer) {
        size_t eye=renderer->draw_buffer==QA_DRAW_BACK_RIGHT?1u:0u;
        renderer->gl.FramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,
            renderer->presented_target.color[eye],0);
    }
    gl_state_read_buffer(renderer, read_buffer);
    bool ok=pack_state(renderer,1,error);
    if (ok) {
        renderer->gl.ReadPixels(0,0,(GLsizei)width,(GLsizei)height,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        ok=gl_check(renderer,"OpenGL color readback",error);
    }
    gl_state_framebuffer(renderer, GL_READ_FRAMEBUFFER,0); gl_state_read_buffer(renderer, (GLenum)native_read);
    gl_state_framebuffer(renderer, GL_READ_FRAMEBUFFER,(GLuint)parent_read);
    if (!gl_check(renderer,"Restoring color capture bindings",error)) ok=false;
    if (!ok) { free(pixels); free(row); return false; }
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
    gl_state_active_texture(renderer, GL_TEXTURE2);
    gl_state_bind_texture(renderer, GL_TEXTURE_2D, texture->name);
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
    ticket->targets=*renderer; gl_state_invalidate(&ticket->targets); ticket->targets.options.display=display;
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
    uint32_t native_buffer_mask=0;
    if (!gl_native_buffers(&ticket->targets,renderer->capabilities.stereo,&native_buffer_mask,error) ||
        native_buffer_mask!=renderer->capabilities.native_buffer_mask) {
        if (error && error->code==QA_OK) qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"Candidate native color buffers differ from the retained drawable");
        return false;
    }
    if (!gl_check(renderer,"Beginning compatible surface target preparation",error) ||
        !gl_presentation_copy(renderer,ticket->native,ticket->width,ticket->height,error) ||
        !gl_surface_targets_prepare(&ticket->targets,renderer,gamma,ticket->replace_output,
            ticket->replace_opacity,&ticket->reader,error) ||
        !gl_presentation_restore_bindings(renderer,ticket->native,error)) return false;
    gl_state_invalidate(&ticket->targets);
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
        renderer->programs.stage[0]!=ticket->original.programs.stage[0] ||
        renderer->programs.stage[1]!=ticket->original.programs.stage[1] || renderer->programs.gamma!=ticket->original.programs.gamma ||
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
    gl_state_invalidate(renderer);
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
    gl_state_invalidate(renderer);
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
        gl_state_enable(renderer, GL_STENCIL_TEST, true); gl_state_stencil_mask(renderer, UINT_MAX);
        renderer->gl.ClearStencil(0); gl_state_stencil_func(renderer, GL_ALWAYS,0,UINT_MAX);
        gl_state_stencil_op(renderer, GL_KEEP,GL_INCR,GL_INCR);
    } else gl_state_enable(renderer, GL_STENCIL_TEST, false);
    return gl_check(renderer,"Source frame overdraw policy",error);
}
bool qa_gl_source_image_grid(qa_render_controls *controls,int32_t mode,qa_error *error)
{
    qa_gl_renderer *renderer=controls->owner.gl;
    uint32_t width=0,height=0;
    if (renderer->target || renderer->opacity.active || !qa_display_make_current(renderer->options.display,error) ||
        !gl_dimensions(renderer,&width,&height,error) || !gl_bind_destination(renderer,error)) return false;
    qa_scene_rect target={0,0,width,height};
    if (!qa_output_domains_assign(&renderer->output_domains,target,renderer->draw_buffer,true,width,height,error)) return false;
    renderer->source_frame=true; renderer->preblend_gamma=false;
    if (!controls->source.projection_2d) {
        qa_scene_state state=renderer->pipeline;
        qa_render_source_state_bits(&state,false,true);
        state.depth_test=QA_DEPTH_DISABLED; state.cull=QA_CULL_NONE;
        draw_state(renderer,&state,QA_SCENE_TRIANGLES);
        renderer->view=(qa_scene_view){.viewport=target,.depth=1};
        renderer->gl.Viewport(0,0,(GLsizei)width,(GLsizei)height);
        renderer->gl.Scissor(0,0,(GLsizei)width,(GLsizei)height);
        gl_state_enable(renderer, GL_SCISSOR_TEST, true);
        controls->source.projection_2d=true;
        controls->source.picture_milliseconds=controls->frame_values.milliseconds;
    }
    renderer->gl.Clear(GL_COLOR_BUFFER_BIT);
    renderer->gl.Finish();
    uint64_t start=(qa_platform_time_ns() / UINT64_C(1000000));
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
        ok=gl_source_texture_bind(renderer,binding,error);
        if (!ok) break;
        size_t first=frame.command_count;
        ok=qa_scene_frame_picture_f(&frame,binding,target,(qa_scene_rect_f){x,y,w,h},
            (qa_scene_vec4){0,0,1,1},controls->attributes.color,error);
        for (size_t c=first;ok && c<frame.command_count;++c) {
            if (frame.commands[c].kind!=QA_SCENE_COMMAND_DRAW) continue;
      qa_scene_draw *draw=&frame.commands[c].data.draw;
            draw->source_direct=QA_SOURCE_DIRECT_IMAGE_GRID;
            ok=draw_scene(renderer,draw,NULL,error);
        }
    }
    if (ok && renderer->source_image_count) {
        controls->attributes.coordinates[0]=(qa_scene_vec2){0,1};
        controls->attributes.coordinates_known[0]=true; renderer->gl.TexCoord2f(0,1);
    }
    qa_scene_frame_destroy(&frame);
    renderer->gl.Finish();
    if (ok) ok=gl_check(renderer,"Source image grid",error);
    if (ok && controls->source_print) {
        char text[100]; snprintf(text,sizeof(text),"%llu msec to draw all images\n",(unsigned long long)((qa_platform_time_ns() / UINT64_C(1000000))-start));
        controls->source_print(controls->source_print_context,text);
    }
    return ok;
}
