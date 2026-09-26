/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "internal.h"
#include <float.h>

bool model_fail(model_reader *r, const char *message) {
    if (r->ok)
        qa_error_set(r->error, QA_ERROR_FORMAT, r->pos, "%s", message);
    r->ok = false;
    return false;
}
bool model_range(model_reader *r, size_t offset, size_t count, size_t stride, size_t minimum,
                 size_t end) {
    if (!r->ok)
        return false;
    if (end > r->bytes.size || offset < minimum || offset > end ||
        (stride && count > (end - offset) / stride))
        return model_fail(r, "model section exceeds source bounds");
    return true;
}
void *model_alloc(model_reader *r, size_t count, size_t size) {
    if (!r->ok || count == 0)
        return NULL;
    if (size && count > SIZE_MAX / size) {
        model_fail(r, "model allocation overflows");
        return NULL;
    }
    void *ptr = calloc(count, size);
    if (!ptr) {
        qa_error_set(r->error, QA_ERROR_MEMORY, r->pos, "model allocation failed");
        r->ok = false;
    }
    return ptr;
}
void *model_grow(model_reader *r, void *ptr, size_t old_count, size_t new_count, size_t size) {
    if (!r->ok || new_count == old_count)
        return ptr;
    if (new_count < old_count || (size && new_count > SIZE_MAX / size)) {
        model_fail(r, "model allocation overflows");
        return ptr;
    }
    void *grown = realloc(ptr, new_count * size);
    if (!grown) {
        qa_error_set(r->error, QA_ERROR_MEMORY, r->pos, "model allocation failed");
        r->ok = false;
        return ptr;
    }
    memset((uint8_t *)grown + old_count * size, 0, (new_count - old_count) * size);
    return grown;
}
const uint8_t *model_take(model_reader *r, size_t size) {
    if (!model_range(r, r->pos, size, 1, 0, r->bytes.size))
        return NULL;
    const uint8_t *result = r->bytes.data + r->pos;
    r->pos += size;
    return result;
}
int32_t model_i32(model_reader *r) {
    const uint8_t *p = model_take(r, 4);
    return p ? qa_load_i32le(p) : 0;
}
uint32_t model_count(model_reader *r, uint32_t minimum, uint32_t maximum) {
    int32_t value = model_i32(r);
    if (value < 0 || (uint32_t)value < minimum || (uint32_t)value > maximum)
        model_fail(r, "invalid model record count or index");
    return r->ok ? (uint32_t)value : 0;
}
float model_float(model_reader *r) {
    const uint8_t *p = model_take(r, 4);
    float value = p ? qa_load_f32le(p) : 0;
    if (!isfinite(value))
        model_fail(r, "non-finite model float");
    return value;
}
void model_vec(model_reader *r, float *out, size_t count) {
    for (size_t i = 0; i < count; ++i)
        out[i] = model_float(r);
}
void model_string(model_reader *r, char *out, size_t width) {
    const uint8_t *p = model_take(r, width);
    if (p) {
        memcpy(out, p, width);
        out[width] = '\0';
    }
}
void model_bounds_clear(qa_model_bounds *b) {
    for (unsigned i = 0; i < 3; ++i) {
        b->min[i] = FLT_MAX;
        b->max[i] = -FLT_MAX;
    }
}
void model_bounds_add(qa_model_bounds *b, const float p[3]) {
    for (unsigned i = 0; i < 3; ++i) {
        if (p[i] < b->min[i])
            b->min[i] = p[i];
        if (p[i] > b->max[i])
            b->max[i] = p[i];
    }
}
void model_bounds_union(qa_model_bounds *b, const qa_model_bounds *p) {
    model_bounds_add(b, p->min);
    model_bounds_add(b, p->max);
}
void model_normalize(float p[3]) { model_v3_store(p, qa_vec_normalize(model_v3(p))); }
static void normalize_md3(float p[3]) {
    float square = qa_vec_dot(model_v3(p), model_v3(p));
    uint32_t bits;
    memcpy(&bits, &square, sizeof(bits));
    bits = 0x5f3759dfu - (bits >> 1);
    float inverse;
    memcpy(&inverse, &bits, sizeof(inverse));
    inverse *= 1.5f - (square * 0.5f * inverse * inverse);
    for (unsigned i = 0; i < 3; ++i)
        p[i] *= inverse;
}
qa_bytes qa_model_shader_name(const qa_model_shader *shader) {
    if (!shader)
        return (qa_bytes){0};
    return shader->text_name.data ? shader->text_name
                                  : (qa_bytes){(const uint8_t *)shader->name, strlen(shader->name)};
}
qa_bytes qa_model_bone_name(const qa_model_bone *bone) {
    if (!bone)
        return (qa_bytes){0};
    return bone->text_name.data ? bone->text_name
                                : (qa_bytes){(const uint8_t *)bone->name, strlen(bone->name)};
}

