#include "internal.h"

#include <limits.h>
#include <stdio.h>
#include <SDL_video.h>
#include "qa/display_settings.h"
static gl_texture_entry *source_entry(qa_gl_renderer *,const qa_scene_image *);
static void source_index_admit(qa_gl_renderer *renderer, gl_texture_entry *entry)
{
    render_resource_put(&renderer->source_image_index, 0, 0, entry->image, entry);
    render_resource_put(&renderer->source_image_index, 0, 0, &entry->source_texture.view, entry);
    render_resource_put(&renderer->source_image_index, entry->image->identity, 0,
                        entry->source_owner, entry);
}

static bool finite4(qa_scene_vec4 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z) &&
           isfinite(value.w);
}

bool gl_check(qa_gl_renderer *renderer, const char *operation, qa_error *error)
{
    GLenum code = renderer->gl.GetError();
    if (code == GL_NO_ERROR) return true;
    qa_error_set(error, QA_ERROR_IO, 0, "%s failed with OpenGL error 0x%x",
                 operation, (unsigned)code);
    while (renderer->gl.GetError() != GL_NO_ERROR) {}
    return false;
}

GLenum gl_draw_buffer_name(qa_scene_draw_buffer buffer)
{
    switch (buffer) {
    case QA_DRAW_FRONT: return GL_FRONT;
    case QA_DRAW_BACK: return GL_BACK;
    case QA_DRAW_BACK_LEFT: return GL_BACK_LEFT;
    case QA_DRAW_BACK_RIGHT: return GL_BACK_RIGHT;
    }
    return GL_BACK;
}

unsigned gl_draw_buffer_index(qa_scene_draw_buffer buffer)
{
    switch (buffer) {
    case QA_DRAW_FRONT: return 0;
    case QA_DRAW_BACK: return 1;
    case QA_DRAW_BACK_LEFT: return 2;
    case QA_DRAW_BACK_RIGHT: return 3;
    }
    return 1;
}

bool gl_dimensions(qa_gl_renderer *renderer, uint32_t *width, uint32_t *height,
                   qa_error *error)
{
    qa_display_info info;
    if (renderer == NULL || width == NULL || height == NULL ||
        !qa_display_info_get(renderer == NULL ? NULL : renderer->options.display,
                             &info, error)) return false;
    if (info.backend != QA_DISPLAY_OPENGL || info.drawable_width == 0 ||
        info.drawable_height == 0 || info.drawable_width > INT_MAX ||
        info.drawable_height > INT_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "OpenGL renderer requires a valid GL drawable");
        return false;
    }
    *width = info.drawable_width;
    *height = info.drawable_height;
    return true;
}

static bool image_valid(const qa_gl_renderer *renderer,
                        const qa_scene_image *image, qa_error *error)
{
    if (image == NULL || image->levels == NULL || image->level_count == 0 ||
        image->level_count > (size_t)INT_MAX ||
        (unsigned)image->kind > QA_SCENE_DEPTH32F ||
        (unsigned)image->wrap > QA_SCENE_CLAMP ||
        (unsigned)image->filter > QA_SCENE_LINEAR_MIPMAP_LINEAR ||
        !finite4(image->border) || (image->source_q3 &&
            ((unsigned)image->source_format>QA_Q3_TEXTURE_RGB4_S3TC ||
             (image->source_format==QA_Q3_TEXTURE_RGB4_S3TC && !renderer->capabilities.s3tc)))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "Invalid OpenGL texture descriptor");
        return false;
    }
    uint32_t expected_width = image->levels[0].width;
    uint32_t expected_height = image->levels[0].height;
    for (size_t i = 0; i < image->level_count; ++i) {
        const qa_scene_image_level *level = &image->levels[i];
        if (level->width == 0 || level->height == 0 ||
            level->width > renderer->capabilities.maximum_texture_size ||
            level->height > renderer->capabilities.maximum_texture_size ||
            level->width != expected_width || level->height != expected_height ||
            (size_t)level->width > SIZE_MAX / level->height ||
            (size_t)level->width * level->height > SIZE_MAX / 4 ||
            level->pixels == NULL ||
            level->bytes < (size_t)level->width * level->height * 4) {
            qa_error_set(error, QA_ERROR_ARGUMENT, i,
                         "Invalid OpenGL texture level storage");
            return false;
        }
        if (image->kind == QA_SCENE_DEPTH32F) {
            size_t count = (size_t)level->width * level->height;
            const float *pixels = level->pixels;
            for (size_t sample = 0; sample < count; ++sample)
                if (!isfinite(pixels[sample]) || pixels[sample] < 0 ||
                    pixels[sample] > 1) {
                    qa_error_set(error, QA_ERROR_ARGUMENT, sample,
                                 "OpenGL depth texture sample is outside 0..1");
                    return false;
                }
        }
        expected_width = expected_width > 1 ? expected_width / 2 : 1;
        expected_height = expected_height > 1 ? expected_height / 2 : 1;
        if (i + 1 < image->level_count && level->width == 1 &&
            level->height == 1) {
            qa_error_set(error, QA_ERROR_ARGUMENT, i + 1,
                         "OpenGL texture has levels beyond 1 by 1");
            return false;
        }
    }
    return true;
}
static GLint source_internal_format(qa_q3_texture_format format)
{
    switch (format) {
    case QA_Q3_TEXTURE_RGB: return 3;
    case QA_Q3_TEXTURE_RGBA: return 4;
    case QA_Q3_TEXTURE_RGB5: return GL_RGB5;
    case QA_Q3_TEXTURE_RGBA4: return GL_RGBA4;
    case QA_Q3_TEXTURE_RGB8: return GL_RGB8;
    case QA_Q3_TEXTURE_RGBA8: return GL_RGBA8;
    case QA_Q3_TEXTURE_RGB4_S3TC: return 0x83a1;
    }
    return GL_RGBA8;
}

static void texture_filter(qa_gl_renderer *renderer,qa_scene_filter filter)
{
    static const GLenum minimum[] = {
        GL_NEAREST, GL_LINEAR, GL_NEAREST_MIPMAP_NEAREST,
        GL_LINEAR_MIPMAP_NEAREST, GL_NEAREST_MIPMAP_LINEAR,
        GL_LINEAR_MIPMAP_LINEAR
    };
    static const GLenum magnification[] = {
        GL_NEAREST, GL_LINEAR, GL_NEAREST, GL_LINEAR, GL_NEAREST, GL_LINEAR
    };
    gl_api *gl = &renderer->gl;
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                      (GLint)minimum[filter]);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                      (GLint)magnification[filter]);
}
static void texture_parameters(qa_gl_renderer *renderer,
                               const qa_scene_image *image)
{
    gl_api *gl=&renderer->gl;
    texture_filter(renderer,qa_render_controls_image_filter(&renderer->controls,image));
    GLenum wrap = image->wrap == QA_SCENE_REPEAT ? GL_REPEAT :
                  image->kind == QA_SCENE_DEPTH32F ? GL_CLAMP_TO_EDGE : GL_CLAMP;
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)wrap);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)wrap);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,
                      (GLint)(image->level_count - 1));
    GLfloat border[4] = {image->border.x, image->border.y, image->border.z,
                         image->border.w};
    gl->TexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
}
bool qa_gl_source_texture_filter_apply(qa_render_controls *controls,bool no_bind,qa_error *error)
{
    if (qa_gl_render_controls_callback_candidate(controls) && !controls->ticket &&
        !controls->image_ticket && !controls->source.entered && !controls->owner.gl->source_image_count)
        return true;
    if (!qa_gl_source_scratch_current(controls) || controls->ticket || controls->source.entered) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source filter lost its real idle OpenGL image owner");
        return false;
    }
    qa_gl_renderer *renderer=controls->owner.gl;
    if (!qa_display_make_current(renderer->options.display,error)) return false;
    const qa_scene_image *dlight=NULL;
    if (no_bind && !qa_render_controls_source_dlight_read(controls,&dlight,error)) return false;
    for (uint32_t i=0;i<renderer->source_image_count;++i) {
        gl_texture_entry *entry=renderer->source_images[i];
        if (entry->image->source_mipmap) {
            gl_texture_entry *selected=dlight?source_entry(renderer,dlight):entry;
            if (!gl_source_texture_bind(renderer,selected->image,error)) return false;
            texture_filter(renderer,controls->source_filter);
            if (!gl_check(renderer,"Source texture-mode image",error)) return false;
            if (!controls->attributes.actual_empty[controls->attributes.texture_unit]) {
                selected->source_filter=controls->source_filter;
                qa_render_source_texture_filter(&selected->source_texture,controls->source_filter);
            } else qa_render_source_texture_filter(&controls->zero_texture,controls->source_filter);
        }
    }
    return true;
}

