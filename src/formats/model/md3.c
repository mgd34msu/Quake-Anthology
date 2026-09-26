/* MD3 v15; source semantics from Anthology's q3-model/md3.ts.
 * Copyright (C) 1999-2005 Id Software, Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "internal.h"
#include <ctype.h>

static void normalize_name(char name[65]) {
    for (size_t i = 0; name[i]; ++i)
        if (name[i] >= 'A' && name[i] <= 'Z')
            name[i] = (char)(name[i] + ('a' - 'A'));
    size_t length = strlen(name);
    if (length > 2 && name[length - 2] == '_')
        name[length - 2] = '\0';
}
static float table_sine(unsigned index) {
    float degrees = (float)((index & 1023u) * 360.0 / 1023.0);
    return (float)sin((double)degrees * 3.14159265358979323846 / 180.0);
}
bool model_md3(model_reader *r, qa_model *m) {
    r->pos = 4;
    if (model_i32(r) != 15)
        return model_fail(r, "unsupported MD3 version");
    model_string(r, m->name, 64);
    m->flags = model_i32(r);
    m->frame_count = model_count(r, 1, 1024);
    m->tag_count = model_count(r, 0, 16);
    m->mesh_count = model_count(r, 0, 32);
    m->declared_skin_count =
        model_count(r, 0, INT32_MAX); /* Header metadata only: MD3 skins live externally. */
    uint32_t frames = model_count(r, 108, INT32_MAX), tags = model_count(r, 108, INT32_MAX),
             surfaces = model_count(r, 108, INT32_MAX), end = model_count(r, 108, INT32_MAX);
    if (!model_range(r, frames, m->frame_count, 56, 108, end) ||
        !model_range(r, tags, (size_t)m->frame_count * m->tag_count, 112, 108, end) ||
        !model_range(r, surfaces, m->mesh_count, 108, 108, end))
        return false;
    m->frames = model_alloc(r, m->frame_count, sizeof(*m->frames));
    m->tags = model_alloc(r, (size_t)m->frame_count * m->tag_count, sizeof(*m->tags));
    m->meshes = model_alloc(r, m->mesh_count, sizeof(*m->meshes));
    if (!r->ok)
        return false;
    r->pos = frames;
    for (uint32_t i = 0; i < m->frame_count; ++i) {
        qa_model_frame *f = &m->frames[i];
        model_vec(r, f->bounds.min, 3);
        model_vec(r, f->bounds.max, 3);
        model_vec(r, f->origin, 3);
        f->radius = model_float(r);
        model_string(r, f->name, 16);
        model_bounds_union(&m->bounds, &f->bounds);
    }
    r->pos = tags;
    for (size_t i = 0; i < (size_t)m->frame_count * m->tag_count; ++i) {
        model_string(r, m->tags[i].name, 64);
        model_vec(r, m->tags[i].origin, 3);
        for (unsigned j = 0; j < 3; ++j)
            model_vec(r, m->tags[i].axes[j], 3);
    }
    size_t base = surfaces;
    for (uint32_t i = 0; i < m->mesh_count; ++i) {
        if (!model_range(r, base, 1, 108, 108, end))
            return false;
        r->pos = base;
        const uint8_t *magic = model_take(r, 4);
        if (!magic || memcmp(magic, "IDP3", 4))
            return model_fail(r, "invalid MD3 surface magic");
        qa_model_mesh *s = &m->meshes[i];
        model_string(r, s->name, 64);
        normalize_name(s->name);
        s->flags = model_i32(r);
        s->frame_count = model_count(r, 0, 1024);
        s->shader_count = model_count(r, 0, 256);
        s->vertex_count = model_count(r, 0, 4096);
        s->triangle_count = model_count(r, 0, 8192);
        s->texcoord_count = s->vertex_count;
        uint32_t triangles = model_count(r, 108, INT32_MAX),
                 shaders = model_count(r, 108, INT32_MAX), coords = model_count(r, 108, INT32_MAX),
                 vertices = model_count(r, 108, INT32_MAX), length = model_count(r, 108, INT32_MAX);
        if (s->frame_count != m->frame_count || !model_range(r, base, length, 1, 108, end))
            return model_fail(r, "invalid MD3 surface frame count or end");
        if (!model_range(r, triangles, s->triangle_count, 12, 108, length) ||
            !model_range(r, shaders, s->shader_count, 68, 108, length) ||
            !model_range(r, coords, s->vertex_count, 8, 108, length) ||
            !model_range(r, vertices, (size_t)s->frame_count * s->vertex_count, 8, 108, length))
            return false;
        s->triangles = model_alloc(r, s->triangle_count, sizeof(*s->triangles));
        s->shaders = model_alloc(r, s->shader_count, sizeof(*s->shaders));
        s->texcoords = model_alloc(r, s->vertex_count, sizeof(*s->texcoords));
        s->vertices =
            model_alloc(r, (size_t)s->frame_count * s->vertex_count, sizeof(*s->vertices));
        if (!r->ok)
            return false;
        r->pos = base + triangles;
        for (uint32_t j = 0; j < s->triangle_count; ++j) {
            if (!s->vertex_count)
                return model_fail(r, "MD3 triangles without vertices");
            for (unsigned k = 0; k < 3; ++k)
                s->triangles[j].texcoord[k] = s->triangles[j].vertex[k] =
                    model_count(r, 0, s->vertex_count - 1);
        }
        r->pos = base + shaders;
        for (uint32_t j = 0; j < s->shader_count; ++j) {
            model_string(r, s->shaders[j].name, 64);
            s->shaders[j].index = model_i32(r);
        }
        r->pos = base + coords;
        for (uint32_t j = 0; j < s->vertex_count; ++j)
            model_vec(r, s->texcoords[j].uv, 2);
        r->pos = base + vertices;
        for (size_t j = 0; j < (size_t)s->frame_count * s->vertex_count; ++j) {
            const uint8_t *p = model_take(r, 8);
            if (!p)
                return false;
            for (unsigned k = 0; k < 3; ++k)
                s->vertices[j].position[k] = (float)qa_load_i16le(p + k * 2) / 64.0f;
            uint16_t packed = qa_load_u16le(p + 6);
            unsigned latitude = (packed >> 8) * 4u, longitude = (packed & 255u) * 4u;
            s->vertices[j].normal[0] = table_sine(latitude + 256) * table_sine(longitude);
            s->vertices[j].normal[1] = table_sine(latitude) * table_sine(longitude);
            s->vertices[j].normal[2] = table_sine(longitude + 256);
        }
        base += length;
    }
    if (base != end)
        return model_fail(r, "MD3 surface chain does not reach model end");
    return r->ok;
}
