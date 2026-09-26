/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qa/bsp.h"
#include "qa/binary.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); \
} } while (0)

typedef struct fixture { uint8_t data[8192]; size_t size, directory, count; } fixture;

static fixture make_fixture(qa_bsp_format format)
{
    fixture file = {0};
    static const uint32_t magic[] = {29, UINT32_C(0x32505342), UINT32_C(0x42535032),
        UINT32_C(0x51363420), UINT32_C(0x50534249), UINT32_C(0x50534251),
        UINT32_C(0x50534249), UINT32_C(0x50534249)};
    file.directory = format <= QA_BSP_QUAKE64 ? 4 : 8;
    file.count = format <= QA_BSP_QUAKE64 || format == QA_BSP_IBSP44 ? 15
        : format == QA_BSP_IBSP46 ? 17 : 19;
    file.size = file.directory + file.count * 8;
    qa_store_u32le(file.data, magic[format]);
    if (file.directory == 8)
        qa_store_u32le(file.data + 4, format == QA_BSP_IBSP44 ? 44 : format == QA_BSP_IBSP46 ? 46 : 38);
    return file;
}

static size_t add_lump(fixture *file, size_t disk_index, const void *data, size_t size)
{
    CHECK(disk_index < file->count && size <= sizeof(file->data) - file->size);
    size_t offset = file->size;
    qa_store_u32le(file->data + file->directory + disk_index * 8, (uint32_t)offset);
    qa_store_u32le(file->data + file->directory + disk_index * 8 + 4, (uint32_t)size);
    if (size > 0) memcpy(file->data + offset, data, size);
    file->size += size;
    return offset;
}

static void put_float(void *out, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    qa_store_u32le(out, bits);
}

static bool equal_text(qa_bytes bytes, const char *text)
{
    return bytes.size == strlen(text) && memcmp(bytes.data, text, bytes.size) == 0;
}

static void headers(void)
{
    qa_error error = {0};
    qa_bsp_view map;
    for (int format = QA_BSP_29; format <= QA_BSP_IBSP46; ++format) {
        fixture file = make_fixture((qa_bsp_format)format);
        CHECK(qa_bsp_open((qa_bytes){file.data,file.size}, &map, &error));
        CHECK(map.format == (qa_bsp_format)format);
        CHECK(map.family == (format <= QA_BSP_QUAKE64 ? QA_BSP_Q1 : format <= QA_BSP_QBSP ? QA_BSP_Q2 : QA_BSP_Q3));
        CHECK(strcmp(qa_bsp_format_name(map.format), "unknown") != 0);
        CHECK(map.source.data == file.data);
        for (size_t size = 0; size < file.size; ++size)
            CHECK(!qa_bsp_open((qa_bytes){file.data,size}, &map, &error));
        qa_store_u32le(file.data + file.directory, UINT32_MAX);
        CHECK(!qa_bsp_open((qa_bytes){file.data,file.size}, &map, &error));
        qa_store_u32le(file.data + file.directory, (uint32_t)file.size);
        CHECK(qa_bsp_open((qa_bytes){file.data,file.size}, &map, &error));
        qa_store_u32le(file.data + file.directory + 4, 1);
        CHECK(!qa_bsp_open((qa_bytes){file.data,file.size}, &map, &error));
    }
    CHECK(!qa_bsp_open((qa_bytes){NULL,4}, &map, &error));
    fixture file = make_fixture(QA_BSP_IBSP46);
    qa_store_u32le(file.data + 4, 47);
    CHECK(!qa_bsp_open((qa_bytes){file.data,file.size}, &map, &error) && error.code == QA_ERROR_UNSUPPORTED);
    file = make_fixture(QA_BSP_QBSP);
    qa_store_u32le(file.data + 4, 46);
    CHECK(!qa_bsp_open((qa_bytes){file.data,file.size}, &map, &error));
    file = make_fixture(QA_BSP_29);
    const char text[] = "{\"classname\" \"worldspawn\"}";
    size_t offset = add_lump(&file, 0, text, sizeof(text));
    CHECK(qa_bsp_open((qa_bytes){file.data,file.size}, &map, &error));
    CHECK(map.lumps[QA_BSP_ENTITIES].bytes.data == file.data + offset);
    qa_store_u32le(file.data + 4, 123);
    CHECK(!qa_bsp_open((qa_bytes){file.data,file.size}, &map, &error));
    file = make_fixture(QA_BSP_IBSP38);
    offset = add_lump(&file, 0, text, sizeof(text));
    qa_store_u32le(file.data + 12, UINT32_MAX);
    CHECK(qa_bsp_open((qa_bytes){file.data,file.size}, &map, &error));
    CHECK(map.diagnostics == QA_BSP_DIAGNOSTIC_ENTITY_CLAMPED);
    CHECK(map.lumps[QA_BSP_ENTITIES].bytes.size == sizeof(text));
    CHECK(map.lumps[QA_BSP_ENTITIES].offset == offset);
}

