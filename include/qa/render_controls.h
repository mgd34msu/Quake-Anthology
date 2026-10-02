#ifndef QA_RENDER_CONTROLS_H
#define QA_RENDER_CONTROLS_H
#include "qa/common.h"
#include "qa/scene.h"

typedef struct qa_cpu_renderer qa_cpu_renderer;
typedef struct qa_gl_renderer qa_gl_renderer;
typedef struct qa_render_controls qa_render_controls;
typedef struct qa_render_controls_ticket qa_render_controls_ticket;
typedef struct qa_render_source_images_ticket qa_render_source_images_ticket;
typedef struct qa_render_controls_values {
    int32_t primitives;
    bool compiled_vertex_arrays;
} qa_render_controls_values;
typedef struct qa_render_source_restart_values {
    int32_t max_polys, max_polyverts;
    qa_scene_filter filter;
} qa_render_source_restart_values;

/* These are the actual renderer's settings; the renderer owns their lifetime. */
qa_render_controls *qa_cpu_render_controls(qa_cpu_renderer *);
qa_render_controls *qa_gl_render_controls(qa_gl_renderer *);
bool qa_render_controls_read(const qa_render_controls *, qa_render_controls_values *, qa_error *);
bool qa_render_controls_live_primitives(qa_render_controls *, int32_t, qa_error *);
bool qa_render_controls_source_texture_mode_read(const qa_render_controls *,
    qa_scene_filter *, bool *initialized, qa_error *);
bool qa_render_controls_source_texture_mode(qa_render_controls *, qa_scene_filter, bool no_bind, qa_error *);
bool qa_render_controls_source_scene_limits_initialize(qa_render_controls *,int32_t max_polys,
    int32_t max_polyverts,qa_error *);
bool qa_render_controls_source_scene_limits_read(const qa_render_controls *,uint32_t *max_polys,
    uint32_t *max_polyverts,bool *initialized,qa_error *);
bool qa_render_controls_source_image_admit(qa_render_controls *,const qa_scene_image *,
    const qa_scene_image *binding,uint32_t texture_unit,qa_error *);
bool qa_render_controls_source_dlight_read(const qa_render_controls *,const qa_scene_image **,qa_error *);
bool qa_render_controls_source_texture_border(qa_render_controls *,qa_scene_vec4,qa_error *);
bool qa_render_controls_source_texture_upload(qa_render_controls *,const qa_scene_image *registered_slot,
    const qa_scene_image *version,const qa_scene_image *selected_binding,bool redefine,bool dirty,qa_error *);
bool qa_render_controls_source_images_metadata(const qa_render_controls *,size_t *count,qa_error *);
bool qa_render_controls_source_image_metadata(const qa_render_controls *,size_t ordinal,
    const qa_scene_image **,qa_error *);
bool qa_render_controls_source_texture_metadata(const qa_render_controls *,size_t *count,qa_error *);
bool qa_render_controls_source_texture_level_metadata(const qa_render_controls *,size_t ordinal,
    const qa_scene_image **,qa_error *);
/* Native image objects prepare separately from the current bindings. Publish
 * after resource banks and the real renderer surface have transferred. */
bool qa_render_controls_source_images_prepare(qa_render_controls *,qa_scene_resource_policy *const *,
    size_t,bool no_bind,const qa_render_source_restart_values *restart,
    qa_render_source_images_ticket **,qa_error *);
bool qa_render_controls_source_images_ready(const qa_render_source_images_ticket *,qa_error *);
bool qa_render_controls_source_images_ready_is(const qa_render_source_images_ticket *);
void qa_render_controls_source_images_publish(qa_render_source_images_ticket *);
bool qa_render_controls_source_images_finish(qa_render_source_images_ticket **,qa_error *);
bool qa_render_controls_source_images_abort(qa_render_source_images_ticket **,qa_error *);

/* Preserve the Source integer, including values which suppress stage draws.
 * A ticket holds the renderer until publish/finish or checked abort. */
bool qa_render_controls_prepare(qa_render_controls *, const qa_render_controls_values *,
    qa_render_controls_ticket **, qa_error *);
bool qa_render_controls_ready(const qa_render_controls_ticket *, qa_error *);
bool qa_render_controls_ready_is(const qa_render_controls_ticket *);
void qa_render_controls_publish(qa_render_controls_ticket *);
void qa_render_controls_consume(qa_render_controls_ticket **);
bool qa_render_controls_finish(qa_render_controls_ticket **, qa_error *);
bool qa_render_controls_abort(qa_render_controls_ticket **, qa_error *);
#endif
