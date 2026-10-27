/*
 * Port of src/contracts/scene.ts (types only): decoded BSP/model
 * geometry, trace queries/results, and scene snapshots.
 * TS readonly tuple fields become fixed C arrays; readonly arrays
 * become pointer + count; Uint8Array becomes qa_byte_view.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_SCENE_H
#define QA_CONTRACTS_SCENE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/numeric.h"
#include "qa/contracts/time.h"

typedef struct qa_index_range { double first; double count; } qa_index_range;

typedef struct qa_indexed_model_skin {
  const char *name;
  double width, height;
  qa_byte_view pixels;
} qa_indexed_model_skin;

typedef struct qa_bsp_plane {
  qa_plane plane;
  double type;
  double signbits;
} qa_bsp_plane;

/* TS BspChild: {kind:"node",index} | {kind:"leaf",index}. */
typedef struct qa_bsp_child { bool is_node; double index; } qa_bsp_child;

typedef struct qa_bsp_node {
  double plane;
  qa_bsp_child children[2];
  qa_bounds bounds;
  qa_index_range faces; /* absent (Omit) in Q3 nodes; unused there */
} qa_bsp_node;

typedef struct qa_bsp_edge { double vertices[2]; } qa_bsp_edge;
typedef struct qa_texture_projection { qa_vec4 s; qa_vec4 t; } qa_texture_projection;
typedef struct qa_bsp_extension { const char *name; qa_byte_view bytes; } qa_bsp_extension;

/* TS Q1ClipChild: {kind:"clipnode",index} | {kind:"contents",value}. */
typedef struct qa_q1_clip_child { bool is_clipnode; double value; } qa_q1_clip_child;
typedef struct qa_q1_clip_node { double plane; qa_q1_clip_child children[2]; } qa_q1_clip_node;

typedef struct qa_q1_hull {
  const qa_q1_clip_node *clipnodes; size_t clipnode_count;
  const qa_bsp_plane *planes; size_t plane_count;
  double first_clipnode, last_clipnode;
  qa_bounds clip_bounds;
} qa_q1_hull;

typedef struct qa_q1_leaf {
  double contents;
  qa_bounds bounds;
  qa_index_range faces;
  bool has_visibility_offset;
  double visibility_offset;
  double ambient_sound[4];
} qa_q1_leaf;

/* TS Q1MipTexture: header & ({kind:"external"} |
 * {kind:"embedded", levels:[4 x Uint8Array]}). */
typedef struct qa_q1_mip_texture {
  const char *name;
  double width, height;
  bool has_quake64_shift;
  double quake64_shift;
  bool is_embedded;
  qa_byte_view levels[4];
} qa_q1_mip_texture;

typedef struct qa_q1_texture_info {
  qa_texture_projection projection;
  double texture;
  double flags;
} qa_q1_texture_info;

typedef struct qa_bsp_face {
  double plane;
  bool back;
  qa_index_range edges;
  double texture_info;
  qa_double_view styles;
  bool has_lighting_offset;
  double lighting_offset;
} qa_bsp_face;

/* TS BspLighting: {kind:"luminance8"} | {kind:"rgb8",
 * source:"bsp"|"lit"|"bspx"}. */
typedef struct qa_bsp_lighting {
  bool is_rgb8;
  qa_byte_view samples;
  const char *source;
} qa_bsp_lighting;

typedef struct qa_decoupled_lightmap {
  double width, height;
  bool has_lighting_offset;
  double lighting_offset;
  qa_vec3 axes[2];
  qa_vec2 offset;
} qa_decoupled_lightmap;

typedef struct qa_q1_brush_list_brush {
  qa_bounds bounds;
  double contents;
  const qa_plane *planes; size_t plane_count;
} qa_q1_brush_list_brush;
typedef struct qa_q1_brush_list_model {
  double model;
  const qa_q1_brush_list_brush *brushes; size_t brush_count;
} qa_q1_brush_list_model;