static void geometry(void)
{
    qa_error error = {0};
    for (int format = QA_BSP_29; format <= QA_BSP_IBSP46; ++format) {
        fixture file = make_fixture((qa_bsp_format)format);
        bool q1 = format <= QA_BSP_QUAKE64, q2 = format == QA_BSP_IBSP38 || format == QA_BSP_QBSP;
        bool wide = format == QA_BSP_2 || format == QA_BSP_2PSB || format == QA_BSP_QBSP;
        bool float_bounds = format == QA_BSP_2 || format == QA_BSP_QBSP;
        size_t plane_disk = format == QA_BSP_IBSP46 ? 2 : 1;
        uint8_t plane[20] = {0};
        put_float(plane, -1.0f); put_float(plane + 12, 128.5f); qa_store_u32le(plane + 16, 3);
        add_lump(&file, plane_disk, plane, format == QA_BSP_IBSP46 ? 16 : 20);
        uint8_t vertex[44] = {0};
        put_float(vertex, 1.25f); put_float(vertex + 4, -2.0f); put_float(vertex + 8, 3.5f);
        vertex[40] = 240; vertex[43] = 255;
        add_lump(&file, q1 ? 3 : q2 ? 2 : format == QA_BSP_IBSP44 ? 11 : 10, vertex, q1 || q2 ? 12 : 44);
        uint8_t node[44] = {0};
        size_t child_width = q1 && !wide ? 2 : 4;
        if (child_width == 2) { qa_store_u16le(node + 4, UINT16_MAX); qa_store_u16le(node + 6, UINT16_MAX - 1); }
        else { qa_store_u32le(node + 4, UINT32_MAX); qa_store_u32le(node + 8, UINT32_MAX - 1); }
        size_t bounds = 4 + child_width * 2;
        if (float_bounds) { put_float(node + bounds, -31.5f); put_float(node + bounds + 12, 42.5f); }
        else if (q1 || q2) { qa_store_u16le(node + bounds, (uint16_t)-31); qa_store_u16le(node + bounds + 6, 42); }
        else { qa_store_u32le(node + bounds, (uint32_t)-31); qa_store_u32le(node + bounds + 12, 42); }
        size_t node_size = q1 ? (wide ? float_bounds ? 44 : 32 : 24) : q2 ? (wide ? 44 : 28) : 36;
        add_lump(&file, q1 ? 5 : q2 ? 4 : format == QA_BSP_IBSP44 ? 2 : 3, node, node_size);
        qa_bsp_view map;
        CHECK(qa_bsp_open((qa_bytes){file.data,file.size}, &map, &error));
        qa_bsp_plane p;
        CHECK(qa_bsp_read_plane(&map, 0, &p, &error));
        CHECK(p.normal.x == -1 && p.distance == 128.5f);
        CHECK(p.type == (format == QA_BSP_IBSP46 ? -1 : 3));
        qa_bsp_vertex v;
        CHECK(qa_bsp_read_vertex(&map, 0, &v, &error) && v.position.z == 3.5f);
        if (!q1 && !q2) CHECK(v.color[0] == 240 && v.color[3] == 255);
        qa_bsp_node n;
        CHECK(qa_bsp_read_node(&map, 0, &n, &error));
        CHECK(n.children[0] == -1 && n.children[1] == -2);
        CHECK(n.bounds.min.x == (float_bounds ? -31.5f : -31.0f));
        CHECK(n.bounds.max.x == (float_bounds ? 42.5f : 42.0f));
        CHECK(qa_bsp_record_count(&map, QA_BSP_NODES) == 1);
        CHECK(!qa_bsp_read_node(&map, 1, &n, &error));
        CHECK(!qa_bsp_read_node(&map, 0, NULL, &error));
        put_float(file.data + map.lumps[QA_BSP_PLANES].offset, INFINITY);
        CHECK(!qa_bsp_read_plane(&map, 0, &p, &error) && error.code == QA_ERROR_FORMAT);
        qa_store_u32le(file.data + file.directory + plane_disk * 8 + 4, 1);
        CHECK(!qa_bsp_open((qa_bytes){file.data,file.size}, &map, &error));
    }
}

