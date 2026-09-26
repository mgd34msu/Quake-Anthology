#include "qa/binary.h"
#include "qa/model.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
typedef struct fixture {
    uint8_t data[4096];
    size_t size;
} fixture;
static void word(fixture *f, uint32_t n) {
    CHECK(f->size + 4 <= sizeof(f->data));
    qa_store_u32le(f->data + f->size, n);
    f->size += 4;
}
static void real(fixture *f, float n) {
    uint32_t bits;
    memcpy(&bits, &n, 4);
    word(f, bits);
}
static void text(fixture *f, const char *s, size_t count) {
    CHECK(f->size + count <= sizeof(f->data));
    size_t n = strlen(s);
    if (n > count)
        n = count;
    memcpy(f->data + f->size, s, n);
    f->size += count;
}
static void vec(fixture *f, float x, float y, float z) {
    real(f, x);
    real(f, y);
    real(f, z);
}
static void packed(fixture *f, uint8_t x, uint8_t y, uint8_t z, uint8_t n) {
    CHECK(f->size + 4 <= sizeof(f->data));
    f->data[f->size++] = x;
    f->data[f->size++] = y;
    f->data[f->size++] = z;
    f->data[f->size++] = n;
}
static void patch(fixture *f, size_t offset, uint32_t value) {
    CHECK(offset + 4 <= f->size);
    qa_store_u32le(f->data + offset, value);
}
static qa_bytes bytes(const fixture *f) { return (qa_bytes){f->data, f->size}; }
static qa_model load(const fixture *f) {
    qa_model m = {0};
    qa_error e = {0};
    bool ok = qa_model_load(bytes(f), &m, &e);
    if (!ok)
        fprintf(stderr, "load failed at %zu: %s\n", e.offset, e.message);
    CHECK(ok);
    return m;
}
static void reject(const fixture *f) {
    qa_model m = {0};
    m.flags = 7919;
    qa_error e = {0};
    CHECK(!qa_model_load(bytes(f), &m, &e));
    CHECK(m.flags == 7919);
    CHECK(m.source.data == NULL);
    CHECK(e.code != QA_OK);
}
static void truncated(const fixture *f) {
    fixture copy = *f;
    for (size_t i = 0; i < f->size; ++i) {
        copy.size = i;
        reject(&copy);
    }
}