static bool texture_upload(qa_gl_renderer *renderer,
                           const qa_scene_image *image, GLuint *out,
                           qa_error *error)
{
    if (!image_valid(renderer, image, error)) return false;
    gl_api *gl = &renderer->gl;
    GLint active=0,binding=0;
    gl->GetIntegerv(GL_ACTIVE_TEXTURE,&active);
    gl->ActiveTexture(GL_TEXTURE0);
    gl->GetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
    GLuint texture = 0;
    gl->GenTextures(1, &texture);
    if (texture == 0) {
        gl->ActiveTexture((GLenum)active);
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "OpenGL could not allocate a texture");
        return false;
    }
    gl->ActiveTexture(GL_TEXTURE0);
    gl->BindTexture(GL_TEXTURE_2D, texture);
    gl->BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    gl->PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl->PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    gl->PixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    gl->PixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    for (size_t i = 0; i < image->level_count; ++i) {
        const qa_scene_image_level *level = &image->levels[i];
        GLint internal = image->kind == QA_SCENE_DEPTH32F ? GL_DEPTH_COMPONENT32F :
                         image->source_q3 ? source_internal_format(image->source_format) :
                         image->kind == QA_SCENE_RGB8 ? GL_RGB8 : GL_RGBA8;
        GLenum format = image->kind == QA_SCENE_DEPTH32F ? GL_DEPTH_COMPONENT :
                                                           GL_RGBA;
        GLenum type = image->kind == QA_SCENE_DEPTH32F ? GL_FLOAT :
                                                         GL_UNSIGNED_BYTE;
        gl->TexImage2D(GL_TEXTURE_2D, (GLint)i, internal, (GLsizei)level->width,
                       (GLsizei)level->height, 0, format, type, level->pixels);
    }
    texture_parameters(renderer, image);
    bool ok=gl_check(renderer, "OpenGL texture upload", error);
    gl->BindTexture(GL_TEXTURE_2D,(GLuint)binding);
    gl->ActiveTexture((GLenum)active);
    if (!ok) {
        gl->DeleteTextures(1, &texture);
        return false;
    }
    *out = texture;
    return true;
}
bool gl_source_texture_bind(qa_gl_renderer *renderer,const qa_scene_image *image,qa_error *error)
{
    gl_texture_entry *object=source_entry(renderer,image);
    if (object && object->source_admitted) image=object->image;
    if (object && object->source_admitted)
        renderer->controls.image_used[object->source_ordinal] = true;
    uint32_t unit=renderer->controls.attributes.texture_unit;
    renderer->gl.ActiveTexture(GL_TEXTURE0+unit);
    if (renderer->bound[unit]==image) return gl_check(renderer,"Source cached texture binding",error);
    gl_texture_entry *texture=NULL;
    if (image && !gl_texture_get(renderer,image,&texture,error)) return false;
    renderer->gl.BindTexture(GL_TEXTURE_2D,texture?texture->name:0);
    qa_scene_image_retain(image); qa_scene_image_release(renderer->bound[unit]); renderer->bound[unit]=image;
    renderer->controls.attributes.actual_empty[unit]=image==NULL;
    return gl_check(renderer,"Source texture binding",error);
}
static gl_texture_entry *source_entry(qa_gl_renderer *renderer,const qa_scene_image *image)
{
    if (!image) return NULL;
    gl_texture_entry *entry = render_resource_get(&renderer->source_image_index, 0, 0, image);
    if (entry) return entry;
    qa_scene_resources *owner=image && image->source_q3?qa_scene_image_resource_owner(image):NULL;
    if (owner) {
        entry = render_resource_get(&renderer->source_image_index, image->identity, 0, owner);
        if (entry) return entry;
    }
    entry = gl_texture_resident(renderer, image);
    return entry && entry->image == image ? entry : NULL;
}
size_t qa_gl_source_texture_metadata_count(const qa_render_controls *controls)
{
    size_t count=controls->zero_texture.count;
    const qa_gl_renderer *renderer=controls->owner.gl;
    for (uint32_t i=0;i<renderer->source_image_count;++i) count+=renderer->source_images[i]->source_texture.count;
    return count;
}
const qa_scene_image *qa_gl_source_texture_metadata_at(const qa_render_controls *controls,size_t ordinal)
{
    if (ordinal<controls->zero_texture.count) return controls->zero_texture.images[ordinal];
    ordinal-=controls->zero_texture.count;
    const qa_gl_renderer *renderer=controls->owner.gl;
    for (uint32_t i=0;i<renderer->source_image_count;++i) {
        const qa_render_source_texture *texture=&renderer->source_images[i]->source_texture;
        if (ordinal<texture->count) return texture->images[ordinal];
        ordinal-=texture->count;
    }
    return NULL;
}
bool qa_gl_source_texture_upload(qa_render_controls *controls,const qa_scene_image *slot,
    const qa_scene_image *image,const qa_scene_image *binding,bool redefine,bool dirty,qa_error *error)
{
    qa_gl_renderer *renderer=controls->owner.gl;
    gl_texture_entry *registered=source_entry(renderer,slot),*selected=source_entry(renderer,binding);
    qa_scene_resources *owner=qa_scene_image_resource_owner(image);
    if (!qa_gl_source_scratch_current(controls) || controls->ticket || controls->image_ticket ||
        !registered || !registered->source_admitted || registered->image!=slot ||
        !selected || !selected->source_admitted || !image || !owner || owner!=registered->source_owner ||
        image->identity!=slot->identity || (image->kind!=QA_SCENE_RGBA8 && image->kind!=QA_SCENE_RGB8) || image->level_count!=1 ||
        !image_valid(renderer,image,error) || !qa_display_make_current(renderer->options.display,error)) {
        if (!error || error->code==QA_OK) qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source cinematic upload lost its actual GL scratch object");
        return false;
    }
    if (!gl_source_texture_bind(renderer,selected->image,error)) return false;
    if (!dirty && !redefine) return true;
    uint32_t unit=controls->attributes.texture_unit;
    qa_render_source_texture *texture=controls->attributes.actual_empty[unit]?&controls->zero_texture:&selected->source_texture;
    const qa_scene_image_level *level=image->levels;
    if (redefine) {
        if (!qa_render_source_texture_level(texture,0,image,owner,QA_SCENE_RGB8,QA_Q3_TEXTURE_RGB8,error)) return false;
    } else {
        bool updated=false;
        if (!qa_render_source_texture_subimage(texture,image,&updated,error)) return false;
        if (!updated) return true;
    }
    gl_api *gl=&renderer->gl;
    gl->BindBuffer(GL_PIXEL_UNPACK_BUFFER,0); gl->PixelStorei(GL_UNPACK_ALIGNMENT,1);
    gl->PixelStorei(GL_UNPACK_ROW_LENGTH,0); gl->PixelStorei(GL_UNPACK_SKIP_ROWS,0); gl->PixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    if (redefine) {
        gl->TexImage2D(GL_TEXTURE_2D,0,GL_RGB8,(GLsizei)level->width,(GLsizei)level->height,0,GL_RGBA,GL_UNSIGNED_BYTE,level->pixels);
        qa_render_source_texture_filter(texture,QA_SCENE_LINEAR); texture->wrap=QA_SCENE_CLAMP;
        texture_filter(renderer,QA_SCENE_LINEAR);
        gl->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP); gl->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);
    } else gl->TexSubImage2D(GL_TEXTURE_2D,0,0,0,(GLsizei)level->width,(GLsizei)level->height,GL_RGBA,GL_UNSIGNED_BYTE,level->pixels);
    if (texture==&selected->source_texture) selected->source_filter=texture->filter;
    return gl_check(renderer,"Reached Source cinematic upload",error);
}
const qa_scene_image *qa_gl_source_texture_image(qa_render_controls *controls,uint32_t unit,const qa_scene_image *image)
{
    if (controls->attributes.actual_empty[unit]) return qa_render_source_texture_view(&controls->zero_texture);
    gl_texture_entry *entry=source_entry(controls->owner.gl,image);
    return entry && entry->source_admitted?qa_render_source_texture_view(&entry->source_texture):image;
}
static bool source_upload_bound(qa_gl_renderer *renderer,const qa_scene_image *image,qa_error *error)
{
    gl_api *gl=&renderer->gl;
    gl->BindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    gl->PixelStorei(GL_UNPACK_ALIGNMENT,1); gl->PixelStorei(GL_UNPACK_ROW_LENGTH,0);
    gl->PixelStorei(GL_UNPACK_SKIP_ROWS,0); gl->PixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    for (size_t i=0;i<image->level_count;++i) {
        const qa_scene_image_level *level=image->levels+i;
        GLint internal=image->kind==QA_SCENE_DEPTH32F?GL_DEPTH_COMPONENT32F:
            source_internal_format(image->source_format);
        gl->TexImage2D(GL_TEXTURE_2D,(GLint)i,internal,(GLsizei)level->width,(GLsizei)level->height,0,
            image->kind==QA_SCENE_DEPTH32F?GL_DEPTH_COMPONENT:GL_RGBA,
            image->kind==QA_SCENE_DEPTH32F?GL_FLOAT:GL_UNSIGNED_BYTE,level->pixels);
    }
    texture_filter(renderer,image->source_mipmap?renderer->controls.source_filter:image->filter);
    gl->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,image->wrap==QA_SCENE_REPEAT?GL_REPEAT:GL_CLAMP);
    gl->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,image->wrap==QA_SCENE_REPEAT?GL_REPEAT:GL_CLAMP);
    return gl_check(renderer,"Source upload into actual selected texture object",error);
}
static bool source_storage_apply_bound(qa_gl_renderer *renderer,const qa_render_source_texture *texture,
    uint32_t previous_levels,qa_error *error)
{
    gl_api *gl=&renderer->gl;
    gl->BindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    gl->PixelStorei(GL_UNPACK_ALIGNMENT,1); gl->PixelStorei(GL_UNPACK_ROW_LENGTH,0);
    gl->PixelStorei(GL_UNPACK_SKIP_ROWS,0); gl->PixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    for (uint32_t i=0;i<texture->count;++i) {
        const qa_scene_image_level *level=texture->levels+i;
        qa_scene_image_kind kind=texture->kinds[i];
        GLint internal=kind==QA_SCENE_DEPTH32F?GL_DEPTH_COMPONENT32F:source_internal_format(texture->formats[i]);
        gl->TexImage2D(GL_TEXTURE_2D,(GLint)i,internal,(GLsizei)level->width,(GLsizei)level->height,0,
            kind==QA_SCENE_DEPTH32F?GL_DEPTH_COMPONENT:GL_RGBA,
            kind==QA_SCENE_DEPTH32F?GL_FLOAT:GL_UNSIGNED_BYTE,level->pixels);
    }
    for (uint32_t i=texture->count;i<previous_levels;++i)
        gl->TexImage2D(GL_TEXTURE_2D,(GLint)i,GL_RGBA8,0,0,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    texture_filter(renderer,texture->filter);
    gl->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,texture->magnification_linear?GL_LINEAR:GL_NEAREST);
    GLenum wrap=texture->wrap==QA_SCENE_REPEAT?GL_REPEAT:GL_CLAMP;
    gl->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,(GLint)wrap);
    gl->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,(GLint)wrap);
    gl->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,1000);
    GLfloat border[4]={texture->border.x,texture->border.y,texture->border.z,texture->border.w};
    gl->TexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_BORDER_COLOR,border);
    return gl_check(renderer,"Preparing actual Source texture object levels and sampler",error);
}
bool qa_gl_source_image_admit(qa_render_controls *controls,const qa_scene_image *image,const qa_scene_image *binding,
    uint32_t unit,qa_error *error)
{
    if (!qa_gl_source_scratch_current(controls) || controls->ticket || !image || !image->source_q3 || unit>1) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source image admission lost its actual OpenGL recipient"); return false;
    }
    qa_gl_renderer *renderer=controls->owner.gl;
    if (!qa_display_make_current(renderer->options.display,error)) return false;
    gl_texture_entry *entry = gl_texture_resident(renderer, image);
    if (entry && entry->image != image) entry = NULL;
    if (entry && entry->source_admitted) return true;
    if (renderer->source_image_count>=GL_SOURCE_IMAGES_QA) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"MAX_DRAWIMAGES hit in actual Source image admission"); return false;
    }
    if (!image_valid(renderer,image,error)) return false;
    if (!render_resource_reserve(&renderer->source_image_index,
                                 renderer->source_image_index.count + 3, error) ||
        (!entry && !render_resource_reserve(&renderer->texture_index,
                                            renderer->texture_index.count + 1, error))) return false;
    gl_texture_entry *selected=binding==image?NULL:source_entry(renderer,binding);
    if (binding!=image && (!selected || !selected->source_admitted)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source no-bind upload lost its genuinely admitted GL target"); return false;
    }
    qa_scene_resources *owner=qa_scene_image_resource_owner(image);
    if (!owner || !qa_scene_resources_retain(owner,error)) {
        if (!error || error->code==QA_OK) qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source image lost its actual bank owner");
        return false;
    }
    if (!entry) {
        entry=calloc(1,sizeof(*entry));
        if (!entry) { qa_scene_resources_destroy(owner); qa_error_set(error,QA_ERROR_MEMORY,0,"Allocating Source texture object"); return false; }
        renderer->gl.GenTextures(1,&entry->name);
        if (!entry->name) { free(entry); qa_scene_resources_destroy(owner); qa_error_set(error,QA_ERROR_MEMORY,0,"Creating Source texture object"); return false; }
        qa_scene_image_retain(image); entry->image=image;
        qa_render_source_texture_init(&entry->source_texture);
        entry->next=renderer->textures; renderer->textures=entry;
        render_resource_put(&renderer->texture_index, image->identity, image->revision, NULL, entry);
    } else if (!qa_render_source_texture_upload(&entry->source_texture,image,owner,
        image->source_mipmap?controls->source_filter:image->filter,error)) {
        qa_scene_resources_destroy(owner); return false;
    }
    entry->source_owner=owner; entry->source_ordinal=renderer->source_image_count;
    entry->source_admitted=true; renderer->source_images[renderer->source_image_count++]=entry;
    source_index_admit(renderer, entry);
    controls->attributes.texture_unit=unit;
    renderer->gl.ActiveTexture(GL_TEXTURE0+unit); renderer->gl.ClientActiveTexture(GL_TEXTURE0+unit);
    if (!gl_source_texture_bind(renderer,binding,error)) return false;
    if (!selected) selected=entry;
    qa_render_source_texture *texture=controls->attributes.actual_empty[unit]?&controls->zero_texture:&selected->source_texture;
    qa_scene_filter filter=image->source_mipmap?controls->source_filter:image->filter;
    if (!qa_render_source_texture_upload(texture,image,owner,filter,error) || !source_upload_bound(renderer,image,error)) return false;
    if (texture==&selected->source_texture) selected->source_filter=filter;
    renderer->gl.BindTexture(GL_TEXTURE_2D,0);
    controls->attributes.actual_empty[unit]=true;
    if (unit==1) {
        renderer->gl.ActiveTexture(GL_TEXTURE0); renderer->gl.ClientActiveTexture(GL_TEXTURE0);
        controls->attributes.texture_unit=0;
    }
    return gl_check(renderer,"Source completed image admission",error);
}
bool qa_gl_source_texture_border(qa_render_controls *controls,qa_scene_vec4 color,qa_error *error)
{
    qa_gl_renderer *renderer=controls->owner.gl;
    if (!qa_display_make_current(renderer->options.display,error)) return false;
    GLfloat values[4]={color.x,color.y,color.z,color.w};
    renderer->gl.ActiveTexture(GL_TEXTURE0+controls->attributes.texture_unit);
    renderer->gl.TexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_BORDER_COLOR,values);
    controls->attributes.zero_border=color;
    controls->zero_texture.border=color;
    return gl_check(renderer,"Source post-upload raw-object border",error);
}

