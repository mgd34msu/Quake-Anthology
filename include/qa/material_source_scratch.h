#ifndef QA_MATERIAL_SOURCE_SCRATCH_H
#define QA_MATERIAL_SOURCE_SCRATCH_H
#include "qa/common.h"

typedef struct qa_material_source_scratch qa_material_source_scratch;
typedef struct qa_render_controls qa_render_controls;
typedef struct qa_scene_vertex qa_scene_vertex;
typedef struct qa_scene_frame qa_scene_frame;
typedef struct qa_scene_view qa_scene_view;
typedef struct qa_scene_world_input qa_scene_world_input;
typedef enum qa_material_source_writer {
    QA_SOURCE_WRITE_FULL, QA_SOURCE_WRITE_MODEL, QA_SOURCE_WRITE_POLY,
    QA_SOURCE_WRITE_PICTURE, QA_SOURCE_WRITE_RAIL, QA_SOURCE_WRITE_BSP, QA_SOURCE_WRITE_CLOUD,
    QA_SOURCE_WRITE_BSP_NORMAL
} qa_material_source_writer;
/* Borrowed from the actual renderer. Shared material draws do not use it. */
qa_material_source_scratch *qa_render_controls_source_scratch(qa_render_controls *, qa_error *);
bool qa_material_source_vertices(qa_material_source_scratch *, const qa_scene_vertex **, size_t *, qa_error *);
bool qa_material_source_scene_begin(qa_material_source_scratch *, qa_scene_frame *, qa_error *);
bool qa_material_source_scene_view(qa_material_source_scratch *, const qa_scene_world_input *, qa_error *);
bool qa_material_source_scene_end(qa_material_source_scratch *, qa_scene_frame *, bool submit, qa_error *);
bool qa_material_source_picture_begin(qa_material_source_scratch *, qa_scene_frame *, const qa_scene_view *, bool *first, qa_error *);
bool qa_material_source_picture_end(qa_material_source_scratch *, qa_scene_frame *, qa_error *);
bool qa_material_source_frame_end(qa_material_source_scratch *, qa_scene_frame *, bool submit, qa_error *);
bool qa_material_source_issue_emitted(qa_material_source_scratch *, qa_scene_frame *, qa_error *);
#endif