static fixture mdl(void) {
    fixture f = {0};
    text(&f, "IDPO", 4);
    word(&f, 6);
    vec(&f, 2, 3, 4);
    vec(&f, 1, 2, 3);
    real(&f, 10);
    vec(&f, 0, 0, 0);
    word(&f, 1);
    word(&f, 2);
    word(&f, 1);
    word(&f, 3);
    word(&f, 1);
    word(&f, 1);
    word(&f, 1);
    word(&f, 8);
    real(&f, 1);
    word(&f, 1);
    word(&f, 2);
    real(&f, 0.25f);
    real(&f, 0.5f);
    packed(&f, 1, 2, 3, 4);
    for (uint32_t i = 0; i < 3; ++i) {
        word(&f, i == 0 ? 1 : 0);
        word(&f, i);
        word(&f, 0);
    }
    word(&f, 0);
    word(&f, 0);
    word(&f, 1);
    word(&f, 2);
    word(&f, 1);
    word(&f, 2);
    packed(&f, 0, 0, 0, 0);
    packed(&f, 2, 2, 2, 0);
    real(&f, 0.5f);
    real(&f, 1);
    for (unsigned i = 0; i < 2; ++i) {
        packed(&f, 0, 0, 0, 0);
        packed(&f, 2, 2, 2, 0);
        text(&f, i ? "second" : "first", 16);
        packed(&f, (uint8_t)i, 0, 0, 5);
        packed(&f, 1, 0, 0, 5);
        packed(&f, 0, 1, 0, 5);
    }
    return f;
}
static fixture md2(void) {
    fixture f = {0};
    text(&f, "IDP2", 4);
    word(&f, 8);
    word(&f, 64);
    word(&f, 32);
    word(&f, 52);
    word(&f, 1);
    word(&f, 3);
    word(&f, 3);
    word(&f, 1);
    word(&f, 11);
    word(&f, 1);
    word(&f, 68);
    word(&f, 132);
    word(&f, 144);
    word(&f, 156);
    word(&f, 208);
    word(&f, 252);
    text(&f, "skins/test.pcx", 64);
    for (unsigned i = 0; i < 3; ++i) {
        qa_store_u16le(f.data + f.size, (uint16_t)i);
        qa_store_u16le(f.data + f.size + 2, 0);
        f.size += 4;
    }
    for (unsigned i = 0; i < 6; ++i) {
        qa_store_u16le(f.data + f.size, (uint16_t)(i % 3));
        f.size += 2;
    }
    vec(&f, 1, 1, 1);
    vec(&f, 10, 20, 30);
    text(&f, "pose", 16);
    packed(&f, 0, 0, 0, 5);
    packed(&f, 1, 0, 0, 5);
    packed(&f, 0, 1, 0, 5);
    word(&f, 3);
    for (uint32_t i = 0; i < 3; ++i) {
        real(&f, (float)i * 0.5f);
        real(&f, 0.25f);
        word(&f, i);
    }
    word(&f, 0);
    CHECK(f.size == 252);
    return f;
}
static fixture md3(void) {
    fixture f = {0};
    text(&f, "IDP3", 4);
    word(&f, 15);
    text(&f, "fixture", 64);
    word(&f, 0);
    word(&f, 2);
    word(&f, 1);
    word(&f, 1);
    word(&f, 3);
    word(&f, 108);
    word(&f, 220);
    word(&f, 444);
    word(&f, 704);
    for (unsigned i = 0; i < 2; ++i) {
        vec(&f, -1, -1, -1);
        vec(&f, 2, 2, 2);
        vec(&f, 0, 0, 0);
        real(&f, 3);
        text(&f, "frame", 16);
    }
    for (unsigned i = 0; i < 2; ++i) {
        text(&f, "tag_weapon", 64);
        vec(&f, (float)i * 2, 0, 0);
        vec(&f, 1, 0, 0);
        vec(&f, 0, 1, 0);
        vec(&f, 0, 0, 1);
    }
    CHECK(f.size == 444);
    text(&f, "IDP3", 4);
    text(&f, "BODY_1", 64);
    word(&f, 0);
    word(&f, 2);
    word(&f, 1);
    word(&f, 3);
    word(&f, 1);
    word(&f, 108);
    word(&f, 120);
    word(&f, 188);
    word(&f, 212);
    word(&f, 260);
    word(&f, 0);
    word(&f, 1);
    word(&f, 2);
    text(&f, "textures/body", 64);
    word(&f, 7);
    for (unsigned i = 0; i < 3; ++i) {
        real(&f, (float)i * 0.5f);
        real(&f, 0);
    }
    for (unsigned frame = 0; frame < 2; ++frame)
        for (unsigned i = 0; i < 3; ++i) {
            qa_store_u16le(f.data + f.size, (uint16_t)((i + frame) * 64));
            qa_store_u16le(f.data + f.size + 2, 0);
            qa_store_u16le(f.data + f.size + 4, 0);
            qa_store_u16le(f.data + f.size + 6, 0);
            f.size += 8;
        }
    CHECK(f.size == 704);
    return f;
}
static fixture md4(void) {
    fixture f = {0};
    text(&f, "IDP4", 4);
    word(&f, 1);
    text(&f, "rig", 64);
    word(&f, 1);
    word(&f, 2);
    word(&f, 0);
    word(&f, 100);
    word(&f, 1);
    word(&f, 236);
    word(&f, 484);
    vec(&f, -1, -1, -1);
    vec(&f, 2, 2, 2);
    vec(&f, 0, 0, 0);
    real(&f, 3);
    for (unsigned bone = 0; bone < 2; ++bone)
        for (unsigned row = 0; row < 3; ++row)
            for (unsigned column = 0; column < 4; ++column)
                real(&f, column == row ? 1 : (column == 3 && row == 0 ? (float)bone * 10 : 0));
    CHECK(f.size == 236);
    word(&f, 1);
    word(&f, 12);
    word(&f, 248);
    CHECK(f.size == 248);
    word(&f, 0);
    text(&f, "BODY", 64);
    text(&f, "skin", 64);
    word(&f, 3);
    word(&f, (uint32_t)-248);
    word(&f, 1);
    word(&f, 168);
    word(&f, 1);
    word(&f, 212);
    word(&f, 1);
    word(&f, 224);
    word(&f, 236);
    vec(&f, 0, 0, 1);
    real(&f, 0);
    real(&f, 0);
    word(&f, 1);
    word(&f, 1);
    real(&f, 1);
    vec(&f, 2, 0, 0);
    word(&f, 0);
    word(&f, 0);
    word(&f, 0);
    word(&f, 0);
    word(&f, 0);
    word(&f, 0);
    CHECK(f.size == 484);
    return f;
}
static fixture sprite(bool q2) {
    fixture f = {0};
    text(&f, q2 ? "IDS2" : "IDSP", 4);
    word(&f, q2 ? 2 : 1);
    if (q2) {
        word(&f, 1);
        word(&f, 4);
        word(&f, 2);
        word(&f, 1);
        word(&f, 1);
        text(&f, "sprites/test.pcx", 64);
    } else {
        word(&f, 2);
        real(&f, 2);
        word(&f, 2);
        word(&f, 2);
        word(&f, 1);
        real(&f, 0);
        word(&f, 0);
        word(&f, 1);
        word(&f, 2);
        real(&f, 0.25f);
        real(&f, 0.5f);
        for (unsigned i = 0; i < 2; ++i) {
            word(&f, (uint32_t)-1);
            word(&f, 1);
            word(&f, 2);
            word(&f, 2);
            packed(&f, 1, 2, 3, (uint8_t)(4 + i));
        }
    }
    return f;
}
static const char md5_text[] =
    "MD5Version 10\ncommandline \"test\"\nnumJoints 1\nnumMeshes 1\n"
    "joints { \"origin\" -1 ( 1 2 3 ) ( 0 0 0 ) }\n"
    "mesh { shader \"textures/test\"\nnumverts 3\n"
    "vert 2 ( 0 1 ) 2 1\nvert 0 ( 0 0 ) 0 1\nvert 1 ( 1 0 ) 1 1\n"
    "numtris 1\ntri 0 0 1 2\nnumweights 3\n"
    "weight 2 0 1 ( 0 1 0 )\nweight 0 0 1 ( 0 0 0 )\nweight 1 0 1 ( 1 0 0 )\n}";