typedef struct gl_source_prepared_image {
    const qa_scene_resource_policy *policy;
    const qa_scene_image *image;
    qa_scene_resources *source,*destination;
    gl_texture_entry *entry;
    uint64_t creation;
    size_t policy_ordinal;
    uint32_t unit;
} gl_source_prepared_image;
typedef struct gl_source_prepared_bank {
    const qa_scene_resource_policy *policy;
    size_t count;
} gl_source_prepared_bank;
typedef struct gl_source_prepared_update {
    gl_texture_entry *original;
    qa_render_source_texture texture;
    GLuint name;
} gl_source_prepared_update;
struct qa_gl_source_images_ticket {
    qa_gl_renderer *renderer;
    gl_source_prepared_image *rows;
    size_t count;
    gl_source_prepared_bank *banks;
    size_t bank_count;
    gl_texture_entry *original_textures;
    const qa_scene_image *original_bound[2];
    qa_render_source_attributes original_attributes;
    qa_scene_filter original_filter;
    uint32_t original_count;
    gl_source_prepared_update *updates;
    size_t update_count;
    qa_render_source_texture zero_texture;
    const qa_scene_image *final_bound[2];
    qa_render_source_attributes final_attributes;
    const void *context;
    bool prepared,published,restart,zero_prepared;
};
static int source_prepared_order(const void *a,const void *b)
{
    const gl_source_prepared_image *first=a,*second=b;
    return first->creation<second->creation?-1:first->creation>second->creation;
}
bool qa_gl_source_images_prepare(qa_render_controls *controls,qa_scene_resource_policy *const *banks,size_t count,
    bool no_bind,const qa_render_source_restart_values *restart_values,
    qa_gl_source_images_ticket **out,qa_error *error)
{
    bool restart=restart_values!=NULL;
    qa_scene_filter filter=restart?restart_values->filter:controls->source_filter;
    qa_gl_renderer *renderer=controls->owner.gl;
    qa_display_endpoint endpoint;
    if (!out || *out || !qa_gl_render_controls_current(controls) || controls->source.entered ||
        !qa_display_endpoint_read(renderer->options.display,&endpoint) || !endpoint.context ||
        SDL_GL_GetCurrentContext()!=endpoint.context) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Prepared Source uploads require their actual retained native context"); return false;
    }
    qa_gl_source_images_ticket *ticket=calloc(1,sizeof(*ticket));
    if (!ticket) { qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining prepared native Source images"); return false; }
    ticket->renderer=renderer; ticket->context=endpoint.context; ticket->original_textures=renderer->textures;
    ticket->original_bound[0]=renderer->bound[0]; ticket->original_bound[1]=renderer->bound[1];
    ticket->original_attributes=controls->attributes; ticket->original_count=renderer->source_image_count;
    ticket->original_filter=controls->source_filter;
    ticket->restart=restart;
    *out=ticket;
    ticket->final_bound[0]=restart?NULL:renderer->bound[0]; ticket->final_bound[1]=restart?NULL:renderer->bound[1];
    if (restart) {
        qa_render_source_attributes_init(&ticket->final_attributes);
        qa_render_source_texture_init(&ticket->zero_texture);
    } else {
        ticket->final_attributes=controls->attributes;
        if (!qa_render_source_texture_clone(&ticket->zero_texture,&controls->zero_texture,error)) return false;
    }
    ticket->banks=count?calloc(count,sizeof(*ticket->banks)):NULL;
    if (count && !ticket->banks) { qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining actual Source upload bank children"); return false; }
    ticket->bank_count=count;
    size_t total=0;
    for (size_t i=0;i<count;++i) {
        size_t images=0;
        if (!qa_scene_resource_policy_source_image_count(banks[i],&images,error)) return false;
        ticket->banks[i]=(gl_source_prepared_bank){banks[i],images};
        if (images>GL_SOURCE_IMAGES_QA-total || total+images>GL_SOURCE_IMAGES_QA-(restart?0:ticket->original_count)) {
            qa_error_set(error,QA_ERROR_ARGUMENT,0,"MAX_DRAWIMAGES hit preparing actual Source uploads"); return false;
        }
        total+=images;
    }
    ticket->rows=total?calloc(total,sizeof(*ticket->rows)):NULL;
    ticket->updates=total?calloc(total,sizeof(*ticket->updates)):NULL;
    if (total && (!ticket->rows || !ticket->updates)) { qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining completed Source upload roster"); return false; }
    for (size_t i=0;i<count;++i) {
        size_t images=0;
        if (!qa_scene_resource_policy_source_image_count(banks[i],&images,error)) return false;
        for (size_t ordinal=0;ordinal<images;++ordinal) {
            gl_source_prepared_image *row=ticket->rows+ticket->count;
            row->policy=banks[i]; row->policy_ordinal=ordinal;
            row->source=qa_scene_resource_policy_source(banks[i]);
            row->destination=qa_scene_resource_policy_destination(banks[i]);
            if (!qa_scene_resource_policy_source_image_at(banks[i],ordinal,&row->image,&row->creation,error) ||
                !row->source || !row->destination || !row->image->source_q3 ||
                row->image->source_texture_unit>1 || qa_scene_image_resource_owner(row->image)!=row->destination) return false;
            row->unit=row->image->source_texture_unit;
            ++ticket->count;
        }
    }
    if (!render_resource_reserve(&renderer->texture_index,
                                 renderer->texture_index.count + ticket->count, error) ||
        !render_resource_reserve(&renderer->source_image_index,
                                 renderer->source_image_index.count + 3 * ticket->count, error)) return false;
    if (ticket->count>1) qsort(ticket->rows,ticket->count,sizeof(*ticket->rows),source_prepared_order);
    for (size_t i=1;i<ticket->count;++i)
        if (ticket->rows[i-1].creation==ticket->rows[i].creation) {
            qa_error_set(error,QA_ERROR_ARGUMENT,i,"Prepared Source upload roster duplicates an actual constructor"); return false;
        }
    GLint unpack_buffer=0,unpack[4]={0};
    const GLenum unpack_names[4]={GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS};
    renderer->gl.GetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpack_buffer);
    for (size_t i=0;i<4;++i) renderer->gl.GetIntegerv(unpack_names[i],unpack+i);
    bool ok=gl_check(renderer,"Reading actual Source upload preparation state",error);
    GLint active=0,binding=0;
    renderer->gl.GetIntegerv(GL_ACTIVE_TEXTURE,&active); renderer->gl.ActiveTexture(GL_TEXTURE0);
    renderer->gl.GetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
    for (size_t i=0;ok && i<ticket->count;++i) {
        gl_source_prepared_image *row=ticket->rows+i;
        row->entry=calloc(1,sizeof(*row->entry));
        if (!row->entry) { qa_error_set(error,QA_ERROR_MEMORY,i,"Allocating prepared native Source image"); ok=false; break; }
        if (!qa_scene_resources_retain(row->source,error)) { ok=false; break; }
        row->entry->source_owner=row->source;
        qa_scene_image_retain(row->image); row->entry->image=row->image;
        row->entry->source_admitted=true;
        row->entry->source_ordinal=(restart?0:ticket->original_count)+(uint32_t)i;
        row->entry->source_filter=row->image->source_mipmap?filter:row->image->filter;
        qa_render_source_texture_init(&row->entry->source_texture);
        renderer->gl.GenTextures(1,&row->entry->name);
        if (!row->entry->name) { qa_error_set(error,QA_ERROR_MEMORY,i,"Creating prepared Source texture object"); ok=false; break; }
        gl_texture_entry *selected=row->entry;
        bool old=false;
        if (no_bind) {
            for (size_t j=i;j>0;--j)
                if (ticket->rows[j-1].image->source_dlight) {
                    selected=ticket->rows[j-1].entry; break;
                }
            if (selected==row->entry && !restart)
                for (uint32_t j=renderer->source_image_count;j>0;--j)
                    if (renderer->source_images[j-1]->image->source_dlight) {
                        selected=renderer->source_images[j-1]; old=true; break;
                    }
        }
        uint32_t unit=row->unit;
        if (ticket->final_bound[unit]!=selected->image) {
            ticket->final_bound[unit]=selected->image; ticket->final_attributes.actual_empty[unit]=false;
        }
        qa_render_source_texture *texture=&ticket->zero_texture;
        if (!ticket->final_attributes.actual_empty[unit]) {
            texture=&selected->source_texture;
            if (old) {
                size_t j=0;
                for (;j<ticket->update_count;++j) if (ticket->updates[j].original==selected) break;
                if (j==ticket->update_count) {
                    ticket->updates[j].original=selected;
                    if (!qa_render_source_texture_clone(&ticket->updates[j].texture,&selected->source_texture,error)) { ok=false; break; }
                    ++ticket->update_count;
                }
                texture=&ticket->updates[j].texture;
            }
        }
        ok=qa_render_source_texture_upload(texture,row->image,row->source,row->entry->source_filter,error);
        ticket->final_attributes.actual_empty[unit]=true;
        ticket->final_attributes.texture_unit=unit==1?0:unit;
        if (row->image->source_after_upload_border)
            ticket->zero_texture.border=ticket->final_attributes.zero_border=row->image->source_upload_border;
    }
    for (size_t i=0;ok && i<ticket->count;++i) {
        gl_texture_entry *entry=ticket->rows[i].entry;
        renderer->gl.BindTexture(GL_TEXTURE_2D,entry->name);
        ok=source_storage_apply_bound(renderer,&entry->source_texture,0,error);
        entry->source_filter=entry->source_texture.filter;
    }
    for (size_t i=0;ok && i<ticket->update_count;++i) {
        gl_source_prepared_update *update=ticket->updates+i;
        renderer->gl.GenTextures(1,&update->name);
        if (!update->name) { qa_error_set(error,QA_ERROR_MEMORY,i,"Creating prepared replacement Source object"); ok=false; break; }
        renderer->gl.BindTexture(GL_TEXTURE_2D,update->name);
        ok=source_storage_apply_bound(renderer,&update->texture,0,error);
    }
    if (ok) {
        renderer->gl.BindTexture(GL_TEXTURE_2D,0);
        ticket->zero_prepared=true;
        ok=source_storage_apply_bound(renderer,&ticket->zero_texture,controls->zero_texture.count,error);
    }
    renderer->gl.BindTexture(GL_TEXTURE_2D,(GLuint)binding); renderer->gl.ActiveTexture((GLenum)active);
    renderer->gl.BindBuffer(GL_PIXEL_UNPACK_BUFFER,(GLuint)unpack_buffer);
    for (size_t i=0;i<4;++i) renderer->gl.PixelStorei(unpack_names[i],unpack[i]);
    qa_error restored={0};
    if (!gl_check(renderer,"Restoring native state after Source upload preparation",&restored)) {
        if (error) *error=restored;
        ok=false;
    }
    ticket->prepared=ok;
    return ok;
}
bool qa_gl_source_images_ready_is(const qa_gl_source_images_ticket *ticket)
{
    qa_gl_renderer *renderer=ticket?ticket->renderer:NULL;
    qa_display_endpoint endpoint;
    if (!renderer || !ticket->prepared || ticket->published || !qa_gl_render_controls_current(&renderer->controls) ||
        !qa_display_endpoint_read(renderer->options.display,&endpoint) || endpoint.context!=ticket->context ||
        renderer->textures!=ticket->original_textures || renderer->source_image_count!=ticket->original_count ||
        renderer->controls.source_filter!=ticket->original_filter ||
        renderer->bound[0]!=ticket->original_bound[0] || renderer->bound[1]!=ticket->original_bound[1] ||
        memcmp(&renderer->controls.attributes,&ticket->original_attributes,sizeof(ticket->original_attributes))) return false;
    for (size_t i=0;i<ticket->bank_count;++i) {
        size_t count=0;
        if (!qa_scene_resource_policy_source_image_count(ticket->banks[i].policy,&count,NULL) || count!=ticket->banks[i].count)
            return false;
    }
    for (size_t i=0;i<ticket->count;++i) {
        const gl_source_prepared_image *row=ticket->rows+i;
        const qa_scene_image *image=NULL; uint64_t sequence=0;
        if (!row->entry || !row->entry->name ||
            !qa_scene_resource_policy_source_image_at(row->policy,row->policy_ordinal,&image,&sequence,NULL) ||
            image!=row->image || sequence!=row->creation || image->source_texture_unit!=row->unit ||
            qa_scene_image_resource_owner(image)!=row->destination ||
            qa_scene_resource_policy_source(row->policy)!=row->source) return false;
    }
    return true;
}
void qa_gl_source_images_publish(qa_gl_source_images_ticket *ticket)
{
    if (!ticket || !ticket->prepared || ticket->published) return;
    qa_gl_renderer *renderer=ticket->renderer;
    for (uint32_t unit=0;unit<2;++unit) {
        qa_scene_image_retain(ticket->final_bound[unit]); qa_scene_image_release(renderer->bound[unit]);
        renderer->bound[unit]=ticket->final_bound[unit];
    }
    if (ticket->restart) {
        render_resource_clear(&renderer->source_image_index);
        gl_texture_entry **cursor=&renderer->textures;
        while (*cursor) {
            gl_texture_entry *entry=*cursor;
            if (!entry->source_admitted) { cursor=&entry->next; continue; }
            *cursor=entry->next; renderer->gl.DeleteTextures(1,&entry->name);
            qa_render_source_texture_release(&entry->source_texture);
            qa_scene_image_release(entry->image); qa_scene_resources_destroy(entry->source_owner); free(entry);
        }
        memset(renderer->source_images,0,sizeof(renderer->source_images)); renderer->source_image_count=0;
        render_resource_clear(&renderer->texture_index);
        for (gl_texture_entry *entry = renderer->textures; entry; entry = entry->next)
            if (!gl_texture_resident(renderer, entry->image))
                render_resource_put(&renderer->texture_index, entry->image->identity,
                                    entry->image->revision, NULL, entry);
        renderer->pipeline.blend_source=QA_BLEND_ONE;
        renderer->pipeline.blend_destination=QA_BLEND_ZERO;
        renderer->pipeline.depth_test=QA_DEPTH_DISABLED;
        renderer->pipeline.depth_write=true;
        renderer->pipeline.cull=QA_CULL_NONE;
        renderer->controls.source_cull_type=QA_CULL_FRONT;
        renderer->controls.source_cull_valid=true;
        renderer->pipeline.wireframe=false;
        renderer->clear_depth=1;
        renderer->view=(qa_scene_view){.viewport=renderer->view.viewport,.depth=1};
        renderer->preblend_gamma=renderer->source_frame=false;
    }
    for (size_t i=0;i<ticket->update_count;++i) {
        gl_source_prepared_update *update=ticket->updates+i;
        renderer->gl.DeleteTextures(1,&update->original->name); update->original->name=update->name; update->name=0;
        qa_render_source_texture_release(&update->original->source_texture);
        update->original->source_texture=update->texture; update->original->source_filter=update->texture.filter;
        qa_render_source_texture_init(&update->texture);
    }
    for (size_t i=0;i<ticket->count;++i) {
        gl_source_prepared_image *row=ticket->rows+i;
        gl_texture_entry *entry=row->entry;
        entry->next=renderer->textures; renderer->textures=entry;
        renderer->source_images[renderer->source_image_count++]=entry;
        render_resource_put(&renderer->texture_index, entry->image->identity,
                            entry->image->revision, NULL, entry);
        source_index_admit(renderer, entry);
        row->entry=NULL;
    }
    qa_render_source_texture_release(&renderer->controls.zero_texture);
    renderer->controls.zero_texture=ticket->zero_texture; qa_render_source_texture_init(&ticket->zero_texture);
    renderer->controls.attributes=ticket->final_attributes;
    gl_source_pipeline_restore(renderer);
    ticket->published=true;
}
bool qa_gl_source_images_finish(qa_gl_source_images_ticket **out,qa_error *error)
{
    if (!out || !*out) return true;
    qa_gl_source_images_ticket *ticket=*out;
    if (!ticket->published) { qa_error_set(error,QA_ERROR_ARGUMENT,0,"Native Source image finish requires publication"); return false; }
    free(ticket->updates); free(ticket->banks); free(ticket->rows); free(ticket); *out=NULL; return true;
}
bool qa_gl_source_images_abort(qa_gl_source_images_ticket **out,qa_error *error)
{
    if (!out || !*out) return true;
    qa_gl_source_images_ticket *ticket=*out; qa_gl_renderer *renderer=ticket->renderer;
    if (ticket->published || !qa_display_make_current(renderer->options.display,error)) return false;
    if (ticket->zero_prepared) {
        GLint active=0,binding=0,unpack_buffer=0,unpack[4]={0};
        const GLenum names[4]={GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS};
        renderer->gl.GetIntegerv(GL_ACTIVE_TEXTURE,&active); renderer->gl.ActiveTexture(GL_TEXTURE0);
        renderer->gl.GetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
        renderer->gl.GetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpack_buffer);
        for (uint32_t i=0;i<4;++i) renderer->gl.GetIntegerv(names[i],unpack+i);
        renderer->gl.BindTexture(GL_TEXTURE_2D,0);
        bool ok=source_storage_apply_bound(renderer,&renderer->controls.zero_texture,ticket->zero_texture.count,error);
        renderer->gl.BindTexture(GL_TEXTURE_2D,(GLuint)binding); renderer->gl.ActiveTexture((GLenum)active);
        renderer->gl.BindBuffer(GL_PIXEL_UNPACK_BUFFER,(GLuint)unpack_buffer);
        for (uint32_t i=0;i<4;++i) renderer->gl.PixelStorei(names[i],unpack[i]);
        if (!ok || !gl_check(renderer,"Restoring actual Source zero object after rejected preparation",error)) return false;
        ticket->zero_prepared=false;
    }
    for (size_t i=ticket->count;i>0;--i) {
        gl_texture_entry *entry=ticket->rows[i-1].entry;
        if (!entry) continue;
        if (entry->name) {
            if (!gl_check(renderer,"Preparing native Source image abort",error)) return false;
            renderer->gl.DeleteTextures(1,&entry->name);
            if (!gl_check(renderer,"Retiring prepared native Source image",error)) return false;
            entry->name=0;
        }
        qa_render_source_texture_release(&entry->source_texture);
        qa_scene_image_release(entry->image); qa_scene_resources_destroy(entry->source_owner);
        free(entry); ticket->rows[i-1].entry=NULL;
    }
    for (size_t i=0;i<ticket->update_count;++i) {
        gl_source_prepared_update *update=ticket->updates+i;
        if (update->name) {
            renderer->gl.DeleteTextures(1,&update->name);
            if (!gl_check(renderer,"Retiring rejected Source object replacement",error)) return false;
            update->name=0;
        }
        qa_render_source_texture_release(&update->texture);
    }
    qa_render_source_texture_release(&ticket->zero_texture);
    free(ticket->updates); free(ticket->banks); free(ticket->rows); free(ticket); *out=NULL; return true;
}

