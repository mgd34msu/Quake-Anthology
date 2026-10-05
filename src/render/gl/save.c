#include "internal.h"
#include "../save_fields.h"
#include "qa/render_gl_save.h"
#include "qa/scene_save.h"
#include <SDL_video.h>
#include <limits.h>

typedef struct gl_saved_level {
    uint32_t width,height;
    int32_t internal;
    bool depth, borrowed;
    qa_buffer pixels;
} gl_saved_level;
typedef struct gl_saved_texture {
    struct gl_saved_texture *next;
    gl_texture_entry *entry;
    gl_saved_level *levels;
    size_t count;
    int32_t parameters[6];
} gl_saved_texture;
typedef struct gl_saved_mesh {
    struct gl_saved_mesh *next;
    gl_mesh_entry *entry;
    qa_buffer vertices,indices;
    bool active;
} gl_saved_mesh;
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
    gl_saved_texture *textures;
    gl_saved_texture zero_texture;
    GLuint zero_prepared;
    gl_saved_mesh *meshes;
    gl_saved_surface native[4],output[4],opacity[2];
    gl_saved_level presented[2];
    gl_saved_level gamma;
    uint32_t width,height;
    bool output_allocated,opacity_allocated,stream_vertices,stream_indices,target_allocated,fog_allocated;
};
struct qa_gl_restore_guard {
    qa_gl_renderer *active,*candidate;
    gl_restore_storage *saved;
    bool attempted,prepared,transferred,fresh;
};
static bool gl_save_error(qa_error *error,qa_status status,const char *message)
{ qa_error_set(error,status,0,"%s",message); return false; }
static const GLenum gl_pack_names[4]={GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_ROWS,GL_PACK_SKIP_PIXELS};
static const GLenum gl_unpack_names[4]={GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS};
static const GLenum gl_texture_parameters[6]={GL_TEXTURE_MIN_FILTER,GL_TEXTURE_MAG_FILTER,GL_TEXTURE_WRAP_S,
    GL_TEXTURE_WRAP_T,GL_TEXTURE_MAX_LEVEL,GL_TEXTURE_COMPARE_MODE};