static void unsigned_q1_children(void)
{
    const size_t count = 32770, size = 124 + count * 24 + count * 8;
    uint8_t *file = calloc(1, size);
    CHECK(file != NULL);
    qa_store_u32le(file, 29);
    qa_store_u32le(file + 4 + 5 * 8, 124);
    qa_store_u32le(file + 8 + 5 * 8, (uint32_t)(count * 24));
    qa_store_u32le(file + 4 + 9 * 8, (uint32_t)(124 + count * 24));
    qa_store_u32le(file + 8 + 9 * 8, (uint32_t)(count * 8));
    qa_store_u16le(file + 124 + 4, 32768);
    qa_store_u16le(file + 124 + 6, UINT16_MAX);
    qa_store_u16le(file + 124 + count * 24 + 4, 32768);
    qa_store_u16le(file + 124 + count * 24 + 6, UINT16_MAX - 1);
    qa_error error = {0};
    qa_bsp_view map;
    qa_bsp_node node;
    qa_bsp_clipnode clip;
    CHECK(qa_bsp_open((qa_bytes){file,size}, &map, &error));
    CHECK(qa_bsp_read_node(&map, 0, &node, &error));
    CHECK(node.children[0] == 32768 && node.children[1] == -1);
    CHECK(qa_bsp_read_clipnode(&map, 0, &clip, &error));
    CHECK(clip.children[0] == 32768 && clip.children[1] == -2);
    free(file);
}

static void faces_and_models(void)
{
    qa_error error = {0};
    for (int format = QA_BSP_29; format <= QA_BSP_QBSP; ++format) {
        fixture file = make_fixture((qa_bsp_format)format);
        bool q1 = format <= QA_BSP_QUAKE64;
        bool wide = format == QA_BSP_2 || format == QA_BSP_2PSB || format == QA_BSP_QBSP;
        uint8_t face[28] = {0};
        size_t cursor = 0;
        if (wide) { qa_store_u32le(face, 70000); qa_store_u32le(face + 4, 1); cursor = 8; }
        else { qa_store_u16le(face, 50000); qa_store_u16le(face + 2, 1); cursor = 4; }
        qa_store_u32le(face + cursor, 90); cursor += 4;
        if (wide) { qa_store_u32le(face + cursor, 9); qa_store_u32le(face + cursor + 4, 123); cursor += 8; }
        else { qa_store_u16le(face + cursor, 9); qa_store_u16le(face + cursor + 2, 123); cursor += 4; }
        face[cursor] = 7; face[cursor + 3] = 255; cursor += 4;
        qa_store_u32le(face + cursor, 40);
        size_t face_offset = add_lump(&file, q1 ? 7 : 6, face, wide ? 28 : 20);
        uint8_t model[64] = {0};
        put_float(model, -128); put_float(model + 12, 256); put_float(model + 24, 3);
        qa_store_u32le(model + 36, UINT32_MAX);
        if (q1) { qa_store_u32le(model + 52, 9); qa_store_u32le(model + 56, 2); qa_store_u32le(model + 60, 3); }
        else { qa_store_u32le(model + 40, 2); qa_store_u32le(model + 44, 3); }
        add_lump(&file, q1 ? 14 : 13, model, q1 ? 64 : 48);
        qa_bsp_view map;
        qa_bsp_face f;
        qa_bsp_model m;
        CHECK(qa_bsp_open((qa_bytes){file.data,file.size}, &map, &error));
        CHECK(qa_bsp_read_face(&map, 0, &f, &error));
        CHECK(f.plane == (wide ? 70000u : 50000u) && f.draw_flags == 1);
        CHECK(f.edges.first == 90 && f.edges.count == 9 && f.texinfo == 123);
        CHECK(f.styles[0] == 7 && f.styles[3] == 255);
        CHECK(f.lighting_offset == (format == QA_BSP_QUAKE64 ? 20 : 40));
        CHECK(qa_bsp_read_model(&map, 0, &m, &error));
        CHECK(m.headnodes[0] == -1 && m.origin.x == 3 && m.faces.count == 3);
        if (format == QA_BSP_QUAKE64) {
            qa_store_u32le(file.data + face_offset + cursor, 41);
            CHECK(!qa_bsp_read_face(&map, 0, &f, &error));
            qa_store_u32le(file.data + face_offset + cursor, UINT32_MAX);
            CHECK(qa_bsp_read_face(&map, 0, &f, &error) && f.lighting_offset == -1);
        }
    }
}