gl_texture_entry *gl_texture_resident(const qa_gl_renderer *renderer, const qa_scene_image *image)
{
    return image ? render_resource_get(&renderer->texture_index, image->identity,
                                      image->revision, NULL) : NULL;
}
bool gl_texture_get(qa_gl_renderer *renderer, const qa_scene_image *image,
                    gl_texture_entry **out, qa_error *error)
{
    if (renderer == NULL || image == NULL || out == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "Invalid OpenGL texture lookup");
        return false;
    }
    gl_texture_entry *entry = gl_texture_resident(renderer, image);
    if (entry) { *out = entry; return true; }
    if (!render_resource_reserve(&renderer->texture_index,
                                 renderer->texture_index.count + 1, error)) return false;
    GLuint name;
    if (!texture_upload(renderer, image, &name, error)) return false;
    entry = calloc(1, sizeof(*entry));
    if (entry == NULL) {
        renderer->gl.DeleteTextures(1, &name);
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "Allocating OpenGL texture registry entry");
        return false;
    }
    qa_scene_image_retain(image);
    entry->image = image;
    entry->name = name;
    entry->source_filter=image->source_q3 && image->source_mipmap?renderer->controls.source_filter:image->filter;
    qa_render_source_texture_init(&entry->source_texture);
    entry->next = renderer->textures;
    renderer->textures = entry;
    render_resource_put(&renderer->texture_index, image->identity, image->revision, NULL, entry);
    *out = entry;
    return true;
}
qa_scene_filter qa_gl_source_image_filter(const qa_render_controls *controls,const qa_scene_image *image)
{
    const qa_gl_renderer *renderer=controls->owner.gl;
    if (image==&controls->zero_texture.view) return image->filter;
    const gl_texture_entry *entry = render_resource_get(&renderer->source_image_index, 0, 0, image);
    if (entry && image == &entry->source_texture.view) return image->filter;
    if (!entry) entry = gl_texture_resident(renderer, image);
    if (entry && entry->image == image) return entry->source_filter;
    return image->source_mipmap?controls->source_filter:image->filter;
}

