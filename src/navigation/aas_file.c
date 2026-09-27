#include "asset_internal.h"

static const size_t disk_stride[QA_AAS_LUMP_COUNT] = {32, 12, 20, 8,  4,  24, 4,
                                                      48, 28, 44, 12, 20, 4,  16};
static const size_t native_stride[QA_AAS_LUMP_COUNT] = {
    sizeof(qa_aas_box),     sizeof(qa_vec3),       sizeof(qa_aas_plane), sizeof(qa_aas_edge),
    sizeof(int32_t),        sizeof(qa_aas_face),   sizeof(int32_t),      sizeof(qa_aas_area),
    sizeof(qa_aas_setting), sizeof(qa_aas_reach),  sizeof(qa_aas_node),  sizeof(qa_aas_portal),
    sizeof(int32_t),        sizeof(qa_aas_cluster)};
bool nav_aas_allocate(const size_t counts[QA_AAS_LUMP_COUNT], qa_nav_asset **out, qa_error *error) {
    size_t offsets[QA_AAS_LUMP_COUNT], size = 0;
    for (unsigned i = 0; i < QA_AAS_LUMP_COUNT; ++i)
        if (counts[i] > INT32_MAX || !nav_layout(&size, counts[i], native_stride[i], &offsets[i])) {
            qa_error_set(error, QA_ERROR_MEMORY, i, "Navigation record capacity overflow");
            return false;
        }
    qa_nav_asset *asset = calloc(1, sizeof(*asset));
    if (asset == NULL)
        goto memory;
    asset->storage = calloc(size == 0 ? 1 : size, 1);
    if (asset->storage == NULL) {
        free(asset);
        goto memory;
    }
    atomic_init(&asset->references, 1);
    asset->kind = QA_NAV_AAS;
    qa_aas_view *v = &asset->aas;
    memcpy(v->count, counts, sizeof(v->count));
    uint8_t *p = asset->storage;
#define FIELD(name, lump) v->name = (const void *)(p + offsets[lump])
    FIELD(boxes, QA_AAS_BOXES);
    FIELD(vertices, QA_AAS_VERTICES);
    FIELD(planes, QA_AAS_PLANES);
    FIELD(edges, QA_AAS_EDGES);
    FIELD(edge_index, QA_AAS_EDGE_INDEX);
    FIELD(faces, QA_AAS_FACES);
    FIELD(face_index, QA_AAS_FACE_INDEX);
    FIELD(areas, QA_AAS_AREAS);
    FIELD(settings, QA_AAS_SETTINGS);
    FIELD(reachability, QA_AAS_REACHABILITY);
    FIELD(nodes, QA_AAS_NODES);
    FIELD(portals, QA_AAS_PORTALS);
    FIELD(portal_index, QA_AAS_PORTAL_INDEX);
    FIELD(clusters, QA_AAS_CLUSTERS);
#undef FIELD
    *out = asset;
    return true;
memory:
    qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating navigation asset");
    return false;
}
bool nav_aas_read(qa_bytes bytes, const int32_t *checksum, qa_nav_asset **out, qa_error *error) {
    uint32_t version = qa_load_u32le(bytes.data + 4);
    if ((version != 4 && version != 5) || bytes.size < 124) {
        qa_error_set(error, QA_ERROR_FORMAT, 4, "Invalid AAS version or header size");
        return false;
    }
    uint8_t header[116];
    memcpy(header, bytes.data + 8, sizeof(header));
    if (version == 5)
        for (size_t i = 0; i < sizeof(header); ++i)
            header[i] ^= (uint8_t)(i * 119);
    int32_t stored = qa_load_i32le(header);
    if (checksum != NULL && *checksum != stored) {
        qa_error_set(error, QA_ERROR_FORMAT, 8, "AAS belongs to another BSP checksum");
        return false;
    }
    size_t offsets[QA_AAS_LUMP_COUNT], counts[QA_AAS_LUMP_COUNT];
    for (unsigned l = 0; l < QA_AAS_LUMP_COUNT; ++l) {
        int32_t offset = qa_load_i32le(header + 4 + l * 8),
                length = qa_load_i32le(header + 8 + l * 8);
        if (length < 0 || (size_t)length % disk_stride[l] != 0 ||
            (length != 0 && (offset < 124 || (size_t)offset > bytes.size ||
                             (size_t)length > bytes.size - (size_t)offset))) {
            qa_error_set(error, QA_ERROR_FORMAT, 12 + l * 8, "Invalid AAS lump range or stride");
            return false;
        }
        offsets[l] = length == 0 ? 0 : (size_t)offset;
        counts[l] = (size_t)length / disk_stride[l];
    }
    qa_nav_asset *asset;
    if (!nav_aas_allocate(counts, &asset, error))
        return false;
    qa_aas_view *v = &asset->aas;
    v->version = version;
    v->bsp_checksum = stored;
#define I(o) qa_load_i32le(p + (o))
#define V(o) nav_vector(p + (o))
#define RECORD(field, type) type *q = (type *)v->field + i
    for (unsigned l = 0; l < QA_AAS_LUMP_COUNT; ++l)
        for (size_t i = 0; i < counts[l]; ++i) {
            const uint8_t *p = bytes.data + offsets[l] + i * disk_stride[l];
            switch (l) {
            case QA_AAS_BOXES: {
                RECORD(boxes, qa_aas_box);
                *q = (qa_aas_box){I(0), I(4), nav_bounds(p + 8)};
                break;
            }
            case QA_AAS_VERTICES: {
                RECORD(vertices, qa_vec3);
                *q = V(0);
                break;
            }
            case QA_AAS_PLANES: {
                RECORD(planes, qa_aas_plane);
                *q = (qa_aas_plane){V(0), qa_load_f32le(p + 12), I(16)};
                break;
            }
            case QA_AAS_EDGES: {
                RECORD(edges, qa_aas_edge);
                *q = (qa_aas_edge){{I(0), I(4)}};
                break;
            }
            case QA_AAS_EDGE_INDEX: {
                RECORD(edge_index, int32_t);
                *q = I(0);
                break;
            }
            case QA_AAS_FACES: {
                RECORD(faces, qa_aas_face);
                *q = (qa_aas_face){I(0), I(4), I(8), I(12), I(16), I(20)};
                break;
            }
            case QA_AAS_FACE_INDEX: {
                RECORD(face_index, int32_t);
                *q = I(0);
                break;
            }
            case QA_AAS_AREAS: {
                RECORD(areas, qa_aas_area);
                *q = (qa_aas_area){I(0), I(4), I(8), nav_bounds(p + 12), V(36)};
                break;
            }
            case QA_AAS_SETTINGS: {
                RECORD(settings, qa_aas_setting);
                *q = (qa_aas_setting){I(0), I(4), I(8), I(12), I(16), I(20), I(24)};
                break;
            }
            case QA_AAS_REACHABILITY: {
                RECORD(reachability, qa_aas_reach);
                *q = (qa_aas_reach){I(0),
                                    I(4),
                                    I(8),
                                    V(12),
                                    V(24),
                                    I(36),
                                    qa_load_u16le(p + 40),
                                    qa_load_u16le(p + 42)};
                break;
            }
            case QA_AAS_NODES: {
                RECORD(nodes, qa_aas_node);
                *q = (qa_aas_node){I(0), {I(4), I(8)}};
                break;
            }
            case QA_AAS_PORTALS: {
                RECORD(portals, qa_aas_portal);
                *q = (qa_aas_portal){I(0), I(4), I(8), {I(12), I(16)}};
                break;
            }
            case QA_AAS_PORTAL_INDEX: {
                RECORD(portal_index, int32_t);
                *q = I(0);
                break;
            }
            case QA_AAS_CLUSTERS: {
                RECORD(clusters, qa_aas_cluster);
                *q = (qa_aas_cluster){I(0), I(4), I(8), I(12)};
                break;
            }
            }
        }
#undef RECORD
#undef I
#undef V
    if (!qa_aas_validate(v, error)) {
        qa_nav_asset_release(asset);
        return false;
    }
    *out = asset;
    return true;
}
static void store_float(uint8_t *p, float f) {
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    qa_store_u32le(p, bits);
}
static void store_vector(uint8_t *p, qa_vec3 v) {
    store_float(p, v.x);
    store_float(p + 4, v.y);
    store_float(p + 8, v.z);
}
bool qa_aas_write(const qa_aas_view *v, qa_buffer *out, qa_error *error) {
    if (out == NULL || !qa_aas_validate(v, error))
        return false;
    size_t size = 124, offsets[QA_AAS_LUMP_COUNT];
    for (unsigned l = 0; l < QA_AAS_LUMP_COUNT; ++l) {
        offsets[l] = size;
        if (v->count[l] > (INT32_MAX - size) / disk_stride[l]) {
            qa_error_set(error, QA_ERROR_MEMORY, l, "AAS output exceeds source file range");
            return false;
        }
        size += v->count[l] * disk_stride[l];
    }
    uint8_t *bytes = calloc(size, 1);
    if (bytes == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating AAS output");
        return false;
    }
    qa_store_u32le(bytes, UINT32_C(0x53414145));
    qa_store_u32le(bytes + 4, 5);
    qa_store_u32le(bytes + 8, (uint32_t)v->bsp_checksum);
#define I(o, n) qa_store_u32le(p + (o), (uint32_t)(n))
#define V(o, n) store_vector(p + (o), (n))
#define RECORD(field, type) const type *q = v->field + i
    for (unsigned l = 0; l < QA_AAS_LUMP_COUNT; ++l) {
        qa_store_u32le(bytes + 12 + l * 8, (uint32_t)offsets[l]);
        qa_store_u32le(bytes + 16 + l * 8, (uint32_t)(v->count[l] * disk_stride[l]));
        for (size_t i = 0; i < v->count[l]; ++i) {
            uint8_t *p = bytes + offsets[l] + i * disk_stride[l];
            switch (l) {
            case QA_AAS_BOXES: {
                RECORD(boxes, qa_aas_box);
                I(0, q->presence);
                I(4, q->flags);
                V(8, q->bounds.mins);
                V(20, q->bounds.maxs);
                break;
            }
            case QA_AAS_VERTICES:
                V(0, v->vertices[i]);
                break;
            case QA_AAS_PLANES: {
                RECORD(planes, qa_aas_plane);
                V(0, q->normal);
                store_float(p + 12, q->distance);
                I(16, q->type);
                break;
            }
            case QA_AAS_EDGES: {
                RECORD(edges, qa_aas_edge);
                I(0, q->vertices[0]);
                I(4, q->vertices[1]);
                break;
            }
            case QA_AAS_EDGE_INDEX:
                I(0, v->edge_index[i]);
                break;
            case QA_AAS_FACES: {
                RECORD(faces, qa_aas_face);
                I(0, q->plane);
                I(4, q->flags);
                I(8, q->edge_count);
                I(12, q->first_edge);
                I(16, q->front_area);
                I(20, q->back_area);
                break;
            }
            case QA_AAS_FACE_INDEX:
                I(0, v->face_index[i]);
                break;
            case QA_AAS_AREAS: {
                RECORD(areas, qa_aas_area);
                I(0, q->number);
                I(4, q->face_count);
                I(8, q->first_face);
                V(12, q->bounds.mins);
                V(24, q->bounds.maxs);
                V(36, q->center);
                break;
            }
            case QA_AAS_SETTINGS: {
                RECORD(settings, qa_aas_setting);
                I(0, q->contents);
                I(4, q->flags);
                I(8, q->presence);
                I(12, q->cluster);
                I(16, q->cluster_area);
                I(20, q->reach_count);
                I(24, q->first_reach);
                break;
            }
            case QA_AAS_REACHABILITY: {
                RECORD(reachability, qa_aas_reach);
                I(0, q->area);
                I(4, q->face);
                I(8, q->edge);
                V(12, q->start);
                V(24, q->end);
                I(36, q->travel_type);
                qa_store_u16le(p + 40, q->travel_time);
                qa_store_u16le(p + 42, q->padding);
                break;
            }
            case QA_AAS_NODES: {
                RECORD(nodes, qa_aas_node);
                I(0, q->plane);
                I(4, q->children[0]);
                I(8, q->children[1]);
                break;
            }
            case QA_AAS_PORTALS: {
                RECORD(portals, qa_aas_portal);
                I(0, q->area);
                I(4, q->front_cluster);
                I(8, q->back_cluster);
                I(12, q->cluster_areas[0]);
                I(16, q->cluster_areas[1]);
                break;
            }
            case QA_AAS_PORTAL_INDEX:
                I(0, v->portal_index[i]);
                break;
            case QA_AAS_CLUSTERS: {
                RECORD(clusters, qa_aas_cluster);
                I(0, q->area_count);
                I(4, q->reachable_area_count);
                I(8, q->portal_count);
                I(12, q->first_portal);
                break;
            }
            }
        }
    }
#undef RECORD
#undef I
#undef V
    for (size_t i = 8; i < 124; ++i)
        bytes[i] ^= (uint8_t)((i - 8) * 119);
    *out = (qa_buffer){bytes, size};
    return true;
}