static void q3_surfaces(void)
{
    qa_error error = {0};
    for (int version = 0; version < 2; ++version) {
        fixture file = make_fixture(version == 0 ? QA_BSP_IBSP44 : QA_BSP_IBSP46);
        uint8_t surface[164] = {0};
        size_t light;
        if (version == 0) {
            memcpy(surface, "textures/test", 13);
            qa_store_u32le(surface + 64, UINT32_MAX);
            qa_store_u32le(surface + 68, UINT32_MAX);
            qa_store_u32le(surface + 76, 4);
            light = 96;
        } else {
            qa_store_u32le(surface, 3); qa_store_u32le(surface + 4, UINT32_MAX);
            qa_store_u32le(surface + 8, 1); qa_store_u32le(surface + 16, 4);
            qa_store_u32le(surface + 24, 6); light = 28;
        }
        qa_store_u32le(surface + light, UINT32_MAX);
        put_float(surface + light + 20, 123);
        size_t offset = add_lump(&file, version == 0 ? 12 : 13, surface, version == 0 ? 164 : 104);
        qa_bsp_view map;
        qa_bsp_surface s;
        CHECK(qa_bsp_open((qa_bytes){file.data,file.size}, &map, &error));
        CHECK(qa_bsp_read_surface(&map, 0, &s, &error));
        CHECK(s.type == QA_BSP_SURFACE_PLANAR && s.lightmap == -1 && s.lightmap_origin.x == 123);
        CHECK(s.vertices.count == 4 && s.triangle_fan == (version == 0));
        if (version == 0) CHECK(equal_text(s.shader_name, "textures/test") && s.brush_side == -1);
        else CHECK(s.shader == 3 && s.indices.count == 6);
        if (version == 0) {
            qa_store_u32le(file.data + offset + 76, 9);
            qa_store_u32le(file.data + offset + 88, 3); qa_store_u32le(file.data + offset + 92, 3);
        } else {
            qa_store_u32le(file.data + offset + 8, 2); qa_store_u32le(file.data + offset + 16, 9);
            qa_store_u32le(file.data + offset + 96, 3); qa_store_u32le(file.data + offset + 100, 3);
        }
        CHECK(qa_bsp_read_surface(&map, 0, &s, &error) && s.type == QA_BSP_SURFACE_PATCH);
        qa_store_u32le(file.data + offset + (version == 0 ? 88 : 96), 2);
        CHECK(!qa_bsp_read_surface(&map, 0, &s, &error));
    }
}