static void replace_binding(qa_gl_renderer *renderer, size_t unit,
                            const qa_scene_image *image)
{
    qa_scene_image_retain(image);
    qa_scene_image_release(renderer->bound[unit]);
    renderer->bound[unit] = image;
}

bool gl_image_update(qa_gl_renderer *renderer, const qa_scene_image *image,
                     qa_error *error)
{
    gl_texture_entry *source=image && image->source_q3?source_entry(renderer,image):NULL;
    if (source && source->source_admitted) return true;
    gl_texture_entry *entry;
    if (!gl_texture_get(renderer, image, &entry, error)) return false;
    (void)entry;
    if (renderer->target != NULL &&
        renderer->target->identity == image->identity &&
        renderer->target != image &&
        !gl_select_target(renderer, image, error)) return false;
    for (size_t unit = 0; unit < 2; ++unit)
        if (renderer->bound[unit] != NULL &&
            renderer->bound[unit]->identity == image->identity &&
            renderer->bound[unit] != image)
            replace_binding(renderer, unit, image);
    return true;
}

void gl_textures_prune(qa_gl_renderer *renderer)
{
    gl_texture_entry **link = &renderer->textures;
    while (*link != NULL) {
        gl_texture_entry *entry = *link;
        bool current = renderer->target != NULL &&
                       renderer->target->identity == entry->image->identity &&
                       renderer->target->revision == entry->image->revision;
        for (size_t unit = 0; unit < 2; ++unit)
            current = current ||
                (renderer->bound[unit] != NULL &&
                 renderer->bound[unit]->identity == entry->image->identity &&
                 renderer->bound[unit]->revision == entry->image->revision);
        if (!entry->source_admitted && !current && entry->image->references == 1) {
            *link = entry->next;
            if (gl_texture_resident(renderer, entry->image) == entry)
                render_resource_remove(&renderer->texture_index, entry->image->identity,
                                       entry->image->revision, NULL);
            renderer->gl.DeleteTextures(1, &entry->name);
            qa_scene_image_release(entry->image);
            free(entry);
        } else link = &entry->next;
    }
}