typedef struct qa_q1_world_model {
  qa_bounds bounds;
  qa_vec3 origin;
  qa_double_view headnodes;
  double visible_leaves;
  qa_index_range faces;
} qa_q1_world_model;

/* Q1WorldGeometry: kind "q1-bsp", format
 * "bsp29"|"bsp2"|"2psb"|"quake64" (exact string). textures entries
 * are nullable in TS (NULL slot here). */
typedef struct qa_q1_world_geometry {
  const char *format;
  const char *entities;
  const qa_bsp_plane *planes; size_t plane_count;
  const qa_vec3 *vertices; size_t vertex_count;
  const qa_bsp_edge *edges; size_t edge_count;
  qa_double_view surface_edges;
  const qa_bsp_node *nodes; size_t node_count;
  const qa_q1_leaf *leaves; size_t leaf_count;
  qa_double_view leaf_faces;
  const qa_q1_mip_texture *const *textures; size_t texture_count;
  const qa_q1_texture_info *texture_info; size_t texture_info_count;
  const qa_bsp_face *faces; size_t face_count;
  const qa_q1_world_model *models; size_t model_count;
  const qa_q1_clip_node *clipnodes; size_t clipnode_count;
  qa_byte_view visibility;
  qa_bsp_lighting lighting;
  const qa_decoupled_lightmap *decoupled_lightmaps; size_t decoupled_lightmap_count; /* NULL = TS null */
  const qa_q1_brush_list_model *brush_list; size_t brush_list_count; /* NULL = TS null */
  const qa_bsp_extension *extensions; size_t extension_count;
} qa_q1_world_geometry;

typedef struct qa_q2_surface_info {
  const char *name;
  double flags;
  double value;
  const char *material;
} qa_q2_surface_info;

typedef struct qa_q2_texture_info {
  qa_q2_surface_info surface;
  qa_texture_projection projection;
  bool has_next;
  double next;
} qa_q2_texture_info;

typedef struct qa_q2_brush_side { double plane; double texture_info; } qa_q2_brush_side;
typedef struct qa_q2_brush { qa_index_range sides; double contents; } qa_q2_brush;

typedef struct qa_q2_leaf {
  double contents, merged_contents, cluster, area;
  qa_bounds bounds;
  qa_index_range faces, brushes;
} qa_q2_leaf;

typedef struct qa_q2_area { qa_index_range portals; } qa_q2_area;
typedef struct qa_q2_area_portal { double portal; double other_area; } qa_q2_area_portal;

typedef struct qa_q2_visibility_cluster { double pvs_offset; double phs_offset; } qa_q2_visibility_cluster;
typedef struct qa_q2_visibility {
  const qa_q2_visibility_cluster *clusters; size_t cluster_count;
  qa_byte_view compressed;
} qa_q2_visibility;

/* TS Q2LightgridChild: "occluded" | {kind:"leaf",index} | {kind:"node",index}. */
typedef enum qa_q2_lightgrid_child_kind {
  QA_Q2_LIGHTGRID_OCCLUDED, QA_Q2_LIGHTGRID_LEAF, QA_Q2_LIGHTGRID_NODE
} qa_q2_lightgrid_child_kind;
typedef struct qa_q2_lightgrid_child { qa_q2_lightgrid_child_kind kind; double index; } qa_q2_lightgrid_child;
typedef struct qa_q2_lightgrid_node { qa_vec3 point; qa_q2_lightgrid_child children[8]; } qa_q2_lightgrid_node;
typedef struct qa_q2_lightgrid_sample { double style; qa_vec3 rgb; } qa_q2_lightgrid_sample;
typedef struct qa_q2_lightgrid_leaf {
  qa_vec3 min, size;
  double first_sample, point_count;
} qa_q2_lightgrid_leaf;
typedef struct qa_q2_lightgrid {
  qa_vec3 spacing, scale, min, size;
  double style_count;
  qa_q2_lightgrid_child root;
  const qa_q2_lightgrid_node *nodes; size_t node_count;
  const qa_q2_lightgrid_leaf *leaves; size_t leaf_count;
  const qa_q2_lightgrid_sample *samples; size_t sample_count;
} qa_q2_lightgrid;

