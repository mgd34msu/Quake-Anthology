/* SPDX-License-Identifier: GPL-2.0-or-later
 * Disk layouts and compatibility behavior follow quake-typescript's
 * formats/q1-map, q2-map, and q3-map readers. */
#include "qa/bsp.h"
#include "qa/binary.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct lump_layout { qa_bsp_lump_kind kind; uint32_t stride; } lump_layout;

static const lump_layout q1_layout[] = {
    {QA_BSP_ENTITIES,0}, {QA_BSP_PLANES,20}, {QA_BSP_TEXTURES,0},
    {QA_BSP_VERTICES,12}, {QA_BSP_VISIBILITY,0}, {QA_BSP_NODES,24},
    {QA_BSP_TEXINFO,40}, {QA_BSP_FACES,20}, {QA_BSP_LIGHTING,0},
    {QA_BSP_CLIPNODES,8}, {QA_BSP_LEAVES,28}, {QA_BSP_LEAF_FACES,2},
    {QA_BSP_EDGES,4}, {QA_BSP_SURFEDGES,4}, {QA_BSP_MODELS,64}
};
static const lump_layout q2_layout[] = {
    {QA_BSP_ENTITIES,0}, {QA_BSP_PLANES,20}, {QA_BSP_VERTICES,12},
    {QA_BSP_VISIBILITY,0}, {QA_BSP_NODES,28}, {QA_BSP_TEXINFO,76},
    {QA_BSP_FACES,20}, {QA_BSP_LIGHTING,0}, {QA_BSP_LEAVES,28},
    {QA_BSP_LEAF_FACES,2}, {QA_BSP_LEAF_BRUSHES,2}, {QA_BSP_EDGES,4},
    {QA_BSP_SURFEDGES,4}, {QA_BSP_MODELS,48}, {QA_BSP_BRUSHES,12},
    {QA_BSP_BRUSH_SIDES,4}, {QA_BSP_POP,0}, {QA_BSP_AREAS,8},
    {QA_BSP_AREA_PORTALS,8}
};
static const lump_layout q3_layout[] = {
    {QA_BSP_ENTITIES,0}, {QA_BSP_SHADERS,72}, {QA_BSP_PLANES,16},
    {QA_BSP_NODES,36}, {QA_BSP_LEAVES,48}, {QA_BSP_LEAF_FACES,4},
    {QA_BSP_LEAF_BRUSHES,4}, {QA_BSP_MODELS,40}, {QA_BSP_BRUSHES,12},
    {QA_BSP_BRUSH_SIDES,8}, {QA_BSP_VERTICES,44}, {QA_BSP_INDICES,4},
    {QA_BSP_FOGS,72}, {QA_BSP_SURFACES,104}, {QA_BSP_LIGHTING,49152},
    {QA_BSP_LIGHTGRID,8}, {QA_BSP_VISIBILITY,0}
};
static const lump_layout q3_44_layout[] = {
    {QA_BSP_ENTITIES,0}, {QA_BSP_PLANES,20}, {QA_BSP_NODES,36},
    {QA_BSP_LEAVES,48}, {QA_BSP_LEAF_FACES,4}, {QA_BSP_LEAF_BRUSHES,4},
    {QA_BSP_MODELS,48}, {QA_BSP_BRUSHES,12}, {QA_BSP_BRUSH_SIDES,8},
    {QA_BSP_LIGHTING,49152}, {QA_BSP_VISIBILITY,0}, {QA_BSP_VERTICES,44},
    {QA_BSP_SURFACES,164}, {QA_BSP_FOGS,68}, {QA_BSP_INDICES,4}
};

static bool fail(qa_error *error, qa_status code, size_t offset, const char *message)
{
    qa_error_set(error, code, offset, "%s", message);
    return false;
}

const char *qa_bsp_format_name(qa_bsp_format format)
{
    switch (format) {
    case QA_BSP_29: return "bsp29";
    case QA_BSP_2: return "bsp2";
    case QA_BSP_2PSB: return "2psb";
    case QA_BSP_QUAKE64: return "quake64";
    case QA_BSP_IBSP38: return "ibsp38";
    case QA_BSP_QBSP: return "qbsp";
    case QA_BSP_IBSP44: return "ibsp44";
    case QA_BSP_IBSP46: return "ibsp46";
    }
    return "unknown";
}

const char *qa_bsp_lump_name(qa_bsp_lump_kind kind)
{
    static const char *const names[QA_BSP_LUMP_COUNT] = {
        "entities", "planes", "textures", "vertices", "visibility", "nodes",
        "texinfo", "faces", "lighting", "clipnodes", "leaves", "leaf_faces",
        "edges", "surfedges", "models", "leaf_brushes", "brushes", "brush_sides",
        "pop", "areas", "area_portals", "shaders", "indices", "fogs", "surfaces",
        "lightgrid"
    };
    return (unsigned)kind < QA_BSP_LUMP_COUNT ? names[kind] : "unknown";
}

static size_t record_stride(qa_bsp_format format, lump_layout layout)
{
    if (format == QA_BSP_2 || format == QA_BSP_2PSB) {
        switch (layout.kind) {
        case QA_BSP_NODES: case QA_BSP_LEAVES: return format == QA_BSP_2 ? 44 : 32;
        case QA_BSP_CLIPNODES: return 12;
        case QA_BSP_FACES: return 28;
        case QA_BSP_LEAF_FACES: return 4;
        case QA_BSP_EDGES: return 8;
        default: break;
        }
    } else if (format == QA_BSP_QBSP) {
        switch (layout.kind) {
        case QA_BSP_NODES: return 44;
        case QA_BSP_LEAVES: return 52;
        case QA_BSP_FACES: return 28;
        case QA_BSP_LEAF_FACES: case QA_BSP_LEAF_BRUSHES: return 4;
        case QA_BSP_EDGES: case QA_BSP_BRUSH_SIDES: return 8;
        default: break;
        }
    }
    return layout.stride;
}

static qa_bytes string_span(const uint8_t *bytes, size_t capacity)
{
    size_t length = 0;
    while (length < capacity && bytes[length] != 0) ++length;
    return (qa_bytes){bytes, length};
}

static bool same_bytes(qa_bytes a, qa_bytes b)
{
    return a.size == b.size && (a.size == 0 || memcmp(a.data, b.data, a.size) == 0);
}

static qa_bsp_extension extension_raw(const qa_bsp_view *map, uint32_t index)
{
    const uint8_t *entry = map->source.data + map->bspx_offset + 8 + (size_t)index * 32;
    size_t offset = qa_load_u32le(entry + 24), length = qa_load_u32le(entry + 28);
    qa_bsp_extension result = {string_span(entry, 24), {NULL, length}, offset};
    if (offset <= map->source.size && length <= map->source.size - offset)
        result.bytes.data = map->source.data + offset;
    return result;
}

static bool extension_accepted(const qa_bsp_view *map, uint32_t index)
{
    qa_bsp_extension item = extension_raw(map, index);
    if (map->family == QA_BSP_Q1) return item.bytes.data != NULL;
    if (item.bytes.data == NULL || item.bytes.size == 0) return false;
    for (uint32_t before = 0; before < index; ++before) {
        qa_bsp_extension earlier = extension_raw(map, before);
        if (earlier.bytes.data != NULL && earlier.bytes.size != 0 && same_bytes(earlier.name, item.name))
            return false;
    }
    return true;
}

static bool read_bspx(qa_bsp_view *map, size_t end, qa_error *error)
{
    if (map->family == QA_BSP_Q3) return true;
    if (end > SIZE_MAX - 3) return true;
    size_t candidates[2] = {(end + 3) & ~(size_t)3, 124};
    size_t candidate_count = 1;
    if (map->family == QA_BSP_Q1) {
        bool occupied = false;
        for (size_t i = 0; i < QA_BSP_LUMP_COUNT; ++i) {
            qa_bsp_lump lump = map->lumps[i];
            if (lump.bytes.size > 0 && lump.offset < 132 && lump.offset + lump.bytes.size > 124)
                occupied = true;
        }
        if (!occupied && candidates[0] != 124) candidate_count = 2;
    }
    for (size_t i = 0; i < candidate_count; ++i) {
        size_t offset = candidates[i];
        if (offset > map->source.size || map->source.size - offset < 8
            || memcmp(map->source.data + offset, "BSPX", 4) != 0) continue;
        uint32_t count = qa_load_u32le(map->source.data + offset + 4);
        if (count > (map->source.size - offset - 8) / 32) {
            if (map->family == QA_BSP_Q1)
                return fail(error, QA_ERROR_FORMAT, offset, "truncated BSPX directory");
            map->diagnostics |= QA_BSP_DIAGNOSTIC_BSPX_TRUNCATED;
            return true;
        }
        map->bspx_offset = offset;
        map->bspx_record_count = count;
        for (uint32_t entry = 0; entry < count; ++entry) {
            if (extension_accepted(map, entry)) ++map->extension_count;
            else if (map->family == QA_BSP_Q1)
                return fail(error, QA_ERROR_FORMAT, offset + 8 + (size_t)entry * 32, "invalid BSPX lump range");
            else map->diagnostics |= QA_BSP_DIAGNOSTIC_BSPX_IGNORED;
        }
        return true;
    }
    return true;
}

static bool validate_visibility(const qa_bsp_view *map, qa_error *error)
{
    qa_bsp_lump lump = map->lumps[QA_BSP_VISIBILITY];
    if (map->family == QA_BSP_Q1 || lump.bytes.size == 0) return true;
    const uint8_t *p = lump.bytes.data;
    if (map->family == QA_BSP_Q2) {
        if (lump.bytes.size < 4)
            return fail(error, QA_ERROR_FORMAT, lump.offset, "truncated visibility header");
        uint32_t count = qa_load_u32le(p);
        if (count > (lump.bytes.size - 4) / 8)
            return fail(error, QA_ERROR_FORMAT, lump.offset, "truncated visibility table");
        size_t header = 4 + (size_t)count * 8;
        for (size_t i = 4; i < header; i += 4) {
            int32_t offset = qa_load_i32le(p + i);
            if (offset != -1 && (offset < 0 || (size_t)offset < header || (size_t)offset >= lump.bytes.size))
                return fail(error, QA_ERROR_FORMAT, lump.offset + i, "visibility offset outside compressed data");
        }
    } else {
        if (lump.bytes.size < 8)
            return fail(error, QA_ERROR_FORMAT, lump.offset, "truncated visibility header");
        int32_t count = qa_load_i32le(p), width = qa_load_i32le(p + 4);
        if (count < 0 || width < 0 || (uint64_t)(uint32_t)width < ((uint64_t)(uint32_t)count + 7) / 8
            || (uint64_t)(uint32_t)count * (uint32_t)width != lump.bytes.size - 8)
            return fail(error, QA_ERROR_FORMAT, lump.offset, "invalid visibility dimensions");
    }
    return true;
}

bool qa_bsp_open(qa_bytes source, qa_bsp_view *out, qa_error *error)
{
    if (out == NULL || (source.size > 0 && source.data == NULL))
        return fail(error, QA_ERROR_ARGUMENT, 0, "invalid BSP input or output");
    if (source.size < 4) return fail(error, QA_ERROR_FORMAT, 0, "truncated BSP identifier");
    qa_bsp_view map = {.source = source};
    uint32_t magic = qa_load_u32le(source.data);
    const lump_layout *layout = NULL;
    size_t count = 0, directory = 4;
    switch (magic) {
    case 29: map.format = QA_BSP_29; break;
    case UINT32_C(0x32505342): map.format = QA_BSP_2; break;
    case UINT32_C(0x42535032): map.format = QA_BSP_2PSB; break;
    case UINT32_C(0x51363420): map.format = QA_BSP_QUAKE64; break;
    case UINT32_C(0x50534249): case UINT32_C(0x50534251): {
        if (source.size < 8) return fail(error, QA_ERROR_FORMAT, 4, "truncated BSP version");
        uint32_t version = qa_load_u32le(source.data + 4);
        directory = 8;
        if (version == 38) {
            map.format = magic == UINT32_C(0x50534249) ? QA_BSP_IBSP38 : QA_BSP_QBSP;
            map.family = QA_BSP_Q2; layout = q2_layout; count = 19;
        } else if (magic == UINT32_C(0x50534249) && (version == 44 || version == 46)) {
            map.format = version == 44 ? QA_BSP_IBSP44 : QA_BSP_IBSP46;
            map.family = QA_BSP_Q3;
            layout = version == 44 ? q3_44_layout : q3_layout;
            count = version == 44 ? 15 : 17;
        } else return fail(error, QA_ERROR_UNSUPPORTED, 4, "unsupported BSP version");
        break;
    }
    default: return fail(error, QA_ERROR_UNSUPPORTED, 0, "unsupported BSP identifier");
    }
    if (layout == NULL) { map.family = QA_BSP_Q1; layout = q1_layout; count = 15; }
    size_t header = directory + count * 8;
    if (source.size < header) return fail(error, QA_ERROR_FORMAT, directory, "truncated BSP directory");
    size_t end = header;
    for (size_t i = 0; i < count; ++i) {
        const uint8_t *entry = source.data + directory + i * 8;
        uint32_t stored_offset = qa_load_u32le(entry), stored_length = qa_load_u32le(entry + 4);
        size_t offset = stored_offset, length = stored_length;
        if (map.family != QA_BSP_Q2 && (stored_offset > INT32_MAX || stored_length > INT32_MAX))
            return fail(error, QA_ERROR_FORMAT, directory + i * 8, "negative BSP lump range");
        if (map.family == QA_BSP_Q2 && i == 0 && offset < source.size && length > source.size - offset) {
            length = source.size - offset;
            map.diagnostics |= QA_BSP_DIAGNOSTIC_ENTITY_CLAMPED;
        }
        if (offset > source.size || length > source.size - offset
            || (map.family != QA_BSP_Q2 && length > 0 && offset < header))
            return fail(error, QA_ERROR_FORMAT, directory + i * 8, "invalid BSP lump range");
        size_t stride = record_stride(map.format, layout[i]);
        if (stride > 0 && length % stride != 0) {
            qa_error_set(error, QA_ERROR_FORMAT, offset, "%s length is not a multiple of %zu", qa_bsp_lump_name(layout[i].kind), stride);
            return false;
        }
        map.lumps[layout[i].kind] = (qa_bsp_lump){{source.data + offset, length}, offset, stride, true};
        if (offset + length > end) end = offset + length;
    }
    if (!validate_visibility(&map, error) || !read_bspx(&map, end, error)) return false;
    *out = map;
    return true;
}

