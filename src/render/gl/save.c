#include "internal.h"
#include "qa/render_gl_save.h"
#include <SDL_video.h>
#include <limits.h>

typedef struct gl_saved_surface {
    uint32_t width,height;
    qa_buffer color,depth,stencil;
    GLuint framebuffer,color_texture,depth_texture;
    bool floating_depth;
} gl_saved_surface;
typedef struct gl_native_cut {
    GLint program,active,texture[3],array,element,pack_buffer,unpack_buffer,renderbuffer;
    GLint read_framebuffer,draw_framebuffer,read_buffer,draw_buffer;
    GLint pack[4],unpack[4],viewport[4],scissor[4];
    bool scissor_enabled;
} gl_native_cut;
struct gl_restore_storage {
    gl_saved_surface native[4];
    uint32_t width,height;
};
struct qa_gl_restore_guard {
    qa_gl_renderer *active,*candidate;
    gl_restore_storage *saved;
    bool attempted,prepared,transferred;
};
static bool gl_save_error(qa_error *error,qa_status status,const char *message)
{ qa_error_set(error,status,0,"%s",message); return false; }
static const GLenum gl_pack_names[4]={GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_ROWS,GL_PACK_SKIP_PIXELS};
static const GLenum gl_unpack_names[4]={GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS};
static void gl_cut_read(qa_gl_renderer *renderer,gl_native_cut *cut)
{
    gl_state_invalidate(renderer);
    gl_api *gl=&renderer->gl;
    gl->GetIntegerv(GL_CURRENT_PROGRAM,&cut->program); gl->GetIntegerv(GL_ACTIVE_TEXTURE,&cut->active);
    for (size_t i=0;i<3;++i) { gl_state_active_texture(renderer, GL_TEXTURE0+(GLenum)i); gl->GetIntegerv(GL_TEXTURE_BINDING_2D,cut->texture+i); }
    gl_state_active_texture(renderer, (GLenum)cut->active);
    gl->GetIntegerv(GL_ARRAY_BUFFER_BINDING,&cut->array); gl->GetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&cut->element);
    gl->GetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&cut->pack_buffer); gl->GetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&cut->unpack_buffer);
    gl->GetIntegerv(GL_RENDERBUFFER_BINDING,&cut->renderbuffer);
    gl->GetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&cut->read_framebuffer); gl->GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&cut->draw_framebuffer);
    gl->GetIntegerv(GL_READ_BUFFER,&cut->read_buffer); gl->GetIntegerv(GL_DRAW_BUFFER,&cut->draw_buffer);
    for (size_t i=0;i<4;++i) { gl->GetIntegerv(gl_pack_names[i],cut->pack+i); gl->GetIntegerv(gl_unpack_names[i],cut->unpack+i); }
    gl->GetIntegerv(GL_VIEWPORT,cut->viewport); gl->GetIntegerv(GL_SCISSOR_BOX,cut->scissor);
    cut->scissor_enabled=gl->IsEnabled(GL_SCISSOR_TEST)!=GL_FALSE;
}
static void gl_cut_restore(qa_gl_renderer *renderer,const gl_native_cut *cut)
{
    gl_state_invalidate(renderer);
    gl_api *gl=&renderer->gl;
    gl_state_program(renderer, (GLuint)cut->program);
    for (size_t i=0;i<3;++i) { gl_state_active_texture(renderer, GL_TEXTURE0+(GLenum)i); gl_state_bind_texture(renderer, GL_TEXTURE_2D,(GLuint)cut->texture[i]); }
    gl_state_active_texture(renderer, (GLenum)cut->active);
    gl->BindBuffer(GL_ARRAY_BUFFER,(GLuint)cut->array); gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER,(GLuint)cut->element);
    gl->BindBuffer(GL_PIXEL_PACK_BUFFER,(GLuint)cut->pack_buffer); gl->BindBuffer(GL_PIXEL_UNPACK_BUFFER,(GLuint)cut->unpack_buffer);
    gl->BindRenderbuffer(GL_RENDERBUFFER,(GLuint)cut->renderbuffer);
    gl_state_framebuffer(renderer, GL_READ_FRAMEBUFFER,(GLuint)cut->read_framebuffer); gl_state_read_buffer(renderer, (GLenum)cut->read_buffer);
    gl_state_framebuffer(renderer, GL_DRAW_FRAMEBUFFER,(GLuint)cut->draw_framebuffer); gl_state_draw_buffer(renderer, (GLenum)cut->draw_buffer);
    for (size_t i=0;i<4;++i) { gl->PixelStorei(gl_pack_names[i],cut->pack[i]); gl->PixelStorei(gl_unpack_names[i],cut->unpack[i]); }
    gl->Viewport(cut->viewport[0],cut->viewport[1],cut->viewport[2],cut->viewport[3]);
    gl->Scissor(cut->scissor[0],cut->scissor[1],cut->scissor[2],cut->scissor[3]);
    if (cut->scissor_enabled) gl_state_enable(renderer, GL_SCISSOR_TEST, true); else gl_state_enable(renderer, GL_SCISSOR_TEST, false);
}
static void gl_tight_pixels(qa_gl_renderer *renderer)
{
    gl_api *gl=&renderer->gl;
    gl->BindBuffer(GL_PIXEL_PACK_BUFFER,0); gl->BindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    for (size_t i=0;i<4;++i) {
        gl->PixelStorei(gl_pack_names[i],i?0:1); gl->PixelStorei(gl_unpack_names[i],i?0:1);
    }
}
static bool gl_save_extent(uint32_t width,uint32_t height,size_t *size,qa_error *error)
{
    if (!width || !height || width>INT_MAX || height>INT_MAX || (size_t)width>SIZE_MAX/height/4)
        return gl_save_error(error,QA_ERROR_FORMAT,"GPU continuation dimensions exceed actual addressable pixel storage");
    *size=(size_t)width*height*4; return true;
}
static bool gl_save_allocate(qa_buffer *out,size_t size,qa_error *error)
{
    if (!size) return true;
    out->data=malloc(size); out->size=size;
    return out->data!=NULL || gl_save_error(error,QA_ERROR_MEMORY,"Allocating actual GPU continuation pixels/buffer bytes");
}
static bool gl_surface_capture(qa_gl_renderer *renderer,GLuint framebuffer,GLenum buffer,uint32_t width,uint32_t height,
    bool color,bool depth,gl_saved_surface *saved,qa_error *error)
{
    size_t size=0; saved->width=width; saved->height=height;
    saved->floating_depth=depth && renderer->capabilities.floating_depth;
    if (!gl_save_extent(width,height,&size,error) ||
        (color && !gl_save_allocate(&saved->color,size,error)) ||
        (depth && !gl_save_allocate(&saved->depth,size,error)) ||
        (depth && renderer->capabilities.stencil_bits && !gl_save_allocate(&saved->stencil,size,error))) return false;
    gl_api *gl=&renderer->gl; gl_state_framebuffer(renderer, GL_READ_FRAMEBUFFER,framebuffer); gl_state_read_buffer(renderer, buffer);
    if (!gl_check(renderer,"Selecting actual GPU capture buffer",error)) return false;
    gl_tight_pixels(renderer);
    if (color) {
        gl->ReadPixels(0,0,(GLsizei)width,(GLsizei)height,GL_RGBA,GL_UNSIGNED_BYTE,saved->color.data);
        if (!gl_check(renderer,"Capturing actual GPU color buffer",error)) return false;
    }
    if (depth) {
        gl->ReadPixels(0,0,(GLsizei)width,(GLsizei)height,GL_DEPTH_COMPONENT,
            saved->floating_depth?GL_FLOAT:GL_UNSIGNED_INT,saved->depth.data);
        if (!gl_check(renderer,"Capturing actual GPU depth buffer",error)) return false;
    }
    if (depth && renderer->capabilities.stencil_bits) {
        gl->ReadPixels(0,0,(GLsizei)width,(GLsizei)height,GL_STENCIL_INDEX,GL_UNSIGNED_INT,saved->stencil.data);
        if (!gl_check(renderer,"Capturing actual GPU stencil buffer",error)) return false;
    }
    return true;
}
static void gl_surface_free(qa_gl_renderer *renderer,gl_saved_surface *saved)
{
    if (renderer && renderer->gl.DeleteTextures) {
        if (saved->framebuffer) renderer->gl.DeleteFramebuffers(1,&saved->framebuffer);
        if (saved->color_texture) gl_state_delete_textures(renderer, 1,&saved->color_texture);
        if (saved->depth_texture) gl_state_delete_textures(renderer, 1,&saved->depth_texture);
    }
    qa_buffer_free(&saved->color); qa_buffer_free(&saved->depth); qa_buffer_free(&saved->stencil);
    memset(saved,0,sizeof(*saved));
}
static void gl_saved_dispose(gl_restore_storage *saved,qa_gl_renderer *renderer)
{
    if (!saved) return;
    for (size_t i=0;i<4;++i) gl_surface_free(renderer,saved->native+i);
    free(saved);
}
void gl_restore_storage_destroy(qa_gl_renderer *renderer)
{
    if (!renderer) return;
    gl_saved_dispose(renderer->restore,renderer); renderer->restore=NULL;
}
static bool gl_gpu_capture(qa_gl_renderer *renderer,gl_restore_storage *saved,qa_error *error)
{
    if (!renderer || renderer->closed || renderer->detached || renderer->executing || renderer->capturing ||
        renderer->preparing || renderer->opacity.active || renderer->controls.source.entered || renderer->controls.image_ticket ||
        !qa_display_make_current(renderer->options.display,error))
        return gl_save_error(error,QA_ERROR_ARGUMENT,"Video rollback capture requires its completed renderer");
    if (renderer->capabilities.color_bits>24 || renderer->capabilities.alpha_bits>8)
        return gl_save_error(error,QA_ERROR_UNSUPPORTED,"Video rollback capture requires RGBA8 native color");
    if (!gl_dimensions(renderer,&saved->width,&saved->height,error)) return false;
    gl_native_cut cut={0}; gl_cut_read(renderer,&cut); renderer->capturing=true;
    GLint native_read_buffer=0;
    gl_state_framebuffer(renderer, GL_READ_FRAMEBUFFER,0); renderer->gl.GetIntegerv(GL_READ_BUFFER,&native_read_buffer);
    bool ok=true;
    for (size_t i=0;ok && i<4;++i) if (renderer->capabilities.native_buffer_mask&(1u<<i))
        ok=gl_surface_capture(renderer,0,gl_native_buffer(renderer->capabilities.stereo,i),saved->width,saved->height,
            true,i==2,saved->native+i,error);
    gl_state_framebuffer(renderer, GL_READ_FRAMEBUFFER,0); gl_state_read_buffer(renderer, (GLenum)native_read_buffer);
    gl_cut_restore(renderer,&cut); renderer->capturing=false;
    return ok && gl_check(renderer,"Restoring renderer bindings after video rollback capture",error);
}
bool qa_gl_create_detached(const qa_gl_options *options,float gamma,qa_gl_renderer *active,
    qa_gl_renderer **out,qa_gl_restore_guard **guard_out,qa_error *error)
{
    if (!out || !guard_out || !options || !options->display || !active || active->closed || active->detached ||
        active->executing || active->capturing || active->preparing || active->opacity.active || active->surface_ticket ||
        active->controls.ticket || active->controls.image_ticket || active->controls.source.entered || !isfinite(gamma) || gamma<0.5f || gamma>3)
        return gl_save_error(error,QA_ERROR_ARGUMENT,"Fresh GPU owner requires a real display, renderer cut, and supported gamma");
    *out=NULL; *guard_out=NULL;
    qa_display_info info={0};
    if (!qa_display_info_get(options->display,&info,error) || info.backend!=QA_DISPLAY_OPENGL)
        return gl_save_error(error,QA_ERROR_ARGUMENT,"Fresh GPU owner requires its actual OpenGL display");
    qa_gl_renderer *candidate=gl_renderer_allocate(options,&info,error);
    if (!candidate) return false;
    gl_restore_storage *saved=calloc(1,sizeof(*saved));
    qa_gl_restore_guard *guard=calloc(1,sizeof(*guard));
    if (!saved || !guard) {
        free(candidate); free(saved); free(guard);
        return gl_save_error(error,QA_ERROR_MEMORY,"Allocating fresh detached GPU owners");
    }
    candidate->detached=true; candidate->restore=saved;
    candidate->capabilities=active->capabilities; candidate->gamma=gamma;
    saved->width=info.drawable_width; saved->height=info.drawable_height;
    guard->active=active; guard->candidate=candidate; guard->saved=saved;
    *out=candidate; *guard_out=guard; return true;
}
static GLint gl_saved_depth_internal(const qa_gl_renderer *renderer)
{
    const qa_gl_capabilities *caps=&renderer->capabilities;
    if (caps->stencil_bits) return caps->floating_depth?GL_DEPTH32F_STENCIL8:GL_DEPTH24_STENCIL8;
    if (caps->floating_depth) return GL_DEPTH_COMPONENT32F;
    return caps->depth_bits<=16?GL_DEPTH_COMPONENT16:caps->depth_bits<=24?GL_DEPTH_COMPONENT24:GL_DEPTH_COMPONENT32;
}
static void gl_saved_texture_parameters(qa_gl_renderer *renderer)
{
    gl_api *gl=&renderer->gl;
    gl->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    gl->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    gl->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);
}
static bool gl_saved_surface_upload(qa_gl_renderer *renderer,gl_saved_surface *surface,qa_error *error)
{
    gl_api *gl=&renderer->gl; gl->GenFramebuffers(1,&surface->framebuffer);
    if (!surface->framebuffer) return gl_save_error(error,QA_ERROR_MEMORY,"Preparing separate completed GPU framebuffer");
    gl_state_framebuffer(renderer, GL_FRAMEBUFFER,surface->framebuffer);
    if (surface->color.size) {
        gl->GenTextures(1,&surface->color_texture);
        if (!surface->color_texture) return gl_save_error(error,QA_ERROR_MEMORY,"Preparing video rollback texture");
        gl_state_active_texture(renderer, GL_TEXTURE0); gl_state_bind_texture(renderer, GL_TEXTURE_2D,surface->color_texture);
        gl_saved_texture_parameters(renderer);
        gl->TexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,(GLsizei)surface->width,(GLsizei)surface->height,0,
            GL_RGBA,GL_UNSIGNED_BYTE,surface->color.data);
        if (!gl_check(renderer,"Uploading video rollback color",error)) return false;
        gl->FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,surface->color_texture,0);
        gl_state_read_buffer(renderer, GL_COLOR_ATTACHMENT0); gl_state_draw_buffer(renderer, GL_COLOR_ATTACHMENT0);
    } else { gl_state_read_buffer(renderer, GL_NONE); gl_state_draw_buffer(renderer, GL_NONE); }
    if (surface->depth.size) {
        gl->GenTextures(1,&surface->depth_texture);
        if (!surface->depth_texture) return gl_save_error(error,QA_ERROR_MEMORY,"Preparing completed GPU depth/stencil pixels");
        gl_state_bind_texture(renderer, GL_TEXTURE_2D,surface->depth_texture); gl_saved_texture_parameters(renderer);
        GLenum format=GL_DEPTH_COMPONENT,type=surface->floating_depth?GL_FLOAT:GL_UNSIGNED_INT;
        const void *pixels=surface->depth.data; void *packed=NULL;
        if (surface->stencil.size) {
            size_t count=(size_t)surface->width*surface->height,stride=surface->floating_depth?8:4;
            if (count>SIZE_MAX/stride || !(packed=malloc(count*stride)))
                return gl_save_error(error,QA_ERROR_MEMORY,"Packing genuine saved GPU depth/stencil samples");
            for (size_t i=0;i<count;++i) {
                uint32_t depth=0,stencil=0; memcpy(&depth,surface->depth.data+i*4,4); memcpy(&stencil,surface->stencil.data+i*4,4);
                if (surface->floating_depth) {
                    memcpy((uint8_t *)packed+i*8,&depth,4); memcpy((uint8_t *)packed+i*8+4,&stencil,4);
                } else {
                    uint32_t value=(uint32_t)((double)depth*16777215.0/4294967295.0+0.5);
                    value=(value<<8)|(stencil&255u); memcpy((uint8_t *)packed+i*4,&value,4);
                }
            }
            format=GL_DEPTH_STENCIL; type=surface->floating_depth?GL_FLOAT_32_UNSIGNED_INT_24_8_REV:GL_UNSIGNED_INT_24_8;
            pixels=packed;
        }
        gl->TexImage2D(GL_TEXTURE_2D,0,gl_saved_depth_internal(renderer),(GLsizei)surface->width,
            (GLsizei)surface->height,0,format,type,pixels); free(packed);
        gl->FramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,surface->depth_texture,0);
        if (surface->stencil.size) gl->FramebufferTexture2D(GL_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,GL_TEXTURE_2D,surface->depth_texture,0);
    }
    if (gl->CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)
        return gl_save_error(error,QA_ERROR_UNSUPPORTED,"Prepared saved GPU framebuffer is incomplete");
    return gl_check(renderer,"Preparing exact completed GPU framebuffer pixels",error);
}

