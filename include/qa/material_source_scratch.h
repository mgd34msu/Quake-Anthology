#ifndef QA_MATERIAL_SOURCE_SCRATCH_H
#define QA_MATERIAL_SOURCE_SCRATCH_H
#include "qa/common.h"

typedef struct qa_material_source_scratch qa_material_source_scratch;
typedef struct qa_render_controls qa_render_controls;
typedef struct qa_scene_vertex qa_scene_vertex;
typedef struct qa_scene_frame qa_scene_frame;
typedef struct qa_scene_view qa_scene_view;
typedef struct qa_scene_world_input qa_scene_world_input;
typedef struct qa_material qa_material;
typedef struct qa_material_context qa_material_context;
typedef struct qa_scene_world qa_scene_world;
typedef struct qa_scene_image qa_scene_image;
typedef enum qa_material_source_writer {
    QA_SOURCE_WRITE_FULL, QA_SOURCE_WRITE_MODEL, QA_SOURCE_WRITE_POLY,
    QA_SOURCE_WRITE_PICTURE, QA_SOURCE_WRITE_RAIL, QA_SOURCE_WRITE_BSP, QA_SOURCE_WRITE_CLOUD,
    QA_SOURCE_WRITE_BSP_NORMAL
} qa_material_source_writer;
/* Borrowed from the actual renderer. Shared material draws do not use it. */
qa_material_source_scratch *qa_render_controls_source_scratch(qa_render_controls *, qa_error *);
const qa_material_source_scratch *qa_render_controls_source_metadata(const qa_render_controls *, qa_error *);
/* The actual registered shader is retained by the physical renderer. */
bool qa_material_source_material_read(const qa_material_source_scratch *, const qa_material **, qa_error *);
bool qa_material_source_lightmap_read(const qa_material_source_scratch *, const qa_scene_image **, qa_error *);
bool qa_material_source_lightmap_metadata(const qa_material_source_scratch *, const qa_scene_image **, qa_error *);
bool qa_material_source_world_metadata(const qa_material_source_scratch *, const qa_scene_world **, qa_error *);
/* Structural identity remains readable while the owner holds a controls ticket. */
bool qa_material_source_material_metadata(const qa_material_source_scratch *, const qa_material **, qa_error *);
bool qa_material_source_entity_scene(qa_material_source_scratch *, uint32_t *first, qa_error *);
bool qa_material_source_entity_capacity(qa_material_source_scratch *, bool *available, qa_error *);
bool qa_material_source_entity_append(qa_material_source_scratch *, const qa_material_context *,
    uint32_t *ordinal, bool *admitted, qa_error *);
bool qa_material_source_light_capacity(qa_material_source_scratch *, bool *available, qa_error *);
bool qa_material_source_light_append(qa_material_source_scratch *, bool *admitted, qa_error *);
bool qa_material_source_vertices(qa_material_source_scratch *, const qa_scene_vertex **, size_t *, qa_error *);
bool qa_material_source_scene_begin(qa_material_source_scratch *, qa_scene_frame *, qa_error *);
bool qa_material_source_scene_view(qa_material_source_scratch *, const qa_scene_world_input *, qa_error *);
bool qa_material_source_scene_end(qa_material_source_scratch *, qa_scene_frame *, bool submit, qa_error *);
bool qa_material_source_picture_begin(qa_material_source_scratch *, qa_scene_frame *, const qa_scene_view *, bool *first, qa_error *);
bool qa_material_source_picture_end(qa_material_source_scratch *, qa_scene_frame *, qa_error *);
bool qa_material_source_frame_end(qa_material_source_scratch *, qa_scene_frame *, bool submit, qa_error *);
bool qa_material_source_issue_emitted(qa_material_source_scratch *, qa_scene_frame *, qa_error *);
#endif