static bool grow_stream(qa_gl_renderer *renderer, GLenum target, GLuint buffer,
                        size_t required, size_t *capacity, qa_error *error)
{
    if (required <= *capacity) return true;
    size_t next = *capacity == 0 ? 65536 : *capacity;
    while (next < required && next <= SIZE_MAX / 2) next *= 2;
    if (next < required) next = required;
    if (next > (size_t)PTRDIFF_MAX) {
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "OpenGL streaming buffer exceeds address space");
        return false;
    }
    renderer->gl.BindBuffer(target, buffer);
    renderer->gl.BufferData(target, (GLsizeiptr)next, NULL, GL_STREAM_DRAW);
    if (!gl_check(renderer, "OpenGL streaming buffer allocation", error))
        return false;
    *capacity = next;
    return true;
}

const gl_mesh_entry *gl_mesh_resident(const qa_gl_renderer *renderer,
                                    const qa_scene_mesh *mesh)
{
    if (mesh->identity == 0) return NULL;
    return render_resource_get(&renderer->mesh_index, mesh->identity, mesh->revision, NULL);
}

static bool mesh_storage(qa_gl_renderer *renderer, const qa_scene_mesh *mesh,
                         const gl_mesh_entry *resident,
                         GLuint *vertices, GLuint *indices, qa_error *error)
{
    if (mesh->vertex_count > SIZE_MAX / sizeof(*mesh->vertices) ||
        mesh->index_count > SIZE_MAX / sizeof(*mesh->indices) ||
        mesh->index_count > INT_MAX) {
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "OpenGL mesh storage exceeds supported size");
        return false;
    }
    size_t vertex_bytes = mesh->vertex_count * sizeof(*mesh->vertices);
    size_t index_bytes = mesh->index_count * sizeof(*mesh->indices);
    if (vertex_bytes > (size_t)PTRDIFF_MAX || index_bytes > (size_t)PTRDIFF_MAX) {
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "OpenGL mesh buffer exceeds address space");
        return false;
    }
    if (mesh->identity == 0) {
        if (renderer->stream.vertex_buffer == 0)
            renderer->gl.GenBuffers(1, &renderer->stream.vertex_buffer);
        if (renderer->stream.index_buffer == 0)
            renderer->gl.GenBuffers(1, &renderer->stream.index_buffer);
        if (renderer->stream.vertex_buffer == 0 ||
            renderer->stream.index_buffer == 0) {
            qa_error_set(error, QA_ERROR_MEMORY, 0,
                         "OpenGL could not allocate streaming geometry");
            return false;
        }
        if (!grow_stream(renderer, GL_ARRAY_BUFFER,
                         renderer->stream.vertex_buffer, vertex_bytes,
                         &renderer->stream.vertex_bytes, error) ||
            !grow_stream(renderer, GL_ELEMENT_ARRAY_BUFFER,
                         renderer->stream.index_buffer, index_bytes,
                         &renderer->stream.index_bytes, error)) return false;
        renderer->gl.BindBuffer(GL_ARRAY_BUFFER,
                                renderer->stream.vertex_buffer);
        if (vertex_bytes != 0)
            renderer->gl.BufferSubData(GL_ARRAY_BUFFER, 0,
                                       (GLsizeiptr)vertex_bytes,
                                       mesh->vertices);
        renderer->gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER,
                                renderer->stream.index_buffer);
        if (index_bytes != 0)
            renderer->gl.BufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0,
                                       (GLsizeiptr)index_bytes,
                                       mesh->indices);
        *vertices = renderer->stream.vertex_buffer;
        *indices = renderer->stream.index_buffer;
        return gl_check(renderer, "OpenGL streaming geometry upload", error);
    }
    if (resident) {
        if (!gl_mesh_storage_matches(resident, mesh)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                         "OpenGL retained mesh identity changed storage");
            return false;
        }
        *vertices = resident->vertex_buffer;
        *indices = resident->index_buffer;
        return true;
    }
    if (!render_resource_reserve(&renderer->mesh_index,
                                 renderer->mesh_index.count + 1, error)) return false;
    gl_mesh_entry *entry = calloc(1, sizeof(*entry));
    if (entry == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "Allocating OpenGL retained mesh entry");
        return false;
    }
    renderer->gl.GenBuffers(1, &entry->vertex_buffer);
    renderer->gl.GenBuffers(1, &entry->index_buffer);
    if (entry->vertex_buffer == 0 || entry->index_buffer == 0) {
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "OpenGL could not allocate retained mesh buffers");
        goto fail;
    }
    renderer->gl.BindBuffer(GL_ARRAY_BUFFER, entry->vertex_buffer);
    renderer->gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vertex_bytes,
                            mesh->vertices, GL_STATIC_DRAW);
    renderer->gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, entry->index_buffer);
    renderer->gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)index_bytes,
                            mesh->indices, GL_STATIC_DRAW);
    if (!gl_check(renderer, "OpenGL retained mesh upload", error)) goto fail;
    entry->identity = mesh->identity;
    entry->revision = mesh->revision;
    entry->geometry = mesh->geometry;
    qa_scene_geometry_cache_retain(entry->geometry);
    entry->vertex_count = mesh->vertex_count;
    entry->index_count = mesh->index_count;
    entry->next = renderer->meshes;
    renderer->meshes = entry;
    render_resource_put(&renderer->mesh_index, entry->identity, entry->revision, NULL, entry);
    *vertices = entry->vertex_buffer;
    *indices = entry->index_buffer;
    return true;
