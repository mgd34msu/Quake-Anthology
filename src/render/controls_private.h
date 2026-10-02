#ifndef QA_RENDER_CONTROLS_PRIVATE_H
#define QA_RENDER_CONTROLS_PRIVATE_H
#include "qa/render_controls.h"
#include "qa/source_save.h"
#include "material/source_scratch_private.h"

typedef enum qa_render_controls_backend {
    QA_RENDER_CONTROLS_CPU, QA_RENDER_CONTROLS_GL
} qa_render_controls_backend;
struct qa_render_controls {
    qa_render_controls_backend backend;
    union { qa_cpu_renderer *cpu; qa_gl_renderer *gl; } owner;
    qa_render_controls_values values;
    qa_scene_filter source_filter;
    bool source_filter_initialized;
    qa_render_controls_ticket *ticket;
    qa_material_source_scratch source;
};
typedef enum qa_render_primitive_mode {
    QA_RENDER_PRIMITIVES_NONE, QA_RENDER_PRIMITIVES_INDEXED,
    QA_RENDER_PRIMITIVES_ARRAY_STRIPS, QA_RENDER_PRIMITIVES_DISCRETE_STRIPS
} qa_render_primitive_mode;
typedef struct qa_render_strip {
    const uint32_t *indices;
    size_t triangles;
} qa_render_strip;

void qa_render_controls_init_cpu(qa_render_controls *, qa_cpu_renderer *);
void qa_render_controls_init_gl(qa_render_controls *, qa_gl_renderer *);
/* Real renderer bodies qualify the embedded owner without entering GL or SDL. */
bool qa_cpu_render_controls_current(const qa_render_controls *);
bool qa_gl_render_controls_current(const qa_render_controls *);
bool qa_cpu_source_scratch_current(const qa_render_controls *);
bool qa_gl_source_scratch_current(const qa_render_controls *);
bool qa_cpu_source_execute_prefix(qa_render_controls *, const qa_scene_frame *, size_t first, bool begin, bool finish, qa_error *);
bool qa_gl_source_execute_prefix(qa_render_controls *, const qa_scene_frame *, size_t first, bool begin, bool finish, qa_error *);
bool qa_cpu_source_depth_range(qa_render_controls *, float near_depth, float far_depth, qa_error *);
bool qa_gl_source_depth_range(qa_render_controls *, float near_depth, float far_depth, qa_error *);
bool qa_cpu_source_polygon_offset(qa_render_controls *, bool enabled, float factor, float units, qa_error *);
bool qa_gl_source_polygon_offset(qa_render_controls *, bool enabled, float factor, float units, qa_error *);
bool qa_cpu_source_cull(qa_render_controls *, qa_scene_cull, qa_error *);
bool qa_gl_source_cull(qa_render_controls *, qa_scene_cull, qa_error *);
bool qa_gl_source_texture_filter_apply(qa_render_controls *, qa_error *);
void qa_cpu_render_controls_close(qa_render_controls *);
void qa_gl_render_controls_close(qa_render_controls *);
typedef struct qa_render_checkpoint_refs qa_render_checkpoint_refs;
bool qa_render_controls_saved_fields(qa_source_save_io *, qa_render_controls *, uint32_t version,
    const qa_render_checkpoint_refs *);
qa_render_primitive_mode qa_render_primitives_mode(int32_t, bool indexed_arrays);
qa_scene_filter qa_render_controls_image_filter(const qa_render_controls *,const qa_scene_image *);
void qa_render_source_state_bits(qa_scene_state *,bool depth_write,bool additive);
void qa_render_source_direct_state(qa_scene_state *,const qa_scene_state *,const qa_scene_draw *);
/* The caller supplies its already validated triangle indices. */
bool qa_render_strip_next(const uint32_t *, size_t, size_t *, qa_render_strip *);
uint32_t qa_render_strip_vertex(const qa_render_strip *, size_t);
#endif