static bool gl_saved_filter(GLint minimum,GLint magnification,qa_scene_filter *out,bool *linear_magnification)
{
    static const GLint minima[]={GL_NEAREST,GL_LINEAR,GL_NEAREST_MIPMAP_NEAREST,
        GL_LINEAR_MIPMAP_NEAREST,GL_NEAREST_MIPMAP_LINEAR,GL_LINEAR_MIPMAP_LINEAR};
    if (magnification!=GL_NEAREST && magnification!=GL_LINEAR) return false;
    for (size_t i=0;i<sizeof(minima)/sizeof(*minima);++i)
        if (minimum==minima[i]) {
            *out=(qa_scene_filter)i; *linear_magnification=magnification==GL_LINEAR; return true;
        }
    return false;
}
static void gl_saved_zero_defaults(gl_saved_texture *zero)
{
    zero->parameters[0]=GL_NEAREST_MIPMAP_LINEAR; zero->parameters[1]=GL_LINEAR;
    zero->parameters[2]=GL_REPEAT; zero->parameters[3]=GL_REPEAT;
    zero->parameters[4]=1000; zero->parameters[5]=GL_NONE;
}
static void gl_cut_read(qa_gl_renderer *renderer,gl_native_cut *cut)
{
    gl_api *gl=&renderer->gl;
    gl->GetIntegerv(GL_CURRENT_PROGRAM,&cut->program); gl->GetIntegerv(GL_ACTIVE_TEXTURE,&cut->active);
    for (size_t i=0;i<3;++i) { gl->ActiveTexture(GL_TEXTURE0+(GLenum)i); gl->GetIntegerv(GL_TEXTURE_BINDING_2D,cut->texture+i); }
    gl->ActiveTexture((GLenum)cut->active);
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
    gl_api *gl=&renderer->gl;
    gl->UseProgram((GLuint)cut->program);
    for (size_t i=0;i<3;++i) { gl->ActiveTexture(GL_TEXTURE0+(GLenum)i); gl->BindTexture(GL_TEXTURE_2D,(GLuint)cut->texture[i]); }
    gl->ActiveTexture((GLenum)cut->active);
    gl->BindBuffer(GL_ARRAY_BUFFER,(GLuint)cut->array); gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER,(GLuint)cut->element);
    gl->BindBuffer(GL_PIXEL_PACK_BUFFER,(GLuint)cut->pack_buffer); gl->BindBuffer(GL_PIXEL_UNPACK_BUFFER,(GLuint)cut->unpack_buffer);
    gl->BindRenderbuffer(GL_RENDERBUFFER,(GLuint)cut->renderbuffer);
    gl->BindFramebuffer(GL_READ_FRAMEBUFFER,(GLuint)cut->read_framebuffer); gl->ReadBuffer((GLenum)cut->read_buffer);
    gl->BindFramebuffer(GL_DRAW_FRAMEBUFFER,(GLuint)cut->draw_framebuffer); gl->DrawBuffer((GLenum)cut->draw_buffer);
    for (size_t i=0;i<4;++i) { gl->PixelStorei(gl_pack_names[i],cut->pack[i]); gl->PixelStorei(gl_unpack_names[i],cut->unpack[i]); }
    gl->Viewport(cut->viewport[0],cut->viewport[1],cut->viewport[2],cut->viewport[3]);
    gl->Scissor(cut->scissor[0],cut->scissor[1],cut->scissor[2],cut->scissor[3]);
    if (cut->scissor_enabled) gl->Enable(GL_SCISSOR_TEST); else gl->Disable(GL_SCISSOR_TEST);
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
static bool gl_level_capture(qa_gl_renderer *renderer,GLuint texture,size_t level,bool depth,
    const qa_scene_image_level *canonical,gl_saved_level *saved,qa_error *error)
{
    if (level>INT_MAX) return gl_save_error(error,QA_ERROR_FORMAT,"GPU texture continuation has too many levels");
    gl_api *gl=&renderer->gl; GLint width=0,height=0,internal=0;
    gl->ActiveTexture(GL_TEXTURE0); gl->BindTexture(GL_TEXTURE_2D,texture);
    gl->GetTexLevelParameteriv(GL_TEXTURE_2D,(GLint)level,GL_TEXTURE_WIDTH,&width);
    gl->GetTexLevelParameteriv(GL_TEXTURE_2D,(GLint)level,GL_TEXTURE_HEIGHT,&height);
    gl->GetTexLevelParameteriv(GL_TEXTURE_2D,(GLint)level,GL_TEXTURE_INTERNAL_FORMAT,&internal);
    if (width<1 || height<1 || internal<1) return gl_save_error(error,QA_ERROR_FORMAT,"Retained GPU texture has no actual allocated level");
    saved->width=(uint32_t)width; saved->height=(uint32_t)height; saved->internal=internal; saved->depth=depth;
    size_t size=0;
    if (!gl_save_extent(saved->width,saved->height,&size,error)) return false;
    if (canonical && !depth) {
        if (canonical->width!=saved->width || canonical->height!=saved->height ||
            canonical->bytes!=size || !canonical->pixels)
            return gl_save_error(error,QA_ERROR_FORMAT,"GPU image cache has no matching admitted source level");
        saved->pixels=(qa_buffer){(uint8_t *)canonical->pixels,size}; saved->borrowed=true;
        return gl_check(renderer,"Reading retained GPU texture allocation",error);
    }
    if (!gl_save_allocate(&saved->pixels,size,error)) return false;
    gl_tight_pixels(renderer);
    gl->GetTexImage(GL_TEXTURE_2D,(GLint)level,depth?GL_DEPTH_COMPONENT:GL_RGBA,depth?GL_FLOAT:GL_UNSIGNED_BYTE,saved->pixels.data);
    return gl_check(renderer,"Capturing genuine retained GPU texture level",error);
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
    gl_api *gl=&renderer->gl; gl->BindFramebuffer(GL_READ_FRAMEBUFFER,framebuffer); gl->ReadBuffer(buffer);
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
        if (saved->color_texture) renderer->gl.DeleteTextures(1,&saved->color_texture);
        if (saved->depth_texture) renderer->gl.DeleteTextures(1,&saved->depth_texture);
    }
    qa_buffer_free(&saved->color); qa_buffer_free(&saved->depth); qa_buffer_free(&saved->stencil);
    memset(saved,0,sizeof(*saved));
}
static void gl_saved_dispose(gl_restore_storage *guard,qa_gl_renderer *renderer)
{
    if (!guard) return;
    for (size_t i=0;guard->zero_texture.levels && i<guard->zero_texture.count;++i)
        if (!guard->zero_texture.levels[i].borrowed) qa_buffer_free(&guard->zero_texture.levels[i].pixels);
    free(guard->zero_texture.levels);
    if (renderer && renderer->gl.DeleteTextures && guard->zero_prepared)
        renderer->gl.DeleteTextures(1,&guard->zero_prepared);
    while (guard->textures) {
        gl_saved_texture *row=guard->textures; guard->textures=row->next;
        if (row->levels) for (size_t i=0;i<row->count;++i)
            if (!row->levels[i].borrowed) qa_buffer_free(&row->levels[i].pixels);
        free(row->levels); free(row);
    }
    while (guard->meshes) {
        gl_saved_mesh *row=guard->meshes; guard->meshes=row->next;
        qa_buffer_free(&row->vertices); qa_buffer_free(&row->indices); free(row);
    }
    for (size_t i=0;i<4;++i) { gl_surface_free(renderer,guard->native+i); gl_surface_free(NULL,guard->output+i); }
    for (size_t i=0;i<2;++i) gl_surface_free(NULL,guard->opacity+i);
    for (size_t i=0;i<2;++i) qa_buffer_free(&guard->presented[i].pixels);
    qa_buffer_free(&guard->gamma.pixels); free(guard);
}
void gl_restore_storage_destroy(qa_gl_renderer *renderer)
{
    if (!renderer) return;
    gl_saved_dispose(renderer->restore,renderer); renderer->restore=NULL;
}
static bool gl_gpu_capture(qa_gl_renderer *renderer,gl_restore_storage *saved,bool full,qa_error *error)
{
    if (!renderer || renderer->closed || renderer->detached || renderer->executing || renderer->capturing || renderer->preparing || renderer->opacity.active || renderer->controls.source.entered || renderer->controls.image_ticket ||
        !qa_display_make_current(renderer->options.display,error)) return gl_save_error(error,QA_ERROR_ARGUMENT,"GPU capture requires its completed actual renderer owner");
    if (renderer->capabilities.color_bits>24 || renderer->capabilities.alpha_bits>8)
        return gl_save_error(error,QA_ERROR_UNSUPPORTED,"Native GPU color visual exceeds this exact RGBA8 continuation capture");
    if (!gl_dimensions(renderer,&saved->width,&saved->height,error)) return false;
    gl_native_cut cut={0}; gl_cut_read(renderer,&cut); renderer->capturing=true;
    if (!full) gl_saved_zero_defaults(&saved->zero_texture);
    GLint native_read_buffer=0;
    renderer->gl.BindFramebuffer(GL_READ_FRAMEBUFFER,0); renderer->gl.GetIntegerv(GL_READ_BUFFER,&native_read_buffer);
    GLuint read_framebuffer=0;
    if (full && (renderer->output.framebuffer || renderer->opacity.allocated)) renderer->gl.GenFramebuffers(1,&read_framebuffer);
    bool ok=!full || !(renderer->output.framebuffer || renderer->opacity.allocated) || read_framebuffer!=0;
    if (!ok) gl_save_error(error,QA_ERROR_MEMORY,"Allocating isolated GPU continuation read framebuffer");
    for (size_t i=0;ok && i<4;++i) if (renderer->capabilities.native_buffer_mask&(1u<<i))
        ok=gl_surface_capture(renderer,0,gl_native_buffer(renderer->capabilities.stereo,i),saved->width,saved->height,true,i==2,saved->native+i,error);
    for (size_t i=0;full && ok && renderer->presented && i<(renderer->capabilities.stereo?2u:1u);++i)
        if (!(renderer->capabilities.native_buffer_mask&(1u<<i)))
            ok=gl_level_capture(renderer,renderer->presented_target.color[i],0,false,NULL,saved->presented+i,error);
    saved->output_allocated=full && renderer->output.framebuffer!=0;
    for (size_t i=0;full && ok && i<4;++i) if (renderer->output.color_ready[i]) {
        renderer->gl.BindFramebuffer(GL_READ_FRAMEBUFFER,read_framebuffer);
        renderer->gl.FramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,renderer->output.color[i],0);
        renderer->gl.FramebufferRenderbuffer(GL_READ_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,renderer->output.depth_stencil);
        renderer->gl.FramebufferRenderbuffer(GL_READ_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,GL_RENDERBUFFER,
            renderer->capabilities.stencil_bits?renderer->output.depth_stencil:0);
        ok=gl_surface_capture(renderer,read_framebuffer,GL_COLOR_ATTACHMENT0,renderer->output.width,renderer->output.height,true,true,saved->output+i,error);
    }
    if (full && ok && renderer->output.table) ok=gl_level_capture(renderer,renderer->output.table,0,false,NULL,&saved->gamma,error);
    saved->opacity_allocated=full && renderer->opacity.allocated;
    for (size_t i=0;full && ok && i<2 && renderer->opacity.allocated;++i) {
        renderer->gl.BindFramebuffer(GL_READ_FRAMEBUFFER,read_framebuffer);
        renderer->gl.FramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,renderer->opacity.color[i],0);
        renderer->gl.FramebufferRenderbuffer(GL_READ_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,i==1?renderer->opacity.depth_stencil:0);
        renderer->gl.FramebufferRenderbuffer(GL_READ_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,GL_RENDERBUFFER,
            i==1 && renderer->capabilities.stencil_bits?renderer->opacity.depth_stencil:0);
        ok=gl_surface_capture(renderer,read_framebuffer,GL_COLOR_ATTACHMENT0,renderer->opacity.width,renderer->opacity.height,true,i==1,saved->opacity+i,error);
    }
    gl_saved_texture **texture_tail=&saved->textures;
    if (full && ok) {
        gl_saved_texture *row=&saved->zero_texture;
        row->count=renderer->controls.zero_texture.count;
        row->levels=row->count?calloc(row->count,sizeof(*row->levels)):NULL;
        if (row->count && !row->levels) ok=gl_save_error(error,QA_ERROR_MEMORY,"Retaining actual default texture mip levels");
        for (size_t i=0;ok && i<row->count;++i)
            ok=gl_level_capture(renderer,0,i,renderer->controls.zero_texture.kinds[i]==QA_SCENE_DEPTH32F,
                renderer->controls.zero_texture.levels+i,row->levels+i,error);
        renderer->gl.ActiveTexture(GL_TEXTURE0); renderer->gl.BindTexture(GL_TEXTURE_2D,0);
        for (size_t i=0;ok && i<6;++i)
            renderer->gl.GetTexParameteriv(GL_TEXTURE_2D,gl_texture_parameters[i],row->parameters+i);
    }
    for (gl_texture_entry *entry=renderer->textures;full && ok && entry;entry=entry->next) {
        gl_saved_texture *row=calloc(1,sizeof(*row));
        if (!row) { ok=gl_save_error(error,QA_ERROR_MEMORY,"Retaining genuine GPU texture continuation row"); break; }
        *texture_tail=row; texture_tail=&row->next; row->entry=entry;
        row->count=entry->source_admitted?entry->source_texture.count:entry->image->level_count;
        if (row->count>SIZE_MAX/sizeof(*row->levels) || (row->count && !(row->levels=calloc(row->count,sizeof(*row->levels))))) {
            ok=gl_save_error(error,QA_ERROR_MEMORY,"Retaining actual GPU texture mip-level roster"); break;
        }
        for (size_t i=0;ok && i<row->count;++i)
            ok=gl_level_capture(renderer,entry->name,i,(entry->source_admitted?entry->source_texture.kinds[i]:
                entry->image->kind)==QA_SCENE_DEPTH32F,
                entry->source_admitted?entry->source_texture.levels+i:entry->image->levels+i,row->levels+i,error);
        renderer->gl.ActiveTexture(GL_TEXTURE0); renderer->gl.BindTexture(GL_TEXTURE_2D,entry->name);
        for (size_t i=0;ok && i<6;++i)
            renderer->gl.GetTexParameteriv(GL_TEXTURE_2D,gl_texture_parameters[i],row->parameters+i);
    }
    gl_saved_mesh **mesh_tail=&saved->meshes;
    for (gl_mesh_entry *entry=renderer->meshes;full && ok && entry;entry=entry->next) {
        gl_saved_mesh *row=calloc(1,sizeof(*row));
        if (!row) { ok=gl_save_error(error,QA_ERROR_MEMORY,"Retaining real GPU cache geometry continuation row"); break; }
        *mesh_tail=row; mesh_tail=&row->next; row->entry=entry; row->active=qa_scene_geometry_active(entry->geometry);
        if (entry->vertex_count>SIZE_MAX/sizeof(qa_scene_vertex) || entry->index_count>SIZE_MAX/sizeof(uint32_t) ||
            entry->vertex_count*sizeof(qa_scene_vertex)>PTRDIFF_MAX || entry->index_count*sizeof(uint32_t)>PTRDIFF_MAX ||
            !gl_save_allocate(&row->vertices,entry->vertex_count*sizeof(qa_scene_vertex),error) ||
            !gl_save_allocate(&row->indices,entry->index_count*sizeof(uint32_t),error)) { ok=false; break; }
        renderer->gl.BindBuffer(GL_ARRAY_BUFFER,entry->vertex_buffer);
        if (row->vertices.size) renderer->gl.GetBufferSubData(GL_ARRAY_BUFFER,0,(GLsizeiptr)row->vertices.size,row->vertices.data);
        renderer->gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER,entry->index_buffer);
        if (row->indices.size) renderer->gl.GetBufferSubData(GL_ELEMENT_ARRAY_BUFFER,0,(GLsizeiptr)row->indices.size,row->indices.data);
        ok=gl_check(renderer,"Capturing genuine retained GPU mesh buffer storage",error);
    }
    saved->stream_vertices=full && renderer->stream.vertex_buffer!=0; saved->stream_indices=full && renderer->stream.index_buffer!=0;
    saved->target_allocated=full && renderer->target_framebuffer!=0; saved->fog_allocated=full && renderer->fog_depth!=0;
    if (read_framebuffer) renderer->gl.DeleteFramebuffers(1,&read_framebuffer);
    renderer->gl.BindFramebuffer(GL_READ_FRAMEBUFFER,0); renderer->gl.ReadBuffer((GLenum)native_read_buffer);
    gl_cut_restore(renderer,&cut); renderer->capturing=false;
    return ok && gl_check(renderer,"Restoring active GPU bindings after readonly continuation capture",error);
}
static bool gl_saved_pixels(qa_source_save_io *io,qa_buffer *pixels,size_t size,unsigned kind)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if (reading && !gl_save_allocate(pixels,size,io->error)) return false;
    if (pixels->size!=size || (size && !pixels->data)) return false;
    if (!kind) return qa_source_save_bytes(io,pixels->data,size);
    if (size%4) return false;
    for (size_t offset=0;offset<size;offset+=4) {
        if (kind==1) {
            float value=0; if (!reading) memcpy(&value,pixels->data+offset,4);
            if (!qa_source_save_f32(io,&value)) return false;
            if (reading) memcpy(pixels->data+offset,&value,4);
        } else {
            uint32_t value=0; if (!reading) memcpy(&value,pixels->data+offset,4);
            if (!qa_source_save_u32(io,&value)) return false;
            if (reading) memcpy(pixels->data+offset,&value,4);
        }
    }
    return true;
}
static bool gl_saved_level_fields(qa_source_save_io *io,gl_saved_level *level,
    const qa_scene_image_level *canonical)
{
    size_t size=0;
    if (!qa_source_save_u32(io,&level->width) || !qa_source_save_u32(io,&level->height) ||
        !qa_source_save_i32(io,&level->internal) || level->internal<=0 || !qa_source_save_bool(io,&level->depth) ||
        !gl_save_extent(level->width,level->height,&size,io->error)) return false;
    if (canonical && !level->depth) {
        if (canonical->width!=level->width || canonical->height!=level->height || canonical->bytes!=size ||
            !canonical->pixels) return false;
        if (io->direction==QA_SOURCE_SAVE_READ) {
            level->pixels=(qa_buffer){(uint8_t *)canonical->pixels,size}; level->borrowed=true;
        }
        return true;
    }
    return gl_saved_pixels(io,&level->pixels,size,level->depth?1:0);
}
static bool gl_saved_surface_fields(qa_source_save_io *io,gl_saved_surface *surface,unsigned expected)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ,present=surface->width!=0;
    if (!qa_source_save_bool(io,&present) || present!=(expected!=0)) return false;
    if (!present) return true;
    size_t size=0;
    if (!qa_source_save_u32(io,&surface->width) || !qa_source_save_u32(io,&surface->height) ||
        !qa_source_save_bool(io,&surface->floating_depth) || surface->floating_depth!=((expected&8)!=0) ||
        !gl_save_extent(surface->width,surface->height,&size,io->error)) return false;
    if ((expected&1) && !gl_saved_pixels(io,&surface->color,size,0)) return false;
    if ((expected&2) && !gl_saved_pixels(io,&surface->depth,size,surface->floating_depth?1:2)) return false;
    if ((expected&4) && !gl_saved_pixels(io,&surface->stencil,size,2)) return false;
    if (!reading && ((!(expected&1) && surface->color.size) || (!(expected&2) && surface->depth.size) ||
        (!(expected&4) && surface->stencil.size))) return false;
    return true;
}
static bool gl_saved_caps(qa_source_save_io *io,qa_gl_capabilities *caps)
{
    uint32_t color=caps->color_bits,alpha=caps->alpha_bits,depth=caps->depth_bits,stencil=caps->stencil_bits;
    if (!qa_source_save_u32(io,&color) || color>128 || !qa_source_save_u32(io,&alpha) || alpha>32 ||
        !qa_source_save_u32(io,&depth) || !depth || depth>32 || !qa_source_save_u32(io,&stencil) || stencil>32 ||
        !qa_source_save_u32(io,&caps->maximum_texture_size) || !caps->maximum_texture_size ||
        !qa_source_save_u32(io,&caps->texture_units) || caps->texture_units<3 ||
        !qa_source_save_u32(io,&caps->vertex_attributes) || caps->vertex_attributes<5 ||
        !qa_source_save_bool(io,&caps->stereo) || !qa_source_save_u32(io,&caps->native_buffer_mask) ||
        (caps->native_buffer_mask&~(caps->stereo?15u:5u)) ||
        (caps->native_buffer_mask&(caps->stereo?12u:4u))!=(caps->stereo?12u:4u) ||
        !qa_source_save_bool(io,&caps->floating_depth) ||
        !qa_source_save_bool(io,&caps->compiled_vertex_arrays)) return false;
    if (!qa_source_save_bool(io,&caps->s3tc)) return false;
    caps->color_bits=color; caps->alpha_bits=alpha; caps->depth_bits=depth; caps->stencil_bits=stencil;
    char *strings[4]={caps->vendor,caps->renderer,caps->version,caps->shading_language};
    for (size_t i=0;i<4;++i)
        if (!qa_source_save_bytes(io,strings[i],128) || !memchr(strings[i],0,128)) return false;
    return true;
}
static bool gl_caps_equal(const qa_gl_capabilities *a,const qa_gl_capabilities *b)
{
    return a->color_bits==b->color_bits && a->alpha_bits==b->alpha_bits && a->depth_bits==b->depth_bits &&
        a->stencil_bits==b->stencil_bits && a->maximum_texture_size==b->maximum_texture_size &&
        a->texture_units==b->texture_units && a->vertex_attributes==b->vertex_attributes &&
        a->stereo==b->stereo && a->native_buffer_mask==b->native_buffer_mask && a->floating_depth==b->floating_depth &&
        a->compiled_vertex_arrays==b->compiled_vertex_arrays && a->s3tc==b->s3tc &&
        !strcmp(a->vendor,b->vendor) && !strcmp(a->renderer,b->renderer) && !strcmp(a->version,b->version) &&
        !strcmp(a->shading_language,b->shading_language);
}
static bool gl_saved_geometry(qa_source_save_io *io,const qa_render_checkpoint_refs *refs,
    gl_saved_mesh *head,gl_saved_mesh *row)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ; gl_mesh_entry *entry=row->entry;
    uint64_t geometry=0,identity=entry->identity,retired_alias=0;
    if (!reading && !row->active) {
        uint64_t ordinal=1;
        for (gl_saved_mesh *prior=head;prior!=row;prior=prior->next,++ordinal)
            if (!prior->active && prior->entry->geometry==entry->geometry) { retired_alias=ordinal; break; }
    }
    if (!qa_source_save_bool(io,&row->active) || !qa_source_save_u64(io,&retired_alias) ||
        (row->active && retired_alias)) return false;
    if (!reading && row->active && (!refs || !refs->geometry_encode || !refs->mesh_identity_encode ||
        !refs->geometry_encode(refs->context,entry->geometry,&geometry,io->error) || !geometry ||
        !refs->mesh_identity_encode(refs->context,entry->identity,&identity,io->error) || !identity)) return false;
    if (!qa_source_save_u64(io,&geometry) || (row->active!=(geometry!=0)) ||
        !qa_source_save_u64(io,&identity) || !identity || !qa_source_save_u64(io,&entry->revision) ||
        !qa_source_save_count(io,&entry->vertex_count,(size_t)PTRDIFF_MAX/sizeof(qa_scene_vertex)) ||
        !qa_source_save_count(io,&entry->index_count,(size_t)PTRDIFF_MAX/sizeof(uint32_t))) return false;
    if (reading) {
        if (row->active) {
            const qa_scene_geometry *actual=NULL;
            if (!refs || !refs->geometry_decode || !refs->mesh_identity_decode ||
                !refs->geometry_decode(refs->context,geometry,&actual,io->error) || !actual ||
                !refs->mesh_identity_decode(refs->context,identity,&entry->identity,io->error) || !entry->identity ||
                !qa_scene_geometry_active(actual)) return false;
            qa_scene_geometry_cache_retain(actual); entry->geometry=actual;
        } else {
            if (retired_alias) {
                gl_saved_mesh *prior=head; uint64_t ordinal=1;
                while (prior!=row && ordinal<retired_alias) { prior=prior->next; ++ordinal; }
                if (prior==row || ordinal!=retired_alias || prior->active ||
                    !prior->entry->geometry || qa_scene_geometry_active(prior->entry->geometry)) return false;
                entry->geometry=prior->entry->geometry; qa_scene_geometry_cache_retain(entry->geometry);
            } else {
                qa_scene_geometry *retired=NULL;
                if (!qa_scene_geometry_restore_retired(&retired,io->error)) return false;
                entry->geometry=retired;
            }
            entry->identity=identity;
        }
    }
    _Static_assert(sizeof(qa_scene_vertex)==14*sizeof(float),"GPU retained vertex schema uses fourteen source float32 values");
    if (!gl_saved_pixels(io,&row->vertices,entry->vertex_count*sizeof(qa_scene_vertex),1) ||
        !gl_saved_pixels(io,&row->indices,entry->index_count*sizeof(uint32_t),2)) return false;
    for (size_t i=0;i<entry->index_count;++i) {
        uint32_t index=0; memcpy(&index,row->indices.data+i*4,4);
        if (index>=entry->vertex_count) return false;
    }
    /* Actual meshes may borrow an allocation slice or transient vertices
     * while geometry retains only indices. The GPU cache owns its uploaded
     * bytes; its genuine receiver tuple is geometry + identity/revision/counts. */
    if (row->active && !qa_scene_geometry_active(entry->geometry)) return false;
    return true;
}
static bool gl_saved_private_fields(qa_source_save_io *io,qa_gl_renderer *renderer,gl_restore_storage *saved,
    const qa_render_checkpoint_refs *refs,const qa_gl_options *installed,const qa_gl_renderer *active)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    uint8_t magic[4]={'Q','G','L','R'}; uint32_t draw=renderer->draw_buffer;
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QGLR",4) || !qa_render_controls_saved_fields(io,&renderer->controls,refs) ||
        !qa_source_save_u64(io,&renderer->options.owner) || !qa_source_save_u32(io,&saved->width) ||
        !qa_source_save_u32(io,&saved->height) || !gl_saved_caps(io,&renderer->capabilities) ||
        !qa_source_save_u32(io,&draw) || draw>QA_DRAW_BACK_RIGHT ||
        (draw==QA_DRAW_BACK_RIGHT && !renderer->capabilities.stereo) ||
        !qa_source_save_f32(io,&renderer->gamma) || !isfinite(renderer->gamma) || renderer->gamma<=0 ||
        !qa_source_save_u64(io,&renderer->sequence) || !qa_source_save_u32(io,&renderer->presented_width) ||
        !qa_source_save_u32(io,&renderer->presented_height) || !qa_source_save_bool(io,&renderer->presented) ||
        !qa_source_save_bool(io,&renderer->overdraw) || !render_save_view(io,&renderer->view)) return false;
    size_t size=0;
    if (!gl_save_extent(saved->width,saved->height,&size,io->error) ||
        (renderer->presented && (renderer->presented_width!=saved->width || renderer->presented_height!=saved->height))) return false;
    renderer->draw_buffer=(qa_scene_draw_buffer)draw;
    if (!qa_output_domains_codec(io,&renderer->output_domains,saved->width,saved->height)) return false;
    {
        if (!render_save_pipeline(io,&renderer->pipeline)) return false;
    } {
        if (!qa_source_save_f32(io,&renderer->clear_depth) || !isfinite(renderer->clear_depth) ||
            renderer->clear_depth<0 || renderer->clear_depth>1) return false;
    } {
        if (!qa_source_save_bool(io,&renderer->preblend_gamma)) return false;
    } {
        if (!qa_source_save_bool(io,&renderer->source_frame)) return false;
    } if ((!qa_source_save_u32(io,&renderer->source_image_count) ||
        renderer->source_image_count>GL_SOURCE_IMAGES_QA)) return false;
    if (reading) {
        if (!installed || !active || installed->owner!=renderer->options.owner || !installed->display ||
            !gl_caps_equal(&renderer->capabilities,&active->capabilities)) return false;
        renderer->options.display=installed->display;
    }
    for (size_t i=0;i<4;++i) {
        unsigned expected=(renderer->capabilities.native_buffer_mask&(1u<<i))?
            1u|(i==2?2u|(renderer->capabilities.stencil_bits?4u:0u)|
            (renderer->capabilities.floating_depth?8u:0u):0u):0u;
        if (!gl_saved_surface_fields(io,saved->native+i,expected) || (expected &&
            (saved->native[i].width!=saved->width || saved->native[i].height!=saved->height))) return false;
    }
    for (size_t i=0;i<2;++i) {
        bool expected=renderer->presented && (renderer->capabilities.stereo || i==0) &&
            !(renderer->capabilities.native_buffer_mask&(1u<<i));
        bool present=saved->presented[i].width!=0;
        if (!qa_source_save_bool(io,&present) || present!=expected || (present &&
            (!gl_saved_level_fields(io,saved->presented+i,NULL) || saved->presented[i].width!=saved->width ||
                saved->presented[i].height!=saved->height || saved->presented[i].depth ||
                saved->presented[i].internal!=GL_RGBA8))) return false;
    }
    if (!qa_source_save_bool(io,&saved->output_allocated) || !qa_source_save_bool(io,&renderer->output.enabled) ||
        !qa_source_save_u32(io,&renderer->output.width) || !qa_source_save_u32(io,&renderer->output.height) ||
        (!saved->output_allocated && (renderer->output.enabled || renderer->output.width || renderer->output.height))) return false;
    for (size_t i=0;i<4;++i) {
        if (!qa_source_save_bool(io,renderer->output.color_ready+i) || !qa_source_save_bool(io,renderer->output.dirty+i) ||
            (renderer->output.dirty[i] && !renderer->output.color_ready[i]) ||
            (renderer->output.color_ready[i] && !saved->output_allocated)) return false;
        unsigned expected=renderer->output.color_ready[i]?3u|(renderer->capabilities.stencil_bits?4u:0u)|
            (renderer->capabilities.floating_depth?8u:0u):0u;
        if (!gl_saved_surface_fields(io,saved->output+i,expected) ||
            (expected && (saved->output[i].width!=renderer->output.width || saved->output[i].height!=renderer->output.height))) return false;
    }
    bool gamma=reading?false:saved->gamma.width!=0;
    if (!qa_source_save_bool(io,&gamma) || (renderer->output.enabled && !gamma) ||
        (gamma && (!saved->output_allocated || !gl_saved_level_fields(io,&saved->gamma,NULL) ||
            saved->gamma.width!=256 || saved->gamma.height!=1 || saved->gamma.depth))) return false;
    if (!qa_source_save_bool(io,&saved->opacity_allocated) || !qa_source_save_u32(io,&renderer->opacity.width) ||
        !qa_source_save_u32(io,&renderer->opacity.height) || !qa_source_save_f32(io,&renderer->opacity.value) ||
        !isfinite(renderer->opacity.value) || renderer->opacity.value<0 || renderer->opacity.value>1 ||
        !qa_source_save_bool(io,&renderer->opacity.skip) ||
        !qa_source_save_bool(io,&renderer->opacity.parent_scissor_enabled)) return false;
    for (size_t i=0;i<4;++i) {
        int32_t viewport=renderer->opacity.parent_viewport[i],scissor=renderer->opacity.parent_scissor[i];
        if (!qa_source_save_i32(io,&viewport) || !qa_source_save_i32(io,&scissor)) return false;
        renderer->opacity.parent_viewport[i]=viewport; renderer->opacity.parent_scissor[i]=scissor;
    }
    /* Parent bindings are inert after the completed opacity scope. The next
     * actual begin captures fresh bindings before it can restore them. */
    uint32_t parent_draw=renderer->opacity.parent_draw_framebuffer,parent_read=renderer->opacity.parent_read_framebuffer,
        draw_buffer=renderer->opacity.parent_draw_buffer,read_buffer=renderer->opacity.parent_read_buffer;
    if (!qa_source_save_u32(io,&parent_draw) || !qa_source_save_u32(io,&parent_read) ||
        !qa_source_save_u32(io,&draw_buffer) || !qa_source_save_u32(io,&read_buffer)) return false;
    renderer->opacity.parent_draw_framebuffer=parent_draw; renderer->opacity.parent_read_framebuffer=parent_read;
    renderer->opacity.parent_draw_buffer=draw_buffer; renderer->opacity.parent_read_buffer=read_buffer;
    for (size_t i=0;i<2;++i) {
        unsigned expected=saved->opacity_allocated?1u|(i==1?2u|(renderer->capabilities.stencil_bits?4u:0u)|
            (renderer->capabilities.floating_depth?8u:0u):0u):0u;
        if (!gl_saved_surface_fields(io,saved->opacity+i,expected) ||
            (expected && (saved->opacity[i].width!=renderer->opacity.width || saved->opacity[i].height!=renderer->opacity.height))) return false;
    }
    if (!saved->opacity_allocated && (renderer->opacity.width || renderer->opacity.height)) return false;
    size_t count=0; gl_saved_texture *texture=saved->textures,**texture_tail=&saved->textures;
    if (!reading) for (;texture;texture=texture->next) ++count;
    if (!qa_source_save_count(io,&count,SIZE_MAX/sizeof(gl_saved_texture))) return false;
    texture=saved->textures; gl_texture_entry **entry_tail=&renderer->textures;
    for (size_t i=0;i<count;++i) {
        if (reading) {
            texture=calloc(1,sizeof(*texture)); gl_texture_entry *entry=calloc(1,sizeof(*entry));
            if (!texture || !entry) { free(texture); free(entry); return gl_save_error(io->error,QA_ERROR_MEMORY,"Restoring actual GPU image cache rows"); }
            *texture_tail=texture; texture_tail=&texture->next; *entry_tail=entry; entry_tail=&entry->next; texture->entry=entry;
        }
        if (!texture || !render_save_image(io,refs,&texture->entry->image) || !texture->entry->image) return false;
        {
            gl_texture_entry *entry=texture->entry;
            if (!qa_source_save_bool(io,&entry->source_admitted) ||
                !qa_source_save_u32(io,&entry->source_ordinal)) return false;
            if (entry->source_admitted) {
                if (!entry->image->source_q3 || entry->source_ordinal>=renderer->source_image_count) return false;
                if (reading) {
                    if (renderer->source_images[entry->source_ordinal]) return false;
                    qa_scene_resources *owner=qa_scene_image_resource_owner(entry->image);
                    if (!owner || !qa_scene_resources_retain(owner,io->error)) return false;
                    entry->source_owner=owner;
                    renderer->source_images[entry->source_ordinal]=entry;
                } else if (renderer->source_images[entry->source_ordinal]!=entry ||
                    entry->source_owner!=qa_scene_image_resource_owner(entry->image)) return false;
            } else if (entry->source_ordinal) return false;
        }
        if (texture->entry->source_admitted && !qa_render_source_texture_saved_fields(io,&texture->entry->source_texture,refs)) return false;
        size_t expected_count=texture->entry->source_admitted?
            texture->entry->source_texture.count:texture->entry->image->level_count;
        if (!qa_source_save_count(io,&texture->count,(size_t)INT_MAX) || texture->count!=expected_count ||
            (!texture->count && (!texture->entry->source_admitted)) ||
            texture->count>SIZE_MAX/sizeof(gl_saved_level)) return false;
        if (reading) {
            texture->levels=texture->count?calloc(texture->count,sizeof(*texture->levels)):NULL;
            if (texture->count && !texture->levels) return gl_save_error(io->error,QA_ERROR_MEMORY,"Restoring GPU image mip-level continuation");
        }
        for (size_t j=0;j<texture->count;++j) {
            const qa_scene_image *descriptor=texture->entry->source_admitted?
                texture->entry->source_texture.images[j]:texture->entry->image;
            const qa_scene_image_level *image=texture->entry->source_admitted?
                texture->entry->source_texture.levels+j:descriptor->levels+j;
            if (!gl_saved_level_fields(io,texture->levels+j,image) || texture->levels[j].width!=image->width ||
                texture->levels[j].height!=image->height || texture->levels[j].depth!=
                    ((texture->entry->source_admitted?texture->entry->source_texture.kinds[j]:
                        descriptor->kind)==QA_SCENE_DEPTH32F)) return false;
        }
        for (size_t j=0;j<6;++j) if (!qa_source_save_i32(io,texture->parameters+j)) return false;
        qa_scene_filter actual_filter; bool linear_magnification;
        if (!gl_saved_filter(texture->parameters[0],texture->parameters[1],&actual_filter,&linear_magnification)) return false;
        if (reading) texture->entry->source_filter=actual_filter;
        if (texture->entry->source_admitted) {
            qa_render_source_texture *actual=&texture->entry->source_texture;
            if (actual->filter!=actual_filter || (actual->magnification_linear!=linear_magnification)) return false;
            }
        for (gl_saved_texture *prior=saved->textures;prior!=texture;prior=prior->next)
            if (prior->entry->image==texture->entry->image ||
                (prior->entry->image->identity==texture->entry->image->identity && prior->entry->image->revision==texture->entry->image->revision)) return false;
        texture=texture->next;
    }
    for (uint32_t i=0;i<renderer->source_image_count;++i) if (!renderer->source_images[i]) return false;
    {
        gl_saved_texture *zero=&saved->zero_texture;
        if (!qa_source_save_count(io,&zero->count,QA_SOURCE_TEXTURE_LEVELS) ||
            zero->count!=renderer->controls.zero_texture.count) return false;
        if (reading) {
            zero->levels=zero->count?calloc(zero->count,sizeof(*zero->levels)):NULL;
            if (zero->count && !zero->levels) return gl_save_error(io->error,QA_ERROR_MEMORY,"Restoring actual default texture storage");
        }
        for (size_t i=0;i<zero->count;++i) {
            const qa_scene_image_level *actual=renderer->controls.zero_texture.levels+i;
            if (!gl_saved_level_fields(io,zero->levels+i,actual) || zero->levels[i].width!=actual->width ||
                zero->levels[i].height!=actual->height || zero->levels[i].depth!=
                    (renderer->controls.zero_texture.kinds[i]==QA_SCENE_DEPTH32F)) return false;
        }
        for (size_t i=0;i<6;++i) if (!qa_source_save_i32(io,zero->parameters+i)) return false;
        qa_scene_filter actual_filter; bool linear_magnification;
        if (!gl_saved_filter(zero->parameters[0],zero->parameters[1],&actual_filter,&linear_magnification) ||
            actual_filter!=renderer->controls.zero_texture.filter ||
            (linear_magnification!=renderer->controls.zero_texture.magnification_linear) ||
            zero->parameters[2]!=(renderer->controls.zero_texture.wrap==QA_SCENE_REPEAT?GL_REPEAT:GL_CLAMP) ||
            zero->parameters[3]!=zero->parameters[2] || zero->parameters[4]!=1000 || zero->parameters[5]!=GL_NONE) return false;
        }
    count=0; gl_saved_mesh *mesh=saved->meshes,**mesh_tail=&saved->meshes;
    if (!reading) for (;mesh;mesh=mesh->next) ++count;
    if (!qa_source_save_count(io,&count,SIZE_MAX/sizeof(gl_saved_mesh))) return false;
    mesh=saved->meshes; gl_mesh_entry **mesh_entry_tail=&renderer->meshes;
    for (size_t i=0;i<count;++i) {
        if (reading) {
            mesh=calloc(1,sizeof(*mesh)); gl_mesh_entry *entry=calloc(1,sizeof(*entry));
            if (!mesh || !entry) { free(mesh); free(entry); return gl_save_error(io->error,QA_ERROR_MEMORY,"Restoring GPU mesh residency owner rows"); }
            *mesh_tail=mesh; mesh_tail=&mesh->next; *mesh_entry_tail=entry; mesh_entry_tail=&entry->next; mesh->entry=entry;
        }
        if (!mesh || !gl_saved_geometry(io,refs,saved->meshes,mesh)) return false;
        for (gl_saved_mesh *prior=saved->meshes;prior!=mesh;prior=prior->next)
            if (prior->active==mesh->active && prior->entry->identity==mesh->entry->identity &&
                prior->entry->revision==mesh->entry->revision) return false;
        mesh=mesh->next;
    }
    if (!qa_source_save_bool(io,&saved->stream_vertices) || !qa_source_save_bool(io,&saved->stream_indices) ||
        !qa_source_save_count(io,&renderer->stream.vertex_bytes,(size_t)PTRDIFF_MAX) ||
        !qa_source_save_count(io,&renderer->stream.index_bytes,(size_t)PTRDIFF_MAX) ||
        (!saved->stream_vertices && renderer->stream.vertex_bytes) || (!saved->stream_indices && renderer->stream.index_bytes) ||
        !qa_source_save_bool(io,&saved->target_allocated) || !qa_source_save_bool(io,&saved->fog_allocated) ||
        !render_save_image(io,refs,&renderer->target)) return false;
    if (renderer->target && (renderer->target->kind!=QA_SCENE_DEPTH32F || !saved->target_allocated)) return false;
    uint32_t viewport_height=renderer->target?renderer->target->levels[0].height:saved->height;
    int64_t bottom=(int64_t)viewport_height-renderer->view.viewport.y-renderer->view.viewport.height;
    if (!renderer->view.viewport.width || !renderer->view.viewport.height ||
        renderer->view.viewport.width>INT_MAX || renderer->view.viewport.height>INT_MAX || bottom<INT_MIN || bottom>INT_MAX)
        return false;
    for (size_t i=0;i<2;++i) if (!render_save_image(io,refs,renderer->bound+i)) return false;
    return true;
}