static bool gl_saved_guard_current(const qa_gl_restore_guard *guard,qa_error *error)
{
    if (!guard || guard->transferred || !guard->active || !guard->candidate || !guard->saved ||
        guard->active->closed || guard->active->detached || guard->active->executing || guard->active->capturing ||
        guard->active->preparing || guard->active->controls.ticket || guard->active->controls.image_ticket || guard->active->surface_ticket ||
        guard->active->opacity.active || guard->active->controls.source.entered ||
        guard->candidate->closed || guard->candidate->controls.ticket || guard->candidate->controls.image_ticket || guard->candidate->controls.source.entered ||
        !guard->candidate->detached || guard->candidate->restore!=guard->saved)
        return gl_save_error(error,QA_ERROR_ARGUMENT,"GPU publication lost its actual completed renderer owners");
    uint32_t width=0,height=0,candidate_width=0,candidate_height=0;
    return gl_dimensions(guard->active,&width,&height,error) &&
        gl_dimensions(guard->candidate,&candidate_width,&candidate_height,error) &&
        ((width==guard->saved->width && height==guard->saved->height && candidate_width==width && candidate_height==height) ||
        gl_save_error(error,QA_ERROR_ARGUMENT,"GPU publication drawable changed after detached restore"));
}
bool qa_gl_handoff_prepare(qa_gl_restore_guard *guard,qa_error *error)
{
    if (!gl_saved_guard_current(guard,error)) return false;
    if (guard->prepared) return true;
    if (guard->attempted) return gl_save_error(error,QA_ERROR_ARGUMENT,"Failed GPU preparation must retire its candidate");
    qa_gl_renderer *renderer=guard->candidate;
    if (!qa_display_make_current(renderer->options.display,error)) return false;
    guard->attempted=true; renderer->gl=guard->active->gl;
    gl_native_cut cut={0}; gl_cut_read(renderer,&cut); renderer->preparing=true;
    gl_tight_pixels(renderer); gl_state_enable(renderer, GL_SCISSOR_TEST, false);
    bool ok=gl_programs_create(renderer,error) && gl_resources_create(renderer,error) &&
        gl_output_gamma_prepare(renderer,renderer->gamma,false,error) && gl_check(renderer,"Preparing fresh GPU renderer",error);
    gl_cut_restore(renderer,&cut); gl_state_invalidate(guard->active); renderer->preparing=false;
    if (ok) ok=gl_check(renderer,"Restoring active GPU bindings after fresh preparation",error);
    guard->prepared=ok;
    return ok;
}
bool qa_gl_handoff_ready(const qa_gl_restore_guard *guard,qa_error *error)
{
    return gl_saved_guard_current(guard,error) && (guard->prepared ||
        gl_save_error(error,QA_ERROR_ARGUMENT,"GPU publication requires its prepared separate native objects"));
}
void qa_gl_handoff(qa_gl_restore_guard *guard)
{
    if (!guard || !guard->prepared || guard->transferred) return;
    qa_gl_renderer *renderer=guard->candidate; gl_restore_storage *saved=guard->saved; gl_api *gl=&renderer->gl;
    gl_state_invalidate(renderer); gl_state_invalidate(guard->active);
    gl_state_active_texture(renderer, GL_TEXTURE0); gl_state_bind_texture(renderer, GL_TEXTURE_2D,0);
    gl_tight_pixels(renderer);
    for (size_t i=0;i<guard->active->controls.zero_texture.count;++i)
        gl->TexImage2D(GL_TEXTURE_2D,(GLint)i,GL_RGBA8,0,0,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    gl->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST_MIPMAP_LINEAR);
    gl->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    gl->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    gl->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    gl->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,1000);
    gl->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_MODE,GL_NONE);
    gl_state_enable(renderer, GL_SCISSOR_TEST, false);
    gl_state_framebuffer(renderer, GL_FRAMEBUFFER,0);
    gl_state_read_buffer(renderer, gl_draw_buffer_name(renderer->draw_buffer)); gl_state_draw_buffer(renderer, gl_draw_buffer_name(renderer->draw_buffer));
    uint32_t height=saved->height;
    if (renderer->target) {
        gl_state_framebuffer(renderer, GL_FRAMEBUFFER,renderer->target_framebuffer);
        gl_state_read_buffer(renderer, GL_NONE); gl_state_draw_buffer(renderer, GL_NONE);
        height=renderer->target->levels[0].height;
    } else if (renderer->output.enabled && renderer->output.color_ready[gl_draw_buffer_index(renderer->draw_buffer)]) {
        gl_state_framebuffer(renderer, GL_FRAMEBUFFER,renderer->output.framebuffer);
        gl->FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,
            renderer->output.color[gl_draw_buffer_index(renderer->draw_buffer)],0);
        gl_state_read_buffer(renderer, GL_COLOR_ATTACHMENT0); gl_state_draw_buffer(renderer, GL_COLOR_ATTACHMENT0);
    }
    GLint bottom=(GLint)((int64_t)height-renderer->view.viewport.y-renderer->view.viewport.height);
    gl->Viewport(renderer->view.viewport.x,bottom,(GLsizei)renderer->view.viewport.width,(GLsizei)renderer->view.viewport.height);
    gl->Scissor(renderer->view.viewport.x,bottom,(GLsizei)renderer->view.viewport.width,(GLsizei)renderer->view.viewport.height);
    gl_state_enable(renderer, GL_SCISSOR_TEST, true);
    gl_source_pipeline_restore(renderer);
    renderer->detached=false; guard->transferred=true; guard->saved=NULL;
    gl_restore_storage_destroy(renderer);
}
void qa_gl_restore_guard_destroy(qa_gl_restore_guard *guard)
{ free(guard); }