fail:
    if (entry->vertex_buffer != 0)
        renderer->gl.DeleteBuffers(1, &entry->vertex_buffer);
    if (entry->index_buffer != 0)
        renderer->gl.DeleteBuffers(1, &entry->index_buffer);
    free(entry);
    return false;
}

void gl_meshes_prune(qa_gl_renderer *renderer)
{
    gl_mesh_entry **link = &renderer->meshes;
    while (*link != NULL) {
        gl_mesh_entry *entry = *link;
        if (qa_scene_geometry_active(entry->geometry)) {
            link = &entry->next;
            continue;
        }
        *link = entry->next;
        render_resource_remove(&renderer->mesh_index, entry->identity, entry->revision, NULL);
        renderer->gl.DeleteBuffers(1, &entry->vertex_buffer);
        renderer->gl.DeleteBuffers(1, &entry->index_buffer);
        qa_scene_geometry_cache_release(entry->geometry);
        free(entry);
    }
}

bool gl_mesh_bind(qa_gl_renderer *renderer, const qa_scene_mesh *mesh,
                  const gl_mesh_entry *resident, const qa_scene_vertex_inputs *inputs,
                  qa_error *error)
{
    GLuint vertices, indices;
    if (!mesh_storage(renderer, mesh, resident, &vertices, &indices, error)) return false;
    gl_api *gl = &renderer->gl;
    gl->BindBuffer(GL_ARRAY_BUFFER, vertices);
    gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices);
    const GLsizei stride = (GLsizei)sizeof(qa_scene_vertex);
    gl->EnableVertexAttribArray(0);
    gl->EnableVertexAttribArray(1);
    gl->EnableVertexAttribArray(2);
    gl->EnableVertexAttribArray(3);
    gl->EnableVertexAttribArray(4);
    gl->VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                            (const void *)(uintptr_t)offsetof(qa_scene_vertex,
                                                             position));
    gl->VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                            (const void *)(uintptr_t)offsetof(qa_scene_vertex,
                                                             normal));
    gl->VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                            (const void *)(uintptr_t)(inputs->swap_uv
                                ? offsetof(qa_scene_vertex, lightmap)
                                : offsetof(qa_scene_vertex, texcoord)));
    gl->VertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride,
                            (const void *)(uintptr_t)(inputs->swap_uv
                                ? offsetof(qa_scene_vertex, texcoord)
                                : offsetof(qa_scene_vertex, lightmap)));
    gl->VertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride,
                            (const void *)(uintptr_t)offsetof(qa_scene_vertex,
                                                             color));
    if (inputs->constant_color) {
        gl->DisableVertexAttribArray(4);
        gl->VertexAttrib4f(4, inputs->color.x, inputs->color.y, inputs->color.z, inputs->color.w);
    }
    return true;
}

