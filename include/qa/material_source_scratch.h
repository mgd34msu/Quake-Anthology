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
typedef struct qa_material_library qa_material_library;
typedef struct qa_scene_source_diagnostics qa_scene_source_diagnostics;
typedef struct qa_material_context qa_material_context;
typedef struct qa_scene_world qa_scene_world;
typedef struct qa_scene_image qa_scene_image;
typedef struct qa_scene_draw qa_scene_draw;
typedef struct qa_q3_source_scene_bank qa_q3_source_scene_bank;
typedef enum qa_material_source_writer {
    QA_SOURCE_WRITE_FULL, QA_SOURCE_WRITE_MODEL, QA_SOURCE_WRITE_POLY,
    QA_SOURCE_WRITE_PICTURE, QA_SOURCE_WRITE_RAIL, QA_SOURCE_WRITE_BSP, QA_SOURCE_WRITE_CLOUD,
    QA_SOURCE_WRITE_BSP_NORMAL, QA_SOURCE_WRITE_MODEL_MD4
} qa_material_source_writer;
/* Borrowed from the actual renderer. Shared material draws do not use it. */
qa_material_source_scratch *qa_render_controls_source_scratch(qa_render_controls *, qa_error *);
const qa_material_source_scratch *qa_render_controls_source_metadata(const qa_render_controls *, qa_error *);
/* The actual registered shader is retained by the physical renderer. */
bool qa_material_source_material_read(const qa_material_source_scratch *, const qa_material **, qa_error *);
bool qa_material_source_lightmap_read(const qa_material_source_scratch *, const qa_scene_image **, qa_error *);
bool qa_material_source_lightmap_metadata(const qa_material_source_scratch *, const qa_scene_image **, qa_error *);
bool qa_material_source_world_metadata(const qa_material_source_scratch *, const qa_scene_world **, qa_error *);
qa_q3_source_scene_bank *qa_material_source_scene_bank(qa_material_source_scratch *, qa_error *);
bool qa_material_source_scene_bank_metadata(const qa_material_source_scratch *,
    const qa_q3_source_scene_bank **, qa_error *);
/* Includes the current shader and every admitted but not yet issued owner. */
bool qa_material_source_holds_library(const qa_material_source_scratch *, const qa_material_library *);
/* Actual retained owner occurrences; callers may deduplicate exact pointers. */
size_t qa_material_source_library_count(const qa_material_source_scratch *);
const qa_material_library *qa_material_source_library_at(const qa_material_source_scratch *, size_t);
size_t qa_material_source_world_count(const qa_material_source_scratch *);
const qa_scene_world *qa_material_source_world_at(const qa_material_source_scratch *, size_t);
bool qa_render_controls_source_runtime_bind(qa_render_controls *,
    const qa_scene_image *(*video_frame)(void *, uint64_t, double, qa_error *), void *video_context,
    bool (*diagnostics)(void *, qa_scene_source_diagnostics *, qa_error *), void *diagnostics_context,
    bool (*frame_policy)(void *, qa_scene_frame *, qa_error *), void *frame_context, qa_error *);
bool qa_material_source_swap_end(qa_material_source_scratch *, qa_scene_frame *, qa_error *);
bool qa_material_source_raw_submit(qa_material_source_scratch *, qa_scene_frame *,
    const qa_material_context *, const qa_scene_draw *, qa_error *);
bool qa_material_source_no_bind_image(qa_material_source_scratch *, const qa_scene_image *,
    const qa_scene_image **, qa_error *);
/* Structural identity remains readable while the owner holds a controls ticket. */
bool qa_material_source_material_metadata(const qa_material_source_scratch *, const qa_material **, qa_error *);
bool qa_material_source_entity_scene(qa_material_source_scratch *, uint32_t *first, qa_error *);
bool qa_material_source_vertices(qa_material_source_scratch *, const qa_scene_vertex **, size_t *, qa_error *);
bool qa_material_source_scene_begin(qa_material_source_scratch *, qa_scene_frame *, qa_error *);
bool qa_material_source_scene_view(qa_material_source_scratch *, const qa_scene_world_input *, qa_error *);
bool qa_material_source_scene_end(qa_material_source_scratch *, qa_scene_frame *, bool submit, qa_error *);
bool qa_material_source_picture_begin(qa_material_source_scratch *, qa_scene_frame *, const qa_scene_view *, bool *first, qa_error *);
bool qa_material_source_picture_end(qa_material_source_scratch *, qa_scene_frame *, qa_error *);
bool qa_material_source_frame_end(qa_material_source_scratch *, qa_scene_frame *, bool submit, qa_error *);
bool qa_material_source_issue_emitted(qa_material_source_scratch *, qa_scene_frame *, qa_error *);
#endif