size_t qa_bsp_record_count(const qa_bsp_view *map, qa_bsp_lump_kind kind)
{
    if (map == NULL || (unsigned)kind >= QA_BSP_LUMP_COUNT || map->lumps[kind].stride == 0) return 0;
    return map->lumps[kind].bytes.size / map->lumps[kind].stride;
}

bool qa_bsp_record(const qa_bsp_view *map, qa_bsp_lump_kind kind, size_t index,
                   qa_bytes *out, qa_error *error)
{
    if (map == NULL || out == NULL || (unsigned)kind >= QA_BSP_LUMP_COUNT)
        return fail(error, QA_ERROR_ARGUMENT, 0, "invalid BSP record request");
    const qa_bsp_lump *lump = &map->lumps[kind];
    if (!lump->present || lump->stride == 0)
        return fail(error, QA_ERROR_UNSUPPORTED, lump->offset, "lump has no fixed records in this format");
    if (index >= lump->bytes.size / lump->stride)
        return fail(error, QA_ERROR_ARGUMENT, lump->offset, "BSP record index out of bounds");
    *out = (qa_bytes){lump->bytes.data + index * lump->stride, lump->stride};
    return true;
}

bool qa_bsp_extension_at(const qa_bsp_view *map, size_t index, qa_bsp_extension *out, qa_error *error)
{
    if (map == NULL || out == NULL || index >= map->extension_count)
        return fail(error, QA_ERROR_ARGUMENT, 0, "BSPX index out of bounds");
    for (uint32_t raw = 0; raw < map->bspx_record_count; ++raw) {
        if (!extension_accepted(map, raw)) continue;
        if (index-- == 0) { *out = extension_raw(map, raw); return true; }
    }
    return fail(error, QA_ERROR_FORMAT, map->bspx_offset, "BSPX directory changed after opening");
}

bool qa_bsp_find_extension(const qa_bsp_view *map, const char *name, qa_bsp_extension *out, qa_error *error)
{
    if (map == NULL || name == NULL || out == NULL)
        return fail(error, QA_ERROR_ARGUMENT, 0, "invalid BSPX lookup");
    qa_bytes wanted = {(const uint8_t *)name, strlen(name)};
    for (uint32_t raw = 0; raw < map->bspx_record_count; ++raw) {
        qa_bsp_extension item = extension_raw(map, raw);
        if (same_bytes(item.name, wanted) && extension_accepted(map, raw)) { *out = item; return true; }
    }
    return fail(error, QA_ERROR_NOT_FOUND, 0, "BSPX lump not found");
}

typedef struct record_reader {
    qa_bytes bytes;
    size_t position, bad_float, file_offset;
} record_reader;

static bool begin_record(const qa_bsp_view *map, qa_bsp_lump_kind kind, size_t index,
                         void *out, record_reader *reader, qa_error *error)
{
    if (out == NULL) return fail(error, QA_ERROR_ARGUMENT, 0, "missing BSP record output");
    qa_bytes bytes;
    if (!qa_bsp_record(map, kind, index, &bytes, error)) return false;
    *reader = (record_reader){bytes, 0, SIZE_MAX, map->lumps[kind].offset + index * bytes.size};
    return true;
}

static uint16_t r_u16(record_reader *r)
{
    uint16_t value = qa_load_u16le(r->bytes.data + r->position); r->position += 2; return value;
}
static int16_t r_i16(record_reader *r)
{
    int16_t value = qa_load_i16le(r->bytes.data + r->position); r->position += 2; return value;
}
static uint32_t r_u32(record_reader *r)
{
    uint32_t value = qa_load_u32le(r->bytes.data + r->position); r->position += 4; return value;
}
static int32_t r_i32(record_reader *r)
{
    int32_t value = qa_load_i32le(r->bytes.data + r->position); r->position += 4; return value;
}
static float r_float(record_reader *r)
{
    float value = qa_load_f32le(r->bytes.data + r->position);
    if (!isfinite(value) && r->bad_float == SIZE_MAX) r->bad_float = r->position;
    r->position += 4;
    return value;
}
static qa_bsp_vec3 r_vector(record_reader *r)
{
    qa_bsp_vec3 value;
    value.x = r_float(r); value.y = r_float(r); value.z = r_float(r);
    return value;
}
typedef enum bounds_encoding { BOUNDS_SHORT, BOUNDS_INT, BOUNDS_FLOAT } bounds_encoding;
static qa_bsp_bounds r_bounds(record_reader *r, bounds_encoding encoding)
{
    float values[6];
    for (size_t i = 0; i < 6; ++i) {
        if (encoding == BOUNDS_FLOAT) values[i] = r_float(r);
        else if (encoding == BOUNDS_INT) values[i] = (float)r_i32(r);
        else values[i] = (float)r_i16(r);
    }
    return (qa_bsp_bounds){{values[0],values[1],values[2]}, {values[3],values[4],values[5]}};
}
static qa_bsp_range r_range(record_reader *r, bool wide)
{
    qa_bsp_range value;
    value.first = wide ? r_u32(r) : r_u16(r);
    value.count = wide ? r_u32(r) : r_u16(r);
    return value;
}
static qa_bytes r_string(record_reader *r, size_t width)
{
    qa_bytes value = string_span(r->bytes.data + r->position, width);
    r->position += width;
    return value;
}
static bool end_record(const record_reader *r, qa_error *error)
{
    if (r->bad_float != SIZE_MAX)
        return fail(error, QA_ERROR_FORMAT, r->file_offset + r->bad_float, "non-finite BSP coordinate");
    return true;
}
static bool narrow_q1(const qa_bsp_view *map)
{
    return map->format == QA_BSP_29 || map->format == QA_BSP_QUAKE64;
}
static bool wide_faces(const qa_bsp_view *map)
{
    return map->format == QA_BSP_2 || map->format == QA_BSP_2PSB || map->format == QA_BSP_QBSP;
}

bool qa_bsp_read_plane(const qa_bsp_view *map, size_t index, qa_bsp_plane *out, qa_error *error)
{
    record_reader r;
    if (!begin_record(map, QA_BSP_PLANES, index, out, &r, error)) return false;
    qa_bsp_plane value;
    value.normal = r_vector(&r); value.distance = r_float(&r);
    value.type = r.bytes.size == 20 ? r_i32(&r) : -1;
    if (!end_record(&r, error)) return false;
    *out = value;
    return true;
}

bool qa_bsp_read_vertex(const qa_bsp_view *map, size_t index, qa_bsp_vertex *out, qa_error *error)
{
    record_reader r;
    if (!begin_record(map, QA_BSP_VERTICES, index, out, &r, error)) return false;
    qa_bsp_vertex value = {0};
    value.position = r_vector(&r);
    if (map->family == QA_BSP_Q3) {
        value.texcoord[0] = r_float(&r); value.texcoord[1] = r_float(&r);
        /* Retail unlit patches contain unused NaN lightmap coordinates. */
        value.lightmap_coord[0] = qa_load_f32le(r.bytes.data + r.position);
        value.lightmap_coord[1] = qa_load_f32le(r.bytes.data + r.position + 4);
        r.position += 8;
        value.normal = r_vector(&r);
        memcpy(value.color, r.bytes.data + r.position, 4);
    }
    if (!end_record(&r, error)) return false;
    *out = value;
    return true;
}

bool qa_bsp_read_node(const qa_bsp_view *map, size_t index, qa_bsp_node *out, qa_error *error)
{
    record_reader r;
    if (!begin_record(map, QA_BSP_NODES, index, out, &r, error)) return false;
    qa_bsp_node value = {0};
    value.plane = r_u32(&r);
    for (size_t i = 0; i < 2; ++i) {
        if (narrow_q1(map)) {
            uint16_t child = r_u16(&r);
            value.children[i] = child < qa_bsp_record_count(map, QA_BSP_NODES)
                ? (int32_t)child : (int32_t)child - 65536;
        } else value.children[i] = r_i32(&r);
    }
    bounds_encoding encoding = map->family == QA_BSP_Q3 ? BOUNDS_INT
        : map->format == QA_BSP_2 || map->format == QA_BSP_QBSP ? BOUNDS_FLOAT : BOUNDS_SHORT;
    value.bounds = r_bounds(&r, encoding);
    if (map->family != QA_BSP_Q3) value.faces = r_range(&r, wide_faces(map));
    if (!end_record(&r, error)) return false;
    *out = value;
    return true;
}

bool qa_bsp_read_leaf(const qa_bsp_view *map, size_t index, qa_bsp_leaf *out, qa_error *error)
{
    record_reader r;
    if (!begin_record(map, QA_BSP_LEAVES, index, out, &r, error)) return false;
    qa_bsp_leaf value = {.cluster = -1, .area = -1, .visibility_offset = -1};
    if (map->family == QA_BSP_Q1) {
        value.contents = r_i32(&r); value.visibility_offset = r_i32(&r);
        value.bounds = r_bounds(&r, map->format == QA_BSP_2 ? BOUNDS_FLOAT : BOUNDS_SHORT);
        value.faces = r_range(&r, !narrow_q1(map));
        memcpy(value.ambient, r.bytes.data + r.position, 4);
    } else if (map->family == QA_BSP_Q2) {
        bool wide = map->format == QA_BSP_QBSP;
        value.contents = r_i32(&r);
        uint32_t cluster = wide ? r_u32(&r) : r_u16(&r);
        value.cluster = cluster == (wide ? UINT32_MAX : UINT16_MAX) ? -1 : (int64_t)cluster;
        value.area = wide ? r_u32(&r) : r_u16(&r);
        value.bounds = r_bounds(&r, wide ? BOUNDS_FLOAT : BOUNDS_SHORT);
        value.faces = r_range(&r, wide); value.brushes = r_range(&r, wide);
    } else {
        value.cluster = r_i32(&r); value.area = r_i32(&r);
        value.bounds = r_bounds(&r, BOUNDS_INT);
        value.faces = r_range(&r, true); value.brushes = r_range(&r, true);
    }
    if (!end_record(&r, error)) return false;
    *out = value;
    return true;
}

bool qa_bsp_read_edge(const qa_bsp_view *map, size_t index, qa_bsp_edge *out, qa_error *error)
{
    record_reader r;
    if (!begin_record(map, QA_BSP_EDGES, index, out, &r, error)) return false;
    qa_bsp_edge value;
    value.vertices[0] = wide_faces(map) ? r_u32(&r) : r_u16(&r);
    value.vertices[1] = wide_faces(map) ? r_u32(&r) : r_u16(&r);
    *out = value;
    return true;
}

bool qa_bsp_read_face(const qa_bsp_view *map, size_t index, qa_bsp_face *out, qa_error *error)
{
    record_reader r;
    if (!begin_record(map, QA_BSP_FACES, index, out, &r, error)) return false;
    qa_bsp_face value;
    bool wide = wide_faces(map);
    value.plane = wide ? r_u32(&r) : r_u16(&r);
    value.draw_flags = wide ? r_u32(&r) : r_u16(&r);
    if (map->family == QA_BSP_Q1 && value.draw_flags > 1)
        return fail(error, QA_ERROR_FORMAT, r.file_offset, "invalid Q1 face side");
    value.edges.first = r_u32(&r);
    value.edges.count = wide ? r_u32(&r) : r_u16(&r);
    value.texinfo = wide ? r_u32(&r) : r_u16(&r);
    memcpy(value.styles, r.bytes.data + r.position, 4); r.position += 4;
    value.lighting_offset = r_i32(&r);
    if (map->format == QA_BSP_QUAKE64 && value.lighting_offset != -1) {
        if (value.lighting_offset % 2 != 0)
            return fail(error, QA_ERROR_FORMAT, r.file_offset + r.position - 4, "unaligned Quake64 light offset");
        value.lighting_offset /= 2;
    }
    *out = value;
    return true;
}

bool qa_bsp_read_clipnode(const qa_bsp_view *map, size_t index, qa_bsp_clipnode *out, qa_error *error)
{
    record_reader r;
    if (!begin_record(map, QA_BSP_CLIPNODES, index, out, &r, error)) return false;
    qa_bsp_clipnode value;
    value.plane = r_i32(&r);
    for (size_t i = 0; i < 2; ++i) {
        if (narrow_q1(map)) {
            uint16_t child = r_u16(&r);
            value.children[i] = child < qa_bsp_record_count(map, QA_BSP_CLIPNODES)
                ? (int32_t)child : (int32_t)child - 65536;
        } else value.children[i] = r_i32(&r);
    }
    *out = value;
    return true;
}