struct gl_presentation_snapshot {
    gl_restore_storage *saved;
    gl_native_cut cut;
    GLint native_read,native_draw,depth_mask,color_mask[4],polygon[2],attributes[5];
    bool enabled[6],uploaded[4],cut_valid,captured;
};
static const GLenum gl_presentation_enables[6]={GL_DEPTH_TEST,GL_STENCIL_TEST,GL_CULL_FACE,
    GL_BLEND,GL_POLYGON_OFFSET_FILL,GL_ALPHA_TEST};

bool gl_presentation_capture(qa_gl_renderer *renderer,gl_presentation_snapshot **out,qa_error *error)
{
    if (!out || *out || !renderer || renderer->closed || renderer->detached || renderer->executing ||
        renderer->capturing || renderer->preparing || renderer->opacity.active || renderer->controls.source.entered || renderer->controls.image_ticket || renderer->target ||
        (renderer->capabilities.stencil_bits && renderer->capabilities.stencil_bits!=8) ||
        !qa_display_make_current(renderer->options.display,error))
        return gl_save_error(error,QA_ERROR_ARGUMENT,"Native presentation capture requires a completed exact renderer");
    if (renderer->capabilities.stencil_bits &&
        renderer->capabilities.depth_bits!=(renderer->capabilities.floating_depth?32u:24u))
        return gl_save_error(error,QA_ERROR_UNSUPPORTED,"Native packed depth/stencil visual has no exact retained upload format");
    gl_presentation_snapshot *snapshot=calloc(1,sizeof(*snapshot));
    gl_restore_storage *saved=calloc(1,sizeof(*saved));
    if (!snapshot || !saved) { free(snapshot); free(saved); return gl_save_error(error,QA_ERROR_MEMORY,"Retaining actual native presentation"); }
    snapshot->saved=saved; *out=snapshot;
    typedef void (APIENTRY *get_attribute_fn)(GLuint,GLenum,GLint *);
    get_attribute_fn get_attribute=NULL;
    _Static_assert(sizeof(get_attribute)==sizeof(void *),"SDL GL procedure pointers must fit in void pointers");
    void *address=SDL_GL_GetProcAddress("glGetVertexAttribiv");
    if (!address) address=SDL_GL_GetProcAddress("glGetVertexAttribivARB");
    memcpy(&get_attribute,&address,sizeof(get_attribute));
    if (!get_attribute) return gl_save_error(error,QA_ERROR_UNSUPPORTED,"Reading retained GL vertex attribute state");
    gl_api *gl=&renderer->gl;
    if (!gl_check(renderer,"Beginning actual native presentation capture",error)) return false;
    gl_cut_read(renderer,&snapshot->cut);
    gl->GetIntegerv(GL_DEPTH_WRITEMASK,&snapshot->depth_mask);
    gl->GetIntegerv(GL_COLOR_WRITEMASK,snapshot->color_mask);
    gl->GetIntegerv(GL_POLYGON_MODE,snapshot->polygon);
    for (size_t i=0;i<6;++i) {
        snapshot->enabled[i]=gl->IsEnabled(gl_presentation_enables[i])!=GL_FALSE;
        if (i<5) get_attribute((GLuint)i,GL_VERTEX_ATTRIB_ARRAY_ENABLED,snapshot->attributes+i);
    }
    if (!gl_check(renderer,"Reading actual native presentation state",error)) return false;
    gl_state_framebuffer(renderer, GL_FRAMEBUFFER,0);
    GLint samples=0;
    gl->GetIntegerv(GL_SAMPLES,&samples);
    gl->GetIntegerv(GL_READ_BUFFER,&snapshot->native_read);
    gl->GetIntegerv(GL_DRAW_BUFFER,&snapshot->native_draw);
    gl_cut_restore(renderer,&snapshot->cut);
    snapshot->cut_valid=true;
    if (!gl_check(renderer,"Restoring bindings before presentation capture",error)) return false;
    if (samples)
        return gl_save_error(error,QA_ERROR_UNSUPPORTED,"Native multisample presentation cannot be retained as exact single-sample surfaces");
    snapshot->captured=gl_gpu_capture(renderer,saved,error);
    bool ok=snapshot->captured;
    for (size_t i=0;ok && i<4;++i) if (saved->native[i].width) {
        ok=gl_saved_surface_upload(renderer,saved->native+i,error);
        snapshot->uploaded[i]=ok;
    }
    qa_error restore_error={0};
    if (!gl_presentation_restore_bindings(renderer,snapshot,&restore_error)) {
        if (error) *error=restore_error;
        return false;
    }
    return ok;
}

