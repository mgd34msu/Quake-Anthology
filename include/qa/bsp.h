/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QA_BSP_H
#define QA_BSP_H

#include "qa/common.h"
#include "qa/math.h"

typedef enum qa_bsp_family { QA_BSP_Q1 = 1, QA_BSP_Q2, QA_BSP_Q3 } qa_bsp_family;
typedef enum qa_bsp_format {
    QA_BSP_29, QA_BSP_2, QA_BSP_2PSB, QA_BSP_QUAKE64,
    QA_BSP_IBSP38, QA_BSP_QBSP, QA_BSP_IBSP44, QA_BSP_IBSP46
} qa_bsp_format;

/* Semantic names resolve to each format's actual on-disk directory. */
typedef enum qa_bsp_lump_kind {
    QA_BSP_ENTITIES, QA_BSP_PLANES, QA_BSP_TEXTURES, QA_BSP_VERTICES,
    QA_BSP_VISIBILITY, QA_BSP_NODES, QA_BSP_TEXINFO, QA_BSP_FACES,
    QA_BSP_LIGHTING, QA_BSP_CLIPNODES, QA_BSP_LEAVES, QA_BSP_LEAF_FACES,
    QA_BSP_EDGES, QA_BSP_SURFEDGES, QA_BSP_MODELS, QA_BSP_LEAF_BRUSHES,
    QA_BSP_BRUSHES, QA_BSP_BRUSH_SIDES, QA_BSP_POP, QA_BSP_AREAS,
    QA_BSP_AREA_PORTALS, QA_BSP_SHADERS, QA_BSP_INDICES, QA_BSP_FOGS,
    QA_BSP_SURFACES, QA_BSP_LIGHTGRID, QA_BSP_LUMP_COUNT
} qa_bsp_lump_kind;

typedef struct qa_bsp_lump {
    qa_bytes bytes;
    size_t offset;
    size_t stride; /* Zero for absent and variable-length lumps. */
    bool present;
} qa_bsp_lump;

enum {
    QA_BSP_DIAGNOSTIC_ENTITY_CLAMPED = 1u << 0,
    QA_BSP_DIAGNOSTIC_BSPX_TRUNCATED = 1u << 1,
    QA_BSP_DIAGNOSTIC_BSPX_IGNORED = 1u << 2
};

/* All byte spans borrow the input. Keep it alive and immutable until consumers
 * are finished. Opening checks headers, ranges, strides and visibility tables;
 * typed reads check values. Whole-map reference validation is a separate step. */
typedef struct qa_bsp_view {
    qa_bytes source;
    qa_bsp_family family;
    qa_bsp_format format;
    qa_bsp_lump lumps[QA_BSP_LUMP_COUNT];
    size_t bspx_offset;
    uint32_t bspx_record_count;
    uint32_t extension_count;
    uint32_t diagnostics;
} qa_bsp_view;

bool qa_bsp_open(qa_bytes source, qa_bsp_view *out, qa_error *error);
const char *qa_bsp_format_name(qa_bsp_format format);
const char *qa_bsp_lump_name(qa_bsp_lump_kind kind);
size_t qa_bsp_record_count(const qa_bsp_view *map, qa_bsp_lump_kind kind);
bool qa_bsp_record(const qa_bsp_view *map, qa_bsp_lump_kind kind, size_t index,
                   qa_bytes *out, qa_error *error);

