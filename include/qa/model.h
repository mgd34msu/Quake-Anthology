/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QA_MODEL_H
#define QA_MODEL_H
#include "qa/common.h"
#include "qa/math.h"

typedef enum qa_model_format {
    QA_MODEL_MDL,
    QA_MODEL_MD2,
    QA_MODEL_MD3,
    QA_MODEL_MD4,
    QA_MODEL_MD5,
    QA_MODEL_SPR,
    QA_MODEL_SP2
} qa_model_format;
typedef struct qa_model_bounds {
    float min[3], max[3];
} qa_model_bounds;
typedef struct qa_model_vertex {
    float position[3], normal[3];
} qa_model_vertex;
typedef struct qa_model_texcoord {
    float uv[2];
    int32_t s, t;
    bool on_seam;
} qa_model_texcoord;
typedef struct qa_model_triangle {
    uint32_t vertex[3], texcoord[3];
    bool front;
} qa_model_triangle;
typedef struct qa_model_shader {
    char name[65];
    int32_t index;
    qa_bytes text_name; /* MD5: authoritative full name; name is a 64-byte preview. */
} qa_model_shader;
typedef struct qa_model_weight {
    uint32_t bone;
    float bias, offset[3];
} qa_model_weight;
typedef struct qa_model_weight_range {
    uint32_t first, count;
} qa_model_weight_range;
typedef struct qa_model_mesh {
    char name[65];
    int32_t flags;
    uint32_t vertex_count, texcoord_count, triangle_count, frame_count, shader_count;
    /* Alias/MD3: frame-major. MD4/MD5: one bind vertex record per vertex. */
    qa_model_vertex *vertices;
    qa_model_texcoord *texcoords;
    qa_model_triangle *triangles;
    qa_model_shader *shaders;
    uint32_t weight_count, bone_reference_count;
    qa_model_weight *weights;
    qa_model_weight_range *vertex_weights;
    uint32_t *bone_references;
} qa_model_mesh;
typedef struct qa_model_frame {
    char name[17];
    qa_model_bounds bounds;
    float origin[3], radius;
    float scale[3], translation[3];
    qa_bytes packed_vertices; /* Borrowed source vertex records: x/y/z/normal bytes. */
} qa_model_frame;
typedef struct qa_model_group {
    uint32_t first, count;
    float *intervals; /* Cumulative endpoints; NULL denotes an ungrouped record. */
    qa_model_bounds bounds;
} qa_model_group;
typedef struct qa_model_skin {
    char name[65];
    qa_bytes pixels;
} qa_model_skin;
typedef struct qa_model_tag {
    char name[65];
    float origin[3], axes[3][3];
} qa_model_tag;
typedef struct qa_model_sprite {
    uint32_t width, height;
    int32_t origin_x, origin_y;
    char image[65];
    qa_bytes pixels;
} qa_model_sprite;
typedef struct qa_model_lod {
    uint32_t first_mesh, mesh_count;
} qa_model_lod;
typedef struct qa_model_bone {
    char name[65];
    int32_t parent;
    qa_bytes text_name; /* MD5: authoritative full name; name is a 64-byte preview. */
} qa_model_bone;
typedef struct qa_model_pose {
    float position[3], orientation[4], scale;
} qa_model_pose;
typedef struct qa_model {
    qa_model_format format;
    char name[65];
    int32_t flags, sync, orientation;
    float radius, size, beam_length, scale[3], translation[3], eye_position[3];
    uint32_t skin_width, skin_height, declared_skin_count;
    qa_model_bounds bounds;
    uint32_t mesh_count, frame_count, frame_group_count, skin_count, skin_group_count;
    uint32_t tag_count, sprite_count, lod_count, bone_count, gl_command_count;
    qa_model_mesh *meshes;
    qa_model_frame *frames;
    qa_model_group *frame_groups, *skin_groups;
    qa_model_skin *skins;
    qa_model_tag *tags; /* frame_count * tag_count; names retained per frame. */
    qa_model_sprite *sprites;
    qa_model_lod *lods;
    qa_model_bone *bones;
    float *bone_matrices;     /* MD4 frame-major bone_count * 12 floats. */
    qa_model_pose *bind_pose; /* MD5 joints in model space. */
    int32_t *gl_commands;     /* Original MD2 words, including float bit patterns. */
    qa_bytes command_line;    /* MD5 text views retain full strings, without length
                                 truncation. */
    qa_buffer source;         /* Owns source bytes and all skin/sprite pixel views. */
} qa_model;