bool gl_presentation_restore_bindings(qa_gl_renderer *renderer,const gl_presentation_snapshot *snapshot,qa_error *error)
{
    if (!snapshot || !snapshot->cut_valid) return true;
    gl_api *gl=&renderer->gl;
    gl_state_framebuffer(renderer, GL_FRAMEBUFFER,0);
    gl_state_read_buffer(renderer, (GLenum)snapshot->native_read); gl_state_draw_buffer(renderer, (GLenum)snapshot->native_draw);
    gl_cut_restore(renderer,&snapshot->cut);
    gl_state_depth_mask(renderer, snapshot->depth_mask?GL_TRUE:GL_FALSE);
    gl_state_color_mask(renderer, snapshot->color_mask[0]?GL_TRUE:GL_FALSE,snapshot->color_mask[1]?GL_TRUE:GL_FALSE,
        snapshot->color_mask[2]?GL_TRUE:GL_FALSE,snapshot->color_mask[3]?GL_TRUE:GL_FALSE);
    gl_state_polygon_mode(renderer, GL_FRONT,(GLenum)snapshot->polygon[0]);
    gl_state_polygon_mode(renderer, GL_BACK,(GLenum)snapshot->polygon[1]);
    for (size_t i=0;i<6;++i) {
        if (snapshot->enabled[i]) gl_state_enable(renderer, gl_presentation_enables[i], true);
        else gl_state_enable(renderer, gl_presentation_enables[i], false);
        if (i<5) {
            if (snapshot->attributes[i]) gl->EnableVertexAttribArray((GLuint)i);
            else gl->DisableVertexAttribArray((GLuint)i);
        }
    }
    return gl_check(renderer,"Restoring complete retained presentation bindings",error);
}