bool qa_bsp_read_texinfo(const qa_bsp_view *map, size_t index, qa_bsp_texinfo *out, qa_error *error)
{
    record_reader r;
    if (!begin_record(map, QA_BSP_TEXINFO, index, out, &r, error)) return false;
    qa_bsp_texinfo value = {.next = -1, .texture = -1};
    for (size_t axis = 0; axis < 2; ++axis)
        for (size_t component = 0; component < 4; ++component)
            value.projection[axis][component] = r_float(&r);
    if (map->family == QA_BSP_Q1) { value.texture = r_i32(&r); value.flags = r_i32(&r); }
    else {
        value.flags = r_i32(&r); value.value = r_i32(&r);
        value.name = r_string(&r, 32); value.next = r_i32(&r);
    }
    if (!end_record(&r, error)) return false;
    *out = value;
    return true;
}

bool qa_bsp_read_model(const qa_bsp_view *map, size_t index, qa_bsp_model *out, qa_error *error)
{
    record_reader r;
    if (!begin_record(map, QA_BSP_MODELS, index, out, &r, error)) return false;
    qa_bsp_model value = {0};
    value.bounds = r_bounds(&r, BOUNDS_FLOAT);
    if (map->format == QA_BSP_IBSP46) {
        value.faces = r_range(&r, true); value.brushes = r_range(&r, true);
    } else {
        value.origin = r_vector(&r);
        value.headnodes[0] = r_i32(&r);
        if (map->family == QA_BSP_Q1) {
            for (size_t i = 1; i < 4; ++i) value.headnodes[i] = r_i32(&r);
            value.visible_leaves = r_i32(&r);
        }
        value.faces = r_range(&r, true);
        value.membership_from_tree = map->format == QA_BSP_IBSP44;
    }
    if (!end_record(&r, error)) return false;
    *out = value;
    return true;
}

bool qa_bsp_read_brush(const qa_bsp_view *map, size_t index, qa_bsp_brush *out, qa_error *error)
{
    record_reader r;
    if (!begin_record(map, QA_BSP_BRUSHES, index, out, &r, error)) return false;
    qa_bsp_brush value = {.shader = -1};
    value.sides = r_range(&r, true);
    if (map->format == QA_BSP_IBSP46) value.shader = r_i32(&r);
    else value.contents = r_i32(&r);
    *out = value;
    return true;
}

bool qa_bsp_read_brush_side(const qa_bsp_view *map, size_t index, qa_bsp_brush_side *out, qa_error *error)
{
    record_reader r;
    if (!begin_record(map, QA_BSP_BRUSH_SIDES, index, out, &r, error)) return false;
    qa_bsp_brush_side value = {.texinfo = -1, .shader = -1};
    if (map->family == QA_BSP_Q2) {
        bool wide = map->format == QA_BSP_QBSP;
        value.plane = wide ? r_u32(&r) : r_u16(&r);
        uint32_t texinfo = wide ? r_u32(&r) : r_u16(&r);
        value.texinfo = texinfo == (wide ? UINT32_MAX : UINT16_MAX) ? -1 : (int64_t)texinfo;
    } else {
        value.plane = r_u32(&r);
        if (map->format == QA_BSP_IBSP44) value.flags = r_i32(&r);
        else value.shader = r_i32(&r);
    }
    *out = value;
    return true;
}

bool qa_bsp_read_shader(const qa_bsp_view *map, size_t index, qa_bsp_shader *out, qa_error *error)
{
    record_reader r;
    if (!begin_record(map, QA_BSP_SHADERS, index, out, &r, error)) return false;
    qa_bsp_shader value;
    value.name = r_string(&r, 64); value.surface_flags = r_i32(&r); value.content_flags = r_i32(&r);
    *out = value;
    return true;
}

bool qa_bsp_read_fog(const qa_bsp_view *map, size_t index, qa_bsp_fog *out, qa_error *error)
{
    record_reader r;
    if (!begin_record(map, QA_BSP_FOGS, index, out, &r, error)) return false;
    qa_bsp_fog value;
    value.name = r_string(&r, 64); value.brush = r_i32(&r);
    value.visible_side = map->format == QA_BSP_IBSP44 ? -1 : r_i32(&r);
    *out = value;
    return true;
}

bool qa_bsp_read_surface(const qa_bsp_view *map, size_t index, qa_bsp_surface *out, qa_error *error)
{
    record_reader r;
    if (!begin_record(map, QA_BSP_SURFACES, index, out, &r, error)) return false;
    qa_bsp_surface value = {.shader = -1, .brush_side = -1};
    if (map->format == QA_BSP_IBSP44) {
        value.shader_name = r_string(&r, 64); value.fog = r_i32(&r); value.brush_side = r_i32(&r);
        value.vertices = r_range(&r, true); value.indices = r_range(&r, true);
        value.patch_width = r_i32(&r); value.patch_height = r_i32(&r);
        value.type = value.patch_width > 0 && value.patch_height > 0 ? QA_BSP_SURFACE_PATCH
            : value.indices.count > 0 ? QA_BSP_SURFACE_TRIANGLES : QA_BSP_SURFACE_PLANAR;
        value.triangle_fan = value.type == QA_BSP_SURFACE_PLANAR;
        if (value.triangle_fan && value.vertices.count < 3)
            return fail(error, QA_ERROR_FORMAT, r.file_offset + 76, "IBSP44 polygon has fewer than three vertices");
    } else {
        value.shader = r_i32(&r); value.fog = r_i32(&r);
        int32_t type = r_i32(&r);
        if (type < 1 || type > 4)
            return fail(error, QA_ERROR_FORMAT, r.file_offset + 8, "unknown BSP surface type");
        value.type = (qa_bsp_surface_type)type;
        value.vertices = r_range(&r, true); value.indices = r_range(&r, true);
    }
    value.lightmap = r_i32(&r); value.lightmap_x = r_i32(&r); value.lightmap_y = r_i32(&r);
    value.lightmap_width = r_i32(&r); value.lightmap_height = r_i32(&r);
    value.lightmap_origin = r_vector(&r);
    for (size_t i = 0; i < 3; ++i) value.lightmap_vectors[i] = r_vector(&r);
    if (map->format != QA_BSP_IBSP44) { value.patch_width = r_i32(&r); value.patch_height = r_i32(&r); }
    if (value.type == QA_BSP_SURFACE_PATCH && (value.patch_width < 3 || value.patch_height < 3
        || value.patch_width % 2 == 0 || value.patch_height % 2 == 0
        || (uint64_t)(uint32_t)value.patch_width * (uint32_t)value.patch_height != value.vertices.count))
        return fail(error, QA_ERROR_FORMAT, r.file_offset, "invalid patch control grid");
    if (!value.triangle_fan && (value.type == QA_BSP_SURFACE_PLANAR || value.type == QA_BSP_SURFACE_TRIANGLES)
        && value.indices.count % 3 != 0)
        return fail(error, QA_ERROR_FORMAT, r.file_offset, "triangle index count is not a multiple of three");
    if (!end_record(&r, error)) return false;
    *out = value;
    return true;
}

bool qa_bsp_read_index(const qa_bsp_view *map, qa_bsp_lump_kind kind, size_t index, int64_t *out, qa_error *error)
{
    if (kind != QA_BSP_LEAF_FACES && kind != QA_BSP_LEAF_BRUSHES && kind != QA_BSP_SURFEDGES && kind != QA_BSP_INDICES)
        return fail(error, QA_ERROR_ARGUMENT, 0, "lump is not an index array");
    record_reader r;
    if (!begin_record(map, kind, index, out, &r, error)) return false;
    if (r.bytes.size == 2) *out = r_u16(&r);
    else if (kind == QA_BSP_SURFEDGES || map->family == QA_BSP_Q3) *out = r_i32(&r);
    else *out = r_u32(&r);
    return true;
}

bool qa_bsp_read_area(const qa_bsp_view *map, size_t index, qa_bsp_area *out, qa_error *error)
{
    record_reader r;
    if (!begin_record(map,QA_BSP_AREAS,index,out,&r,error)) return false;
    qa_bsp_area area;
    area.portals.count = r_u32(&r); area.portals.first = r_u32(&r);
    *out = area;
    return true;
}

bool qa_bsp_read_area_portal(const qa_bsp_view *map, size_t index, qa_bsp_area_portal *out, qa_error *error)
{
    record_reader r;
    if (!begin_record(map,QA_BSP_AREA_PORTALS,index,out,&r,error)) return false;
    qa_bsp_area_portal portal;
    portal.portal = r_u32(&r); portal.other_area = r_u32(&r);
    *out = portal;
    return true;
}

bool qa_bsp_read_grid_point(const qa_bsp_view *map, size_t index, qa_bsp_grid_point *out, qa_error *error)
{
    record_reader r;
    if (!begin_record(map,QA_BSP_LIGHTGRID,index,out,&r,error)) return false;
    memcpy(out->ambient,r.bytes.data,3); memcpy(out->directed,r.bytes.data + 3,3); memcpy(out->lat_long,r.bytes.data + 6,2);
    return true;
}

bool qa_bsp_decode_rle(qa_bytes input, int64_t offset, uint8_t *output,
                        size_t output_size, qa_error *error)
{
    if ((output_size > 0 && output == NULL) || (input.size > 0 && input.data == NULL))
        return fail(error, QA_ERROR_ARGUMENT, 0, "invalid visibility buffer");
    if (offset == -1) { if (output_size > 0) memset(output, 255, output_size); return true; }
    if (offset < 0 || (uint64_t)offset > input.size)
        return fail(error, QA_ERROR_FORMAT, 0, "invalid visibility offset");
    size_t source = (size_t)offset, target = 0;
    while (target < output_size) {
        if (source >= input.size)
            return fail(error, QA_ERROR_FORMAT, source, "truncated visibility row");
        uint8_t value = input.data[source++];
        if (value != 0) { output[target++] = value; continue; }
        if (source >= input.size)
            return fail(error, QA_ERROR_FORMAT, source, "truncated visibility zero run");
        uint8_t run = input.data[source++];
        if (run == 0 || run > output_size - target)
            return fail(error, QA_ERROR_FORMAT, source - 1, "invalid visibility zero run");
        memset(output + target, 0, run);
        target += run;
    }
    return true;
}

bool qa_bsp_visibility(const qa_bsp_view *map, int32_t selector, bool phs,
                       uint32_t fallback_clusters, uint8_t *output,
                       size_t capacity, size_t *written, qa_error *error)
{
    if (map == NULL || written == NULL || (capacity > 0 && output == NULL))
        return fail(error, QA_ERROR_ARGUMENT, 0, "invalid visibility request");
    qa_bsp_lump vis = map->lumps[QA_BSP_VISIBILITY];
    size_t width;
    int64_t offset = -1;
    if (map->family == QA_BSP_Q1) {
        if (phs) return fail(error, QA_ERROR_UNSUPPORTED, 0, "Q1 BSP has no stored PHS");
        qa_bsp_leaf leaf;
        if (selector < 0) return fail(error, QA_ERROR_ARGUMENT, 0, "invalid Q1 visibility leaf");
        if (!qa_bsp_read_leaf(map, (size_t)selector, &leaf, error)) return false;
        size_t leaves = qa_bsp_record_count(map, QA_BSP_LEAVES);
        size_t count = leaves == 0 ? 0 : leaves - 1;
        if (qa_bsp_record_count(map, QA_BSP_MODELS) > 0) {
            qa_bsp_model model;
            if (!qa_bsp_read_model(map, 0, &model, error)) return false;
            if (model.visible_leaves < 0)
                return fail(error, QA_ERROR_FORMAT, map->lumps[QA_BSP_MODELS].offset + 52, "negative visible leaf count");
            count = (size_t)model.visible_leaves;
        }
        width = (count + 7) / 8;
        if (selector != 0 && vis.bytes.size > 0) offset = leaf.visibility_offset;
    } else if (map->family == QA_BSP_Q2) {
        uint32_t count = vis.bytes.size > 0 ? qa_load_u32le(vis.bytes.data) : fallback_clusters;
        width = (size_t)(((uint64_t)count + 7) / 8);
        if (selector < -1 || (selector >= 0 && vis.bytes.size > 0 && (uint32_t)selector >= count))
            return fail(error, QA_ERROR_ARGUMENT, 0, "invalid Q2 visibility cluster");
        if (selector >= 0 && vis.bytes.size > 0)
            offset = qa_load_i32le(vis.bytes.data + 4 + (size_t)selector * 8 + (phs ? 4 : 0));
    } else {
        if (phs) return fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 BSP has no stored PHS");
        uint32_t count = vis.bytes.size > 0 ? qa_load_u32le(vis.bytes.data) : fallback_clusters;
        width = vis.bytes.size > 0 ? qa_load_u32le(vis.bytes.data + 4) : (size_t)(((uint64_t)count + 7) / 8);
        if (selector < -1 || (selector >= 0 && vis.bytes.size > 0 && (uint32_t)selector >= count))
            return fail(error, QA_ERROR_ARGUMENT, 0, "invalid Q3 visibility cluster");
        if (selector >= 0 && vis.bytes.size > 0) offset = (int64_t)(8 + (size_t)selector * width);
    }
    *written = width;
    if (width > capacity) return fail(error, QA_ERROR_ARGUMENT, 0, "visibility output is too small");
    if (width == 0) return true;
    if (map->family == QA_BSP_Q2 && selector == -1) { memset(output, 0, width); return true; }
    if (map->family == QA_BSP_Q3 && offset >= 0) {
        memcpy(output, vis.bytes.data + (size_t)offset, width);
        return true;
    }
    return qa_bsp_decode_rle(vis.bytes, offset, output, width, error);
}