typedef qa_vec3 qa_bsp_vec3;
typedef struct qa_bsp_bounds { qa_bsp_vec3 min, max; } qa_bsp_bounds;
typedef struct qa_bsp_range { uint32_t first, count; } qa_bsp_range;
typedef struct qa_bsp_plane { qa_bsp_vec3 normal; float distance; int32_t type; } qa_bsp_plane;
typedef struct qa_bsp_vertex {
    qa_bsp_vec3 position;
    float texcoord[2], lightmap_coord[2];
    qa_bsp_vec3 normal;
    uint8_t color[4];
} qa_bsp_vertex;
typedef struct qa_bsp_node {
    uint32_t plane;
    int32_t children[2]; /* Negative children encode leaf = -1 - child. */
    qa_bsp_bounds bounds;
    qa_bsp_range faces;
} qa_bsp_node;
typedef struct qa_bsp_leaf {
    int32_t contents;
    int64_t cluster, area;
    int32_t visibility_offset; /* Q1 only, -1 means all visible. */
    qa_bsp_bounds bounds;
    qa_bsp_range faces, brushes;
    uint8_t ambient[4];
} qa_bsp_leaf;
typedef struct qa_bsp_edge { uint32_t vertices[2]; } qa_bsp_edge;
typedef struct qa_bsp_face {
    uint32_t plane, draw_flags;
    qa_bsp_range edges;
    uint32_t texinfo;
    uint8_t styles[4];
    int32_t lighting_offset; /* Quake64 converted to sample offset. */
} qa_bsp_face;
typedef struct qa_bsp_clipnode { int32_t plane, children[2]; } qa_bsp_clipnode;
typedef struct qa_bsp_texinfo {
    float projection[2][4];
    int32_t flags, texture, value, next;
    qa_bytes name;
} qa_bsp_texinfo;
typedef struct qa_bsp_model {
    qa_bsp_bounds bounds;
    qa_bsp_vec3 origin;
    int32_t headnodes[4], visible_leaves;
    qa_bsp_range faces, brushes;
    /* IBSP44 membership is defined by headnodes[0], not a contiguous range. */
    bool membership_from_tree;
} qa_bsp_model;
typedef struct qa_bsp_brush { qa_bsp_range sides; int32_t contents, shader; } qa_bsp_brush;
typedef struct qa_bsp_brush_side { uint32_t plane; int64_t texinfo; int32_t shader, flags; } qa_bsp_brush_side;
typedef struct qa_bsp_shader { qa_bytes name; int32_t surface_flags, content_flags; } qa_bsp_shader;
typedef struct qa_bsp_fog { qa_bytes name; int32_t brush, visible_side; } qa_bsp_fog;
typedef struct qa_bsp_area { qa_bsp_range portals; } qa_bsp_area;
typedef struct qa_bsp_area_portal { uint32_t portal, other_area; } qa_bsp_area_portal;
typedef struct qa_bsp_grid_point { uint8_t ambient[3], directed[3], lat_long[2]; } qa_bsp_grid_point;
typedef enum qa_bsp_surface_type {
    QA_BSP_SURFACE_PLANAR = 1, QA_BSP_SURFACE_PATCH,
    QA_BSP_SURFACE_TRIANGLES, QA_BSP_SURFACE_FLARE
} qa_bsp_surface_type;
typedef struct qa_bsp_surface {
    qa_bsp_surface_type type;
    int32_t shader, fog;
    qa_bsp_range vertices, indices;
    int32_t lightmap, lightmap_x, lightmap_y, lightmap_width, lightmap_height;
    qa_bsp_vec3 lightmap_origin, lightmap_vectors[3];
    int32_t patch_width, patch_height;
    /* IBSP44 stores these instead of a shader index. Indexless polygons use
     * an implicit triangle fan; no temporary BSP46 file is manufactured. */
    qa_bytes shader_name;
    int32_t brush_side;
    bool triangle_fan;
} qa_bsp_surface;

bool qa_bsp_read_plane(const qa_bsp_view *, size_t, qa_bsp_plane *, qa_error *);
bool qa_bsp_read_vertex(const qa_bsp_view *, size_t, qa_bsp_vertex *, qa_error *);
bool qa_bsp_read_node(const qa_bsp_view *, size_t, qa_bsp_node *, qa_error *);
bool qa_bsp_read_leaf(const qa_bsp_view *, size_t, qa_bsp_leaf *, qa_error *);
bool qa_bsp_read_edge(const qa_bsp_view *, size_t, qa_bsp_edge *, qa_error *);
bool qa_bsp_read_face(const qa_bsp_view *, size_t, qa_bsp_face *, qa_error *);
bool qa_bsp_read_clipnode(const qa_bsp_view *, size_t, qa_bsp_clipnode *, qa_error *);
bool qa_bsp_read_texinfo(const qa_bsp_view *, size_t, qa_bsp_texinfo *, qa_error *);
bool qa_bsp_read_model(const qa_bsp_view *, size_t, qa_bsp_model *, qa_error *);
bool qa_bsp_read_brush(const qa_bsp_view *, size_t, qa_bsp_brush *, qa_error *);
bool qa_bsp_read_brush_side(const qa_bsp_view *, size_t, qa_bsp_brush_side *, qa_error *);
bool qa_bsp_read_shader(const qa_bsp_view *, size_t, qa_bsp_shader *, qa_error *);
bool qa_bsp_read_fog(const qa_bsp_view *, size_t, qa_bsp_fog *, qa_error *);
bool qa_bsp_read_surface(const qa_bsp_view *, size_t, qa_bsp_surface *, qa_error *);
bool qa_bsp_read_area(const qa_bsp_view *, size_t, qa_bsp_area *, qa_error *);
bool qa_bsp_read_area_portal(const qa_bsp_view *, size_t, qa_bsp_area_portal *, qa_error *);
bool qa_bsp_read_grid_point(const qa_bsp_view *, size_t, qa_bsp_grid_point *, qa_error *);
/* Reads leaf indices, surfedges, and Q3 indices, preserving unsigned u16/u32
 * index ranges while retaining signed surfedges and Q3 index values. */
