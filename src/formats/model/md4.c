#include "internal.h"

static bool md4_surface(model_reader *r, qa_model *m, qa_model_mesh *s, size_t base, size_t lod_end,
                        size_t *next) {
    if (!model_range(r, base, 1, 168, 100, lod_end))
        return false;
    r->pos = base;
    (void)model_i32(r);
    model_string(r, s->name, 64);
    s->shader_count = 1;
    s->shaders = model_alloc(r, 1, sizeof(*s->shaders));
    if (!r->ok)
        return false;
    for (size_t i = 0; s->name[i]; ++i)
        if (s->name[i] >= 'A' && s->name[i] <= 'Z')
            s->name[i] = (char)(s->name[i] + ('a' - 'A'));
    model_string(r, s->shaders[0].name, 64);
    s->shaders[0].index = model_i32(r);
    int32_t header = model_i32(r);
    if ((int64_t)base + header != 0)
        return model_fail(r, "MD4 surface does not point to header");
    s->vertex_count = model_count(r, 0, 1000);
    uint32_t vertices = model_count(r, 0, INT32_MAX);
    s->triangle_count = model_count(r, 0, 2000);
    uint32_t triangles = model_count(r, 0, INT32_MAX);
    s->bone_reference_count = model_count(r, 0, INT32_MAX);
    uint32_t references = model_count(r, 0, INT32_MAX), length = model_count(r, 168, INT32_MAX);
    s->texcoord_count = s->vertex_count;
    s->frame_count = m->frame_count;
    if (!model_range(r, base, length, 1, 100, lod_end) ||
        (s->vertex_count && !model_range(r, vertices, s->vertex_count, 24, 168, length)) ||
        (s->triangle_count && !model_range(r, triangles, s->triangle_count, 12, 168, length)) ||
        (s->bone_reference_count &&
         !model_range(r, references, s->bone_reference_count, 4, 168, length)))
        return false;
    s->vertices = model_alloc(r, s->vertex_count, sizeof(*s->vertices));
    s->texcoords = model_alloc(r, s->vertex_count, sizeof(*s->texcoords));
    s->vertex_weights = model_alloc(r, s->vertex_count, sizeof(*s->vertex_weights));
    s->triangles = model_alloc(r, s->triangle_count, sizeof(*s->triangles));
    s->bone_references = model_alloc(r, s->bone_reference_count, sizeof(*s->bone_references));
    if (!r->ok)
        return false;
    r->pos = base + triangles;
    for (uint32_t i = 0; i < s->triangle_count; ++i) {
        if (!s->vertex_count)
            return model_fail(r, "MD4 triangles without vertices");
        for (unsigned k = 0; k < 3; ++k)
            s->triangles[i].texcoord[k] = s->triangles[i].vertex[k] =
                model_count(r, 0, s->vertex_count - 1);
    }
    r->pos = base + references;
    for (uint32_t i = 0; i < s->bone_reference_count; ++i) {
        if (!m->bone_count)
            return model_fail(r, "MD4 references without bones");
        s->bone_references[i] = model_count(r, 0, m->bone_count - 1);
    }
    r->pos = base + vertices;
    for (uint32_t i = 0; i < s->vertex_count; ++i) {
        if (!model_range(r, r->pos, 1, 24, base + 168, base + length))
            return false;
        model_vec(r, s->vertices[i].normal, 3);
        model_vec(r, s->texcoords[i].uv, 2);
        qa_model_weight_range *range = &s->vertex_weights[i];
        range->first = s->weight_count;
        range->count = model_count(r, 0, INT32_MAX);
        if (range->count > UINT32_MAX - s->weight_count ||
            !model_range(r, r->pos, range->count, 20, base + 168, base + length))
            return model_fail(r, "invalid MD4 weight span");
        s->weights = model_grow(r, s->weights, s->weight_count, s->weight_count + range->count,
                                sizeof(*s->weights));
        if (!r->ok)
            return false;
        s->weight_count += range->count;
        for (uint32_t j = 0; j < range->count; ++j) {
            if (!m->bone_count)
                return model_fail(r, "MD4 weights without bones");
            qa_model_weight *w = &s->weights[range->first + j];
            w->bone = model_count(r, 0, m->bone_count - 1);
            w->bias = model_float(r);
            model_vec(r, w->offset, 3);
        }
    }
    *next = base + length;
    return r->ok;
}
bool model_md4(model_reader *r, qa_model *m) {
    r->pos = 4;
    if (model_i32(r) != 1)
        return model_fail(r, "unsupported MD4 version");
    model_string(r, m->name, 64);
    m->frame_count = model_count(r, 1, INT32_MAX);
    m->bone_count = model_count(r, 0, 128);
    int32_t names = model_i32(r);
    uint32_t frames = model_count(r, 100, INT32_MAX);
    m->lod_count = model_count(r, 0, INT32_MAX);
    uint32_t lods = model_count(r, 0, INT32_MAX), end = model_count(r, 100, INT32_MAX);
    size_t frame_size = 40 + (size_t)m->bone_count * 48;
    if (!model_range(r, frames, m->frame_count, frame_size, 100, end) ||
        (m->lod_count && !model_range(r, lods, m->lod_count, 12, 100, end)))
        return false;
    m->frames = model_alloc(r, m->frame_count, sizeof(*m->frames));
    m->bones = model_alloc(r, m->bone_count, sizeof(*m->bones));
    m->bone_matrices = model_alloc(r, (size_t)m->frame_count * m->bone_count, sizeof(float) * 12);
    m->lods = model_alloc(r, m->lod_count, sizeof(*m->lods));
    if (!r->ok)
        return false;
    for (uint32_t i = 0; i < m->bone_count; ++i)
        m->bones[i].parent = -1;
    if (names >= 100 && (size_t)names <= end && m->bone_count <= (end - (size_t)names) / 64) {
        r->pos = (size_t)names;
        for (uint32_t i = 0; i < m->bone_count; ++i)
            model_string(r, m->bones[i].name, 64);
    }
    r->pos = frames;
    for (uint32_t i = 0; i < m->frame_count; ++i) {
        qa_model_frame *f = &m->frames[i];
        model_vec(r, f->bounds.min, 3);
        model_vec(r, f->bounds.max, 3);
        model_vec(r, f->origin, 3);
        f->radius = model_float(r);
        if (m->bone_count)
            model_vec(r, m->bone_matrices + (size_t)i * m->bone_count * 12,
                      (size_t)m->bone_count * 12);
        model_bounds_union(&m->bounds, &f->bounds);
    }
    size_t base = lods;
    for (uint32_t i = 0; i < m->lod_count; ++i) {
        if (!model_range(r, base, 1, 12, 100, end))
            return false;
        r->pos = base;
        qa_model_lod *lod = &m->lods[i];
        lod->first_mesh = m->mesh_count;
        lod->mesh_count = model_count(r, 0, INT32_MAX);
        uint32_t surfaces = model_count(r, 0, INT32_MAX), length = model_count(r, 12, INT32_MAX);
        if (!model_range(r, base, length, 1, 100, end) ||
            (lod->mesh_count && !model_range(r, surfaces, lod->mesh_count, 168, 12, length)) ||
            lod->mesh_count > UINT32_MAX - m->mesh_count)
            return model_fail(r, "invalid MD4 LOD");
        m->meshes = model_grow(r, m->meshes, m->mesh_count, m->mesh_count + lod->mesh_count,
                               sizeof(*m->meshes));
        if (!r->ok)
            return false;
        m->mesh_count += lod->mesh_count;
        size_t surface = base + surfaces;
        for (uint32_t j = 0; j < lod->mesh_count; ++j)
            if (!md4_surface(r, m, &m->meshes[lod->first_mesh + j], surface, base + length,
                             &surface))
                return false;
        base += length;
    }
    return r->ok;
}