typedef enum token_kind { TOKEN_END, TOKEN_TEXT, TOKEN_OPEN, TOKEN_CLOSE } token_kind;
typedef struct entity_token { token_kind kind; qa_bytes text; size_t offset; } entity_token;
typedef struct entity_parser { qa_bytes text; size_t offset; qa_entity_syntax syntax; } entity_parser;

static bool entity_token_next(entity_parser *parser, bool allow_newline,
                               entity_token *out, qa_error *error)
{
    const uint8_t *text = parser->text.data;
    size_t length = parser->text.size;
    bool newline = false;
    for (;;) {
        while (parser->offset < length && text[parser->offset] <= 32) {
            uint8_t byte = text[parser->offset++];
            newline |= byte == '\n' || byte == '\r';
        }
        if (parser->offset < length && length - parser->offset >= 2 && text[parser->offset] == '/') {
            if (text[parser->offset + 1] == '/') {
                parser->offset += 2;
                while (parser->offset < length && text[parser->offset] != '\n' && text[parser->offset] != '\r')
                    ++parser->offset;
                continue;
            }
            if (parser->syntax == QA_ENTITY_Q3 && text[parser->offset + 1] == '*') {
                parser->offset += 2;
                while (parser->offset < length) {
                    if (length - parser->offset >= 2 && text[parser->offset] == '*' && text[parser->offset + 1] == '/') {
                        parser->offset += 2;
                        break;
                    }
                    uint8_t byte = text[parser->offset++];
                    newline |= byte == '\n' || byte == '\r';
                }
                continue;
            }
        }
        break;
    }
    *out = (entity_token){.kind = TOKEN_END, .offset = parser->offset};
    if ((!allow_newline && newline) || parser->offset == length) return true;
    size_t start = parser->offset;
    uint8_t first = text[parser->offset++];
    if (parser->syntax == QA_ENTITY_Q1 && (first == '{' || first == '}')) {
        out->kind = first == '{' ? TOKEN_OPEN : TOKEN_CLOSE;
        return true;
    }
    if (first == '"') {
        start = parser->offset;
        while (parser->offset < length && text[parser->offset] != '"') ++parser->offset;
        out->text = (qa_bytes){text + start, parser->offset - start};
        if (parser->offset < length) ++parser->offset;
        else if (parser->syntax == QA_ENTITY_Q1)
            return fail(error, QA_ERROR_FORMAT, start, "unterminated entity quote");
    } else {
        while (parser->offset < length && text[parser->offset] > 32) {
            uint8_t byte = text[parser->offset];
            if (parser->syntax == QA_ENTITY_Q1 && (byte == '{' || byte == '}')) break;
            ++parser->offset;
        }
        out->text = (qa_bytes){text + start, parser->offset - start};
    }
    out->kind = TOKEN_TEXT;
    if (parser->syntax == QA_ENTITY_Q3) {
        if (out->text.size >= 1024)
            return fail(error, QA_ERROR_FORMAT, start, "Q3 entity token exceeds 1023 bytes");
        if (out->text.size == 1 && out->text.data[0] == '{') out->kind = TOKEN_OPEN;
        if (out->text.size == 1 && out->text.data[0] == '}') out->kind = TOKEN_CLOSE;
    }
    return true;
}

static bool entity_pass(qa_bytes text, qa_entity_syntax syntax, qa_entities *result,
                        bool store, qa_error *error)
{
    entity_parser parser = {text, 0, syntax};
    size_t entities = 0, properties = 0;
    for (;;) {
        entity_token token;
        if (!entity_token_next(&parser, true, &token, error)) return false;
        if (token.kind == TOKEN_END) break;
        if (token.kind != TOKEN_OPEN)
            return fail(error, QA_ERROR_FORMAT, token.offset, "expected opening entity brace");
        size_t first = properties;
        for (;;) {
            entity_token key, value;
            if (!entity_token_next(&parser, true, &key, error)) return false;
            if (key.kind == TOKEN_CLOSE) break;
            if (key.kind != TOKEN_TEXT && !(syntax == QA_ENTITY_Q3 && key.kind == TOKEN_OPEN))
                return fail(error, QA_ERROR_FORMAT, key.offset, "expected entity key or closing brace");
            if (!entity_token_next(&parser, syntax != QA_ENTITY_Q3, &value, error)) return false;
            if (value.kind != TOKEN_TEXT && !(syntax == QA_ENTITY_Q3 && value.kind == TOKEN_OPEN))
                return fail(error, QA_ERROR_FORMAT, value.offset, "missing entity value");
            if (store) result->properties[properties] = (qa_entity_property){key.text, value.text};
            ++properties;
        }
        if (store) result->records[entities] = (qa_entity_record){first, properties - first};
        ++entities;
    }
    result->count = entities;
    result->property_count = properties;
    return true;
}

bool qa_entities_parse(qa_bytes text, qa_entity_syntax syntax, qa_entities *out, qa_error *error)
{
    if (out == NULL || (text.size > 0 && text.data == NULL) || (syntax != QA_ENTITY_Q1 && syntax != QA_ENTITY_Q3))
        return fail(error, QA_ERROR_ARGUMENT, 0, "invalid entity parser request");
    if (text.size > 0) text = string_span(text.data, text.size);
    qa_entities result = {0};
    if (!entity_pass(text, syntax, &result, false, error)) return false;
    if (result.count > SIZE_MAX / sizeof(*result.records)
        || result.property_count > SIZE_MAX / sizeof(*result.properties))
        return fail(error, QA_ERROR_MEMORY, 0, "entity table is too large");
    if (result.count > 0) result.records = malloc(result.count * sizeof(*result.records));
    if (result.property_count > 0) result.properties = malloc(result.property_count * sizeof(*result.properties));
    if ((result.count > 0 && result.records == NULL) || (result.property_count > 0 && result.properties == NULL)) {
        qa_entities_free(&result);
        return fail(error, QA_ERROR_MEMORY, 0, "allocating entity tables");
    }
    if (!entity_pass(text, syntax, &result, true, error)) { qa_entities_free(&result); return false; }
    *out = result;
    return true;
}

void qa_entities_free(qa_entities *entities)
{
    if (entities == NULL) return;
    free(entities->records);
    free(entities->properties);
    *entities = (qa_entities){0};
}

bool qa_entity_value(const qa_entities *entities, size_t entity, const char *key, qa_bytes *out)
{
    if (entities == NULL || key == NULL || out == NULL || entity >= entities->count) return false;
    qa_bytes wanted = {(const uint8_t *)key, strlen(key)};
    qa_entity_record record = entities->records[entity];
    for (size_t i = record.property_count; i > 0; --i) {
        qa_entity_property property = entities->properties[record.first_property + i - 1];
        if (same_bytes(property.key, wanted)) { *out = property.value; return true; }
    }
    return false;
}

bool qa_bsp_texture_count(const qa_bsp_view *map, size_t *out, qa_error *error)
{
    if (map == NULL || out == NULL || map->family != QA_BSP_Q1)
        return fail(error, QA_ERROR_ARGUMENT, 0, "texture table requires a Q1 map");
    qa_bsp_lump lump = map->lumps[QA_BSP_TEXTURES];
    if (lump.bytes.size == 0) { *out = 0; return true; }
    if (lump.bytes.size < 4) return fail(error, QA_ERROR_FORMAT, lump.offset, "truncated texture table");
    int32_t count = qa_load_i32le(lump.bytes.data);
    if (count < 0 || (uint32_t)count > (lump.bytes.size - 4) / 4)
        return fail(error, QA_ERROR_FORMAT, lump.offset, "invalid texture count");
    *out = (size_t)count;
    return true;
}

bool qa_bsp_read_texture(const qa_bsp_view *map, size_t index, qa_bsp_texture *out, qa_error *error)
{
    size_t count;
    if (out == NULL) return fail(error, QA_ERROR_ARGUMENT, 0, "missing texture output");
    if (!qa_bsp_texture_count(map, &count, error)) return false;
    if (index >= count) return fail(error, QA_ERROR_ARGUMENT, 0, "texture index out of bounds");
    qa_bsp_lump lump = map->lumps[QA_BSP_TEXTURES];
    int32_t stored = qa_load_i32le(lump.bytes.data + 4 + index * 4);
    qa_bsp_texture texture = {0};
    if (stored == -1) { *out = texture; return true; }
    size_t header = map->format == QA_BSP_QUAKE64 ? 44 : 40;
    if (stored < 0 || (size_t)stored < 4 + count * 4 || (size_t)stored > lump.bytes.size
        || header > lump.bytes.size - (size_t)stored)
        return fail(error, QA_ERROR_FORMAT, lump.offset + 4 + index * 4, "invalid texture header offset");
    size_t offset = (size_t)stored;
    const uint8_t *p = lump.bytes.data + offset;
    texture.name = string_span(p, 16);
    texture.width = qa_load_u32le(p + 16); texture.height = qa_load_u32le(p + 20);
    if (texture.width == 0 || texture.height == 0)
        return fail(error, QA_ERROR_FORMAT, lump.offset + offset, "zero-sized mip texture");
    bool quake64 = map->format == QA_BSP_QUAKE64, external = !quake64;
    if (quake64) texture.quake64_shift = qa_load_u32le(p + 24);
    for (size_t i = 0; i < 4; ++i) {
        texture.mip_offsets[i] = qa_load_u32le(p + header - 16 + i * 4);
        external &= texture.mip_offsets[i] == 0;
    }
    texture.storage = external ? QA_BSP_TEXTURE_EXTERNAL : QA_BSP_TEXTURE_EMBEDDED;
    if (!external) for (size_t i = 0; i < 4; ++i) {
        size_t mip = quake64 && i == 0 ? header : texture.mip_offsets[i];
        if (quake64 && mip == 0) continue;
        uint64_t pixels = (uint64_t)(texture.width >> i) * (texture.height >> i);
        if (mip < header || mip > lump.bytes.size - offset || pixels > lump.bytes.size - offset - mip)
            return fail(error, QA_ERROR_FORMAT, lump.offset + offset, "invalid mip pixel range");
        texture.levels[i] = (qa_bytes){p + mip, (size_t)pixels};
    }
    *out = texture;
    return true;
}

bool qa_bsp_select_lighting(const qa_bsp_view *map, qa_bytes lit, qa_bsp_lighting *out, qa_error *error)
{
    if (map == NULL || out == NULL || (lit.size > 0 && lit.data == NULL))
        return fail(error, QA_ERROR_ARGUMENT, 0, "invalid lighting request");
    qa_bytes samples = map->lumps[QA_BSP_LIGHTING].bytes;
    bool packed = map->format == QA_BSP_QUAKE64;
    if (packed && samples.size % 2 != 0)
        return fail(error, QA_ERROR_FORMAT, map->lumps[QA_BSP_LIGHTING].offset, "incomplete Quake64 light sample");
    size_t source_count = packed ? samples.size / 2 : samples.size;
    qa_bsp_light_encoding encoding = map->family == QA_BSP_Q1
        ? packed ? QA_BSP_LIGHT_QUAKE64 : QA_BSP_LIGHT_LUMINANCE : QA_BSP_LIGHT_RGB8;
    if (map->family == QA_BSP_Q1) {
        qa_bsp_extension rgb;
        bool replacement = false;
        if (lit.size > 0) {
            if (lit.size < 8 || memcmp(lit.data, "QLIT", 4) != 0 || qa_load_u32le(lit.data + 4) != 1)
                return fail(error, QA_ERROR_FORMAT, 0, "invalid QLIT version 1 file");
            samples = (qa_bytes){lit.data + 8, lit.size - 8}; replacement = true;
        } else if (qa_bsp_find_extension(map, "RGBLIGHTING", &rgb, NULL)) {
            samples = rgb.bytes; replacement = true;
        }
        if (replacement) {
            if (samples.size % 3 != 0 || (source_count > 0 && samples.size / 3 != source_count))
                return fail(error, QA_ERROR_FORMAT, 0, "RGB sample count differs from BSP lighting");
            encoding = QA_BSP_LIGHT_RGB8;
        }
    } else if (lit.size > 0) return fail(error, QA_ERROR_ARGUMENT, 0, "QLIT replacement requires Q1");
    *out = (qa_bsp_lighting){encoding, samples, samples.size / (encoding == QA_BSP_LIGHT_RGB8 ? 3 : encoding == QA_BSP_LIGHT_QUAKE64 ? 2 : 1)};
    return true;
}