static fixture md5(void) {
    fixture f = {0};
    f.size = strlen(md5_text);
    memcpy(f.data, md5_text, f.size);
    return f;
}

int main(void) {
    fixture fixtures[7] = {mdl(), md2(), md3(), md4(), md5(), sprite(false), sprite(true)};
    for (unsigned i = 0; i < 7; ++i) {
        qa_model m = load(&fixtures[i]);
        CHECK(m.source.data != fixtures[i].data);
        CHECK(m.source.size == fixtures[i].size);
        qa_model_free(&m);
        CHECK(m.source.data == NULL);
        qa_model_free(&m);
        truncated(&fixtures[i]);
    }
    qa_error e = {0};
    qa_model_vertex vertices[3];
    qa_model m = load(&fixtures[0]);
    CHECK(m.format == QA_MODEL_MDL);
    CHECK(m.skin_count == 2 && m.frame_count == 2);
    CHECK(m.skin_groups[0].intervals[1] == 0.5f);
    CHECK(qa_model_group_sample(&m.frame_groups[0], 0.5, 0) == 1);
    CHECK(qa_model_group_sample(&m.frame_groups[0], 1, 0) == 0);
    CHECK(qa_model_group_sample(&m.frame_groups[0], -0.2, 0) == 0);
    CHECK(m.meshes[0].texcoords[0].on_seam);
    CHECK(!m.meshes[0].triangles[0].front);
    CHECK(qa_model_sample_mesh(&m, 0, 1, 0, 0.5f, vertices, 3, &e));
    CHECK(vertices[0].position[0] == 2 && vertices[0].normal[2] == 1);
    CHECK(m.skins[0].pixels.data >= m.source.data &&
          m.skins[0].pixels.data < m.source.data + m.source.size);
    qa_model_free(&m);
    m = load(&fixtures[1]);
    CHECK(m.format == QA_MODEL_MD2);
    CHECK(m.gl_command_count == 11);
    CHECK(m.frames[0].bounds.min[0] == 10);
    CHECK(m.frames[0].bounds.max[1] == 21);
    CHECK(m.gl_commands[2] == (int32_t)0x3e800000);
    qa_model_free(&m);
    m = load(&fixtures[2]);
    CHECK(m.format == QA_MODEL_MD3);
    CHECK(strcmp(m.meshes[0].name, "body") == 0);
    CHECK(m.declared_skin_count == 3 && m.skin_count == 0);
    CHECK(m.meshes[0].shaders[0].index == 7);
    qa_model_tag tag;
    CHECK(qa_model_lerp_tag(&m, "tag_weapon", 0, 999, 0.5f, &tag));
    CHECK(tag.origin[0] == 1 && tag.axes[1][1] == 1);
    CHECK(!qa_model_lerp_tag(&m, "absent", 0, 1, 0.5f, &tag));
    CHECK(qa_model_sample_mesh(&m, 0, 1, 0, 0.5f, vertices, 3, &e));
    CHECK(vertices[0].position[0] == 0.5f);
    CHECK(!qa_model_sample_mesh(&m, 0, 2, 0, 0.5f, vertices, 3, &e));
    qa_model_free(&m);
    m = load(&fixtures[3]);
    CHECK(m.format == QA_MODEL_MD4 && m.bone_count == 2 && m.lod_count == 1);
    CHECK(m.meshes[0].bone_references[0] == 0 && m.meshes[0].weights[0].bone == 1);
    CHECK(qa_model_sample_mesh(&m, 0, 0, 0, 0, vertices, 3, &e));
    CHECK(vertices[0].position[0] == 12 && vertices[0].normal[2] == 1);
    qa_model_free(&m);
    m = load(&fixtures[4]);
    CHECK(m.format == QA_MODEL_MD5);
    CHECK(m.bind_pose[0].orientation[3] == -1);
    CHECK(m.bones[0].text_name.size == 6);
    CHECK(qa_model_sample_mesh(&m, 0, 0, 0, 0, vertices, 3, &e));
    CHECK(vertices[2].position[0] == 1 && vertices[2].position[1] == 3 &&
          vertices[2].normal[2] == -1);
    qa_model_pose pose = m.bind_pose[0];
    pose.position[0] = 8;
    CHECK(qa_model_skin_md5(&m, 0, &pose, 1, vertices, 3, &e));
    CHECK(vertices[1].position[0] == 9);
    qa_model_free(&m);
    m = load(&fixtures[5]);
    CHECK(m.format == QA_MODEL_SPR && m.sprite_count == 2 && m.orientation == 2);
    CHECK(m.sprites[1].pixels.data[3] == 5);
    qa_model_free(&m);
    m = load(&fixtures[6]);
    CHECK(m.format == QA_MODEL_SP2 && m.sprite_count == 1);
    CHECK(fabsf(m.bounds.max[0] - sqrtf(10)) < 0.00001f);
    qa_model_free(&m);
    fixture bad = fixtures[0];
    patch(&bad, 48, UINT32_MAX);
    reject(&bad);
    bad = fixtures[1];
    patch(&bad, 64, UINT32_MAX);
    reject(&bad);
    bad = fixtures[1];
    patch(&bad, 220, 3);
    reject(&bad);
    bad = fixtures[1];
    patch(&bad, 248, 3);
    reject(&bad);
    bad = fixtures[2];
    patch(&bad, 444 + 100, UINT32_MAX);
    reject(&bad);
    bad = fixtures[2];
    patch(&bad, 444 + 108, 3);
    reject(&bad);
    bad = fixtures[2];
    patch(&bad, 108, 0x7fc00000);
    reject(&bad);
    bad = fixtures[3];
    patch(&bad, 248 + 136, 0);
    reject(&bad);
    bad = fixtures[3];
    patch(&bad, 248 + 168 + 24, 2);
    reject(&bad);
    bad = fixtures[4];
    char *dup = strstr((char *)bad.data, "vert 2");
    CHECK(dup);
    dup[5] = '0';
    reject(&bad);
    puts("model parsers: seven formats, ownership, truncation, bounds, groups, tags and skinning "
         "passed");
    return 0;
}