bool qa_bsp_read_index(const qa_bsp_view *, qa_bsp_lump_kind, size_t, int64_t *, qa_error *);

/* Full geometry/reference validation. Does not build another copy of the map. */
bool qa_bsp_validate(const qa_bsp_view *map, qa_error *error);

/* IBSP44 shader declarations are derived and interned once. IBSP46 keeps its
 * original shader IDs. Every string borrows the BSP; only index tables own RAM. */
typedef struct qa_bsp_materials {
    qa_bsp_shader *shaders;
    uint32_t *surfaces, *brushes, *sides;
    size_t shader_count, surface_count, brush_count, side_count;
} qa_bsp_materials;
bool qa_bsp_build_materials(const qa_bsp_view *, qa_bsp_materials *, qa_error *);
void qa_bsp_materials_free(qa_bsp_materials *);
/* Q3 model membership is returned in source index order. IBSP44 tree-defined
 * models share the original surfaces/brushes instead of cloning them. */
bool qa_bsp_model_members(const qa_bsp_view *, size_t model, bool brushes,
                          uint32_t *output, size_t capacity, size_t *count, qa_error *);
size_t qa_bsp_surface_triangle_count(const qa_bsp_surface *);
bool qa_bsp_surface_triangle(const qa_bsp_view *, const qa_bsp_surface *, size_t triangle,
                             uint32_t indices[3], qa_error *);

typedef enum qa_bsp_texture_storage { QA_BSP_TEXTURE_MISSING, QA_BSP_TEXTURE_EXTERNAL, QA_BSP_TEXTURE_EMBEDDED } qa_bsp_texture_storage;
typedef struct qa_bsp_texture {
    qa_bsp_texture_storage storage;
    qa_bytes name;
    uint32_t width, height, quake64_shift, mip_offsets[4];
    qa_bytes levels[4];
} qa_bsp_texture;
bool qa_bsp_texture_count(const qa_bsp_view *, size_t *, qa_error *);
bool qa_bsp_read_texture(const qa_bsp_view *, size_t, qa_bsp_texture *, qa_error *);

typedef enum qa_bsp_light_encoding { QA_BSP_LIGHT_LUMINANCE, QA_BSP_LIGHT_RGB8, QA_BSP_LIGHT_QUAKE64 } qa_bsp_light_encoding;
typedef struct qa_bsp_lighting { qa_bsp_light_encoding encoding; qa_bytes samples; size_t sample_count; } qa_bsp_lighting;
/* A nonempty external_lit overrides Q1 BSPX/BSP light samples. Empty .lit
 * input means no replacement. Packed Quake64 data stays shared. */
bool qa_bsp_select_lighting(const qa_bsp_view *, qa_bytes external_lit, qa_bsp_lighting *, qa_error *);
bool qa_bsp_light_sample(const qa_bsp_lighting *, size_t, uint8_t rgb[3], qa_error *);

typedef struct qa_bsp_decoupled_lightmap {
    uint16_t width, height;
    int64_t lighting_offset;
    float projection[2][4];
    bool ignored_lighting_offset;
} qa_bsp_decoupled_lightmap;
bool qa_bsp_read_decoupled_lightmap(const qa_bsp_view *, size_t, qa_bsp_decoupled_lightmap *, qa_error *);