bool qa_bsp_light_sample(const qa_bsp_lighting *lighting, size_t index, uint8_t rgb[3], qa_error *error)
{
    if (lighting == NULL || rgb == NULL || index >= lighting->sample_count)
        return fail(error, QA_ERROR_ARGUMENT, 0, "light sample index out of bounds");
    const uint8_t *p = lighting->samples.data;
    switch (lighting->encoding) {
    case QA_BSP_LIGHT_LUMINANCE: rgb[0] = rgb[1] = rgb[2] = p[index]; break;
    case QA_BSP_LIGHT_RGB8: memcpy(rgb, p + index * 3, 3); break;
    case QA_BSP_LIGHT_QUAKE64: {
        uint8_t a = p[index * 2], b = p[index * 2 + 1];
        rgb[0] = a & 0xf8u;
        rgb[1] = (uint8_t)(((a & 7u) << 5) | ((b & 0xc0u) >> 5));
        rgb[2] = (uint8_t)((b & 0x3fu) << 2);
        break;
    }
    }
    return true;
}

bool qa_bsp_read_decoupled_lightmap(const qa_bsp_view *map, size_t index, qa_bsp_decoupled_lightmap *out, qa_error *error)
{
    if (map == NULL || out == NULL) return fail(error, QA_ERROR_ARGUMENT, 0, "invalid lightmap request");
    qa_bsp_extension extension;
    if (!qa_bsp_find_extension(map, "DECOUPLED_LM", &extension, error)) return false;
    size_t faces = qa_bsp_record_count(map, QA_BSP_FACES);
    if (extension.bytes.size % 40 != 0 || extension.bytes.size / 40 < faces
        || (map->family == QA_BSP_Q1 && extension.bytes.size / 40 != faces))
        return fail(error, QA_ERROR_FORMAT, extension.offset, "decoupled lightmap count differs from faces");
    if (index >= faces) return fail(error, QA_ERROR_ARGUMENT, extension.offset, "lightmap index out of bounds");
    record_reader r = {{extension.bytes.data + index * 40,40},0,SIZE_MAX,extension.offset + index * 40};
    qa_bsp_decoupled_lightmap result = {0};
    result.width = r_u16(&r); result.height = r_u16(&r);
    uint32_t offset = r_u32(&r);
    result.lighting_offset = offset == UINT32_MAX ? -1 : (int64_t)offset;
    if (map->family == QA_BSP_Q2 && result.lighting_offset >= (int64_t)map->lumps[QA_BSP_LIGHTING].bytes.size) {
        result.lighting_offset = -1;
        result.ignored_lighting_offset = true;
    }
    for (size_t axis = 0; axis < 2; ++axis)
        for (size_t i = 0; i < 4; ++i) result.projection[axis][i] = r_float(&r);
    if (!end_record(&r, error)) return false;
    *out = result;
    return true;
}

bool qa_bsp_read_face_normals(const qa_bsp_view *map, qa_bsp_face_normals *out, qa_error *error)
{
    if (map == NULL || out == NULL) return fail(error, QA_ERROR_ARGUMENT, 0, "invalid face normals request");
    qa_bsp_extension extension;
    if (!qa_bsp_find_extension(map, "FACENORMALS", &extension, error)) return false;
    if (extension.bytes.size < 4) return fail(error, QA_ERROR_FORMAT, extension.offset, "truncated normals header");
    size_t count = qa_load_u32le(extension.bytes.data), corners = 0;
    if (count > (extension.bytes.size - 4) / 12)
        return fail(error, QA_ERROR_FORMAT, extension.offset, "truncated face normal vectors");
    for (size_t i = 0; i < qa_bsp_record_count(map, QA_BSP_FACES); ++i) {
        qa_bsp_face face;
        if (!qa_bsp_read_face(map, i, &face, error)) return false;
        if (face.edges.count > SIZE_MAX - corners) return fail(error, QA_ERROR_FORMAT, extension.offset, "too many face corners");
        corners += face.edges.count;
    }
    size_t indices = 4 + count * 12;
    if (corners > (extension.bytes.size - indices) / 12)
        return fail(error, QA_ERROR_FORMAT, extension.offset, "truncated face normal indices");
    qa_bsp_face_normals result = {{extension.bytes.data + 4,count * 12},
        {extension.bytes.data + indices,corners * 12},count,corners};
    record_reader r = {result.vectors,0,SIZE_MAX,extension.offset + 4};
    for (size_t i = 0; i < count; ++i) (void)r_vector(&r);
    if (!end_record(&r, error)) return false;
    for (size_t corner = 0; corner < corners; ++corner)
        for (size_t component = 0; component < (map->family == QA_BSP_Q1 ? 3u : 1u); ++component)
            if (qa_load_u32le(result.corner_indices.data + corner * 12 + component * 4) >= count)
                return fail(error, QA_ERROR_FORMAT, extension.offset + indices + corner * 12, "invalid face normal index");
    *out = result;
    return true;
}

bool qa_bsp_face_corner_normal(const qa_bsp_face_normals *normals, size_t corner, unsigned component,
                                qa_bsp_vec3 *out, qa_error *error)
{
    if (normals == NULL || out == NULL || corner >= normals->corner_count || component > 2)
        return fail(error, QA_ERROR_ARGUMENT, 0, "invalid face corner request");
    size_t index = qa_load_u32le(normals->corner_indices.data + corner * 12 + component * 4);
    if (index >= normals->vector_count) return fail(error, QA_ERROR_FORMAT, 0, "invalid face corner vector");
    record_reader r = {{normals->vectors.data + index * 12,12},0,SIZE_MAX,index * 12};
    *out = r_vector(&r);
    return end_record(&r, error);
}

static qa_bytes optional_extension(const qa_bsp_view *map, const char *name)
{
    qa_bsp_extension extension;
    return qa_bsp_find_extension(map, name, &extension, NULL) ? extension.bytes : (qa_bytes){0};
}

bool qa_bsp_read_q1_metadata(const qa_bsp_view *map, qa_bsp_q1_metadata *out, qa_error *error)
{
    if (map == NULL || out == NULL || map->family != QA_BSP_Q1)
        return fail(error, QA_ERROR_ARGUMENT, 0, "metadata requires Q1 BSP");
    qa_bsp_q1_metadata result = {
        .shifts = optional_extension(map,"LMSHIFT"), .offsets = optional_extension(map,"LMOFFSET"),
        .styles = optional_extension(map,"LMSTYLE"), .styles16 = optional_extension(map,"LMSTYLE16"),
        .hdr_lighting = optional_extension(map,"LIGHTING_E5BGR9"), .directions = optional_extension(map,"LIGHTINGDIR"),
        .vertex_normals = optional_extension(map,"VERTEXNORMALS")
    };
    size_t faces = qa_bsp_record_count(map, QA_BSP_FACES), vertices = qa_bsp_record_count(map, QA_BSP_VERTICES);
    if ((result.shifts.data != NULL && result.shifts.size != faces)
        || (result.offsets.data != NULL && (result.offsets.size % 4 != 0 || result.offsets.size / 4 != faces))
        || (result.vertex_normals.data != NULL && (result.vertex_normals.size % 12 != 0 || result.vertex_normals.size / 12 != vertices))
        || result.hdr_lighting.size % 4 != 0)
        return fail(error, QA_ERROR_FORMAT, 0, "BSPX metadata count differs from geometry");
    if (faces == 0) {
        if (result.styles.size > 0 || result.styles16.size > 0)
            return fail(error, QA_ERROR_FORMAT, 0, "light styles with no faces");
    } else {
        if (result.styles.size % faces != 0 || result.styles16.size % 2 != 0 || (result.styles16.size / 2) % faces != 0)
            return fail(error, QA_ERROR_FORMAT, 0, "light style count differs from faces");
        result.styles_per_face = result.styles.size / faces;
        result.styles16_per_face = result.styles16.size / 2 / faces;
    }
    record_reader r = {result.vertex_normals,0,SIZE_MAX,0};
    for (size_t i = 0; i < result.vertex_normals.size / 12; ++i) (void)r_vector(&r);
    if (!end_record(&r, error)) return false;
    *out = result;
    return true;
}

static bool require_bytes(const record_reader *r, size_t count, qa_error *error)
{
    if (r->position > r->bytes.size || count > r->bytes.size - r->position)
        return fail(error, QA_ERROR_FORMAT, r->file_offset + r->position, "truncated BSP extension record");
    return true;
}

static bool allocate_array(size_t count, size_t width, void **out, qa_error *error)
{
    *out = NULL;
    if (count == 0) return true;
    if (count > SIZE_MAX / width) return fail(error, QA_ERROR_MEMORY, 0, "BSP table is too large");
    *out = calloc(count, width);
    return *out != NULL || fail(error, QA_ERROR_MEMORY, 0, "allocating BSP table");
}

static bool brush_list_pass(const qa_bsp_view *map, qa_bsp_extension extension,
                            qa_bsp_brush_list *list, bool store, qa_error *error)
{
    record_reader r = {extension.bytes,0,SIZE_MAX,extension.offset};
    size_t models = 0, brushes = 0, planes = 0;
    while (r.position < r.bytes.size) {
        if (!require_bytes(&r, 16, error)) return false;
        uint32_t version = r_u32(&r), model = r_u32(&r), count = r_u32(&r), expected_planes = r_u32(&r);
        if (version != 1) return fail(error, QA_ERROR_UNSUPPORTED, r.file_offset + r.position - 16, "unsupported BRUSHLIST version");
        if (model >= qa_bsp_record_count(map, QA_BSP_MODELS))
            return fail(error, QA_ERROR_FORMAT, r.file_offset + r.position - 12, "invalid brush model reference");
        if (count > (r.bytes.size - r.position) / 28)
            return fail(error, QA_ERROR_FORMAT, r.file_offset + r.position, "invalid brush list count");
        if (store) list->models[models] = (qa_bsp_brush_model){model,{(uint32_t)brushes,count}};
        ++models;
        size_t first_plane = planes;
        for (uint32_t b = 0; b < count; ++b) {
            if (!require_bytes(&r, 28, error)) return false;
            qa_bsp_extra_brush brush;
            brush.bounds = r_bounds(&r, BOUNDS_FLOAT); brush.contents = r_i16(&r);
            uint16_t count_planes = r_u16(&r);
            brush.planes = (qa_bsp_range){(uint32_t)planes,count_planes};
            if (!require_bytes(&r, (size_t)count_planes * 16, error)) return false;
            if (store) list->brushes[brushes] = brush;
            ++brushes;
            for (uint16_t p = 0; p < count_planes; ++p) {
                qa_bsp_plane plane;
                plane.normal = r_vector(&r); plane.distance = r_float(&r); plane.type = -1;
                if (store) list->planes[planes] = plane;
                ++planes;
            }
            if (planes - first_plane > expected_planes)
                return fail(error, QA_ERROR_FORMAT, r.file_offset + r.position, "brush planes exceed model total");
        }
        if (planes - first_plane != expected_planes)
            return fail(error, QA_ERROR_FORMAT, r.file_offset + r.position, "brush plane count differs from model total");
    }
    if (!end_record(&r, error)) return false;
    list->model_count = models; list->brush_count = brushes; list->plane_count = planes;
    return true;
}

bool qa_bsp_read_brush_list(const qa_bsp_view *map, qa_bsp_brush_list *out, qa_error *error)
{
    if (map == NULL || out == NULL || map->family != QA_BSP_Q1)
        return fail(error, QA_ERROR_ARGUMENT, 0, "brush list requires Q1");
    qa_bsp_extension extension;
    if (!qa_bsp_find_extension(map, "BRUSHLIST", &extension, error)) return false;
    qa_bsp_brush_list list = {0};
    if (!brush_list_pass(map, extension, &list, false, error)) return false;
    void *models = NULL, *brushes = NULL, *planes = NULL;
    bool allocated = allocate_array(list.model_count,sizeof(*list.models),&models,error)
        && allocate_array(list.brush_count,sizeof(*list.brushes),&brushes,error)
        && allocate_array(list.plane_count,sizeof(*list.planes),&planes,error);
    list.models = models; list.brushes = brushes; list.planes = planes;
    if (!allocated || !brush_list_pass(map, extension, &list, true, error)) { qa_bsp_brush_list_free(&list); return false; }
    *out = list;
    return true;
}

void qa_bsp_brush_list_free(qa_bsp_brush_list *list)
{
    if (list == NULL) return;
    free(list->models); free(list->brushes); free(list->planes);
    *list = (qa_bsp_brush_list){0};
}

static bool lightgrid_leaves(record_reader r, qa_bsp_lightgrid *grid, bool store, qa_error *error)
{
    size_t samples = 0;
    for (size_t i = 0; i < grid->leaf_count; ++i) {
        if (!require_bytes(&r, 24, error)) return false;
        qa_bsp_lightgrid_leaf leaf;
        for (size_t axis = 0; axis < 3; ++axis) leaf.min[axis] = r_u32(&r);
        for (size_t axis = 0; axis < 3; ++axis) leaf.size[axis] = r_u32(&r);
        size_t points = 1;
        for (size_t axis = 0; axis < 3; ++axis) {
            if (leaf.size[axis] != 0 && points > SIZE_MAX / leaf.size[axis])
                return fail(error, QA_ERROR_FORMAT, r.file_offset + r.position, "lightgrid leaf is too large");
            points *= leaf.size[axis];
        }
        if (points > r.bytes.size - r.position || points > (SIZE_MAX - samples) / grid->style_count)
            return fail(error, QA_ERROR_FORMAT, r.file_offset + r.position, "invalid lightgrid point count");
        leaf.first_sample = samples;
        if (store) grid->leaves[i] = leaf;
        for (size_t point = 0; point < points; ++point) {
            if (!require_bytes(&r, 1, error)) return false;
            uint8_t count = r.bytes.data[r.position++];
            if (count != 255 && count > grid->style_count)
                return fail(error, QA_ERROR_FORMAT, r.file_offset + r.position - 1, "too many lightgrid sample styles");
            size_t present = count == 255 ? 0 : count;
            if (!require_bytes(&r, present * 4, error)) return false;
            for (size_t style = 0; style < grid->style_count; ++style) {
                qa_bsp_lightgrid_sample sample = {255,{255,255,255}};
                if (style < present) {
                    sample.style = r.bytes.data[r.position++];
                    memcpy(sample.rgb, r.bytes.data + r.position, 3); r.position += 3;
                }
                if (store) grid->samples[samples] = sample;
                ++samples;
            }
        }
    }
    grid->sample_count = samples;
    return true;
}

