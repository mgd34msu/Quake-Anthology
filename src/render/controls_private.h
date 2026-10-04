#ifndef QA_RENDER_CONTROLS_PRIVATE_H
#define QA_RENDER_CONTROLS_PRIVATE_H
#include "qa/render_controls.h"
#include "qa/source_save.h"
#include "material/source_scratch_private.h"
#include <math.h>

typedef enum qa_render_controls_backend {
    QA_RENDER_CONTROLS_CPU, QA_RENDER_CONTROLS_GL
} qa_render_controls_backend;
#define QA_SOURCE_TEXTURE_LEVELS 32u
typedef struct qa_render_source_texture {
    const qa_scene_image *images[QA_SOURCE_TEXTURE_LEVELS];
    qa_scene_resources *owners[QA_SOURCE_TEXTURE_LEVELS];
    qa_scene_image_level levels[QA_SOURCE_TEXTURE_LEVELS];
    void *pixels[QA_SOURCE_TEXTURE_LEVELS];
    qa_scene_image_kind kinds[QA_SOURCE_TEXTURE_LEVELS];
    qa_q3_texture_format formats[QA_SOURCE_TEXTURE_LEVELS];
    qa_scene_image view;
    qa_scene_filter filter;
    bool magnification_linear;
    qa_scene_wrap wrap;
    qa_scene_vec4 border;
    uint32_t count;
} qa_render_source_texture;
typedef struct qa_render_source_attributes {
    qa_scene_vec4 color;
    qa_scene_vec4 zero_border;
    qa_scene_vec2 coordinates[2];
    bool color_known, coordinates_known[2];
    bool color_array, coordinate_array[2], texture_enabled[2];
    bool actual_empty[2];
    qa_scene_texture_environment environment[2];
    material_source_coordinate_kind coordinate_kind[2];
    uint32_t coordinate_bank[2];
    uint32_t texture_unit;
} qa_render_source_attributes;
struct qa_render_controls {
    qa_render_controls_backend backend;
    union { qa_cpu_renderer *cpu; qa_gl_renderer *gl; } owner;
    qa_render_controls_values values;
    qa_scene_filter source_filter;
    bool source_filter_initialized;
    uint32_t source_max_polys,source_max_polyverts;
    bool source_limits_initialized;
    qa_scene_cull source_cull_type;
    bool source_cull_valid;
    qa_render_source_attributes attributes;
    qa_render_source_texture zero_texture;
    qa_render_controls_ticket *ticket;
    qa_render_source_images_ticket *image_ticket;
    qa_material_source_scratch source;
    qa_render_source_frame_values frame_values;
    qa_render_source_counters counters;
    bool finish_called;
    bool image_used[2048];
    void (*source_print)(void *,const char *);
    void *source_print_context;
};
bool qa_cpu_source_overdraw(qa_render_controls *,bool,qa_error *);
bool qa_gl_source_overdraw(qa_render_controls *,bool,qa_error *);
bool qa_cpu_source_image_grid(qa_render_controls *,int32_t,qa_error *);
bool qa_gl_source_image_grid(qa_render_controls *,int32_t,qa_error *);
void qa_render_source_image_used(qa_render_controls *,const qa_scene_image *);
void qa_render_source_report(qa_render_controls *,uint32_t,uint32_t);

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
/* Retained detached decode owners may bind callbacks, but cannot issue GPU work. */
bool qa_gl_render_controls_callback_candidate(const qa_render_controls *);
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
bool qa_gl_source_color(qa_render_controls *, qa_scene_vec4, qa_error *);
bool qa_cpu_source_texture_bind(qa_render_controls *, const qa_scene_image *, qa_error *);
bool qa_gl_source_texture_bind(qa_render_controls *, const qa_scene_image *, qa_error *);
bool qa_gl_source_texture_select(qa_render_controls *, uint32_t, qa_error *);
bool qa_gl_source_texture_enable(qa_render_controls *, bool, qa_error *);
bool qa_gl_source_texture_environment(qa_render_controls *, qa_scene_texture_environment, qa_error *);
bool qa_gl_source_client_arrays(qa_render_controls *, bool, bool, qa_error *);
bool qa_cpu_source_stage_state(qa_render_controls *, const qa_scene_state *, qa_error *);
bool qa_gl_source_stage_state(qa_render_controls *, const qa_scene_state *, qa_error *);
bool qa_cpu_source_view_read(qa_render_controls *, qa_scene_view *, qa_error *);
bool qa_gl_source_view_read(qa_render_controls *, qa_scene_view *, qa_error *);
bool qa_gl_source_image_admit(qa_render_controls *,const qa_scene_image *,const qa_scene_image *,uint32_t,qa_error *);
bool qa_gl_source_texture_border(qa_render_controls *,qa_scene_vec4,qa_error *);
bool qa_cpu_source_image_admit(qa_render_controls *,const qa_scene_image *,const qa_scene_image *,uint32_t,qa_error *);
size_t qa_cpu_source_images_metadata_count(const qa_render_controls *);
size_t qa_cpu_source_texture_metadata_count(const qa_render_controls *);
const qa_scene_image *qa_cpu_source_texture_metadata_at(const qa_render_controls *,size_t);
const qa_scene_image *qa_cpu_source_image_metadata_at(const qa_render_controls *,size_t);
qa_scene_filter qa_cpu_source_image_filter(const qa_render_controls *,const qa_scene_image *);
bool qa_cpu_source_image_magnification_linear(const qa_render_controls *,const qa_scene_image *);
bool qa_cpu_source_texture_filter_apply(qa_render_controls *,bool no_bind,qa_error *);
typedef struct qa_cpu_source_images_ticket qa_cpu_source_images_ticket;
bool qa_cpu_source_images_prepare(qa_render_controls *,qa_scene_resource_policy *const *,size_t,bool,
    const qa_render_source_restart_values *,
    qa_cpu_source_images_ticket **,qa_error *);