typedef struct qa_bsp_face_normals { qa_bytes vectors, corner_indices; size_t vector_count, corner_count; } qa_bsp_face_normals;
bool qa_bsp_read_face_normals(const qa_bsp_view *, qa_bsp_face_normals *, qa_error *);
bool qa_bsp_face_corner_normal(const qa_bsp_face_normals *, size_t corner, unsigned component, qa_bsp_vec3 *, qa_error *);

typedef struct qa_bsp_q1_metadata {
    qa_bytes shifts, offsets, styles, styles16, hdr_lighting, directions, vertex_normals;
    size_t styles_per_face, styles16_per_face;
} qa_bsp_q1_metadata;
bool qa_bsp_read_q1_metadata(const qa_bsp_view *, qa_bsp_q1_metadata *, qa_error *);

typedef struct qa_bsp_extra_brush { qa_bsp_bounds bounds; int32_t contents; qa_bsp_range planes; } qa_bsp_extra_brush;
typedef struct qa_bsp_brush_model { uint32_t model; qa_bsp_range brushes; } qa_bsp_brush_model;
typedef struct qa_bsp_brush_list {
    qa_bsp_brush_model *models;
    qa_bsp_extra_brush *brushes;
    qa_bsp_plane *planes;
    size_t model_count, brush_count, plane_count;
} qa_bsp_brush_list;
bool qa_bsp_read_brush_list(const qa_bsp_view *, qa_bsp_brush_list *, qa_error *);
void qa_bsp_brush_list_free(qa_bsp_brush_list *);

typedef struct qa_bsp_lightgrid_sample { uint8_t style, rgb[3]; } qa_bsp_lightgrid_sample;
typedef struct qa_bsp_lightgrid_leaf { uint32_t min[3], size[3]; size_t first_sample; } qa_bsp_lightgrid_leaf;
typedef struct qa_bsp_lightgrid {
    qa_bsp_vec3 spacing, min;
    uint32_t size[3], root;
    uint8_t style_count;
    qa_bytes nodes;
    qa_bsp_lightgrid_leaf *leaves;
    qa_bsp_lightgrid_sample *samples;
    size_t leaf_count, sample_count;
} qa_bsp_lightgrid;
bool qa_bsp_read_lightgrid(const qa_bsp_view *, qa_bsp_lightgrid *, qa_error *);
void qa_bsp_lightgrid_free(qa_bsp_lightgrid *);
/* Integer grid coordinates. Returns style_count adjacent samples or NULL. */
const qa_bsp_lightgrid_sample *qa_bsp_lightgrid_lookup(const qa_bsp_lightgrid *, const int64_t point[3]);

typedef struct qa_bsp_extension { qa_bytes name, bytes; size_t offset; } qa_bsp_extension;
bool qa_bsp_extension_at(const qa_bsp_view *, size_t, qa_bsp_extension *, qa_error *);
bool qa_bsp_find_extension(const qa_bsp_view *, const char *, qa_bsp_extension *, qa_error *);

/* The caller owns output storage. Offset -1 gives all-visible; malformed RLE
 * fails without reading or writing outside either span. */
bool qa_bsp_decode_rle(qa_bytes input, int64_t offset, uint8_t *output,
                        size_t output_size, qa_error *error);
/* Q1 selector is a leaf, Q2/Q3 a cluster. Q2 missing visibility uses
 * fallback_clusters. Q3 PHS is unsupported. Returns required bytes in written
 * even when output capacity is too small. */
bool qa_bsp_visibility(const qa_bsp_view *map, int32_t selector, bool phs,
                       uint32_t fallback_clusters, uint8_t *output,
                       size_t capacity, size_t *written, qa_error *error);

typedef enum qa_entity_syntax { QA_ENTITY_Q1, QA_ENTITY_Q3 } qa_entity_syntax;
typedef struct qa_entity_property { qa_bytes key, value; } qa_entity_property;
typedef struct qa_entity_record { size_t first_property, property_count; } qa_entity_record;
typedef struct qa_entities {
    qa_entity_record *records;
    qa_entity_property *properties;
    size_t count, property_count;
} qa_entities;
/* Tokens borrow text. Only compact entity/property tables are allocated.
 * Ordered duplicate keys are retained; lookup returns the last value. */
bool qa_entities_parse(qa_bytes text, qa_entity_syntax syntax, qa_entities *out, qa_error *error);
void qa_entities_free(qa_entities *entities);
bool qa_entity_value(const qa_entities *, size_t entity, const char *key, qa_bytes *out);

#endif