/* Auto-detects MDL/MD2/MD3/MD4/MD5mesh/SPR/SP2. Copies source once.
 * Output is unchanged on failure. Release a previous output before replacement.
 * Loaded arrays are shared immutable resources; callers must not mutate them. */
bool qa_model_load(qa_bytes bytes, qa_model *out, qa_error *error);
/* Adopts source without copying on success and clears it. On failure source and
 * output are unchanged, so the caller remains responsible for source. */
bool qa_model_load_owned(qa_buffer *source, qa_model *out, qa_error *error);
void qa_model_free(qa_model *model);
/* Full source name for binary or text models. Returned views are borrowed. */
qa_bytes qa_model_shader_name(const qa_model_shader *shader);
qa_bytes qa_model_bone_name(const qa_model_bone *bone);
/* Caller supplies storage; no per-sample allocation. */
bool qa_model_sample_mesh(const qa_model *model, uint32_t mesh, uint32_t frame, uint32_t old_frame,
                          float back_lerp, qa_model_vertex *vertices, size_t count,
                          qa_error *error);
/* Alias interpolation includes Q2 previous-origin compensation when requested. */
bool qa_model_sample_alias(const qa_model *model, uint32_t frame, uint32_t old_frame,
                           float back_lerp, qa_vec3 previous_origin_delta,
                           qa_model_vertex *vertices, size_t count, qa_error *error);
bool qa_model_corner_uv(const qa_model *model, uint32_t mesh, uint32_t triangle, uint32_t corner,
                        float out[2]);
bool qa_model_lerp_tag(const qa_model *model, const char *name, uint32_t from, uint32_t to,
                       float fraction, qa_model_tag *out);
bool qa_model_skin_md5(const qa_model *model, uint32_t mesh, const qa_model_pose *pose,
                       size_t joint_count, qa_model_vertex *vertices, size_t count,
                       qa_error *error);
uint32_t qa_model_group_sample(const qa_model_group *group, double seconds, double sync_base);
typedef struct qa_model_animation_joint {
    qa_model_bone bone;
    uint32_t flags, first_component;
    bool scale_positions;
} qa_model_animation_joint;
typedef struct qa_model_animation {
    uint32_t frame_count, joint_count, component_count, frame_rate;
    qa_model_animation_joint *joints;
    qa_model_pose *base_pose, *local_poses, *poses; /* Frame-major. */
    qa_model_bounds *bounds;
    float *components;
    qa_bytes command_line;
    qa_buffer source;
} qa_model_animation;
/* Imported scale entries are indexed after resolving names in the shared JSON
 * service. Later entries replace earlier entries; scale_positions is per joint. */
typedef struct qa_model_scale {
    uint32_t frame, joint;
    float value;
} qa_model_scale;
bool qa_model_animation_load(qa_bytes md5anim, qa_model_animation *out, qa_error *error);
void qa_model_animation_free(qa_model_animation *animation);
bool qa_model_animation_scales(qa_model_animation *animation, const qa_model_scale *scales,
                               size_t count, const bool *scale_positions, size_t joint_count,
                               qa_error *error);
bool qa_model_animation_sample(const qa_model_animation *animation, uint32_t frame,
                               uint32_t old_frame, float back_lerp, qa_model_pose *out,
                               size_t count, qa_error *error);
/* Produces a joint attachment with the same column-axis convention as MD3 tags. */
void qa_model_joint_tag(const qa_model_pose *pose, qa_model_tag *tag);

#define QA_PLAYER_ANIMATION_COUNT 37
typedef enum qa_model_footstep {
    QA_FOOTSTEP_NORMAL,
    QA_FOOTSTEP_BOOT,
    QA_FOOTSTEP_FLESH,
    QA_FOOTSTEP_MECH,
    QA_FOOTSTEP_ENERGY
} qa_model_footstep;
typedef enum qa_model_gender { QA_MODEL_MALE, QA_MODEL_FEMALE, QA_MODEL_NEUTER } qa_model_gender;
typedef struct qa_player_animation {
    int32_t first_frame, num_frames, loop_frames, frame_lerp, initial_lerp;
    bool reversed, flipflop, present;
} qa_player_animation;
typedef void (*qa_model_diagnostic)(void *context, size_t offset, const char *message);
typedef struct qa_player_animation_config {
    qa_model_footstep footsteps;
    qa_model_gender gender;
    float head_offset[3];
    bool fixed_legs, fixed_torso;
    qa_player_animation animations[QA_PLAYER_ANIMATION_COUNT];
} qa_player_animation_config;
/* Invalid optional scale records report diagnostics and retain usable records. */
bool qa_model_animation_scale_json(qa_model_animation *animation, qa_bytes json,
                                   qa_model_diagnostic diagnostic, void *context, qa_error *error);