typedef struct qa_q2_world_model {
  qa_bounds bounds;
  qa_vec3 origin;
  double headnode;
  qa_index_range faces;
} qa_q2_world_model;

/* Q2WorldGeometry: kind "q2-bsp", format "ibsp38"|"qbsp". */
typedef struct qa_q2_world_geometry {
  const char *format;
  const char *entities;
  const qa_bsp_plane *planes; size_t plane_count;
  const qa_vec3 *vertices; size_t vertex_count;
  const qa_bsp_edge *edges; size_t edge_count;
  qa_double_view surface_edges;
  const qa_bsp_node *nodes; size_t node_count;
  const qa_q2_leaf *leaves; size_t leaf_count;
  qa_double_view leaf_faces, leaf_brushes;
  const qa_q2_texture_info *texture_info; size_t texture_info_count;
  const qa_bsp_face *faces; size_t face_count;
  const qa_q2_brush *brushes; size_t brush_count;
  const qa_q2_brush_side *brush_sides; size_t brush_side_count;
  const qa_q2_world_model *models; size_t model_count;
  const qa_q2_area *areas; size_t area_count;
  const qa_q2_area_portal *area_portals; size_t area_portal_count;
  const qa_q2_visibility *visibility; /* nullable */
  qa_bsp_lighting lighting;
  const qa_q2_lightgrid *lightgrid; /* nullable */
  const qa_decoupled_lightmap *decoupled_lightmaps; size_t decoupled_lightmap_count;
  const qa_bsp_extension *extensions; size_t extension_count;
} qa_q2_world_geometry;

typedef struct qa_q3_bsp_shader {
  const char *name;
  double surface_flags, content_flags;
} qa_q3_bsp_shader;
typedef struct qa_q3_bsp_vertex {
  qa_vec3 position;
  qa_vec2 tex_coord, lightmap_coord;
  qa_vec3 normal;
  qa_vec4 color;
} qa_q3_bsp_vertex;
typedef struct qa_q3_bsp_leaf {
  double cluster, area;
  qa_bounds bounds;
  qa_index_range surfaces, brushes;
} qa_q3_bsp_leaf;
typedef struct qa_q3_bsp_model {
  qa_bounds bounds;
  qa_index_range surfaces, brushes;
} qa_q3_bsp_model;
typedef struct qa_q3_bsp_lightmap {
  double image, x, y, width, height;
  qa_vec3 origin;
  qa_vec3 vectors[3];
} qa_q3_bsp_lightmap;
/* TS Q3BspSurface kind: "planar"|"triangles"|"flare"|"patch"(+w/h). */
typedef struct qa_q3_bsp_surface {
  const char *kind;
  double shader, fog;
  qa_index_range vertices, indices;
  qa_q3_bsp_lightmap lightmap;
  double width, height; /* patch */
} qa_q3_bsp_surface;
typedef struct qa_q3_bsp_fog { const char *shader; double brush, visible_side; } qa_q3_bsp_fog;
typedef struct qa_q3_light_grid_point { qa_vec3 ambient, directed; qa_vec2 lat_long; } qa_q3_light_grid_point;
typedef struct qa_q3_bsp_brush { qa_index_range sides; double shader; } qa_q3_bsp_brush;
typedef struct qa_q3_bsp_brush_side { double plane, shader; } qa_q3_bsp_brush_side;
typedef struct qa_q3_visibility {
  double cluster_count, bytes_per_cluster;
  qa_byte_view bits;
} qa_q3_visibility;