static bool lightgrid_tree(const qa_bsp_lightgrid *grid, qa_error *error)
{
    if (grid->leaf_count == 0) return true;
    size_t node_count = grid->nodes.size / 44;
    if (node_count > (SIZE_MAX - 1) / 8)
        return fail(error, QA_ERROR_MEMORY, 0, "lightgrid traversal is too large");
    void *seen_storage = NULL, *stack_storage = NULL;
    if (!allocate_array(node_count,1,&seen_storage,error)
        || !allocate_array(node_count * 8 + 1,sizeof(uint32_t),&stack_storage,error)) {
        free(seen_storage); free(stack_storage); return false;
    }
    uint8_t *seen = seen_storage;
    uint32_t *stack = stack_storage;
    size_t pending = 1;
    stack[0] = grid->root;
    bool valid = true;
    while (pending > 0 && valid) {
        uint32_t next = stack[--pending];
        if ((next & UINT32_C(0x40000000)) != 0) continue;
        if ((next & UINT32_C(0x80000000)) != 0) {
            if ((next & UINT32_C(0x7fffffff)) >= grid->leaf_count)
                valid = fail(error, QA_ERROR_FORMAT, 37, "invalid lightgrid leaf reference");
        } else {
            if (next >= node_count || seen[next]) {
                valid = fail(error, QA_ERROR_FORMAT, 37, "invalid or repeated lightgrid node");
                break;
            }
            seen[next] = 1;
            const uint8_t *node = grid->nodes.data + (size_t)next * 44;
            for (size_t child = 0; child < 8; ++child) stack[pending++] = qa_load_u32le(node + 12 + child * 4);
        }
    }
    free(seen_storage); free(stack_storage);
    return valid;
}

bool qa_bsp_read_lightgrid(const qa_bsp_view *map, qa_bsp_lightgrid *out, qa_error *error)
{
    if (map == NULL || out == NULL) return fail(error, QA_ERROR_ARGUMENT, 0, "invalid lightgrid request");
    qa_bsp_extension extension;
    if (!qa_bsp_find_extension(map, "LIGHTGRID_OCTREE", &extension, error)) return false;
    record_reader r = {extension.bytes,0,SIZE_MAX,extension.offset};
    if (!require_bytes(&r, 45, error)) return false;
    qa_bsp_lightgrid grid = {0};
    grid.spacing = r_vector(&r);
    for (size_t i = 0; i < 3; ++i) grid.size[i] = r_u32(&r);
    grid.min = r_vector(&r); grid.style_count = r.bytes.data[r.position++];
    grid.root = r_u32(&r);
    size_t nodes = r_u32(&r);
    if (!end_record(&r, error)) return false;
    if (grid.spacing.x <= 0 || grid.spacing.y <= 0 || grid.spacing.z <= 0 || grid.style_count < 1 || grid.style_count > 4)
        return fail(error, QA_ERROR_FORMAT, extension.offset, "invalid lightgrid spacing or style count");
    if (nodes > (r.bytes.size - r.position) / 44)
        return fail(error, QA_ERROR_FORMAT, extension.offset + r.position, "truncated lightgrid nodes");
    grid.nodes = (qa_bytes){r.bytes.data + r.position,nodes * 44}; r.position += nodes * 44;
    if (!require_bytes(&r, 4, error)) return false;
    grid.leaf_count = r_u32(&r);
    if (grid.leaf_count > (r.bytes.size - r.position) / 24)
        return fail(error, QA_ERROR_FORMAT, extension.offset + r.position, "invalid lightgrid leaf count");
    if (!lightgrid_leaves(r, &grid, false, error) || !lightgrid_tree(&grid, error)) return false;
    void *leaves = NULL, *samples = NULL;
    bool allocated = allocate_array(grid.leaf_count,sizeof(*grid.leaves),&leaves,error)
        && allocate_array(grid.sample_count,sizeof(*grid.samples),&samples,error);
    grid.leaves = leaves; grid.samples = samples;
    if (!allocated || !lightgrid_leaves(r, &grid, true, error)) { qa_bsp_lightgrid_free(&grid); return false; }
    *out = grid;
    return true;
}

void qa_bsp_lightgrid_free(qa_bsp_lightgrid *grid)
{
    if (grid == NULL) return;
    free(grid->leaves); free(grid->samples); *grid = (qa_bsp_lightgrid){0};
}

const qa_bsp_lightgrid_sample *qa_bsp_lightgrid_lookup(const qa_bsp_lightgrid *grid, const int64_t point[3])
{
    if (grid == NULL || point == NULL) return NULL;
    uint32_t next = grid->root;
    size_t remaining = grid->nodes.size / 44 + 1;
    while ((next & UINT32_C(0xc0000000)) == 0) {
        if (next >= grid->nodes.size / 44 || remaining-- == 0) return NULL;
        const uint8_t *node = grid->nodes.data + (size_t)next * 44;
        unsigned octant = 0;
        for (unsigned i = 0; i < 3; ++i)
            if (point[i] >= qa_load_u32le(node + i * 4)) octant |= 1u << (2 - i);
        next = qa_load_u32le(node + 12 + octant * 4);
    }
    if ((next & UINT32_C(0x40000000)) != 0) return NULL;
    size_t index = next & UINT32_C(0x7fffffff);
    if (index >= grid->leaf_count) return NULL;
    const qa_bsp_lightgrid_leaf *leaf = &grid->leaves[index];
    size_t relative[3];
    for (size_t i = 0; i < 3; ++i) {
        if (point[i] < leaf->min[i] || (uint64_t)point[i] - leaf->min[i] >= leaf->size[i]) return NULL;
        relative[i] = (size_t)((uint64_t)point[i] - leaf->min[i]);
    }
    size_t first = leaf->first_sample + ((size_t)leaf->size[0] * ((size_t)leaf->size[1] * relative[2] + relative[1]) + relative[0]) * grid->style_count;
    return grid->samples + first;
}

static bool reference(int64_t value, size_t count, size_t offset, const char *label, qa_error *error)
{
    if (value < 0 || (uint64_t)value >= count) {
        qa_error_set(error, QA_ERROR_FORMAT, offset, "invalid %s reference", label);
        return false;
    }
    return true;
}

static bool range_reference(qa_bsp_range range, size_t count, size_t offset, const char *label, qa_error *error)
{
    if (range.first > count || range.count > count - range.first) {
        qa_error_set(error, QA_ERROR_FORMAT, offset, "invalid %s range", label);
        return false;
    }
    return true;
}

static bool child_reference(const qa_bsp_view *map, int32_t child, size_t offset, qa_error *error)
{
    return child >= 0
        ? reference(child,qa_bsp_record_count(map,QA_BSP_NODES),offset,"node",error)
        : reference(-1 - (int64_t)child,qa_bsp_record_count(map,QA_BSP_LEAVES),offset,"leaf",error);
}

typedef struct tree_frame { uint32_t index; unsigned next_child; } tree_frame;

static bool validate_tree(const qa_bsp_view *map, bool clips, qa_error *error)
{
    qa_bsp_lump_kind kind = clips ? QA_BSP_CLIPNODES : QA_BSP_NODES;
    size_t count = qa_bsp_record_count(map,kind);
    if (count == 0) return true;
    void *color_storage = NULL, *stack_storage = NULL;
    if (!allocate_array(count,1,&color_storage,error)
        || !allocate_array(count,sizeof(tree_frame),&stack_storage,error)) {
        free(color_storage); free(stack_storage); return false;
    }
    uint8_t *colors = color_storage;
    tree_frame *stack = stack_storage;
    bool valid = true;
    for (size_t root = 0; root < count && valid; ++root) {
        if (colors[root] == 2) continue;
        size_t depth = 1;
        stack[0] = (tree_frame){(uint32_t)root,0}; colors[root] = 1;
        while (depth > 0 && valid) {
            tree_frame *frame = &stack[depth - 1];
            if (frame->next_child == 2) { colors[frame->index] = 2; --depth; continue; }
            int32_t children[2];
            if (clips) {
                qa_bsp_clipnode node;
                if (!qa_bsp_read_clipnode(map,frame->index,&node,error)) { valid = false; break; }
                memcpy(children,node.children,sizeof(children));
            } else {
                qa_bsp_node node;
                if (!qa_bsp_read_node(map,frame->index,&node,error)) { valid = false; break; }
                memcpy(children,node.children,sizeof(children));
            }
            int32_t child = children[frame->next_child++];
            if (child < 0) continue;
            size_t offset = map->lumps[kind].offset + (size_t)frame->index * map->lumps[kind].stride;
            if (!reference(child,count,offset,"tree child",error)) { valid = false; break; }
            if (colors[child] == 1) { valid = fail(error,QA_ERROR_FORMAT,offset,"cycle in BSP tree"); break; }
            if (colors[child] == 2) continue;
            colors[child] = 1;
            stack[depth++] = (tree_frame){(uint32_t)child,0};
        }
    }
    free(color_storage); free(stack_storage);
    return valid;
}

static bool validate_q3_surfaces(const qa_bsp_view *map, qa_error *error)
{
    size_t vertices = qa_bsp_record_count(map,QA_BSP_VERTICES);
    size_t fogs = qa_bsp_record_count(map,QA_BSP_FOGS), lightmaps = qa_bsp_record_count(map,QA_BSP_LIGHTING);
    void *storage = NULL;
    if (!allocate_array(vertices,1,&storage,error)) return false;
    uint8_t *lightmap_use = storage;
    bool valid = true;
    for (size_t i = 0; i < qa_bsp_record_count(map,QA_BSP_SURFACES) && valid; ++i) {
        qa_bsp_surface surface;
        if (!qa_bsp_read_surface(map,i,&surface,error)) { valid = false; break; }
        size_t offset = map->lumps[QA_BSP_SURFACES].offset + i * map->lumps[QA_BSP_SURFACES].stride;
        if (map->format == QA_BSP_IBSP46) {
            if (!reference(surface.shader,qa_bsp_record_count(map,QA_BSP_SHADERS),offset,"surface shader",error)) { valid = false; break; }
        } else if (surface.brush_side >= 0
            && !reference(surface.brush_side,qa_bsp_record_count(map,QA_BSP_BRUSH_SIDES),offset,"surface brush side",error)) { valid = false; break; }
        bool retail_flare = surface.type == QA_BSP_SURFACE_FLARE && surface.fog == 0 && fogs == 0;
        if (surface.fog != -1 && !retail_flare && !reference(surface.fog,fogs,offset,"surface fog",error)) { valid = false; break; }
        if (!range_reference(surface.vertices,vertices,offset,"surface vertices",error)
            || !range_reference(surface.indices,qa_bsp_record_count(map,QA_BSP_INDICES),offset,"surface indices",error)) { valid = false; break; }
        bool planar = surface.type == QA_BSP_SURFACE_PLANAR || surface.type == QA_BSP_SURFACE_PATCH;
        if (planar && surface.lightmap < -4) { valid = fail(error,QA_ERROR_FORMAT,offset,"invalid lightmap sentinel"); break; }
        bool lit = planar && surface.lightmap >= 0 && (size_t)surface.lightmap < lightmaps;
        for (size_t vertex = surface.vertices.first; vertex < (size_t)surface.vertices.first + surface.vertices.count; ++vertex) {
            if (lit) lightmap_use[vertex] = 2;
            else if (lightmap_use[vertex] != 2) lightmap_use[vertex] = 1;
        }
        for (size_t index = surface.indices.first; index < (size_t)surface.indices.first + surface.indices.count; ++index) {
            int64_t local;
            if (!qa_bsp_read_index(map,QA_BSP_INDICES,index,&local,error)
                || !reference(local,surface.vertices.count,map->lumps[QA_BSP_INDICES].offset + index * 4,"surface local vertex",error)) { valid = false; break; }
        }
    }
    for (size_t i = 0; i < vertices && valid; ++i) {
        if (lightmap_use[i] == 1) continue;
        qa_bsp_vertex vertex;
        if (!qa_bsp_read_vertex(map,i,&vertex,error)) { valid = false; break; }
        if (!isfinite(vertex.lightmap_coord[0]) || !isfinite(vertex.lightmap_coord[1]))
            valid = fail(error,QA_ERROR_FORMAT,map->lumps[QA_BSP_VERTICES].offset + i * 44 + 20,"non-finite used lightmap coordinate");
    }
    free(storage);
    return valid;
}