static bool gl_saved_write(qa_gl_renderer *renderer,gl_restore_storage *saved,
    const qa_render_checkpoint_refs *refs,qa_buffer *out,qa_error *error)
{
    qa_source_save_io io={0}; qa_gl_renderer state=*renderer;
    bool ok=qa_source_save_writer(&io,NULL,error) &&
        gl_saved_private_fields(&io,&state,saved,refs,NULL,NULL) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);
    if (!ok && error && error->code==QA_OK) gl_save_error(error,QA_ERROR_FORMAT,"Actual GPU owner continuation is inconsistent");
    return ok;
}
bool qa_gl_checkpoint(qa_gl_renderer *renderer,const qa_render_checkpoint_refs *refs,qa_buffer *out,qa_error *error)
{
    if (!out || out->data || out->size || !renderer || renderer->surface_ticket || renderer->controls.ticket || renderer->controls.image_ticket || renderer->controls.source.entered)
        return gl_save_error(error,QA_ERROR_ARGUMENT,"GPU checkpoint requires empty output and an owner without retained surface settings");
    gl_restore_storage *saved=calloc(1,sizeof(*saved));
    if (!saved) return gl_save_error(error,QA_ERROR_MEMORY,"Retaining completed GPU owner continuation");
    bool ok=gl_gpu_capture(renderer,saved,true,error) && gl_saved_write(renderer,saved,refs,out,error);
    gl_saved_dispose(saved,NULL); return ok;
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
    gl_saved_zero_defaults(&saved->zero_texture);
    guard->active=active; guard->candidate=candidate; guard->saved=saved; guard->fresh=true;
    *out=candidate; *guard_out=guard; return true;
}
bool qa_gl_restore(qa_bytes bytes,const qa_gl_options *options,const qa_render_checkpoint_refs *refs,
    const qa_gl_renderer *active,qa_gl_renderer **out,qa_gl_restore_guard **guard_out,qa_error *error)
{
    if (!out || !guard_out || !options || !options->display || !active || active->closed || active->detached ||
        active->executing || active->capturing || active->preparing || active->surface_ticket ||
        active->controls.ticket || active->controls.image_ticket || active->controls.source.entered || active->opacity.active)
        return gl_save_error(error,QA_ERROR_ARGUMENT,"GPU restore requires its actual idle enclosing renderer/display");
    *out=NULL; *guard_out=NULL;
    qa_gl_renderer *candidate=calloc(1,sizeof(*candidate));
    gl_restore_storage *saved=calloc(1,sizeof(*saved));
    qa_gl_restore_guard *guard=calloc(1,sizeof(*guard));
    if (!candidate || !saved || !guard) {
        free(candidate); free(saved); free(guard);
        return gl_save_error(error,QA_ERROR_MEMORY,"Allocating detached GPU continuation owners");
    }
    candidate->options=*options; candidate->detached=true; candidate->restore=saved;
    qa_render_controls_init_gl(&candidate->controls, candidate);
    qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) &&
        gl_saved_private_fields(&io,candidate,saved,refs,options,active) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    uint32_t width=0,height=0;
    if (ok) ok=gl_dimensions(candidate,&width,&height,error) && width==saved->width && height==saved->height;
    if (!ok) {
        qa_gl_destroy(candidate); free(guard);
        if (error && error->code==QA_OK) gl_save_error(error,QA_ERROR_FORMAT,"Saved GPU owner does not match its actual drawable/receiver resources");
        return false;
    }
    guard->active=(qa_gl_renderer *)active; guard->candidate=candidate; guard->saved=saved;
    *out=candidate; *guard_out=guard; return true;
}
bool qa_gl_restore_checkpoint(const qa_gl_restore_guard *guard,const qa_render_checkpoint_refs *refs,
    qa_buffer *out,qa_error *error)
{
    if (!guard || guard->transferred || !guard->candidate || guard->candidate->closed ||
        guard->candidate->restore!=guard->saved || !guard->candidate->detached || !out || out->data || out->size)
        return gl_save_error(error,QA_ERROR_ARGUMENT,"GPU candidate recapture requires its retained detached owner");
    return gl_saved_write(guard->candidate,guard->saved,refs,out,error);
}

