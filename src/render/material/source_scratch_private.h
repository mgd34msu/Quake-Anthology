#ifndef QA_MATERIAL_SOURCE_SCRATCH_PRIVATE_H
#define QA_MATERIAL_SOURCE_SCRATCH_PRIVATE_H
#include "qa/material_source_scratch.h"
#include "qa/scene.h"
#include "qa/material.h"
typedef enum material_source_coordinate_kind {
    MATERIAL_SOURCE_COORDINATES_STAGE, MATERIAL_SOURCE_COORDINATES_TESS, MATERIAL_SOURCE_COORDINATES_DRAW
} material_source_coordinate_kind;

typedef struct material_source_submission {
    struct material_source_submission *next;
    const qa_material *original;
    qa_scene_mesh mesh;
    qa_material_context context;
    qa_scene_command *commands;
    size_t command_count;
    qa_scene_world *held_light_world;
    uint32_t packed_sort;
} material_source_submission;
typedef struct material_source_view {
    size_t command_offset;
    qa_scene_source_diagnostics diagnostics;
    qa_scene_source_diagnostics_read_fn read;
    void *context;
    int64_t milliseconds;
    bool valid, no_world, hyperspace;
    qa_scene_view view;
    qa_scene_light lights[32];
    size_t light_count;
    char texts[8][33];
    qa_scene_world *world;
    float far_clip;
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
    qa_scene_matrix model;
    qa_vec3 ambient, directed, light_direction;
    float ambient_alpha, time_offset, shadow_plane;
    uint32_t number;
    bool non_normalized_axis, projection_shadow;
} material_source_entity;

#define QA_SOURCE_TESS_VERTICES 1000
#define QA_SOURCE_TESS_INDEXES 6000
static inline bool material_source_vertex_storage_valid(const qa_scene_draw *draw)
{
    return !draw->source_vertex_storage ||
        (draw->source_arrays && draw->source_vertex_storage <= QA_SOURCE_TESS_VERTICES &&
         draw->mesh.vertex_count <= draw->source_vertex_storage && draw->mesh.vertices);
}
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
    float identity_light;
    char texts[8][33];
    uint32_t fog_index;
    qa_scene_fog fog;
    float fog_tc_scale;
    bool fog_has_surface;
    qa_scene_plane fog_surface;
    qa_vec3 fog_volume_color;
    const qa_scene_image *lightmap;
    qa_scene_resources *lightmap_owner;
    qa_scene_world *world;
    float far_clip;
    material_source_entity entity;
    qa_q3_source_scene_bank *scene_bank;
    qa_material_order *queued_order;
    const qa_scene_image *(*runtime_video_frame)(void *, uint64_t, double, qa_error *);
    void *runtime_video_context;
    qa_scene_source_diagnostics_read_fn runtime_diagnostics;
    void *runtime_diagnostics_context;
    bool (*runtime_frame_policy)(void *, qa_scene_frame *, qa_error *);
    void *runtime_frame_context;
    material_source_entity entities[1023];
    uint32_t entity_count, first_scene_entity, entity_cell;
    uint32_t submitted_light_count, first_scene_light;
    bool entity_is_cell;
    uint32_t light_mask;
    qa_scene_light lights[32];
    size_t light_count;
};
bool material_source_enter(qa_material_source_scratch *, qa_error *);
bool material_source_current(const qa_material_source_scratch *, qa_error *);
void material_source_leave(qa_material_source_scratch *);
bool material_source_execute_prefix(qa_material_source_scratch *, const qa_scene_frame *, bool finish, qa_error *);
void material_source_release(qa_material_source_scratch *);
typedef struct material_source_reset material_source_reset;
bool material_source_reset_prepare(qa_render_controls *, int32_t max_polys, int32_t max_vertices,
    material_source_reset **, qa_error *);
bool material_source_reset_ready(const material_source_reset *);
void material_source_reset_publish(material_source_reset *);
void material_source_reset_dispose(material_source_reset **);
bool material_source_depth_range(qa_material_source_scratch *, float near_depth, float far_depth, qa_error *);
bool material_source_polygon_offset(qa_material_source_scratch *, bool enabled, float factor, float units, qa_error *);
bool material_source_cull(qa_material_source_scratch *, qa_scene_cull, qa_error *);
bool material_source_cull_disable(qa_material_source_scratch *, qa_error *);
bool material_source_cull_invalidate(qa_material_source_scratch *, qa_error *);
bool material_source_color(qa_material_source_scratch *, qa_scene_vec4, qa_error *);
bool material_source_client_arrays(qa_material_source_scratch *, bool color, bool current_unit_uv, qa_error *);
bool material_source_client_coordinate_pointer(qa_material_source_scratch *, material_source_coordinate_kind,
    uint32_t bank, qa_error *);
bool material_source_stage_state(qa_material_source_scratch *, const qa_scene_state *, qa_error *);
bool material_source_view_read(qa_material_source_scratch *, qa_scene_view *, qa_error *);
bool material_source_texture_select(qa_material_source_scratch *, uint32_t unit, qa_error *);
bool material_source_texture_enable(qa_material_source_scratch *, bool enabled, qa_error *);
bool material_source_texture_environment(qa_material_source_scratch *, qa_scene_texture_environment, qa_error *);
bool material_source_texture_bind(qa_material_source_scratch *, const qa_scene_image *, qa_error *);
bool material_source_lightmap_set(qa_material_source_scratch *, const qa_scene_image *, qa_error *);
bool material_source_deform_overflow(qa_material_source_scratch *, const qa_material_context *,
    qa_scene_frame *, qa_error *);
bool material_source_order_attach(qa_material_order *, qa_material_source_scratch *, qa_error *);
void material_source_order_detach(qa_material_order *, qa_material_source_scratch *);
void material_source_sort_inserted(qa_material_source_scratch *, uint32_t);
#endif