bool qa_bsp_validate(const qa_bsp_view *map, qa_error *error)
{
    if (map == NULL) return fail(error,QA_ERROR_ARGUMENT,0,"missing BSP view");
    size_t counts[QA_BSP_LUMP_COUNT];
    for (size_t i = 0; i < QA_BSP_LUMP_COUNT; ++i) counts[i] = qa_bsp_record_count(map,(qa_bsp_lump_kind)i);
    if (map->family == QA_BSP_Q2 && (counts[QA_BSP_MODELS] == 0 || counts[QA_BSP_NODES] == 0 || counts[QA_BSP_LEAVES] == 0))
        return fail(error,QA_ERROR_FORMAT,0,"Q2 map requires models, nodes and leaves");
    qa_entities entities = {0};
    if (!qa_entities_parse(map->lumps[QA_BSP_ENTITIES].bytes,map->family == QA_BSP_Q3 ? QA_ENTITY_Q3 : QA_ENTITY_Q1,&entities,error)) return false;
    qa_entities_free(&entities);
    for (size_t i = 0; i < counts[QA_BSP_PLANES]; ++i) {
        qa_bsp_plane plane;
        if (!qa_bsp_read_plane(map,i,&plane,error)) return false;
    }
    for (size_t i = 0; i < counts[QA_BSP_VERTICES]; ++i) {
        qa_bsp_vertex vertex;
        if (!qa_bsp_read_vertex(map,i,&vertex,error)) return false;
    }
    size_t texture_count = 0, q1_light_count = 0;
    bool extra_brushes = false;
    if (map->family == QA_BSP_Q1) {
        if (!qa_bsp_texture_count(map,&texture_count,error)) return false;
        for (size_t i = 0; i < texture_count; ++i) {
            qa_bsp_texture texture;
            if (!qa_bsp_read_texture(map,i,&texture,error)) return false;
        }
        qa_bsp_lighting lighting;
        qa_bsp_q1_metadata metadata;
        if (!qa_bsp_select_lighting(map,(qa_bytes){0},&lighting,error) || !qa_bsp_read_q1_metadata(map,&metadata,error)) return false;
        q1_light_count = lighting.sample_count;
        if (metadata.hdr_lighting.size / 4 > q1_light_count) q1_light_count = metadata.hdr_lighting.size / 4;
        qa_bsp_extension extension;
        if (qa_bsp_find_extension(map,"FACENORMALS",&extension,NULL)) {
            qa_bsp_face_normals normals;
            if (!qa_bsp_read_face_normals(map,&normals,error)) return false;
        }
        if (qa_bsp_find_extension(map,"DECOUPLED_LM",&extension,NULL)) {
            if (extension.bytes.size % 40 != 0 || extension.bytes.size / 40 != counts[QA_BSP_FACES])
                return fail(error,QA_ERROR_FORMAT,extension.offset,"decoupled lightmap count differs from faces");
            for (size_t i = 0; i < counts[QA_BSP_FACES]; ++i) {
                qa_bsp_decoupled_lightmap lightmap;
                if (!qa_bsp_read_decoupled_lightmap(map,i,&lightmap,error)) return false;
            }
        }
        if (qa_bsp_find_extension(map,"BRUSHLIST",&extension,NULL)) {
            qa_bsp_brush_list list = {0};
            qa_error local = {0};
            if (!qa_bsp_read_brush_list(map,&list,&local)) {
                if (local.code != QA_ERROR_UNSUPPORTED) { if (error != NULL) *error = local; return false; }
            } else { extra_brushes = true; qa_bsp_brush_list_free(&list); }
        }
    }
    for (size_t i = 0; i < counts[QA_BSP_EDGES]; ++i) {
        qa_bsp_edge edge;
        if (!qa_bsp_read_edge(map,i,&edge,error)) return false;
        for (size_t j = 0; j < 2; ++j)
            if (!reference(edge.vertices[j],counts[QA_BSP_VERTICES],map->lumps[QA_BSP_EDGES].offset + i * map->lumps[QA_BSP_EDGES].stride,"edge vertex",error)) return false;
    }
    const qa_bsp_lump_kind index_lumps[] = {QA_BSP_SURFEDGES,QA_BSP_LEAF_FACES,QA_BSP_LEAF_BRUSHES};
    for (size_t type = 0; type < 3; ++type) {
        qa_bsp_lump_kind kind = index_lumps[type];
        size_t total = type == 0 ? counts[QA_BSP_EDGES] : type == 1
            ? counts[map->family == QA_BSP_Q3 ? QA_BSP_SURFACES : QA_BSP_FACES] : counts[QA_BSP_BRUSHES];
        for (size_t i = 0; i < counts[kind]; ++i) {
            int64_t value;
            if (!qa_bsp_read_index(map,kind,i,&value,error)) return false;
            if (kind == QA_BSP_SURFEDGES && value < 0) value = -value;
            if (!reference(value,total,map->lumps[kind].offset + i * map->lumps[kind].stride,"BSP array index",error)) return false;
        }
    }
    for (size_t i = 0; i < counts[QA_BSP_TEXINFO]; ++i) {
        qa_bsp_texinfo texture;
        if (!qa_bsp_read_texinfo(map,i,&texture,error)) return false;
        size_t offset = map->lumps[QA_BSP_TEXINFO].offset + i * map->lumps[QA_BSP_TEXINFO].stride;
        if (map->family == QA_BSP_Q1 && texture_count > 0 && !reference(texture.texture,texture_count,offset,"mip texture",error)) return false;
        if (map->family == QA_BSP_Q2 && texture.next > 0 && !reference(texture.next,counts[QA_BSP_TEXINFO],offset,"animated texture",error)) return false;
    }
    for (size_t i = 0; i < counts[QA_BSP_FACES]; ++i) {
        qa_bsp_face face;
        if (!qa_bsp_read_face(map,i,&face,error)) return false;
        size_t offset = map->lumps[QA_BSP_FACES].offset + i * map->lumps[QA_BSP_FACES].stride;
        if (!reference(face.plane,counts[QA_BSP_PLANES],offset,"face plane",error)
            || !reference(face.texinfo,counts[QA_BSP_TEXINFO],offset,"face texture",error)
            || !range_reference(face.edges,counts[QA_BSP_SURFEDGES],offset,"face edges",error)) return false;
        if (map->family == QA_BSP_Q1) {
            if (face.lighting_offset != -1 && q1_light_count > 0 && !reference(face.lighting_offset,q1_light_count,offset,"face lighting",error)) return false;
        } else if (face.edges.count < 3 || face.lighting_offset < -1
            || (map->lumps[QA_BSP_LIGHTING].bytes.size > 0 && face.lighting_offset >= 0 && (size_t)face.lighting_offset >= map->lumps[QA_BSP_LIGHTING].bytes.size))
            return fail(error,QA_ERROR_FORMAT,offset,"invalid Q2 face edge count or light offset");
    }
    for (size_t i = 0; i < counts[QA_BSP_LEAVES]; ++i) {
        qa_bsp_leaf leaf;
        if (!qa_bsp_read_leaf(map,i,&leaf,error)) return false;
        size_t offset = map->lumps[QA_BSP_LEAVES].offset + i * map->lumps[QA_BSP_LEAVES].stride;
        if (!range_reference(leaf.faces,counts[QA_BSP_LEAF_FACES],offset,"leaf faces",error)
            || !range_reference(leaf.brushes,counts[QA_BSP_LEAF_BRUSHES],offset,"leaf brushes",error)) return false;
        qa_bytes vis = map->lumps[QA_BSP_VISIBILITY].bytes;
        if (map->family == QA_BSP_Q1) {
            if (leaf.visibility_offset != -1 && vis.size > 0 && !reference(leaf.visibility_offset,vis.size,offset,"leaf visibility",error)) return false;
        } else {
            if (leaf.cluster < -1 || (vis.size > 0 && leaf.cluster != -1 && !reference(leaf.cluster,qa_load_u32le(vis.data),offset,"leaf cluster",error)))
                return fail(error,QA_ERROR_FORMAT,offset,"invalid leaf cluster");
            if (map->family == QA_BSP_Q2) {
                if (i == 0 && leaf.contents != 1) return fail(error,QA_ERROR_FORMAT,offset,"Q2 leaf zero is not solid");
                if (!reference(leaf.area,counts[QA_BSP_AREAS],offset,"leaf area",error)) return false;
            } else if (leaf.area < -1) return fail(error,QA_ERROR_FORMAT,offset,"invalid leaf area");
        }
    }
    for (size_t i = 0; i < counts[QA_BSP_NODES]; ++i) {
        qa_bsp_node node;
        if (!qa_bsp_read_node(map,i,&node,error)) return false;
        size_t offset = map->lumps[QA_BSP_NODES].offset + i * map->lumps[QA_BSP_NODES].stride;
        if (!reference(node.plane,counts[QA_BSP_PLANES],offset,"node plane",error)
            || !range_reference(node.faces,counts[QA_BSP_FACES],offset,"node faces",error)
            || !child_reference(map,node.children[0],offset,error) || !child_reference(map,node.children[1],offset,error)) return false;
    }
    for (size_t i = 0; i < counts[QA_BSP_CLIPNODES]; ++i) {
        qa_bsp_clipnode node;
        if (!qa_bsp_read_clipnode(map,i,&node,error)) return false;
        size_t offset = map->lumps[QA_BSP_CLIPNODES].offset + i * map->lumps[QA_BSP_CLIPNODES].stride;
        if (!reference(node.plane,counts[QA_BSP_PLANES],offset,"clipnode plane",error)) return false;
        for (size_t child = 0; child < 2; ++child)
            if (node.children[child] >= 0 && !reference(node.children[child],counts[QA_BSP_CLIPNODES],offset,"clipnode child",error)) return false;
    }
    for (size_t i = 0; i < counts[QA_BSP_BRUSHES]; ++i) {
        qa_bsp_brush brush;
        if (!qa_bsp_read_brush(map,i,&brush,error)) return false;
        size_t offset = map->lumps[QA_BSP_BRUSHES].offset + i * map->lumps[QA_BSP_BRUSHES].stride;
        if (!range_reference(brush.sides,counts[QA_BSP_BRUSH_SIDES],offset,"brush sides",error)) return false;
        if (map->format == QA_BSP_IBSP46 && !reference(brush.shader,counts[QA_BSP_SHADERS],offset,"brush shader",error)) return false;
    }
    for (size_t i = 0; i < counts[QA_BSP_BRUSH_SIDES]; ++i) {
        qa_bsp_brush_side side;
        if (!qa_bsp_read_brush_side(map,i,&side,error)) return false;
        size_t offset = map->lumps[QA_BSP_BRUSH_SIDES].offset + i * map->lumps[QA_BSP_BRUSH_SIDES].stride;
        if (!reference(side.plane,counts[QA_BSP_PLANES],offset,"brush plane",error)) return false;
        if (map->family == QA_BSP_Q2 && side.texinfo != -1 && !reference(side.texinfo,counts[QA_BSP_TEXINFO],offset,"brush texture",error)) return false;
        if (map->format == QA_BSP_IBSP46 && !reference(side.shader,counts[QA_BSP_SHADERS],offset,"brush shader",error)) return false;
    }
    for (size_t i = 0; i < counts[QA_BSP_MODELS]; ++i) {
        qa_bsp_model model;
        if (!qa_bsp_read_model(map,i,&model,error)) return false;
        size_t offset = map->lumps[QA_BSP_MODELS].offset + i * map->lumps[QA_BSP_MODELS].stride;
        if (map->format != QA_BSP_IBSP44 && !range_reference(model.faces,counts[map->family == QA_BSP_Q3 ? QA_BSP_SURFACES : QA_BSP_FACES],offset,"model faces",error)) return false;
        if (map->format == QA_BSP_IBSP46) {
            if (!range_reference(model.brushes,counts[QA_BSP_BRUSHES],offset,"model brushes",error)) return false;
        } else if (!child_reference(map,model.headnodes[0],offset,error)) return false;
        if (map->family == QA_BSP_Q1) {
            size_t leaves = counts[QA_BSP_LEAVES] == 0 ? 0 : counts[QA_BSP_LEAVES] - 1;
            if (model.visible_leaves < 0 || (size_t)model.visible_leaves > leaves)
                return fail(error,QA_ERROR_FORMAT,offset,"invalid model visible leaf count");
            for (size_t hull = 1; hull < 3; ++hull)
                if (model.headnodes[hull] >= 0 && (counts[QA_BSP_CLIPNODES] > 0 || !extra_brushes)
                    && !reference(model.headnodes[hull],counts[QA_BSP_CLIPNODES],offset,"model clip headnode",error)) return false;
        }
    }
    for (size_t i = 0; i < counts[QA_BSP_AREAS]; ++i) {
        qa_bsp_area area;
        if (!qa_bsp_read_area(map,i,&area,error)) return false;
        if (!range_reference(area.portals,counts[QA_BSP_AREA_PORTALS],map->lumps[QA_BSP_AREAS].offset + i * 8,"area portals",error)) return false;
    }
    for (size_t i = 0; i < counts[QA_BSP_AREA_PORTALS]; ++i) {
        qa_bsp_area_portal portal;
        if (!qa_bsp_read_area_portal(map,i,&portal,error)) return false;
        size_t offset = map->lumps[QA_BSP_AREA_PORTALS].offset + i * 8;
        if (!reference(portal.portal,counts[QA_BSP_AREA_PORTALS],offset,"area portal",error)
            || !reference(portal.other_area,counts[QA_BSP_AREAS],offset,"portal area",error)) return false;
    }
    for (size_t i = 0; i < counts[QA_BSP_FOGS]; ++i) {
        qa_bsp_fog fog;
        if (!qa_bsp_read_fog(map,i,&fog,error)) return false;
        size_t offset = map->lumps[QA_BSP_FOGS].offset + i * map->lumps[QA_BSP_FOGS].stride;
        if (!reference(fog.brush,counts[QA_BSP_BRUSHES],offset,"fog brush",error)) return false;
        qa_bsp_brush brush;
        if (!qa_bsp_read_brush(map,(size_t)fog.brush,&brush,error)) return false;
        if (fog.visible_side != -1 && !reference(fog.visible_side,brush.sides.count,offset,"fog visible side",error)) return false;
    }
    if (!validate_tree(map,false,error) || !validate_tree(map,true,error)) return false;
    return map->family != QA_BSP_Q3 || validate_q3_surfaces(map,error);
}