static GLint gl_saved_color_internal(const qa_gl_renderer *renderer)
{
    return renderer->capabilities.alpha_bits?GL_RGBA8:renderer->capabilities.color_bits<=16?GL_RGB565:GL_RGB8;
}
static GLint gl_saved_depth_internal(const qa_gl_renderer *renderer,bool native)
{
    const qa_gl_capabilities *caps=&renderer->capabilities;
    if (caps->stencil_bits) return caps->floating_depth?GL_DEPTH32F_STENCIL8:GL_DEPTH24_STENCIL8;
    if (caps->floating_depth) return GL_DEPTH_COMPONENT32F;
    (void)native;
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
static bool gl_saved_level_upload(qa_gl_renderer *renderer,GLuint *name,const gl_saved_level *levels,
    size_t count,qa_error *error)
{
    gl_api *gl=&renderer->gl; gl->GenTextures(1,name);
    if (!*name) return gl_save_error(error,QA_ERROR_MEMORY,"Preparing separate GPU texture continuation");
    gl->ActiveTexture(GL_TEXTURE0); gl->BindTexture(GL_TEXTURE_2D,*name); gl_saved_texture_parameters(renderer);
    for (size_t i=0;i<count;++i) {
        const gl_saved_level *level=levels+i;
        gl->TexImage2D(GL_TEXTURE_2D,(GLint)i,level->internal,(GLsizei)level->width,(GLsizei)level->height,0,
            level->depth?GL_DEPTH_COMPONENT:GL_RGBA,level->depth?GL_FLOAT:GL_UNSIGNED_BYTE,level->pixels.data);
    }
    return gl_check(renderer,"Preparing retained GPU texture levels",error);
}
static bool gl_saved_surface_upload(qa_gl_renderer *renderer,gl_saved_surface *surface,bool native,qa_error *error)
{
    gl_api *gl=&renderer->gl; gl->GenFramebuffers(1,&surface->framebuffer);
    if (!surface->framebuffer) return gl_save_error(error,QA_ERROR_MEMORY,"Preparing separate completed GPU framebuffer");
    gl->BindFramebuffer(GL_FRAMEBUFFER,surface->framebuffer);
    if (surface->color.size) {
        gl_saved_level color={.width=surface->width,.height=surface->height,
            .internal=native?GL_RGBA8:gl_saved_color_internal(renderer),.pixels=surface->color};
        if (!gl_saved_level_upload(renderer,&surface->color_texture,&color,1,error)) return false;
        gl->FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,surface->color_texture,0);
        gl->ReadBuffer(GL_COLOR_ATTACHMENT0); gl->DrawBuffer(GL_COLOR_ATTACHMENT0);
    } else { gl->ReadBuffer(GL_NONE); gl->DrawBuffer(GL_NONE); }
    if (surface->depth.size) {
        gl->GenTextures(1,&surface->depth_texture);
        if (!surface->depth_texture) return gl_save_error(error,QA_ERROR_MEMORY,"Preparing completed GPU depth/stencil pixels");
        gl->BindTexture(GL_TEXTURE_2D,surface->depth_texture); gl_saved_texture_parameters(renderer);
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
        gl->TexImage2D(GL_TEXTURE_2D,0,gl_saved_depth_internal(renderer,native),(GLsizei)surface->width,
            (GLsizei)surface->height,0,format,type,pixels); free(packed);
        gl->FramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,surface->depth_texture,0);
        if (surface->stencil.size) gl->FramebufferTexture2D(GL_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,GL_TEXTURE_2D,surface->depth_texture,0);
    }
    if (gl->CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)
        return gl_save_error(error,QA_ERROR_UNSUPPORTED,"Prepared saved GPU framebuffer is incomplete");
    return gl_check(renderer,"Preparing exact completed GPU framebuffer pixels",error);
}