/* Q3WorldGeometry: kind "q3-bsp", format "ibsp46". */
typedef struct qa_q3_world_geometry {
  const char *entities;
  const qa_q3_bsp_shader *shaders; size_t shader_count;
  const qa_bsp_plane *planes; size_t plane_count;
  const qa_bsp_node *nodes; size_t node_count; /* TS Omit<BspNode,"faces"> */
  const qa_q3_bsp_leaf *leaves; size_t leaf_count;
  qa_double_view leaf_surfaces, leaf_brushes;
  const qa_q3_bsp_model *models; size_t model_count;
  const qa_q3_bsp_brush *brushes; size_t brush_count;
  const qa_q3_bsp_brush_side *brush_sides; size_t brush_side_count;
  const qa_q3_bsp_vertex *vertices; size_t vertex_count;
  qa_double_view indices;
  const qa_q3_bsp_fog *fogs; size_t fog_count;
  const qa_q3_bsp_surface *surfaces; size_t surface_count;
  const qa_byte_view *lightmaps; size_t lightmap_count;
  const qa_q3_light_grid_point *light_grid; size_t light_grid_count;
  const qa_q3_visibility *visibility; /* nullable */
} qa_q3_world_geometry;

/* TS DecodedWorld union on kind: "q1-bsp"|"q2-bsp"|"q3-bsp". */
typedef enum qa_decoded_world_kind {
  QA_DECODED_WORLD_Q1, QA_DECODED_WORLD_Q2, QA_DECODED_WORLD_Q3
} qa_decoded_world_kind;
typedef struct qa_decoded_world {
  qa_decoded_world_kind kind;
  union {
    const qa_q1_world_geometry *q1;
    const qa_q2_world_geometry *q2;
    const qa_q3_world_geometry *q3;
  } as;
} qa_decoded_world;

typedef struct qa_model_vertex { qa_vec3 position, normal; } qa_model_vertex;
typedef struct qa_model_frame {
  const char *name;
  qa_bounds bounds;
  qa_vec3 origin;
  const qa_model_vertex *vertices; size_t vertex_count;
} qa_model_frame;

/* TS TimedFrames<T> generic: {kind:"single",frame} |
 * {kind:"group",frames:[{intervalSeconds,frame}]}. The C form is
 * elem_size byte storage over the element type named by the field. */
typedef struct qa_timed_frame_entry { double interval_seconds; const void *frame; } qa_timed_frame_entry;
typedef struct qa_timed_frames {
  bool is_group;
  const void *single_frame;
  const qa_timed_frame_entry *frames; size_t frame_count;
} qa_timed_frames;

typedef struct qa_packed_alias_vertex { double position[3]; double normal_index; } qa_packed_alias_vertex;

typedef struct qa_q1_alias_frame {
  qa_model_frame frame;
  qa_packed_alias_vertex compressed_bounds[2];
  const qa_packed_alias_vertex *compressed_vertices; size_t compressed_vertex_count;
} qa_q1_alias_frame;

/* TS Q1AliasFrameSet: single | group(+bounds/compressedBounds). */
typedef struct qa_q1_alias_frame_set {
  bool is_group;
  const qa_q1_alias_frame *single_frame;
  qa_bounds bounds;
  qa_packed_alias_vertex compressed_bounds[2];
  const qa_timed_frame_entry *frames; size_t frame_count; /* frame: qa_q1_alias_frame */
} qa_q1_alias_frame_set;

typedef struct qa_alias_tex_coord { bool on_seam; double s, t; } qa_alias_tex_coord;
typedef struct qa_q1_alias_triangle { bool front; double vertices[3]; } qa_q1_alias_triangle;

/* Q1AliasModel: kind "q1-mdl", sync "synchronized"|"random".
 * skins are TimedFrames<Uint8Array> (frame: qa_byte_view). */
typedef struct qa_q1_alias_model {
  qa_bounds bounds;
  double flags;
  const char *sync;
  qa_vec3 eye_position, scale, scale_origin;
  double bounding_radius, size, skin_width, skin_height;
  const qa_timed_frames *skins; size_t skin_count;
  const qa_alias_tex_coord *texture_coordinates; size_t texture_coordinate_count;
  const qa_q1_alias_triangle *triangles; size_t triangle_count;
  const qa_q1_alias_frame_set *frames; size_t frame_count;
} qa_q1_alias_model;