static void remaining_records(void)
{
    qa_error error = {0};
    for (int format = QA_BSP_29; format <= QA_BSP_IBSP46; ++format) {
        fixture file = make_fixture((qa_bsp_format)format);
        bool q1 = format <= QA_BSP_QUAKE64, q2 = format == QA_BSP_IBSP38 || format == QA_BSP_QBSP;
        bool wide = format == QA_BSP_2 || format == QA_BSP_2PSB || format == QA_BSP_QBSP;
        size_t leaf_size = q1 ? (wide ? format == QA_BSP_2 ? 44 : 32 : 28) : q2 ? (wide ? 52 : 28) : 48;
        uint8_t leaf[52] = {0};
        qa_store_u32le(leaf, q1 ? (uint32_t)-2 : q2 ? 1 : UINT32_MAX);
        if (q1) { qa_store_u32le(leaf + 4, UINT32_MAX); leaf[leaf_size - 4] = 42; }
        else if (q2) {
            if (wide) { qa_store_u32le(leaf + 4, 70000); qa_store_u32le(leaf + 8, 4); }
            else { qa_store_u16le(leaf + 4, UINT16_MAX); qa_store_u16le(leaf + 6, 4); }
        } else qa_store_u32le(leaf + 4, 4);
        add_lump(&file, q1 ? 10 : q2 ? 8 : format == QA_BSP_IBSP44 ? 3 : 4, leaf, leaf_size);
        uint8_t index_bytes[4] = {0};
        if ((q1 || q2) && !wide) qa_store_u16le(index_bytes, 50000);
        else qa_store_u32le(index_bytes, q1 || q2 ? UINT32_C(3000000000) : 123);
        add_lump(&file, q1 ? 11 : q2 ? 9 : format == QA_BSP_IBSP44 ? 4 : 5,
                 index_bytes, (q1 || q2) && !wide ? 2 : 4);
        if (q1 || q2) {
            uint8_t edge[8] = {0};
            if (wide) { qa_store_u32le(edge, 70000); qa_store_u32le(edge + 4, 70001); }
            else { qa_store_u16le(edge, 50000); qa_store_u16le(edge + 2, 50001); }
            add_lump(&file, q1 ? 12 : 11, edge, wide ? 8 : 4);
            qa_store_u32le(index_bytes, (uint32_t)-123);
            add_lump(&file, q1 ? 13 : 12, index_bytes, 4);
            uint8_t texinfo[76] = {0};
            put_float(texinfo, 0.5f); put_float(texinfo + 28, 3.5f);
            if (q1) { qa_store_u32le(texinfo + 32, 9); qa_store_u32le(texinfo + 36, 7); }
            else { qa_store_u32le(texinfo + 32, 7); memcpy(texinfo + 40, "test/wall", 9); qa_store_u32le(texinfo + 72, UINT32_MAX); }
            add_lump(&file, q1 ? 6 : 5, texinfo, q1 ? 40 : 76);
        }
        if (!q1) {
            uint8_t brush[12] = {0}, side[8] = {0};
            qa_store_u32le(brush, 11); qa_store_u32le(brush + 4, 6); qa_store_u32le(brush + 8, 1234);
            if (q2 && !wide) { qa_store_u16le(side, 9); qa_store_u16le(side + 2, UINT16_MAX); }
            else { qa_store_u32le(side, 9); qa_store_u32le(side + 4, q2 ? UINT32_MAX : 16); }
            add_lump(&file, q2 ? 14 : format == QA_BSP_IBSP44 ? 7 : 8, brush, sizeof(brush));
            add_lump(&file, q2 ? 15 : format == QA_BSP_IBSP44 ? 8 : 9, side, q2 && !wide ? 4 : 8);
        }
        if (!q1 && !q2) {
            uint8_t fog[72] = {0}; memcpy(fog, "fog/test", 8); qa_store_u32le(fog + 64, 2); qa_store_u32le(fog + 68, 3);
            add_lump(&file, format == QA_BSP_IBSP44 ? 13 : 12, fog, format == QA_BSP_IBSP44 ? 68 : 72);
            if (format == QA_BSP_IBSP46) {
                uint8_t shader[72] = {0}; memcpy(shader, "shader/test", 11);
                qa_store_u32le(shader + 64, 17); qa_store_u32le(shader + 68, 19);
                add_lump(&file, 1, shader, sizeof(shader));
            }
            uint8_t model[48] = {0};
            if (format == QA_BSP_IBSP44) qa_store_u32le(model + 36, (uint32_t)-7);
            else { qa_store_u32le(model + 24, 10); qa_store_u32le(model + 28, 11); qa_store_u32le(model + 32, 12); qa_store_u32le(model + 36, 13); }
            add_lump(&file, format == QA_BSP_IBSP44 ? 6 : 7, model, format == QA_BSP_IBSP44 ? 48 : 40);
        }
        qa_bsp_view map;
        CHECK(qa_bsp_open((qa_bytes){file.data,file.size}, &map, &error));
        qa_bsp_leaf l;
        CHECK(qa_bsp_read_leaf(&map, 0, &l, &error));
        if (q1) CHECK(l.contents == -2 && l.visibility_offset == -1 && l.ambient[0] == 42);
        else CHECK(l.area == 4 && l.cluster == (wide ? 70000 : -1));
        int64_t value;
        CHECK(qa_bsp_read_index(&map, QA_BSP_LEAF_FACES, 0, &value, &error));
        CHECK(value == (q1 || q2 ? wide ? INT64_C(3000000000) : 50000 : 123));
        CHECK(!qa_bsp_read_index(&map, QA_BSP_PLANES, 0, &value, &error));
        if (q1 || q2) {
            qa_bsp_edge e;
            qa_bsp_texinfo t;
            CHECK(qa_bsp_read_edge(&map, 0, &e, &error) && e.vertices[0] == (wide ? 70000u : 50000u));
            CHECK(qa_bsp_read_texinfo(&map, 0, &t, &error));
            CHECK(t.projection[0][0] == 0.5f && t.projection[1][3] == 3.5f && t.flags == 7);
            if (q1) CHECK(t.texture == 9);
            else CHECK(equal_text(t.name, "test/wall") && t.next == -1);
            CHECK(qa_bsp_read_index(&map, QA_BSP_SURFEDGES, 0, &value, &error) && value == -123);
        }
        if (!q1) {
            qa_bsp_brush b;
            qa_bsp_brush_side s;
            CHECK(qa_bsp_read_brush(&map, 0, &b, &error) && b.sides.first == 11 && b.sides.count == 6);
            CHECK((format == QA_BSP_IBSP46 ? b.shader : b.contents) == 1234);
            CHECK(qa_bsp_read_brush_side(&map, 0, &s, &error) && s.plane == 9);
            CHECK(q2 ? s.texinfo == -1 : format == QA_BSP_IBSP44 ? s.flags == 16 : s.shader == 16);
        }
        if (!q1 && !q2) {
            qa_bsp_fog fog;
            qa_bsp_model model;
            CHECK(qa_bsp_read_fog(&map, 0, &fog, &error) && equal_text(fog.name, "fog/test") && fog.brush == 2);
            CHECK(fog.visible_side == (format == QA_BSP_IBSP44 ? -1 : 3));
            CHECK(qa_bsp_read_model(&map, 0, &model, &error));
            CHECK(model.membership_from_tree == (format == QA_BSP_IBSP44));
            if (format == QA_BSP_IBSP44) CHECK(model.headnodes[0] == -7);
            else {
                CHECK(model.faces.first == 10 && model.faces.count == 11 && model.brushes.first == 12 && model.brushes.count == 13);
                qa_bsp_shader shader;
                CHECK(qa_bsp_read_shader(&map, 0, &shader, &error));
                CHECK(equal_text(shader.name, "shader/test") && shader.surface_flags == 17 && shader.content_flags == 19);
            }
        }
    }
}