bool qa_cpu_source_images_ready_is(const qa_cpu_source_images_ticket *);
void qa_cpu_source_images_publish(qa_cpu_source_images_ticket *);
bool qa_cpu_source_images_finish(qa_cpu_source_images_ticket **,qa_error *);
bool qa_cpu_source_images_abort(qa_cpu_source_images_ticket **,qa_error *);
size_t qa_gl_source_images_metadata_count(const qa_render_controls *);
size_t qa_gl_source_texture_metadata_count(const qa_render_controls *);
const qa_scene_image *qa_gl_source_texture_metadata_at(const qa_render_controls *,size_t);
const qa_scene_image *qa_gl_source_image_metadata_at(const qa_render_controls *,size_t);
qa_scene_filter qa_gl_source_image_filter(const qa_render_controls *,const qa_scene_image *);
typedef struct qa_gl_source_images_ticket qa_gl_source_images_ticket;
bool qa_gl_source_images_prepare(qa_render_controls *,qa_scene_resource_policy *const *,size_t,bool,
    const qa_render_source_restart_values *,
    qa_gl_source_images_ticket **,qa_error *);
bool qa_gl_source_images_ready_is(const qa_gl_source_images_ticket *);
void qa_gl_source_images_publish(qa_gl_source_images_ticket *);
bool qa_gl_source_images_finish(qa_gl_source_images_ticket **,qa_error *);
bool qa_gl_source_images_abort(qa_gl_source_images_ticket **,qa_error *);
void qa_render_source_stage_state(qa_scene_state *, const qa_scene_state *);
bool qa_gl_source_texture_filter_apply(qa_render_controls *,bool no_bind,qa_error *);
void qa_cpu_render_controls_close(qa_render_controls *);
void qa_gl_render_controls_close(qa_render_controls *);
typedef struct qa_render_checkpoint_refs qa_render_checkpoint_refs;
bool qa_render_controls_saved_fields(qa_source_save_io *, qa_render_controls *,
    const qa_render_checkpoint_refs *);
qa_render_primitive_mode qa_render_primitives_mode(int32_t, bool indexed_arrays);
qa_scene_filter qa_render_controls_image_filter(const qa_render_controls *,const qa_scene_image *);
void qa_render_source_texture_init(qa_render_source_texture *);
void qa_render_source_texture_filter(qa_render_source_texture *,qa_scene_filter);
void qa_render_source_texture_release(qa_render_source_texture *);
bool qa_render_source_texture_upload(qa_render_source_texture *,const qa_scene_image *,qa_scene_resources *,qa_scene_filter,qa_error *);
bool qa_render_source_texture_clone(qa_render_source_texture *,const qa_render_source_texture *,qa_error *);
bool qa_render_source_texture_level(qa_render_source_texture *,uint32_t,const qa_scene_image *,
    qa_scene_resources *,qa_scene_image_kind,qa_q3_texture_format,qa_error *);
bool qa_render_source_texture_subimage(qa_render_source_texture *,const qa_scene_image *,bool *,qa_error *);
const qa_scene_image *qa_render_source_texture_view(qa_render_source_texture *);
static inline double qa_render_source_texture_component(qa_q3_texture_format format,uint8_t value)
{
    if (format!=QA_Q3_TEXTURE_RGB5 && format!=QA_Q3_TEXTURE_RGBA4) return value/255.0;
    double maximum=format==QA_Q3_TEXTURE_RGB5?31:15;
    return floor((double)value*maximum/255+.5)/maximum;
}
bool qa_render_source_texture_alpha(const qa_scene_image *);
bool qa_render_source_texture_saved_fields(qa_source_save_io *,qa_render_source_texture *,const qa_render_checkpoint_refs *);
const qa_scene_image *qa_cpu_source_texture_image(qa_render_controls *,uint32_t,const qa_scene_image *);
const qa_scene_image *qa_gl_source_texture_image(qa_render_controls *,uint32_t,const qa_scene_image *);
bool qa_cpu_source_texture_upload(qa_render_controls *,const qa_scene_image *,const qa_scene_image *,
    const qa_scene_image *,bool,bool,qa_error *);
bool qa_gl_source_texture_upload(qa_render_controls *,const qa_scene_image *,const qa_scene_image *,
    const qa_scene_image *,bool,bool,qa_error *);
void qa_render_source_state_bits(qa_scene_state *,bool depth_write,bool additive);
void qa_render_source_direct_state(qa_scene_state *,const qa_scene_state *,const qa_scene_draw *);
void qa_render_source_attributes_init(qa_render_source_attributes *);
bool qa_render_source_attributes_resolve(qa_render_controls *, qa_scene_draw *, const qa_scene_image *const[2],
    qa_render_primitive_mode, qa_error *);
void qa_render_source_attributes_vertex(qa_render_controls *, const qa_scene_draw *, qa_render_primitive_mode,
    size_t index, const qa_scene_vertex *, qa_scene_vec4 *, qa_scene_vec2[2]);
void qa_render_source_attributes_finish(qa_render_controls *, const qa_scene_draw *, qa_render_primitive_mode);
/* The caller supplies its already validated triangle indices. */
bool qa_render_strip_next(const uint32_t *, size_t, size_t *, qa_render_strip *);
uint32_t qa_render_strip_vertex(const qa_render_strip *, size_t);
#endif