typedef struct qa_sprite_frame {
  double width, height, origin_x, origin_y;
  qa_byte_view pixels;
} qa_sprite_frame;

/* Q1SpriteModel: kind "q1-spr", orientation 0..4. frames are
 * TimedFrames<SpriteFrame>. */
typedef struct qa_q1_sprite_model {
  qa_bounds bounds;
  int64_t orientation;
  double max_width, max_height, bounding_radius;
  const char *sync;
  double beam_length;
  const qa_timed_frames *frames; size_t frame_count;
} qa_q1_sprite_model;

typedef struct qa_q2_sprite_frame {
  const char *image;
  double width, height, origin_x, origin_y;
} qa_q2_sprite_frame;
typedef struct qa_q2_sprite_model {
  qa_bounds bounds;
  /* TS kind is "q2-sp2". */
  const qa_q2_sprite_frame *frames; size_t frame_count;
} qa_q2_sprite_model;

/* TS SpriteModel = Q1SpriteModel | Q2SpriteModel. */
typedef struct qa_sprite_model {
  bool is_q1;
  union { const qa_q1_sprite_model *q1; const qa_q2_sprite_model *q2; } as;
} qa_sprite_model;

typedef struct qa_q2_alias_frame {
  qa_model_frame frame;
  qa_vec3 scale, translation;
  const qa_packed_alias_vertex *compressed_vertices; size_t compressed_vertex_count;
} qa_q2_alias_frame;
typedef struct qa_q2_alias_triangle { double vertices[3]; double tex_coords[3]; } qa_q2_alias_triangle;

/* Q2AliasModel: kind "q2-md2". glCommands is TS Int32Array. */
typedef struct qa_q2_alias_model {
  qa_bounds bounds;
  double skin_width, skin_height;
  qa_string_view_list skins;
  const qa_vec2 *texture_coordinates; size_t texture_coordinate_count;
  const qa_q2_alias_triangle *triangles; size_t triangle_count;
  const qa_q2_alias_frame *frames; size_t frame_count;
  const int32_t *gl_commands; size_t gl_command_count;
} qa_q2_alias_model;

typedef struct qa_model_tag { const char *name; qa_vec3 origin; qa_axis axis; } qa_model_tag;
typedef struct qa_q3_mesh_frame {
  const char *name;
  qa_bounds bounds;
  qa_vec3 local_origin;
  double radius;
} qa_q3_mesh_frame;
typedef struct qa_q3_mesh_surface {
  const char *name;
  qa_string_view_list shaders;
  const qa_vec2 *texture_coordinates; size_t texture_coordinate_count;
  qa_double_view indices;
  /* TS frames: per-frame vertex rows. */
  const qa_model_vertex *const *frame_vertices;
  const size_t *frame_vertex_counts;
  size_t frame_count;
} qa_q3_mesh_surface;
/* Q3MeshModel: kind "q3-md3". tags are per-frame tag rows. */
typedef struct qa_q3_mesh_model {
  const char *name;
  const qa_q3_mesh_frame *frames; size_t frame_count;
  const qa_model_tag *const *tag_rows;
  const size_t *tag_row_counts;
  size_t tag_row_count;
  const qa_q3_mesh_surface *surfaces; size_t surface_count;
} qa_q3_mesh_model;