static void entities(void)
{
    static const uint8_t text[] = "// first\n{\"classname\" \"worldspawn\" \"key\" \"old\" \"key\" \"new\" \"path\" \"a\\n\"} { unquoted value }\0ignored";
    qa_entities result = {0};
    qa_error error = {0};
    CHECK(qa_entities_parse((qa_bytes){text,sizeof(text)}, QA_ENTITY_Q1, &result, &error));
    CHECK(result.count == 2 && result.property_count == 5 && result.records[0].property_count == 4);
    qa_bytes value;
    CHECK(qa_entity_value(&result, 0, "key", &value) && equal_text(value, "new"));
    CHECK(value.data >= text && value.data < text + sizeof(text));
    CHECK(qa_entity_value(&result, 0, "path", &value) && equal_text(value, "a\\n"));
    CHECK(!qa_entity_value(&result, 0, "missing", &value));
    qa_entities_free(&result);
    CHECK(result.records == NULL && result.properties == NULL && result.count == 0);
    qa_entities_free(&result);
    const char *q3 = "/* comment */ { \"key\" \"first\"\n\"key\" \"second\" }\n{ }";
    CHECK(qa_entities_parse((qa_bytes){(const uint8_t *)q3,strlen(q3)}, QA_ENTITY_Q3, &result, &error));
    CHECK(result.count == 2 && qa_entity_value(&result, 0, "key", &value) && equal_text(value, "second"));
    qa_entities_free(&result);
    const char *bad[] = {"key value", "{ key }", "{ \"unterminated", "{ key value", "{ { value }"};
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i)
        CHECK(!qa_entities_parse((qa_bytes){(const uint8_t *)bad[i],strlen(bad[i])}, QA_ENTITY_Q1, &result, &error));
    const char *newline = "{ key\nvalue }";
    CHECK(!qa_entities_parse((qa_bytes){(const uint8_t *)newline,strlen(newline)}, QA_ENTITY_Q3, &result, &error));
    CHECK(qa_entities_parse((qa_bytes){(const uint8_t *)newline,strlen(newline)}, QA_ENTITY_Q1, &result, &error));
    qa_entities_free(&result);
    char oversized[1100];
    memcpy(oversized, "{ key \"", 7); memset(oversized + 7, 'a', 1024); memcpy(oversized + 1031, "\" }", 3);
    CHECK(!qa_entities_parse((qa_bytes){(const uint8_t *)oversized,1034}, QA_ENTITY_Q3, &result, &error));
    CHECK(qa_entities_parse((qa_bytes){NULL,0}, QA_ENTITY_Q1, &result, &error) && result.count == 0);
    qa_entities_free(&result);
}