static bool gl_saved_buffer_upload(qa_gl_renderer *renderer,GLenum target,GLuint *name,size_t size,
    const void *pixels,GLenum usage,qa_error *error)
{
    renderer->gl.GenBuffers(1,name);
    if (!*name) return gl_save_error(error,QA_ERROR_MEMORY,"Preparing separate GPU geometry buffer");
    renderer->gl.BindBuffer(target,*name); renderer->gl.BufferData(target,(GLsizeiptr)size,pixels,usage);
    return gl_check(renderer,"Preparing retained GPU geometry buffer bytes",error);
}
static bool gl_saved_target_upload(qa_gl_renderer *renderer,gl_saved_surface *saved,GLuint *framebuffer,
    GLuint *color,GLuint *depth_stencil,bool allocate_depth,qa_error *error)
{
    gl_api *gl=&renderer->gl;
    if (!*framebuffer) gl->GenFramebuffers(1,framebuffer);
    if (!*framebuffer) return gl_save_error(error,QA_ERROR_MEMORY,"Preparing actual retained private GPU target");
    gl->BindFramebuffer(GL_FRAMEBUFFER,*framebuffer);
    if (saved->color.size) {
        gl_saved_level level={.width=saved->width,.height=saved->height,
            .internal=gl_saved_color_internal(renderer),.pixels=saved->color};
        if (!gl_saved_level_upload(renderer,color,&level,1,error)) return false;
        gl->FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,*color,0);
        gl->DrawBuffer(GL_COLOR_ATTACHMENT0); gl->ReadBuffer(GL_COLOR_ATTACHMENT0);
    }
    if (allocate_depth) {
        if (!*depth_stencil) gl->GenRenderbuffers(1,depth_stencil);
        if (!*depth_stencil) return gl_save_error(error,QA_ERROR_MEMORY,"Preparing retained private GPU depth/stencil owner");
        gl->BindRenderbuffer(GL_RENDERBUFFER,*depth_stencil);
        gl->RenderbufferStorage(GL_RENDERBUFFER,(GLenum)gl_saved_depth_internal(renderer,false),
            (GLsizei)saved->width,(GLsizei)saved->height);
    }
    if (depth_stencil && *depth_stencil) {
        gl->FramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,*depth_stencil);
        if (renderer->capabilities.stencil_bits)
            gl->FramebufferRenderbuffer(GL_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,GL_RENDERBUFFER,*depth_stencil);
    }
    if (gl->CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)
        return gl_save_error(error,QA_ERROR_UNSUPPORTED,"Restored private GPU target is incomplete");
    if (allocate_depth && saved->depth.size) {
        gl_saved_surface source=*saved; source.framebuffer=source.color_texture=source.depth_texture=0;
        bool ok=gl_saved_surface_upload(renderer,&source,false,error);
        if (ok) {
            gl->BindFramebuffer(GL_READ_FRAMEBUFFER,source.framebuffer); gl->ReadBuffer(GL_COLOR_ATTACHMENT0);
            gl->BindFramebuffer(GL_DRAW_FRAMEBUFFER,*framebuffer); gl->DrawBuffer(GL_COLOR_ATTACHMENT0);
            gl->BlitFramebuffer(0,0,(GLint)saved->width,(GLint)saved->height,0,0,(GLint)saved->width,(GLint)saved->height,
                GL_DEPTH_BUFFER_BIT|(saved->stencil.size?GL_STENCIL_BUFFER_BIT:0),GL_NEAREST);
            ok=gl_check(renderer,"Preparing retained private GPU depth/stencil values",error);
        }
        /* Pixels belong to saved; only the separate temporary objects retire. */
        source.color=(qa_buffer){0}; source.depth=(qa_buffer){0}; source.stencil=(qa_buffer){0};
        gl_surface_free(renderer,&source);
        if (!ok) return false;
    }
    return gl_check(renderer,"Preparing retained private GPU target color",error);
}
static bool gl_saved_guard_current(const qa_gl_restore_guard *guard,qa_error *error)
{
    if (!guard || guard->transferred || !guard->active || !guard->candidate || !guard->saved ||
        guard->active->closed || guard->active->detached || guard->active->executing || guard->active->capturing ||
        guard->active->preparing || guard->active->controls.ticket || guard->active->controls.image_ticket || guard->active->surface_ticket ||
        guard->active->opacity.active || guard->active->controls.source.entered ||
        guard->candidate->closed || guard->candidate->controls.ticket || guard->candidate->controls.image_ticket || guard->candidate->controls.source.entered ||
        !guard->candidate->detached || guard->candidate->restore!=guard->saved ||
        !gl_caps_equal(&guard->active->capabilities,&guard->candidate->capabilities))
        return gl_save_error(error,QA_ERROR_ARGUMENT,"GPU publication lost its actual completed renderer owners");
    uint32_t width=0,height=0,candidate_width=0,candidate_height=0;
    for (gl_saved_mesh *row=guard->saved->meshes;row;row=row->next)
        if (row->active!=qa_scene_geometry_active(row->entry->geometry))
            return gl_save_error(error,QA_ERROR_ARGUMENT,"GPU receiver geometry lifetime changed before publication");
    return gl_dimensions(guard->active,&width,&height,error) &&
        gl_dimensions(guard->candidate,&candidate_width,&candidate_height,error) &&
        ((width==guard->saved->width && height==guard->saved->height && candidate_width==width && candidate_height==height) ||
        gl_save_error(error,QA_ERROR_ARGUMENT,"GPU publication drawable changed after detached restore"));
}
bool qa_gl_handoff_prepare(qa_gl_restore_guard *guard,qa_error *error)
{
    if (!gl_saved_guard_current(guard,error)) return false;
    if (guard->prepared) return true;
    if (guard->attempted) return gl_save_error(error,QA_ERROR_ARGUMENT,"Failed GPU preparation must retire through its enclosing candidate owner");
    qa_gl_renderer *renderer=guard->candidate; gl_restore_storage *saved=guard->saved;
    if (!guard->fresh && renderer->capabilities.stencil_bits && (renderer->capabilities.stencil_bits!=8 ||
        (!renderer->capabilities.floating_depth && renderer->capabilities.depth_bits!=24)))
        return gl_save_error(error,QA_ERROR_UNSUPPORTED,"Native GPU depth/stencil visual requires an exact upload format before publication");
    if (!guard->fresh && (renderer->capabilities.color_bits>24 || renderer->capabilities.alpha_bits>8))
        return gl_save_error(error,QA_ERROR_UNSUPPORTED,"Native GPU color visual exceeds this exact RGBA8 continuation transfer");
    if (!qa_display_make_current(renderer->options.display,error)) return false;
    guard->attempted=true; renderer->gl=guard->active->gl;
    gl_native_cut cut={0}; gl_cut_read(renderer,&cut); renderer->preparing=true;
    gl_tight_pixels(renderer); renderer->gl.Disable(GL_SCISSOR_TEST);
    bool ok=gl_programs_create(renderer,error) && gl_resources_create(renderer,error);
    if (ok && guard->fresh) ok=gl_output_gamma_prepare(renderer,renderer->gamma,false,error);
    for (gl_saved_texture *row=saved->textures;ok && row;row=row->next) {
        ok=gl_saved_level_upload(renderer,&row->entry->name,row->levels,row->count,error);
        if (ok) {
            for (size_t i=0;i<6;++i) renderer->gl.TexParameteri(GL_TEXTURE_2D,gl_texture_parameters[i],row->parameters[i]);
            qa_scene_vec4 color=row->entry->source_admitted?row->entry->source_texture.border:row->entry->image->border;
            GLfloat border[4]={color.x,color.y,color.z,color.w};
            renderer->gl.TexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_BORDER_COLOR,border);
            ok=gl_check(renderer,"Preparing actual retained GPU image sampler",error);
        }
    }
    if (ok && saved->zero_texture.count) {
        ok=gl_saved_level_upload(renderer,&saved->zero_prepared,saved->zero_texture.levels,saved->zero_texture.count,error);
        if (ok) for (size_t i=0;i<6;++i)
            renderer->gl.TexParameteri(GL_TEXTURE_2D,gl_texture_parameters[i],saved->zero_texture.parameters[i]);
    }
    for (gl_saved_mesh *row=saved->meshes;ok && row;row=row->next)
        ok=gl_saved_buffer_upload(renderer,GL_ARRAY_BUFFER,&row->entry->vertex_buffer,row->vertices.size,row->vertices.data,GL_STATIC_DRAW,error) &&
            gl_saved_buffer_upload(renderer,GL_ELEMENT_ARRAY_BUFFER,&row->entry->index_buffer,row->indices.size,row->indices.data,GL_STATIC_DRAW,error);
    if (ok && saved->stream_vertices) ok=gl_saved_buffer_upload(renderer,GL_ARRAY_BUFFER,&renderer->stream.vertex_buffer,
        renderer->stream.vertex_bytes,NULL,GL_STREAM_DRAW,error);
    if (ok && saved->stream_indices) ok=gl_saved_buffer_upload(renderer,GL_ELEMENT_ARRAY_BUFFER,&renderer->stream.index_buffer,
        renderer->stream.index_bytes,NULL,GL_STREAM_DRAW,error);
    for (size_t i=0;ok && i<4;++i) if (saved->native[i].width)
        ok=gl_saved_surface_upload(renderer,saved->native+i,true,error);
    for (size_t i=0;ok && i<2;++i) if (saved->presented[i].width) {
        ok=gl_saved_level_upload(renderer,renderer->presented_target.color+i,saved->presented+i,1,error);
        renderer->presented_target.width=saved->width; renderer->presented_target.height=saved->height;
    }
    if (ok && saved->output_allocated) {
        renderer->gl.GenFramebuffers(1,&renderer->output.framebuffer);
        renderer->gl.GenRenderbuffers(1,&renderer->output.depth_stencil);
        ok=renderer->output.framebuffer && renderer->output.depth_stencil;
        bool first=true;
        for (size_t i=0;ok && i<4;++i) if (renderer->output.color_ready[i]) {
            ok=gl_saved_target_upload(renderer,saved->output+i,&renderer->output.framebuffer,
                renderer->output.color+i,&renderer->output.depth_stencil,first,error); first=false;
        }
        if (ok && saved->gamma.width) ok=gl_saved_level_upload(renderer,&renderer->output.table,&saved->gamma,1,error);
    }
    if (ok && saved->opacity_allocated) {
        /* Row 1 owns the actual depth allocation; row 0 is color-only. */
        ok=gl_saved_target_upload(renderer,saved->opacity+1,renderer->opacity.framebuffer+1,
            renderer->opacity.color+1,&renderer->opacity.depth_stencil,true,error) &&
            gl_saved_target_upload(renderer,saved->opacity,renderer->opacity.framebuffer,
                renderer->opacity.color,NULL,false,error);
        renderer->opacity.allocated=ok;
    }
    if (ok && saved->target_allocated) {
        renderer->gl.GenFramebuffers(1,&renderer->target_framebuffer); ok=renderer->target_framebuffer!=0;
        if (ok && renderer->target) {
            gl_texture_entry *entry=renderer->textures;
            while (entry && entry->image!=renderer->target) entry=entry->next;
            if (!entry) ok=gl_save_error(error,QA_ERROR_FORMAT,"Actual restored GPU depth target has no retained image cache row");
            else {
                renderer->gl.BindFramebuffer(GL_FRAMEBUFFER,renderer->target_framebuffer);
                renderer->gl.FramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,entry->name,0);
                renderer->gl.ReadBuffer(GL_NONE); renderer->gl.DrawBuffer(GL_NONE);
                ok=renderer->gl.CheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
            }
        }
    }
    if (ok && saved->fog_allocated) { renderer->gl.GenTextures(1,&renderer->fog_depth); ok=renderer->fog_depth!=0; }
    if (ok) ok=gl_check(renderer,"Preparing complete detached native GPU owner",error);
    gl_cut_restore(renderer,&cut); renderer->preparing=false;
    if (ok) ok=gl_check(renderer,"Restoring active native GPU cut after detached preparation",error);
    guard->prepared=ok;
    if (!ok && (!error || error->code==QA_OK)) gl_save_error(error,QA_ERROR_IO,"Preparing actual native GPU continuation failed");
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
    gl->ActiveTexture(GL_TEXTURE0); gl->BindTexture(GL_TEXTURE_2D,0);
    gl_tight_pixels(renderer);
    uint32_t old_zero_levels=guard->active->controls.zero_texture.count;
    for (size_t i=0;i<saved->zero_texture.count;++i) {
        const gl_saved_level *level=saved->zero_texture.levels+i;
        gl->TexImage2D(GL_TEXTURE_2D,(GLint)i,level->internal,(GLsizei)level->width,(GLsizei)level->height,0,
            level->depth?GL_DEPTH_COMPONENT:GL_RGBA,level->depth?GL_FLOAT:GL_UNSIGNED_BYTE,level->pixels.data);
    }
    for (size_t i=saved->zero_texture.count;i<old_zero_levels;++i)
        gl->TexImage2D(GL_TEXTURE_2D,(GLint)i,GL_RGBA8,0,0,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    for (size_t i=0;i<6;++i) gl->TexParameteri(GL_TEXTURE_2D,gl_texture_parameters[i],saved->zero_texture.parameters[i]);
    gl->Disable(GL_SCISSOR_TEST);
    for (size_t i=0;i<4;++i) if (saved->native[i].width) {
        gl->BindFramebuffer(GL_READ_FRAMEBUFFER,saved->native[i].framebuffer); gl->ReadBuffer(GL_COLOR_ATTACHMENT0);
        gl->BindFramebuffer(GL_DRAW_FRAMEBUFFER,0); gl->DrawBuffer(gl_native_buffer(renderer->capabilities.stereo,i));
        gl->BlitFramebuffer(0,0,(GLint)saved->width,(GLint)saved->height,0,0,(GLint)saved->width,(GLint)saved->height,
            GL_COLOR_BUFFER_BIT|(i==2?GL_DEPTH_BUFFER_BIT|(saved->native[i].stencil.size?GL_STENCIL_BUFFER_BIT:0):0),GL_NEAREST);
    }
    gl->BindFramebuffer(GL_FRAMEBUFFER,0);
    gl->ReadBuffer(gl_draw_buffer_name(renderer->draw_buffer)); gl->DrawBuffer(gl_draw_buffer_name(renderer->draw_buffer));
    uint32_t height=saved->height;
    if (renderer->target) {
        gl->BindFramebuffer(GL_FRAMEBUFFER,renderer->target_framebuffer);
        gl->ReadBuffer(GL_NONE); gl->DrawBuffer(GL_NONE);
        height=renderer->target->levels[0].height;
    } else if (renderer->output.enabled && renderer->output.color_ready[gl_draw_buffer_index(renderer->draw_buffer)]) {
        gl->BindFramebuffer(GL_FRAMEBUFFER,renderer->output.framebuffer);
        gl->FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,
            renderer->output.color[gl_draw_buffer_index(renderer->draw_buffer)],0);
        gl->ReadBuffer(GL_COLOR_ATTACHMENT0); gl->DrawBuffer(GL_COLOR_ATTACHMENT0);
    }
    GLint bottom=(GLint)((int64_t)height-renderer->view.viewport.y-renderer->view.viewport.height);
    gl->Viewport(renderer->view.viewport.x,bottom,(GLsizei)renderer->view.viewport.width,(GLsizei)renderer->view.viewport.height);
    gl->Scissor(renderer->view.viewport.x,bottom,(GLsizei)renderer->view.viewport.width,(GLsizei)renderer->view.viewport.height);
    gl->Enable(GL_SCISSOR_TEST);
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
    gl->BindFramebuffer(GL_FRAMEBUFFER,0);
    GLint samples=0;
    gl->GetIntegerv(GL_SAMPLES,&samples);
    gl->GetIntegerv(GL_READ_BUFFER,&snapshot->native_read);
    gl->GetIntegerv(GL_DRAW_BUFFER,&snapshot->native_draw);
    gl_cut_restore(renderer,&snapshot->cut);
    snapshot->cut_valid=true;
    if (!gl_check(renderer,"Restoring bindings before presentation capture",error)) return false;
    if (samples)
        return gl_save_error(error,QA_ERROR_UNSUPPORTED,"Native multisample presentation cannot be retained as exact single-sample surfaces");
    snapshot->captured=gl_gpu_capture(renderer,saved,false,error);
    bool ok=snapshot->captured;
    for (size_t i=0;ok && i<4;++i) if (saved->native[i].width) {
        ok=gl_saved_surface_upload(renderer,saved->native+i,true,error);
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
    gl->BindFramebuffer(GL_FRAMEBUFFER,0);
    gl->ReadBuffer((GLenum)snapshot->native_read); gl->DrawBuffer((GLenum)snapshot->native_draw);
    gl_cut_restore(renderer,&snapshot->cut);
    gl->DepthMask(snapshot->depth_mask?GL_TRUE:GL_FALSE);
    gl->ColorMask(snapshot->color_mask[0]?GL_TRUE:GL_FALSE,snapshot->color_mask[1]?GL_TRUE:GL_FALSE,
        snapshot->color_mask[2]?GL_TRUE:GL_FALSE,snapshot->color_mask[3]?GL_TRUE:GL_FALSE);
    gl->PolygonMode(GL_FRONT,(GLenum)snapshot->polygon[0]);
    gl->PolygonMode(GL_BACK,(GLenum)snapshot->polygon[1]);
    for (size_t i=0;i<6;++i) {
        if (snapshot->enabled[i]) gl->Enable(gl_presentation_enables[i]);
        else gl->Disable(gl_presentation_enables[i]);
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
        if (!j) gl->DeleteFramebuffers(1,names[j]); else gl->DeleteTextures(1,names[j]);
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
    gl_tight_pixels(renderer); gl->Disable(GL_SCISSOR_TEST);
    for (size_t i=0;i<4;++i) if (saved->native[i].width) {
        gl_saved_surface *surface=saved->native+i;
        if (!snapshot->uploaded[i]) {
            /* A rejected upload can retain partial objects. Retire those with
             * proof before retrying; the captured CPU samples remain owned. */
            if (!gl_presentation_surface_delete(renderer,surface,error) ||
                !gl_saved_surface_upload(renderer,surface,true,error)) return false;
            snapshot->uploaded[i]=true;
        }
        if (!surface->color_texture || (i==2 && !surface->depth_texture))
            return gl_save_error(error,QA_ERROR_ARGUMENT,"Native presentation upload is incomplete");
        gl->BindFramebuffer(GL_READ_FRAMEBUFFER,surface->framebuffer); gl->ReadBuffer(GL_COLOR_ATTACHMENT0);
        gl->BindFramebuffer(GL_DRAW_FRAMEBUFFER,0); gl->DrawBuffer(gl_native_buffer(renderer->capabilities.stereo,i));
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