void gl_mesh_unbind(qa_gl_renderer *renderer)
{
    for (GLuint attribute = 0; attribute < 5; ++attribute)
        renderer->gl.DisableVertexAttribArray(attribute);
    renderer->gl.BindBuffer(GL_ARRAY_BUFFER, 0);
    renderer->gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

bool gl_resources_create(qa_gl_renderer *renderer, qa_error *error)
{
    const uint8_t white[4] = {255, 255, 255, 255};
    renderer->gl.GenTextures(1, &renderer->white_texture);
    if (renderer->white_texture == 0) {
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "OpenGL could not allocate its white texture");
        return false;
    }
    renderer->gl.ActiveTexture(GL_TEXTURE0);
    renderer->gl.BindTexture(GL_TEXTURE_2D, renderer->white_texture);
    renderer->gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    renderer->gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA,
                            GL_UNSIGNED_BYTE, white);
    renderer->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    renderer->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    renderer->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                               GL_CLAMP_TO_EDGE);
    renderer->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                               GL_CLAMP_TO_EDGE);
    return gl_check(renderer, "OpenGL renderer texture initialization", error);
}

void gl_resources_destroy(qa_gl_renderer *renderer)
{
    render_resource_destroy(&renderer->texture_index);
    render_resource_destroy(&renderer->source_image_index);
    render_resource_destroy(&renderer->mesh_index);
    qa_scene_image_release(renderer->target);
    renderer->target = NULL;
    for (size_t unit = 0; unit < 2; ++unit) {
        qa_scene_image_release(renderer->bound[unit]);
        renderer->bound[unit] = NULL;
    }
    while (renderer->textures != NULL) {
        gl_texture_entry *entry = renderer->textures;
        renderer->textures = entry->next;
        renderer->gl.DeleteTextures(1, &entry->name);
        qa_scene_image_release(entry->image);
        qa_scene_resources_destroy(entry->source_owner);
        qa_render_source_texture_release(&entry->source_texture);
        free(entry);
    }
    while (renderer->meshes != NULL) {
        gl_mesh_entry *entry = renderer->meshes;
        renderer->meshes = entry->next;
        renderer->gl.DeleteBuffers(1, &entry->vertex_buffer);
        renderer->gl.DeleteBuffers(1, &entry->index_buffer);
        qa_scene_geometry_cache_release(entry->geometry);
        free(entry);
    }
    if (renderer->stream.vertex_buffer != 0)
        renderer->gl.DeleteBuffers(1, &renderer->stream.vertex_buffer);
    if (renderer->stream.index_buffer != 0)
        renderer->gl.DeleteBuffers(1, &renderer->stream.index_buffer);
    if (renderer->white_texture != 0)
        renderer->gl.DeleteTextures(1, &renderer->white_texture);
    memset(&renderer->stream, 0, sizeof(renderer->stream));
    renderer->white_texture = 0;
}

bool qa_gl_resident_images(const qa_gl_renderer *renderer, qa_arena *scratch,
                           const qa_scene_image *const **out, size_t *count, qa_error *error)
{
    if (!renderer || renderer->closed || !scratch || !out || !count) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid GPU image inventory observation"); return false;
    }
    size_t n = 0;
    for (const gl_texture_entry *entry = renderer->textures; entry; entry = entry->next) ++n;
    if (n > SIZE_MAX / sizeof(qa_scene_image *)) { qa_error_set(error, QA_ERROR_MEMORY, 0, "GPU inventory count overflow"); return false; }
    const qa_scene_image **rows = n ? qa_arena_alloc(scratch, n * sizeof(*rows), _Alignof(qa_scene_image *), error) : NULL;
    if (n && !rows) return false;
    size_t at = n;
    for (const gl_texture_entry *entry = renderer->textures; entry; entry = entry->next) rows[--at] = entry->image;
    *out = rows; *count = n; return true;
}