static void visibility(void)
{
    qa_error error = {0};
    uint8_t output[8];
    memset(output, 99, sizeof(output));
    const uint8_t input[] = {0x81,0,2,0x04};
    CHECK(qa_bsp_decode_rle((qa_bytes){input,sizeof(input)}, 0, output + 1, 4, &error));
    CHECK(output[0] == 99 && output[1] == 0x81 && output[2] == 0 && output[3] == 0 && output[4] == 4 && output[5] == 99);
    const uint8_t zero[] = {0,0}, large[] = {0,5}, truncated[] = {0};
    CHECK(!qa_bsp_decode_rle((qa_bytes){zero,2}, 0, output, 4, &error));
    CHECK(!qa_bsp_decode_rle((qa_bytes){large,2}, 0, output, 4, &error));
    CHECK(!qa_bsp_decode_rle((qa_bytes){truncated,1}, 0, output, 4, &error));
    CHECK(!qa_bsp_decode_rle((qa_bytes){input,4}, -2, output, 4, &error));
    CHECK(qa_bsp_decode_rle((qa_bytes){NULL,0}, -1, output, 4, &error) && output[3] == 255);
    fixture file = make_fixture(QA_BSP_IBSP38);
    uint8_t vis[14] = {0};
    qa_store_u32le(vis, 1); qa_store_u32le(vis + 4, 12); qa_store_u32le(vis + 8, 13);
    vis[12] = 1; vis[13] = 255;
    size_t offset = add_lump(&file, 3, vis, sizeof(vis));
    qa_bsp_view map;
    size_t written = 0;
    CHECK(qa_bsp_open((qa_bytes){file.data,file.size}, &map, &error));
    CHECK(qa_bsp_visibility(&map, 0, false, 0, output, sizeof(output), &written, &error));
    CHECK(written == 1 && output[0] == 1);
    CHECK(qa_bsp_visibility(&map, 0, true, 0, output, sizeof(output), &written, &error) && output[0] == 255);
    CHECK(qa_bsp_visibility(&map, -1, false, 0, output, sizeof(output), &written, &error) && output[0] == 0);
    CHECK(!qa_bsp_visibility(&map, 1, false, 0, output, sizeof(output), &written, &error));
    CHECK(!qa_bsp_visibility(&map, 0, false, 0, NULL, 0, &written, &error) && written == 1);
    qa_store_u32le(file.data + offset + 4, 0);
    CHECK(!qa_bsp_open((qa_bytes){file.data,file.size}, &map, &error));
    file = make_fixture(QA_BSP_IBSP46);
    uint8_t q3vis[10] = {0};
    qa_store_u32le(q3vis, 2); qa_store_u32le(q3vis + 4, 1); q3vis[8] = 1; q3vis[9] = 3;
    offset = add_lump(&file, 16, q3vis, sizeof(q3vis));
    CHECK(qa_bsp_open((qa_bytes){file.data,file.size}, &map, &error));
    CHECK(qa_bsp_visibility(&map, 1, false, 0, output, sizeof(output), &written, &error) && output[0] == 3);
    CHECK(qa_bsp_visibility(&map, -1, false, 0, output, sizeof(output), &written, &error) && output[0] == 255);
    CHECK(!qa_bsp_visibility(&map, 0, true, 0, output, sizeof(output), &written, &error));
    qa_store_u32le(file.data + offset + 4, 2);
    CHECK(!qa_bsp_open((qa_bytes){file.data,file.size}, &map, &error));
    file = make_fixture(QA_BSP_29);
    uint8_t leaves[56] = {0}, model[64] = {0};
    qa_store_u32le(leaves + 4, UINT32_MAX);
    qa_store_u32le(model + 52, 9);
    add_lump(&file, 10, leaves, sizeof(leaves));
    add_lump(&file, 14, model, sizeof(model));
    const uint8_t q1vis[] = {1,0,1};
    add_lump(&file, 4, q1vis, sizeof(q1vis));
    CHECK(qa_bsp_open((qa_bytes){file.data,file.size}, &map, &error));
    CHECK(qa_bsp_visibility(&map, 1, false, 0, output, sizeof(output), &written, &error));
    CHECK(written == 2 && output[0] == 1 && output[1] == 0);
    CHECK(qa_bsp_visibility(&map, 0, false, 0, output, sizeof(output), &written, &error));
    CHECK(output[0] == 255 && output[1] == 255);
    CHECK(!qa_bsp_visibility(&map, 2, false, 0, output, sizeof(output), &written, &error));
    file = make_fixture(QA_BSP_IBSP38);
    CHECK(qa_bsp_open((qa_bytes){file.data,file.size}, &map, &error));
    CHECK(qa_bsp_visibility(&map, 7, false, 9, output, sizeof(output), &written, &error));
    CHECK(written == 2 && output[0] == 255 && output[1] == 255);
    CHECK(qa_bsp_visibility(&map, -1, false, 9, output, sizeof(output), &written, &error));
    CHECK(output[0] == 0 && output[1] == 0);
}