typedef struct qa_md5_joint { const char *name; double parent; bool scale_positions; } qa_md5_joint;
typedef struct qa_md4_bone { qa_vec4 matrix[3]; } qa_md4_bone;
typedef struct qa_md4_frame {
  qa_bounds bounds;
  qa_vec3 local_origin;
  double radius;
  const qa_md4_bone *bones; size_t bone_count;
} qa_md4_frame;
typedef struct qa_md4_weight { double bone_index, bone_weight; qa_vec3 offset; } qa_md4_weight;
typedef struct qa_md4_vertex {
  qa_vec3 normal;
  qa_vec2 tex_coords;
  const qa_md4_weight *weights; size_t weight_count;
} qa_md4_vertex;
typedef struct qa_md4_triangle { double indices[3]; } qa_md4_triangle;
typedef struct qa_md4_surface {
  const char *name, *shader;
  const qa_md4_vertex *vertices; size_t vertex_count;
  const qa_md4_triangle *triangles; size_t triangle_count;
  qa_double_view bone_references;
} qa_md4_surface;
typedef struct qa_md4_lod { const qa_md4_surface *surfaces; size_t surface_count; } qa_md4_lod;
/* Md4Model: kind "q3-md4". */
typedef struct qa_md4_model {
  double version;
  const char *name;
  double num_bones, byte_length;
  const qa_md4_frame *frames; size_t frame_count;
  const qa_md4_lod *lods; size_t lod_count;
} qa_md4_model;

typedef struct qa_skeleton_joint_pose {
  qa_vec3 position;
  qa_vec4 orientation;
  double scale;
} qa_skeleton_joint_pose;

typedef struct qa_md5_model_frame {
  qa_bounds bounds;
  const qa_skeleton_joint_pose *joints; size_t joint_count;
} qa_md5_model_frame;
typedef struct qa_md5_mesh_vertex { qa_vec2 tex_coord; qa_vec3 normal; qa_index_range weights; } qa_md5_mesh_vertex;
typedef struct qa_md5_mesh_weight { double joint, bias; qa_vec3 position; } qa_md5_mesh_weight;
typedef struct qa_md5_mesh {
  const char *shader;
  const qa_md5_mesh_vertex *vertices; size_t vertex_count;
  qa_double_view indices;
  const qa_md5_mesh_weight *weights; size_t weight_count;
} qa_md5_mesh;
/* TS Md5Model skinSelection union on kind: "mesh-shaders" |
 * "q1-mdl-replacement" (meshSkinGroups TimedFrames<string> rows,
 * flags, timing "entity-frame"|"elapsed-time"+frameRate) |
 * "q2-md2-replacement" (skins, sourceFrameCount, scaleSource,
 * diagnostics). */
typedef struct qa_md5_skin_selection {
  const char *kind;
  const qa_timed_frames *const *mesh_skin_group_rows;
  const size_t *mesh_skin_group_row_counts;
  size_t mesh_skin_group_row_count;
  double flags;
  const char *timing_kind;
  double timing_frame_rate;
  qa_string_view_list skins;
  double source_frame_count;
  const char *scale_source; /* nullable */
  qa_string_view_list diagnostics;
} qa_md5_skin_selection;
/* Md5Model: kind "md5". */
typedef struct qa_md5_model {
  const qa_md5_joint *joints; size_t joint_count;
  double frame_rate;
  const qa_md5_model_frame *frames; size_t frame_count;
  const qa_md5_mesh *meshes; size_t mesh_count;
  qa_md5_skin_selection skin_selection;
} qa_md5_model;

typedef struct qa_model_replacement {
  qa_resolved_resource_reference resource;
  qa_md5_model model;
} qa_model_replacement;

/* TS DecodedModel union: (Q1/Q2 alias with optional replacement) |
 * sprites | md3 | md4 | md5 | {kind:"brush-model", world, model}. */