static bool gl_presentation_surface_delete(qa_gl_renderer *renderer,gl_saved_surface *surface,qa_error *error)
{
    gl_api *gl=&renderer->gl;
    GLuint *names[3]={&surface->framebuffer,&surface->color_texture,&surface->depth_texture};
    for (size_t j=0;j<3;++j) if (*names[j]) {
        if (!gl_check(renderer,"Preparing checked native presentation retirement",error)) return false;
        if (!j) gl->DeleteFramebuffers(1,names[j]); else gl_state_delete_textures(renderer, 1,names[j]);
        if (!gl_check(renderer,"Retiring native presentation storage",error)) return false;
        *names[j]=0;
    }
    return true;
}

bool gl_presentation_copy(qa_gl_renderer *renderer,gl_presentation_snapshot *snapshot,
    uint32_t width,uint32_t height,qa_error *error)
{
    if (!snapshot || !snapshot->captured) return true;
    gl_restore_storage *saved=snapshot->saved;
    gl_api *gl=&renderer->gl;
    gl_tight_pixels(renderer); gl_state_enable(renderer, GL_SCISSOR_TEST, false);
    for (size_t i=0;i<4;++i) if (saved->native[i].width) {
        gl_saved_surface *surface=saved->native+i;
        if (!snapshot->uploaded[i]) {
            /* A rejected upload can retain partial objects. Retire those with
             * proof before retrying; the captured CPU samples remain owned. */
            if (!gl_presentation_surface_delete(renderer,surface,error) ||
                !gl_saved_surface_upload(renderer,surface,error)) return false;
            snapshot->uploaded[i]=true;
        }
        if (!surface->color_texture || (i==2 && !surface->depth_texture))
            return gl_save_error(error,QA_ERROR_ARGUMENT,"Native presentation upload is incomplete");
        gl_state_framebuffer(renderer, GL_READ_FRAMEBUFFER,surface->framebuffer); gl_state_read_buffer(renderer, GL_COLOR_ATTACHMENT0);
        gl_state_framebuffer(renderer, GL_DRAW_FRAMEBUFFER,0); gl_state_draw_buffer(renderer, gl_native_buffer(renderer->capabilities.stereo,i));
        gl->BlitFramebuffer(0,0,(GLint)saved->width,(GLint)saved->height,0,0,(GLint)width,(GLint)height,
            GL_COLOR_BUFFER_BIT,GL_NEAREST);
        if (i==2) {
            uint32_t overlap_width=width<saved->width?width:saved->width;
            uint32_t overlap_height=height<saved->height?height:saved->height;
            gl->BlitFramebuffer(0,0,(GLint)overlap_width,(GLint)overlap_height,
                0,0,(GLint)overlap_width,(GLint)overlap_height,
                GL_DEPTH_BUFFER_BIT|(surface->stencil.size?GL_STENCIL_BUFFER_BIT:0),GL_NEAREST);
        }
        if (!gl_check(renderer,"Copying actual retained native presentation",error)) return false;
    }
    return true;
}

bool gl_presentation_dispose(qa_gl_renderer *renderer,gl_presentation_snapshot **out,qa_error *error)
{
    if (!out) return gl_save_error(error,QA_ERROR_ARGUMENT,"Invalid native presentation disposal");
    gl_presentation_snapshot *snapshot=*out;
    if (!snapshot) return true;
    for (size_t i=0;i<4;++i)
        if (!gl_presentation_surface_delete(renderer,snapshot->saved->native+i,error)) return false;
    gl_saved_dispose(snapshot->saved,NULL); free(snapshot); *out=NULL; return true;
}