static void bspx(void)
{
    qa_error error = {0};
    for (int q2 = 0; q2 < 2; ++q2) {
        fixture file = make_fixture(q2 ? QA_BSP_IBSP38 : QA_BSP_29);
        size_t header = file.size;
        memcpy(file.data + header, "BSPX", 4); qa_store_u32le(file.data + header + 4, 2);
        uint8_t *first = file.data + header + 8, *second = first + 32;
        memcpy(first, "RGBLIGHTING", 11); memcpy(second, "RGBLIGHTING", 11);
        size_t payload = header + 8 + 64;
        qa_store_u32le(first + 24, (uint32_t)payload); qa_store_u32le(first + 28, 3);
        qa_store_u32le(second + 24, (uint32_t)payload + 3); qa_store_u32le(second + 28, 3);
        memcpy(file.data + payload, "abcdef", 6); file.size = payload + 6;
        qa_bsp_view map;
        qa_bsp_extension extension;
        CHECK(qa_bsp_open((qa_bytes){file.data,file.size}, &map, &error));
        CHECK(map.extension_count == (q2 ? 1u : 2u));
        CHECK(qa_bsp_find_extension(&map, "RGBLIGHTING", &extension, &error));
        CHECK(equal_text(extension.bytes, "abc") && extension.bytes.data == file.data + payload);
        CHECK(qa_bsp_extension_at(&map, 0, &extension, &error) && extension.offset == payload);
        CHECK(!qa_bsp_extension_at(&map, map.extension_count, &extension, &error));
        CHECK(!qa_bsp_find_extension(&map, "ABSENT", &extension, &error) && error.code == QA_ERROR_NOT_FOUND);
        qa_store_u32le(second + 24, UINT32_MAX);
        if (q2) {
            CHECK(qa_bsp_open((qa_bytes){file.data,file.size}, &map, &error));
            CHECK(map.extension_count == 1 && (map.diagnostics & QA_BSP_DIAGNOSTIC_BSPX_IGNORED) != 0);
        } else CHECK(!qa_bsp_open((qa_bytes){file.data,file.size}, &map, &error));
        qa_store_u32le(file.data + header + 4, UINT32_MAX);
        if (q2) {
            CHECK(qa_bsp_open((qa_bytes){file.data,file.size}, &map, &error));
            CHECK(map.extension_count == 0 && (map.diagnostics & QA_BSP_DIAGNOSTIC_BSPX_TRUNCATED) != 0);
        } else CHECK(!qa_bsp_open((qa_bytes){file.data,file.size}, &map, &error));
    }
    /* Q1 also permits the directory immediately after its main header. */
    fixture file = make_fixture(QA_BSP_29);
    memcpy(file.data + 124, "BSPX", 4); qa_store_u32le(file.data + 128, 1);
    memcpy(file.data + 132, "TEST", 4); qa_store_u32le(file.data + 156, 164); qa_store_u32le(file.data + 160, 4);
    memcpy(file.data + 164, "data", 4); file.size = 168;
    const char entities_text[] = "{}";
    add_lump(&file, 0, entities_text, sizeof(entities_text));
    qa_bsp_view map;
    CHECK(qa_bsp_open((qa_bytes){file.data,file.size}, &map, &error));
    CHECK(map.bspx_offset == 124 && map.extension_count == 1);
}

int main(void)
{
    headers(); geometry(); unsigned_q1_children(); faces_and_models();
    q3_surfaces(); remaining_records(); entities(); visibility(); bspx();
    printf("BSP foundation: %u checks passed\n", checks);
    return 0;
}