typedef enum qa_decoded_model_kind {
  QA_DECODED_MODEL_Q1_ALIAS,   /* "q1-mdl" */
  QA_DECODED_MODEL_Q2_ALIAS,   /* "q2-md2" */
  QA_DECODED_MODEL_Q1_SPRITE,  /* "q1-spr" */
  QA_DECODED_MODEL_Q2_SPRITE,  /* "q2-sp2" */
  QA_DECODED_MODEL_Q3_MESH,    /* "q3-md3" */
  QA_DECODED_MODEL_MD4,        /* "q3-md4" */
  QA_DECODED_MODEL_MD5,        /* "md5" */
  QA_DECODED_MODEL_BRUSH_MODEL /* "brush-model" */
} qa_decoded_model_kind;
typedef struct qa_decoded_model {
  qa_decoded_model_kind kind;
  union {
    const qa_q1_alias_model *q1_alias;
    const qa_q2_alias_model *q2_alias;
    const qa_q1_sprite_model *q1_sprite;
    const qa_q2_sprite_model *q2_sprite;
    const qa_q3_mesh_model *q3_mesh;
    const qa_md4_model *md4;
    const qa_md5_model *md5;
  } as;
  const qa_model_replacement *replacement; /* alias models; nullable */
  qa_decoded_world brush_world;             /* brush-model */
  double brush_model_index;
} qa_decoded_model;

/* TS TraceShape: "point" | {"box",bounds} | {"capsule",bounds}. */
typedef struct qa_trace_shape {
  const char *kind;
  qa_bounds bounds;
} qa_trace_shape;

/* TS TracePolicy union on kind: "q1" (move
 * "normal"|"no-monsters"|"missile", hull number|null) | "q2"
 * (contentsMask, leafContents "stored"|"merged") | "q3"
 * (contentsMask, curves, playerCurveClip). */
typedef struct qa_trace_policy {
  const char *kind;
  const char *move;
  bool has_hull;
  double hull;
  double contents_mask;
  const char *leaf_contents;
  bool curves, player_curve_clip;
} qa_trace_policy;

/* TS QueryTarget: {kind:"world"} | {kind:"model",model,origin,angles}. */
typedef struct qa_query_target {
  bool is_model;
  double model;
  qa_vec3 origin, angles;
} qa_query_target;

typedef struct qa_trace_query {
  qa_vec3 start, end;
  qa_trace_shape shape;
  qa_query_target target;
  qa_trace_policy policy;
  qa_numeric_profile numeric;
  const qa_actor_id *pass_actor; /* nullable */
} qa_trace_query;

/* TS TraceContact: {kind:"none"} | {kind:"plane",plane}. */
typedef struct qa_trace_contact { bool has_plane; qa_plane plane; } qa_trace_contact;
/* TS TraceHit: {kind:"none"} | {kind:"world",model} | {kind:"actor",actor}. */
typedef enum qa_trace_hit_kind {
  QA_TRACE_HIT_NONE, QA_TRACE_HIT_WORLD, QA_TRACE_HIT_ACTOR
} qa_trace_hit_kind;
typedef struct qa_trace_hit {
  qa_trace_hit_kind kind;
  double model;
  qa_actor_id actor;
} qa_trace_hit;

/* TS TraceResult: common TraceFields plus per-family extras on kind
 * "q1" (inOpen/inWater/sourcePlane/optional surfaceFlags) | "q2"
 * (contents/surface/sourcePlane/secondary) | "q3"
 * (contents/surfaceFlags/sourcePlane). */
typedef struct qa_trace_result {
  const char *kind;
  double fraction;
  qa_vec3 end;
  bool start_solid, all_solid;
  qa_trace_contact contact;
  qa_trace_hit hit;
  bool in_open, in_water;             /* q1 */
  qa_plane source_plane_q1;            /* q1 */
  qa_bsp_plane source_plane;           /* q2/q3 */
  bool has_surface_flags;
  double surface_flags;
  double contents;                     /* q2/q3 */
  const qa_q2_surface_info *surface;   /* q2, nullable */
  bool has_secondary;                  /* q2 */
  qa_bsp_plane secondary_plane;
  const qa_q2_surface_info *secondary_surface;
} qa_trace_result;

typedef struct qa_point_contents_query {
  qa_vec3 point;
  qa_query_target target;
  qa_trace_policy policy;
  qa_numeric_profile numeric;
  const qa_actor_id *pass_actor;
} qa_point_contents_query;
/* TS PointContentsResult on kind: "q1"(contents) | "q2"(stored,merged) | "q3"(contents). */
typedef struct qa_point_contents_result {
  const char *kind;
  double contents;
  double stored, merged;
} qa_point_contents_result;

