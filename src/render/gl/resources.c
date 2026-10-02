#include "internal.h"

#include <limits.h>
#include <stdio.h>

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
        !finite4(image->border)) {
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
bool qa_gl_source_texture_filter_apply(qa_render_controls *controls,qa_error *error)
{
    if (!qa_gl_source_scratch_current(controls) || controls->ticket || controls->source.entered) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Source filter lost its real idle OpenGL image owner");
        return false;
    }
    qa_gl_renderer *renderer=controls->owner.gl;
    if (!qa_display_make_current(renderer->options.display,error)) return false;
    renderer->gl.ActiveTexture(GL_TEXTURE0);
    for (gl_texture_entry *entry=renderer->textures;entry;entry=entry->next)
        if (entry->image->source_q3 && entry->image->source_mipmap) {
            renderer->gl.BindTexture(GL_TEXTURE_2D,entry->name);
            texture_filter(renderer,controls->source_filter);
            if (!gl_check(renderer,"Source texture-mode image",error)) return false;
        }
    return true;
}

static bool texture_upload(qa_gl_renderer *renderer,
                           const qa_scene_image *image, GLuint *out,
                           qa_error *error)
{
    if (!image_valid(renderer, image, error)) return false;
    gl_api *gl = &renderer->gl;
    GLuint texture = 0;
    gl->GenTextures(1, &texture);
    if (texture == 0) {
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
                         image->kind == QA_SCENE_RGB8 ? GL_RGB8 : GL_RGBA8;
        GLenum format = image->kind == QA_SCENE_DEPTH32F ? GL_DEPTH_COMPONENT :
                                                           GL_RGBA;
        GLenum type = image->kind == QA_SCENE_DEPTH32F ? GL_FLOAT :
                                                         GL_UNSIGNED_BYTE;
        gl->TexImage2D(GL_TEXTURE_2D, (GLint)i, internal, (GLsizei)level->width,
                       (GLsizei)level->height, 0, format, type, level->pixels);
    }
    texture_parameters(renderer, image);
    if (!gl_check(renderer, "OpenGL texture upload", error)) {
        gl->DeleteTextures(1, &texture);
        return false;
    }
    *out = texture;
    return true;
}

bool gl_texture_get(qa_gl_renderer *renderer, const qa_scene_image *image,
                    gl_texture_entry **out, qa_error *error)
{
    if (renderer == NULL || image == NULL || out == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "Invalid OpenGL texture lookup");
        return false;
    }
    for (gl_texture_entry *entry = renderer->textures; entry;
         entry = entry->next)
        if (entry->image->identity == image->identity &&
            entry->image->revision == image->revision) {
            *out = entry;
            return true;
        }
    GLuint name;
    if (!texture_upload(renderer, image, &name, error)) return false;
    gl_texture_entry *entry = calloc(1, sizeof(*entry));
    if (entry == NULL) {
        renderer->gl.DeleteTextures(1, &name);
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "Allocating OpenGL texture registry entry");
        return false;
    }
    qa_scene_image_retain(image);
    entry->image = image;
    entry->name = name;
    entry->next = renderer->textures;
    renderer->textures = entry;
    *out = entry;
    return true;
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
        if (!current && entry->image->references == 1) {
            *link = entry->next;
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

static bool mesh_storage(qa_gl_renderer *renderer, const qa_scene_mesh *mesh,
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
    for (gl_mesh_entry *entry = renderer->meshes; entry; entry = entry->next)
        if (entry->identity == mesh->identity && entry->revision == mesh->revision) {
            if (entry->geometry != mesh->geometry ||
                entry->vertex_count != mesh->vertex_count ||
                entry->index_count != mesh->index_count) {
                qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                             "OpenGL retained mesh identity changed storage");
                return false;
            }
            *vertices = entry->vertex_buffer;
            *indices = entry->index_buffer;
            return true;
        }
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
        renderer->gl.DeleteBuffers(1, &entry->vertex_buffer);
        renderer->gl.DeleteBuffers(1, &entry->index_buffer);
        qa_scene_geometry_cache_release(entry->geometry);
        free(entry);
    }
}

bool gl_mesh_bind(qa_gl_renderer *renderer, const qa_scene_mesh *mesh,
                  qa_error *error)
{
    GLuint vertices, indices;
    if (!mesh_storage(renderer, mesh, &vertices, &indices, error)) return false;
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
                            (const void *)(uintptr_t)offsetof(qa_scene_vertex,
                                                             texcoord));
    gl->VertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride,
                            (const void *)(uintptr_t)offsetof(qa_scene_vertex,
                                                             lightmap));
    gl->VertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride,
                            (const void *)(uintptr_t)offsetof(qa_scene_vertex,
                                                             color));
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