void qa_model_free(qa_model *m) {
    if (!m)
        return;
    if (m->meshes)
        for (uint32_t i = 0; i < m->mesh_count; ++i) {
            qa_model_mesh *s = &m->meshes[i];
            free(s->vertices);
            free(s->texcoords);
            free(s->triangles);
            free(s->shaders);
            free(s->weights);
            free(s->vertex_weights);
            free(s->bone_references);
        }
    if (m->frame_groups)
        for (uint32_t i = 0; i < m->frame_group_count; ++i)
            free(m->frame_groups[i].intervals);
    if (m->skin_groups)
        for (uint32_t i = 0; i < m->skin_group_count; ++i)
            free(m->skin_groups[i].intervals);
    free(m->meshes);
    free(m->frames);
    free(m->frame_groups);
    free(m->skin_groups);
    free(m->skins);
    free(m->tags);
    free(m->sprites);
    free(m->lods);
    free(m->bones);
    free(m->bone_matrices);
    free(m->bind_pose);
    free(m->gl_commands);
    qa_buffer_free(&m->source);
    memset(m, 0, sizeof(*m));
}
bool qa_model_load(qa_bytes bytes, qa_model *out, qa_error *error) {
    if (!out || !bytes.data || bytes.size < 4) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "model data and output are required");
        return false;
    }
    qa_model m = {0};
    model_reader r = {bytes, 0, error, true};
    m.source.data = model_alloc(&r, bytes.size, 1);
    if (!r.ok)
        return false;
    m.source.size = bytes.size;
    memcpy(m.source.data, bytes.data, bytes.size);
    r.bytes.data = m.source.data;
    model_bounds_clear(&m.bounds);
    bool ok;
    if (!memcmp(bytes.data, "IDPO", 4)) {
        m.format = QA_MODEL_MDL;
        ok = model_mdl(&r, &m);
    } else if (!memcmp(bytes.data, "IDP2", 4)) {
        m.format = QA_MODEL_MD2;
        ok = model_md2(&r, &m);
    } else if (!memcmp(bytes.data, "IDP3", 4)) {
        m.format = QA_MODEL_MD3;
        ok = model_md3(&r, &m);
    } else if (!memcmp(bytes.data, "IDP4", 4)) {
        m.format = QA_MODEL_MD4;
        ok = model_md4(&r, &m);
    } else if (!memcmp(bytes.data, "IDSP", 4)) {
        m.format = QA_MODEL_SPR;
        ok = model_sprite(&r, &m, false);
    } else if (!memcmp(bytes.data, "IDS2", 4)) {
        m.format = QA_MODEL_SP2;
        ok = model_sprite(&r, &m, true);
    } else if (model_is_md5(r.bytes)) {
        m.format = QA_MODEL_MD5;
        ok = model_md5(&r, &m);
    } else {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "unknown model format");
        ok = false;
    }
    if (!ok || !r.ok) {
        qa_model_free(&m);
        return false;
    }
    *out = m;
    return true;
}
uint32_t qa_model_group_sample(const qa_model_group *g, double seconds, double sync_base) {
    if (!g || !g->count)
        return UINT32_MAX;
    if (!g->intervals || !isfinite(seconds) || !isfinite(sync_base))
        return g->first;
    double time = fmod(seconds + sync_base, g->intervals[g->count - 1]);
    for (uint32_t i = 0; i < g->count; ++i)
        if (g->intervals[i] > time)
            return g->first + i;
    return g->first + g->count - 1;
}
bool qa_model_sample_mesh(const qa_model *m, uint32_t mesh, uint32_t frame, uint32_t old_frame,
                          float back, qa_model_vertex *out, size_t count, qa_error *error) {
    if (!m || mesh >= m->mesh_count || !isfinite(back) || back < 0 || back > 1)
        goto invalid;
    const qa_model_mesh *s = &m->meshes[mesh];
    if (frame >= s->frame_count || old_frame >= s->frame_count || count < s->vertex_count ||
        (!out && s->vertex_count))
        goto invalid;
    if (m->format == QA_MODEL_MD5)
        return qa_model_skin_md5(m, mesh, m->bind_pose, m->bone_count, out, count, error);
    if (m->format == QA_MODEL_MD4) {
        float matrices[128][12];
        for (uint32_t b = 0; b < m->bone_count; ++b)
            for (unsigned k = 0; k < 12; ++k)
                matrices[b][k] =
                    m->bone_matrices[((size_t)frame * m->bone_count + b) * 12 + k] * (1 - back) +
                    m->bone_matrices[((size_t)old_frame * m->bone_count + b) * 12 + k] * back;
        for (uint32_t v = 0; v < s->vertex_count; ++v) {
            memset(&out[v], 0, sizeof(out[v]));
            qa_model_weight_range range = s->vertex_weights[v];
            for (uint32_t w = 0; w < range.count; ++w) {
                const qa_model_weight *weight = &s->weights[range.first + w];
                const float *matrix = matrices[weight->bone];
                for (unsigned k = 0; k < 3; ++k) {
                    float p = matrix[k * 4 + 3], n = 0;
                    for (unsigned j = 0; j < 3; ++j) {
                        p += matrix[k * 4 + j] * weight->offset[j];
                        n += matrix[k * 4 + j] * s->vertices[v].normal[j];
                    }
                    out[v].position[k] += p * weight->bias;
                    out[v].normal[k] += n * weight->bias;
                }
            }
        }
        return true;
    }
    for (uint32_t i = 0; i < s->vertex_count; ++i) {
        const qa_model_vertex *a = &s->vertices[(size_t)frame * s->vertex_count + i],
                              *b = &s->vertices[(size_t)old_frame * s->vertex_count + i];
        for (unsigned k = 0; k < 3; ++k) {
            out[i].position[k] = a->position[k] * (1 - back) + b->position[k] * back;
            out[i].normal[k] = m->format == QA_MODEL_MD3
                                   ? a->normal[k] * (1 - back) + b->normal[k] * back
                                   : a->normal[k];
        }
        if (m->format == QA_MODEL_MD3 && frame != old_frame && back != 0)
            normalize_md3(out[i].normal);
    }
    return true;
invalid:
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid model pose or output span");
    return false;
}
bool qa_model_sample_alias(const qa_model *model, uint32_t frame, uint32_t old_frame, float back,
                           qa_vec3 previous_origin_delta, qa_model_vertex *vertices, size_t count,
                           qa_error *error) {
    if (!model || (model->format != QA_MODEL_MDL && model->format != QA_MODEL_MD2) ||
        !qa_vec_finite(previous_origin_delta)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid alias model or origin compensation");
        return false;
    }
    if (model->format == QA_MODEL_MD2) {
        if (!model->mesh_count || frame >= model->frame_count || old_frame >= model->frame_count ||
            !isfinite(back) || back < 0 || back > 1 || count < model->meshes[0].vertex_count ||
            (!vertices && model->meshes[0].vertex_count)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid MD2 alias pose or output span");
            return false;
        }
        const qa_model_frame *current = &model->frames[frame], *old = &model->frames[old_frame];
        const qa_model_mesh *surface = &model->meshes[0];
        float move[3], old_scale[3], new_scale[3];
        float delta[3] = {previous_origin_delta.x, previous_origin_delta.y,
                          previous_origin_delta.z};
        for (unsigned axis = 0; axis < 3; ++axis) {
            move[axis] = back * (delta[axis] + old->translation[axis]) +
                         (1 - back) * current->translation[axis];
            old_scale[axis] = back * old->scale[axis];
            new_scale[axis] = (1 - back) * current->scale[axis];
        }
        for (uint32_t i = 0; i < surface->vertex_count; ++i) {
            for (unsigned axis = 0; axis < 3; ++axis)
                vertices[i].position[axis] =
                    move[axis] + old->packed_vertices.data[(size_t)i * 4 + axis] * old_scale[axis] +
                    current->packed_vertices.data[(size_t)i * 4 + axis] * new_scale[axis];
            memcpy(vertices[i].normal,
                   surface->vertices[(size_t)frame * surface->vertex_count + i].normal,
                   sizeof(vertices[i].normal));
        }
        return true;
    }
    if (!qa_model_sample_mesh(model, 0, frame, old_frame, back, vertices, count, error))
        return false;
    qa_vec3 delta = qa_vec_scale(previous_origin_delta, back);
    for (uint32_t i = 0; i < model->meshes[0].vertex_count; ++i)
        model_v3_store(vertices[i].position, qa_vec_add(model_v3(vertices[i].position), delta));
    return true;
}
bool qa_model_corner_uv(const qa_model *model, uint32_t mesh, uint32_t triangle, uint32_t corner,
                        float out[2]) {
    if (!model || mesh >= model->mesh_count || corner >= 3 || !out)
        return false;
    const qa_model_mesh *surface = &model->meshes[mesh];
    if (triangle >= surface->triangle_count)
        return false;
    const qa_model_triangle *face = &surface->triangles[triangle];
    const qa_model_texcoord *coordinate = &surface->texcoords[face->texcoord[corner]];
    out[0] = coordinate->uv[0];
    out[1] = coordinate->uv[1];
    if (model->format == QA_MODEL_MDL && !face->front && coordinate->on_seam)
        out[0] += 0.5f;
    return true;
}
bool qa_model_lerp_tag(const qa_model *m, const char *name, uint32_t from, uint32_t to,
                       float fraction, qa_model_tag *out) {
    if (!m || !name || !out || !m->frame_count || !isfinite(fraction))
        return false;
    if (from >= m->frame_count)
        from = m->frame_count - 1;
    if (to >= m->frame_count)
        to = m->frame_count - 1;
    const qa_model_tag *a = NULL, *b = NULL;
    for (uint32_t i = 0; i < m->tag_count; ++i) {
        const qa_model_tag *tag = &m->tags[(size_t)from * m->tag_count + i];
        if (!a && !strcmp(name, tag->name))
            a = tag;
        tag = &m->tags[(size_t)to * m->tag_count + i];
        if (!b && !strcmp(name, tag->name))
            b = tag;
    }
    if (!a || !b)
        return false;
    qa_model_tag result = *a;
    for (unsigned k = 0; k < 3; ++k) {
        result.origin[k] = a->origin[k] * (1 - fraction) + b->origin[k] * fraction;
        for (unsigned j = 0; j < 3; ++j)
            result.axes[k][j] = a->axes[k][j] * (1 - fraction) + b->axes[k][j] * fraction;
        model_normalize(result.axes[k]);
    }
    *out = result;
    return true;
}