typedef struct qa_leaf_query_result {
  qa_double_view leaves;
  bool has_topnode;
  double topnode;
  bool overflow;
} qa_leaf_query_result;

/** TS SceneQueries interface: function table + ctx. */
typedef struct qa_scene_queries {
  void *ctx;
  qa_trace_result (*trace)(void *ctx, qa_trace_query query);
  qa_point_contents_result (*point_contents)(void *ctx, qa_point_contents_query query);
  qa_leaf_query_result (*box_leaves)(void *ctx, qa_bounds bounds, double limit);
  bool (*areas_connected)(void *ctx, double first, double second);
  bool (*cluster_visible)(void *ctx, double from, double to, const char *kind); /* "pvs"|"phs" */
} qa_scene_queries;

typedef struct qa_model_transform {
  qa_vec3 origin;
  qa_axis axis;
  qa_vec3 scale;
} qa_model_transform;

/* TS ModelPose: {kind:"frame",frame,previousFrame,backLerp} |
 * {kind:"skeleton",joints}. */
typedef struct qa_model_pose {
  bool is_frame;
  double frame, previous_frame, back_lerp;
  const qa_skeleton_joint_pose *joints; size_t joint_count;
} qa_model_pose;

typedef struct qa_scene_entity qa_scene_entity;
typedef struct qa_scene_entity_attachment {
  const char *tag;
  const qa_scene_entity *entity;
} qa_scene_entity_attachment;
struct qa_scene_entity {
  bool has_opacity;
  double opacity;
  const qa_actor_id *actor; /* nullable */
  qa_resolved_resource_reference resource;
  qa_decoded_model model;
  qa_model_transform transform;
  qa_vec3 previous_origin;
  qa_model_pose pose;
  double skin;
  qa_vec4 color;
  qa_source_time shader_time;
  const char *flags_kind; /* "q1"|"q2"|"q3" */
  double flags_bits;
  qa_vec3 lighting_origin;
  double shadow_plane;
  const qa_scene_entity_attachment *attachments; size_t attachment_count;
};

/* TS SceneLightProfile: {kind:"q1"|"q3"} | {kind:"q2", scale,
 * cone|null, shadow {kind:"none"}|{kind:"cast",resolution}}. */
typedef struct qa_scene_light_profile {
  const char *kind;
  double scale;
  bool has_cone;
  qa_vec3 cone_direction;
  double cone_cos_half_angle;
  bool shadow_cast;
  double shadow_resolution;
} qa_scene_light_profile;
typedef struct qa_scene_light {
  qa_vec3 origin, color;
  double radius;
  bool additive;
  qa_scene_light_profile profile;
} qa_scene_light;

/* TS SceneParticle: "indexed"(paletteIndex,alpha) | "rgba"(color,rotation). */
typedef struct qa_scene_particle {
  bool is_rgba;
  qa_vec3 origin;
  double palette_index, alpha, size, rotation;
  qa_vec4 color;
} qa_scene_particle;

/* TS SceneLightStyle: "q1"(value) | "q2"(rgb,white). */
typedef struct qa_scene_light_style {
  bool is_q2;
  double style;
  double value;
  qa_vec3 rgb;
  double white;
} qa_scene_light_style;

typedef struct qa_scene_snapshot_world {
  qa_resolved_resource_reference resource;
  qa_decoded_world geometry;
} qa_scene_snapshot_world;

typedef struct qa_scene_snapshot {
  qa_session_id session;
  qa_source_time time;
  const qa_scene_snapshot_world *world; /* nullable */
  const qa_scene_entity *entities; size_t entity_count;
  const qa_scene_light *lights; size_t light_count;
  const qa_scene_particle *particles; size_t particle_count;
  const qa_scene_light_style *light_styles; size_t light_style_count;
  const qa_byte_view *area_bits; /* nullable */
} qa_scene_snapshot;

#endif
