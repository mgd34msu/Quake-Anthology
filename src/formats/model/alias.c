/* MDL, MD2 and sprite formats adapted from Anthology's q12-model readers.
 * Copyright (C) 1996-2001 Id Software, Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "internal.h"

static bool group_header(model_reader *r, qa_model_group *g) {
    uint32_t type = model_count(r, 0, 1);
    g->count = type ? model_count(r, 1, INT32_MAX) : 1;
    return r->ok;
}
static bool group_intervals(model_reader *r, qa_model_group *g, bool grouped) {
    if (!grouped)
        return r->ok;
    if (!model_range(r, r->pos, g->count, 4, 0, r->bytes.size))
        return false;
    g->intervals = model_alloc(r, g->count, sizeof(float));
    if (!r->ok)
        return false;
    for (uint32_t i = 0; i < g->count; ++i) {
        g->intervals[i] = model_float(r);
        if (g->intervals[i] <= 0)
            return model_fail(r, "group interval must be positive");
    }
    return r->ok;
}
static bool packed_bounds(model_reader *r, const qa_model *m, qa_model_bounds *out) {
    const uint8_t *p = model_take(r, 8);
    if (!p)
        return false;
    model_bounds_clear(out);
    for (unsigned point = 0; point < 2; ++point) {
        float v[3];
        for (unsigned k = 0; k < 3; ++k)
            v[k] = (float)p[point * 4 + k] * m->scale[k] + m->translation[k];
        model_bounds_add(out, v);
    }
    return true;
}
static bool alias_vertices(model_reader *r, qa_model_vertex *out, uint32_t count,
                           const float scale[3], const float translate[3],
                           qa_model_bounds *bounds) {
    const uint8_t *p = model_take(r, (size_t)count * 4);
    if (!p)
        return false;
    for (uint32_t i = 0; i < count; ++i, p += 4) {
        if (p[3] >= 162)
            return model_fail(r, "alias normal index exceeds table");
        for (unsigned k = 0; k < 3; ++k) {
            out[i].position[k] = (float)p[k] * scale[k] + translate[k];
            if (!isfinite(out[i].position[k]))
                return model_fail(r, "alias vertex overflows");
        }
        model_alias_normal(p[3], out[i].normal);
        if (bounds)
            model_bounds_add(bounds, out[i].position);
    }
    return true;
}
bool model_mdl(model_reader *r, qa_model *m) {
    r->pos = 4;
    if (model_i32(r) != 6)
        return model_fail(r, "unsupported MDL version");
    model_vec(r, m->scale, 3);
    model_vec(r, m->translation, 3);
    m->radius = model_float(r);
    model_vec(r, m->eye_position, 3);
    m->skin_group_count = model_count(r, 1, INT32_MAX);
    m->skin_width = model_count(r, 1, INT32_MAX);
    m->skin_height = model_count(r, 1, INT32_MAX);
    uint32_t vertices = model_count(r, 1, INT32_MAX), triangles = model_count(r, 1, INT32_MAX);
    m->frame_group_count = model_count(r, 1, INT32_MAX);
    m->sync = (int32_t)model_count(r, 0, 1);
    m->flags = model_i32(r);
    m->size = model_float(r);
    if (m->radius < 0)
        return model_fail(r, "negative model radius");
    if (!model_range(r, r->pos, m->skin_width, m->skin_height, 0, r->bytes.size))
        return false;
    size_t pixels = (size_t)m->skin_width * m->skin_height;
    if (!model_range(r, r->pos, m->skin_group_count, pixels + 4, 0, r->bytes.size))
        return false;
    m->skin_groups = model_alloc(r, m->skin_group_count, sizeof(*m->skin_groups));
    if (!r->ok)
        return false;
    for (uint32_t i = 0; i < m->skin_group_count; ++i) {
        qa_model_group *g = &m->skin_groups[i];
        size_t start = r->pos;
        if (!group_header(r, g) ||
            !group_intervals(r, g, qa_load_i32le(r->bytes.data + start) != 0) ||
            !model_range(r, r->pos, g->count, pixels, 0, r->bytes.size))
            return false;
        if (g->count > UINT32_MAX - m->skin_count)
            return model_fail(r, "too many skins");
        g->first = m->skin_count;
        m->skins =
            model_grow(r, m->skins, m->skin_count, m->skin_count + g->count, sizeof(*m->skins));
        if (!r->ok)
            return false;
        m->skin_count += g->count;
        for (uint32_t j = 0; j < g->count; ++j)
            m->skins[g->first + j].pixels = (qa_bytes){model_take(r, pixels), pixels};
    }
    if (!model_range(r, r->pos, vertices, 12, 0, r->bytes.size))
        return false;
    m->mesh_count = 1;
    m->meshes = model_alloc(r, 1, sizeof(*m->meshes));
    if (!r->ok)
        return false;
    qa_model_mesh *s = &m->meshes[0];
    s->vertex_count = s->texcoord_count = vertices;
    s->triangle_count = triangles;
    s->texcoords = model_alloc(r, vertices, sizeof(*s->texcoords));
    if (!r->ok)
        return false;
    for (uint32_t i = 0; i < vertices; ++i) {
        qa_model_texcoord *t = &s->texcoords[i];
        t->on_seam = model_i32(r) != 0;
        t->s = model_i32(r);
        t->t = model_i32(r);
        t->uv[0] = ((float)t->s + 0.5f) / (float)m->skin_width;
        t->uv[1] = ((float)t->t + 0.5f) / (float)m->skin_height;
    }
    if (!model_range(r, r->pos, triangles, 16, 0, r->bytes.size))
        return false;
    s->triangles = model_alloc(r, triangles, sizeof(*s->triangles));
    if (!r->ok)
        return false;
    for (uint32_t i = 0; i < triangles; ++i) {
        s->triangles[i].front = model_i32(r) != 0;
        for (unsigned k = 0; k < 3; ++k)
            s->triangles[i].texcoord[k] = s->triangles[i].vertex[k] =
                model_count(r, 0, vertices - 1);
    }
    if (!model_range(r, r->pos, m->frame_group_count, 28 + (size_t)vertices * 4, 0, r->bytes.size))
        return false;
    m->frame_groups = model_alloc(r, m->frame_group_count, sizeof(*m->frame_groups));
    if (!r->ok)
        return false;
    for (uint32_t i = 0; i < m->frame_group_count; ++i) {
        qa_model_group *g = &m->frame_groups[i];
        size_t start = r->pos;
        if (!group_header(r, g))
            return false;
        bool grouped = qa_load_i32le(r->bytes.data + start) != 0;
        if (grouped && (!packed_bounds(r, m, &g->bounds) || !group_intervals(r, g, true)))
            return false;
        if (!model_range(r, r->pos, g->count, 24 + (size_t)vertices * 4, 0, r->bytes.size) ||
            g->count > UINT32_MAX - m->frame_count)
            return model_fail(r, "invalid alias frame group size");
        g->first = m->frame_count;
        m->frames =
            model_grow(r, m->frames, m->frame_count, m->frame_count + g->count, sizeof(*m->frames));
        s->vertices =
            model_grow(r, s->vertices, (size_t)m->frame_count * vertices,
                       (size_t)(m->frame_count + g->count) * vertices, sizeof(*s->vertices));
        if (!r->ok)
            return false;
        m->frame_count += g->count;
        s->frame_count = m->frame_count;
        for (uint32_t j = 0; j < g->count; ++j) {
            qa_model_frame *f = &m->frames[g->first + j];
            if (!packed_bounds(r, m, &f->bounds))
                return false;
            model_string(r, f->name, 16);
            memcpy(f->scale, m->scale, sizeof(f->scale));
            memcpy(f->translation, m->translation, sizeof(f->translation));
            if (!alias_vertices(r, s->vertices + (size_t)(g->first + j) * vertices, vertices,
                                m->scale, m->translation, NULL))
                return false;
        }
        if (!grouped)
            g->bounds = m->frames[g->first].bounds;
        model_bounds_union(&m->bounds, &g->bounds);
    }
    return r->ok;
}
bool model_md2(model_reader *r, qa_model *m) {
    r->pos = 4;
    if (model_i32(r) != 8)
        return model_fail(r, "unsupported MD2 version");
    m->skin_width = model_count(r, 1, INT32_MAX);
    m->skin_height = model_count(r, 1, INT32_MAX);
    uint32_t stride = model_count(r, 1, INT32_MAX);
    m->skin_count = model_count(r, 0, INT32_MAX);
    uint32_t vertices = model_count(r, 1, INT32_MAX), coords = model_count(r, 1, INT32_MAX),
             triangles = model_count(r, 1, INT32_MAX);
    m->gl_command_count = model_count(r, 0, INT32_MAX);
    m->frame_count = model_count(r, 1, INT32_MAX);
    uint32_t skin = model_count(r, 68, INT32_MAX), uv = model_count(r, 68, INT32_MAX),
             tri = model_count(r, 68, INT32_MAX);
    uint32_t frames = model_count(r, 68, INT32_MAX), commands = model_count(r, 68, INT32_MAX),
             end = model_count(r, 68, INT32_MAX);
    if (stride < 40 + (uint64_t)vertices * 4 || !model_range(r, skin, m->skin_count, 64, 68, end) ||
        !model_range(r, uv, coords, 4, 68, end) || !model_range(r, tri, triangles, 12, 68, end) ||
        !model_range(r, frames, m->frame_count, stride, 68, end) ||
        !model_range(r, commands, m->gl_command_count, 4, 68, end))
        return model_fail(r, "invalid MD2 sections or frame size");
    m->mesh_count = 1;
    m->meshes = model_alloc(r, 1, sizeof(*m->meshes));
    m->skins = model_alloc(r, m->skin_count, sizeof(*m->skins));
    m->frames = model_alloc(r, m->frame_count, sizeof(*m->frames));
    m->gl_commands = model_alloc(r, m->gl_command_count, sizeof(*m->gl_commands));
    if (!r->ok)
        return false;
    qa_model_mesh *s = m->meshes;
    s->vertex_count = vertices;
    s->texcoord_count = coords;
    s->triangle_count = triangles;
    s->frame_count = m->frame_count;
    s->vertices = model_alloc(r, (size_t)vertices * m->frame_count, sizeof(*s->vertices));
    s->texcoords = model_alloc(r, coords, sizeof(*s->texcoords));
    s->triangles = model_alloc(r, triangles, sizeof(*s->triangles));
    if (!r->ok)
        return false;
    r->pos = skin;
    for (uint32_t i = 0; i < m->skin_count; ++i)
        model_string(r, m->skins[i].name, 64);
    r->pos = uv;
    for (uint32_t i = 0; i < coords; ++i) {
        const uint8_t *p = model_take(r, 4);
        if (!p)
            return false;
        qa_model_texcoord *t = &s->texcoords[i];
        t->s = qa_load_i16le(p);
        t->t = qa_load_i16le(p + 2);
        t->uv[0] = ((float)t->s + 0.5f) / (float)m->skin_width;
        t->uv[1] = ((float)t->t + 0.5f) / (float)m->skin_height;
    }
    r->pos = tri;
    for (uint32_t i = 0; i < triangles; ++i) {
        const uint8_t *p = model_take(r, 12);
        if (!p)
            return false;
        for (unsigned j = 0; j < 3; ++j) {
            uint32_t v = qa_load_u16le(p + j * 2), t = qa_load_u16le(p + 6 + j * 2);
            if (v >= vertices || t >= coords)
                return model_fail(r, "MD2 triangle index exceeds table");
            s->triangles[i].vertex[j] = v;
            s->triangles[i].texcoord[j] = t;
        }
    }
    for (uint32_t i = 0; i < m->frame_count; ++i) {
        r->pos = frames + (size_t)i * stride;
        qa_model_frame *f = &m->frames[i];
        model_vec(r, f->scale, 3);
        model_vec(r, f->translation, 3);
        model_string(r, f->name, 16);
        model_bounds_clear(&f->bounds);
        if (!alias_vertices(r, s->vertices + (size_t)i * vertices, vertices, f->scale,
                            f->translation, &f->bounds))
            return false;
        model_bounds_union(&m->bounds, &f->bounds);
    }
    r->pos = commands;
    for (uint32_t i = 0; i < m->gl_command_count; ++i)
        m->gl_commands[i] = model_i32(r);
    uint32_t word = 0;
    bool terminated = m->gl_command_count == 0;
    while (word < m->gl_command_count) {
        int32_t n = m->gl_commands[word++];
        if (!n) {
            terminated = true;
            break;
        }
        uint32_t count = (uint32_t)(n < 0 ? -(int64_t)n : n);
        if (count < 3 || count > (m->gl_command_count - word) / 3)
            return model_fail(r, "invalid MD2 GL command span");
        for (uint32_t i = 0; i < count; ++i) {
            float u, v;
            memcpy(&u, &m->gl_commands[word], 4);
            memcpy(&v, &m->gl_commands[word + 1], 4);
            int32_t index = m->gl_commands[word + 2];
            if (!isfinite(u) || !isfinite(v) || index < 0 || (uint32_t)index >= vertices)
                return model_fail(r, "invalid MD2 GL command vertex");
            word += 3;
        }
    }
    return terminated ? r->ok : model_fail(r, "MD2 GL commands lack terminator");
}
bool model_sprite(model_reader *r, qa_model *m, bool q2) {
    r->pos = 4;
    if (model_i32(r) != (q2 ? 2 : 1))
        return model_fail(r, "unsupported sprite version");
    if (!q2) {
        m->orientation = (int32_t)model_count(r, 0, 4);
        m->radius = model_float(r);
        m->skin_width = model_count(r, 1, INT32_MAX);
        m->skin_height = model_count(r, 1, INT32_MAX);
    }
    m->frame_group_count = model_count(r, 1, INT32_MAX);
    if (!q2) {
        m->beam_length = model_float(r);
        m->sync = (int32_t)model_count(r, 0, 1);
        if (m->radius < 0)
            return model_fail(r, "negative sprite radius");
    }
    if (!model_range(r, r->pos, m->frame_group_count, q2 ? 80 : 21, 0, r->bytes.size))
        return false;
    m->frame_groups = model_alloc(r, m->frame_group_count, sizeof(*m->frame_groups));
    if (!r->ok)
        return false;
    for (uint32_t i = 0; i < m->frame_group_count; ++i) {
        qa_model_group *g = &m->frame_groups[i];
        g->count = 1;
        if (!q2) {
            size_t start = r->pos;
            if (!group_header(r, g) ||
                !group_intervals(r, g, qa_load_i32le(r->bytes.data + start) != 0))
                return false;
        }
        if (!model_range(r, r->pos, g->count, q2 ? 80 : 17, 0, r->bytes.size) ||
            g->count > UINT32_MAX - m->sprite_count)
            return model_fail(r, "invalid sprite group size");
        g->first = m->sprite_count;
        m->sprites = model_grow(r, m->sprites, m->sprite_count, m->sprite_count + g->count,
                                sizeof(*m->sprites));
        if (!r->ok)
            return false;
        m->sprite_count += g->count;
        for (uint32_t j = 0; j < g->count; ++j) {
            qa_model_sprite *s = &m->sprites[g->first + j];
            if (!q2) {
                s->origin_x = model_i32(r);
                s->origin_y = model_i32(r);
            }
            s->width = model_count(r, 1, INT32_MAX);
            s->height = model_count(r, 1, INT32_MAX);
            if (q2) {
                s->origin_x = model_i32(r);
                s->origin_y = model_i32(r);
                model_string(r, s->image, 64);
                double x = fmax(fabs((double)s->origin_x), fabs((double)s->width - s->origin_x)),
                       y = fmax(fabs((double)s->origin_y), fabs((double)s->height - s->origin_y));
                float radius = (float)hypot(x, y);
                float minimum[3] = {-radius, -radius, -radius},
                      maximum[3] = {radius, radius, radius};
                model_bounds_add(&m->bounds, minimum);
                model_bounds_add(&m->bounds, maximum);
            } else {
                if (!model_range(r, r->pos, s->width, s->height, 0, r->bytes.size))
                    return false;
                s->pixels.size = (size_t)s->width * s->height;
                s->pixels.data = model_take(r, s->pixels.size);
            }
        }
    }
    if (!q2) {
        for (unsigned i = 0; i < 3; ++i) {
            m->bounds.max[i] = (float)(i == 2 ? m->skin_height : m->skin_width) * 0.5f;
            m->bounds.min[i] = -m->bounds.max[i];
        }
    }
    return r->ok;
}