typedef struct material_entry { qa_bytes raw_name; int32_t flags, contents; uint32_t id; bool occupied; } material_entry;

static uint64_t material_hash(qa_bytes name, int32_t flags, int32_t contents)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < name.size; ++i) { hash ^= name.data[i]; hash *= UINT64_C(1099511628211); }
    hash ^= (uint32_t)flags; hash *= UINT64_C(1099511628211);
    hash ^= (uint32_t)contents; hash *= UINT64_C(1099511628211);
    return hash;
}

static uint32_t intern_material(qa_bsp_materials *materials, material_entry *table, size_t mask,
                                qa_bytes name, int32_t flags, int32_t contents)
{
    size_t slot = (size_t)material_hash(name,flags,contents) & mask;
    while (table[slot].occupied) {
        if (table[slot].flags == flags && table[slot].contents == contents && same_bytes(table[slot].raw_name,name))
            return table[slot].id;
        slot = (slot + 1) & mask;
    }
    uint32_t id = (uint32_t)materials->shader_count++;
    table[slot] = (material_entry){name,flags,contents,id,true};
    materials->shaders[id] = (qa_bsp_shader){string_span(name.data,name.size),flags,contents};
    return id;
}

bool qa_bsp_build_materials(const qa_bsp_view *map, qa_bsp_materials *out, qa_error *error)
{
    if (map == NULL || out == NULL || map->family != QA_BSP_Q3)
        return fail(error,QA_ERROR_ARGUMENT,0,"material table requires Q3 BSP");
    qa_bsp_materials result = {
        .surface_count = qa_bsp_record_count(map,QA_BSP_SURFACES),
        .brush_count = qa_bsp_record_count(map,QA_BSP_BRUSHES),
        .side_count = qa_bsp_record_count(map,QA_BSP_BRUSH_SIDES)
    };
    size_t maximum = qa_bsp_record_count(map,QA_BSP_SHADERS);
    if (map->format == QA_BSP_IBSP44) {
        if (result.surface_count > UINT32_MAX - result.brush_count
            || result.surface_count + result.brush_count > UINT32_MAX - result.side_count)
            return fail(error,QA_ERROR_MEMORY,0,"material table is too large");
        maximum = result.surface_count + result.brush_count + result.side_count;
    }
    void *shaders = NULL, *surfaces = NULL, *brushes = NULL, *sides = NULL;
    bool allocated = allocate_array(maximum,sizeof(*result.shaders),&shaders,error)
        && allocate_array(result.surface_count,sizeof(*result.surfaces),&surfaces,error)
        && allocate_array(result.brush_count,sizeof(*result.brushes),&brushes,error)
        && allocate_array(result.side_count,sizeof(*result.sides),&sides,error);
    result.shaders = shaders; result.surfaces = surfaces; result.brushes = brushes; result.sides = sides;
    if (!allocated) { qa_bsp_materials_free(&result); return false; }
    if (map->format == QA_BSP_IBSP46) {
        result.shader_count = maximum;
        for (size_t i = 0; i < maximum; ++i)
            if (!qa_bsp_read_shader(map,i,&result.shaders[i],error)) goto failed;
        for (size_t i = 0; i < result.surface_count; ++i) {
            qa_bsp_surface surface;
            if (!qa_bsp_read_surface(map,i,&surface,error) || !reference(surface.shader,maximum,0,"surface shader",error)) goto failed;
            result.surfaces[i] = (uint32_t)surface.shader;
        }
        for (size_t i = 0; i < result.brush_count; ++i) {
            qa_bsp_brush brush;
            if (!qa_bsp_read_brush(map,i,&brush,error) || !reference(brush.shader,maximum,0,"brush shader",error)) goto failed;
            result.brushes[i] = (uint32_t)brush.shader;
        }
        for (size_t i = 0; i < result.side_count; ++i) {
            qa_bsp_brush_side side;
            if (!qa_bsp_read_brush_side(map,i,&side,error) || !reference(side.shader,maximum,0,"side shader",error)) goto failed;
            result.sides[i] = (uint32_t)side.shader;
        }
    } else {
        static const uint8_t blank[64] = {0};
        void *names_storage = NULL, *contents_storage = NULL, *table_storage = NULL;
        size_t table_size = 1;
        if (maximum > SIZE_MAX / 2) { fail(error,QA_ERROR_MEMORY,0,"material hash table is too large"); goto failed; }
        while (table_size < maximum * 2) {
            if (table_size > SIZE_MAX / 2) { fail(error,QA_ERROR_MEMORY,0,"material hash table is too large"); goto failed; }
            table_size *= 2;
        }
        if (!allocate_array(result.side_count,sizeof(qa_bytes),&names_storage,error)
            || !allocate_array(result.side_count,sizeof(int32_t),&contents_storage,error)
            || !allocate_array(table_size,sizeof(material_entry),&table_storage,error)) {
            free(names_storage); free(contents_storage); free(table_storage); goto failed;
        }
        qa_bytes *names = names_storage;
        int32_t *contents = contents_storage;
        material_entry *table = table_storage;
        bool valid = true;
        for (size_t i = 0; i < result.side_count; ++i) names[i] = (qa_bytes){blank,sizeof(blank)};
        for (size_t i = 0; i < result.surface_count; ++i) {
            qa_bytes raw;
            if (!qa_bsp_record(map,QA_BSP_SURFACES,i,&raw,error)) { valid = false; break; }
            int32_t side = qa_load_i32le(raw.data + 68);
            if (side < 0) continue;
            if (!reference(side,result.side_count,0,"surface brush side",error)) { valid = false; break; }
            names[side] = (qa_bytes){raw.data,64};
        }
        for (size_t i = 0; i < result.brush_count && valid; ++i) {
            qa_bsp_brush brush;
            if (!qa_bsp_read_brush(map,i,&brush,error) || !range_reference(brush.sides,result.side_count,0,"brush sides",error)) { valid = false; break; }
            for (size_t side = brush.sides.first; side < (size_t)brush.sides.first + brush.sides.count; ++side) contents[side] = brush.contents;
            qa_bytes name = brush.sides.first < result.side_count ? names[brush.sides.first] : (qa_bytes){blank,sizeof(blank)};
            result.brushes[i] = intern_material(&result,table,table_size - 1,name,0,brush.contents);
        }
        for (size_t i = 0; i < result.side_count && valid; ++i) {
            qa_bsp_brush_side side;
            if (!qa_bsp_read_brush_side(map,i,&side,error)) { valid = false; break; }
            result.sides[i] = intern_material(&result,table,table_size - 1,names[i],side.flags,contents[i]);
        }
        for (size_t i = 0; i < result.surface_count && valid; ++i) {
            qa_bytes raw;
            if (!qa_bsp_record(map,QA_BSP_SURFACES,i,&raw,error)) { valid = false; break; }
            int32_t side_index = qa_load_i32le(raw.data + 68), flags = 0, content_flags = 0;
            if (side_index >= 0) {
                qa_bsp_brush_side side;
                if (!qa_bsp_read_brush_side(map,(size_t)side_index,&side,error)) { valid = false; break; }
                flags = side.flags; content_flags = contents[side_index];
            }
            result.surfaces[i] = intern_material(&result,table,table_size - 1,(qa_bytes){raw.data,64},flags,content_flags);
        }
        free(names_storage); free(contents_storage); free(table_storage);
        if (!valid) goto failed;
    }
    *out = result;
    return true;
failed:
    qa_bsp_materials_free(&result);
    return false;
}

void qa_bsp_materials_free(qa_bsp_materials *materials)
{
    if (materials == NULL) return;
    free(materials->shaders); free(materials->surfaces); free(materials->brushes); free(materials->sides);
    *materials = (qa_bsp_materials){0};
}

bool qa_bsp_model_members(const qa_bsp_view *map, size_t model_index, bool brushes,
                          uint32_t *output, size_t capacity, size_t *count, qa_error *error)
{
    if (map == NULL || count == NULL || (capacity > 0 && output == NULL) || map->family != QA_BSP_Q3)
        return fail(error,QA_ERROR_ARGUMENT,0,"invalid Q3 model member request");
    qa_bsp_model model;
    if (!qa_bsp_read_model(map,model_index,&model,error)) return false;
    size_t total = qa_bsp_record_count(map,brushes ? QA_BSP_BRUSHES : QA_BSP_SURFACES);
    if (!model.membership_from_tree) {
        qa_bsp_range range = brushes ? model.brushes : model.faces;
        if (!range_reference(range,total,0,"model members",error)) return false;
        *count = range.count;
        if (range.count > capacity) return fail(error,QA_ERROR_ARGUMENT,0,"model member output is too small");
        for (size_t i = 0; i < range.count; ++i) output[i] = range.first + (uint32_t)i;
        return true;
    }
    size_t nodes = qa_bsp_record_count(map,QA_BSP_NODES);
    void *members_storage = NULL, *seen_storage = NULL, *stack_storage = NULL;
    if (nodes > (SIZE_MAX - 1) / 2) return fail(error,QA_ERROR_MEMORY,0,"model traversal is too large");
    if (!allocate_array(total,1,&members_storage,error) || !allocate_array(nodes,1,&seen_storage,error)
        || !allocate_array(nodes * 2 + 1,sizeof(int32_t),&stack_storage,error)) {
        free(members_storage); free(seen_storage); free(stack_storage); return false;
    }
    uint8_t *members = members_storage, *seen = seen_storage;
    int32_t *stack = stack_storage;
    size_t pending = 1, found = 0;
    stack[0] = model.headnodes[0];
    bool valid = true;
    while (pending > 0 && valid) {
        int32_t next = stack[--pending];
        if (!child_reference(map,next,0,error)) { valid = false; break; }
        if (next >= 0) {
            if (seen[next]) continue;
            seen[next] = 1;
            qa_bsp_node node;
            if (!qa_bsp_read_node(map,(size_t)next,&node,error)) { valid = false; break; }
            stack[pending++] = node.children[0]; stack[pending++] = node.children[1];
        } else {
            qa_bsp_leaf leaf;
            if (!qa_bsp_read_leaf(map,(size_t)(-1 - (int64_t)next),&leaf,error)) { valid = false; break; }
            qa_bsp_range range = brushes ? leaf.brushes : leaf.faces;
            qa_bsp_lump_kind lump = brushes ? QA_BSP_LEAF_BRUSHES : QA_BSP_LEAF_FACES;
            if (!range_reference(range,qa_bsp_record_count(map,lump),0,"leaf members",error)) { valid = false; break; }
            for (size_t i = range.first; i < (size_t)range.first + range.count; ++i) {
                int64_t member;
                if (!qa_bsp_read_index(map,lump,i,&member,error) || !reference(member,total,0,"model member",error)) { valid = false; break; }
                if (!members[member]) { members[member] = 1; ++found; }
            }
        }
    }
    *count = found;
    if (valid && found > capacity) valid = fail(error,QA_ERROR_ARGUMENT,0,"model member output is too small");
    if (valid) for (size_t i = 0, write = 0; i < total; ++i) if (members[i]) output[write++] = (uint32_t)i;
    free(members_storage); free(seen_storage); free(stack_storage);
    return valid;
}

size_t qa_bsp_surface_triangle_count(const qa_bsp_surface *surface)
{
    if (surface == NULL || (surface->type != QA_BSP_SURFACE_PLANAR && surface->type != QA_BSP_SURFACE_TRIANGLES)) return 0;
    return surface->triangle_fan ? surface->vertices.count >= 3 ? surface->vertices.count - 2 : 0 : surface->indices.count / 3;
}

bool qa_bsp_surface_triangle(const qa_bsp_view *map, const qa_bsp_surface *surface, size_t triangle,
                             uint32_t indices[3], qa_error *error)
{
    if (map == NULL || surface == NULL || indices == NULL || triangle >= qa_bsp_surface_triangle_count(surface))
        return fail(error,QA_ERROR_ARGUMENT,0,"invalid surface triangle");
    if (surface->triangle_fan) {
        indices[0] = 0; indices[1] = (uint32_t)triangle + 1; indices[2] = (uint32_t)triangle + 2;
    } else for (size_t i = 0; i < 3; ++i) {
        int64_t value;
        if (!qa_bsp_read_index(map,QA_BSP_INDICES,(size_t)surface->indices.first + triangle * 3 + i,&value,error)
            || !reference(value,surface->vertices.count,0,"surface vertex",error)) return false;
        indices[i] = (uint32_t)value;
    }
    return true;
}
