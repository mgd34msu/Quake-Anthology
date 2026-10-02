#ifndef QA_MATERIAL_SOURCE_SCRATCH_PRIVATE_H
#define QA_MATERIAL_SOURCE_SCRATCH_PRIVATE_H
#include "qa/material_source_scratch.h"
#include "qa/scene.h"
#include "qa/material.h"

typedef struct material_source_submission {
    struct material_source_submission *next;
    const qa_material *original;
    qa_scene_mesh mesh;
    qa_material_context context;
    qa_scene_command *commands;
    size_t command_count;
} material_source_submission;
typedef struct material_source_view {
    size_t command_offset;
    qa_scene_source_diagnostics diagnostics;
    qa_scene_source_diagnostics_read_fn read;
    void *context;
    int64_t milliseconds;
    bool valid, no_world, hyperspace;
    qa_scene_view view;
} material_source_view;
typedef struct material_source_operation {
    struct material_source_operation *next;
    material_source_submission *head, *tail;
    size_t count, command_offset;
    bool pictures;
    material_source_view view;
} material_source_operation;
typedef struct material_source_entity {
    qa_scene_vec4 color;
    qa_scene_vec2 texcoord;
    qa_vec3 ambient, directed, light_direction;
    float ambient_alpha, time_offset, shadow_plane;
    uint32_t number;
    bool non_normalized_axis, projection_shadow;
} material_source_entity;

#define QA_SOURCE_TESS_VERTICES 1000
#define QA_SOURCE_TESS_INDEXES 6000
struct qa_material_source_scratch {
    qa_render_controls *owner;
    qa_scene_vertex vertices[QA_SOURCE_TESS_VERTICES];
    uint32_t indices[QA_SOURCE_TESS_INDEXES];
    qa_scene_vec4 colors[QA_SOURCE_TESS_VERTICES];
    qa_scene_vec2 coordinates[2][QA_SOURCE_TESS_VERTICES];
    size_t vertex_count, index_count;
    bool entered;
    bool collecting, dispatching, submitting, pictures, issuing;
    qa_scene_frame *frame;
    material_source_submission *head, *tail;
    size_t submission_count;
    qa_material_context sky;
    material_source_view view;
    material_source_operation *operations, *last_operation;
    size_t issued_count;
    bool issue_started;
    qa_vec3 view_origin, view_axis[3], local_view_origin;
    bool view_mirror;
    bool projection_2d;
    int32_t picture_milliseconds;
    const qa_material *material;
    float shader_time;
    uint32_t fog_index;
    qa_scene_fog fog;
    float fog_tc_scale;
    bool fog_has_surface;
    qa_scene_plane fog_surface;
    qa_vec3 fog_volume_color;
    material_source_entity entity;
};
bool material_source_enter(qa_material_source_scratch *, qa_error *);
bool material_source_current(const qa_material_source_scratch *, qa_error *);
void material_source_leave(qa_material_source_scratch *);
bool material_source_execute_prefix(qa_material_source_scratch *, const qa_scene_frame *, bool finish, qa_error *);
void material_source_release(qa_material_source_scratch *);
#endif