extern const char *const qa_player_animation_names[QA_PLAYER_ANIMATION_COUNT];
bool qa_player_animation_load(qa_bytes text, qa_player_animation_config *out,
                              qa_model_diagnostic diagnostic, void *context, qa_error *error);
typedef struct qa_model_skin_mapping {
    char surface[64];
    char *shader;
} qa_model_skin_mapping;
typedef struct qa_model_skin_map {
    size_t count;
    qa_model_skin_mapping *mappings;
} qa_model_skin_map;
bool qa_model_skin_map_load(qa_bytes text, qa_model_skin_map *out, qa_error *error);
void qa_model_skin_map_free(qa_model_skin_map *map);

typedef struct qa_model_replacement_paths {
    char *mesh, *animation, *scales;
} qa_model_replacement_paths;
bool qa_model_md5_paths(const char *model_path, bool q2, qa_model_replacement_paths *out,
                        qa_error *error);
void qa_model_replacement_paths_free(qa_model_replacement_paths *paths);
bool qa_model_md5_skin_path(const char *skin_path, char **out, qa_error *error);
bool qa_model_q1_skin_path(const char *shader, uint32_t group, uint32_t frame, char **out,
                           qa_error *error);
bool qa_model_md5_replacement_allowed(int64_t alias_rank, int64_t mesh_rank);
/* Mismatched Q1 source/MD5 animation counts use elapsed time at 2 Hz. */
bool qa_model_q1_replacement_uses_time(uint32_t source_frames, uint32_t md5_frames);
typedef enum qa_model_lod_state {
    QA_MODEL_LOD_MISSING,
    QA_MODEL_LOD_LOADED,
    QA_MODEL_LOD_INVALID,
    QA_MODEL_LOD_ALIAS
} qa_model_lod_state;
typedef struct qa_model_lod_set {
    char *paths[3];
    qa_model models[3];
    qa_model_lod_state states[3];
    uint32_t aliases[3], load_order[3], load_count, lod_count;
    size_t byte_length;
} qa_model_lod_set;
/* Reader publishes owned bytes; QA_ERROR_NOT_FOUND denotes an absent slot. */
typedef bool (*qa_model_read_file)(void *context, const char *path, qa_buffer *out,
                                   qa_error *error);
bool qa_model_md3_lods(const char *path, qa_model_read_file read_file, void *context,
                       qa_model_lod_set *out, qa_error *error);
const qa_model *qa_model_at_lod(const qa_model_lod_set *set, uint32_t slot);
void qa_model_lods_free(qa_model_lod_set *set);

typedef struct qa_model_transform {
    float origin[3], axes[3][3], scale[3];
} qa_model_transform;
void qa_model_transform_identity(qa_model_transform *out);
void qa_model_transform_point(const qa_model_transform *transform, const float point[3],
                              float out[3]);
void qa_model_transform_direction(const qa_model_transform *transform, const float value[3],
                                  float out[3]);
void qa_model_transform_compose(const qa_model_transform *parent, const qa_model_transform *child,
                                qa_model_transform *out);
bool qa_model_transform_inverse(const qa_model_transform *transform, qa_model_transform *out);
bool qa_model_attachment_align(const qa_model_transform *grip, const qa_model_transform *socket,
                               qa_model_transform *out);
bool qa_model_triangle_attachment(const qa_model_vertex triangle[3], qa_model_transform *out);
void qa_model_transform_bounds(const qa_model_transform *transform, const qa_model_bounds *bounds,
                               qa_model_bounds *out);
typedef struct qa_model_replacement {
    const qa_model *source, *mesh;
    const qa_model_animation *animation;
    int32_t flags;
    bool elapsed_animation;
} qa_model_replacement;
/* Borrows all three immutable resources; the owner retains their lifetimes. */
bool qa_model_replacement_init(const qa_model *source, const qa_model *md5mesh,
                               const qa_model_animation *animation, qa_model_replacement *out,
                               qa_error *error);
/* Out-of-range source skin indices use source skin zero. */
bool qa_model_replacement_skin(const qa_model_replacement *replacement, uint32_t mesh,
                               uint32_t skin, double seconds, double sync_base, char **out,
                               qa_error *error);
/* Invalid entity frames fall back in the source model's frame domain. Elapsed
 * Q1 replacement animation includes sync_base before sampling at 2 Hz. */
uint32_t qa_model_replacement_frame(const qa_model_replacement *replacement, uint32_t entity_frame,
                                    double seconds, double sync_base);
#endif
