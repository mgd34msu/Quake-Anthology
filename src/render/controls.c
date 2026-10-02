#include "controls_private.h"
#include <stdlib.h>

struct qa_render_controls_ticket {
    qa_render_controls *controls;
    qa_render_controls_values original, candidate;
    bool published;
};
void material_source_release(qa_material_source_scratch *source)
{
    const qa_material *material = source->material;
    source->material = NULL;
    qa_material_release(material);
    qa_scene_image_release(source->lightmap); source->lightmap = NULL;
    qa_scene_resources_destroy(source->lightmap_owner); source->lightmap_owner = NULL;
    qa_scene_world_release(source->world); source->world = NULL;
}
bool material_source_lightmap_set(qa_material_source_scratch *source, const qa_scene_image *image, qa_error *error)
{
    qa_scene_resources *owner = qa_scene_image_resource_owner(image);
    if (owner && !qa_scene_resources_retain(owner, error)) return false;
    qa_scene_image_retain(image);
    qa_scene_image_release(source->lightmap);
    qa_scene_resources_destroy(source->lightmap_owner);
    source->lightmap = image; source->lightmap_owner = owner;
    return true;
}
void qa_render_controls_init_cpu(qa_render_controls *controls, qa_cpu_renderer *owner)
{
    *controls = (qa_render_controls){.backend = QA_RENDER_CONTROLS_CPU, .owner.cpu = owner,
        .values.compiled_vertex_arrays = true, .source_filter = QA_SCENE_LINEAR_MIPMAP_NEAREST};
    controls->source.owner = controls;
}
void qa_render_controls_init_gl(qa_render_controls *controls, qa_gl_renderer *owner)
{
    *controls = (qa_render_controls){.backend = QA_RENDER_CONTROLS_GL, .owner.gl = owner,
        .values.compiled_vertex_arrays = true, .source_filter = QA_SCENE_LINEAR_MIPMAP_NEAREST};
    controls->source.owner = controls;
}
static bool current(const qa_render_controls *controls)
{
    if (!controls) return false;
    switch (controls->backend) {
    case QA_RENDER_CONTROLS_CPU: return qa_cpu_render_controls_current(controls);
    case QA_RENDER_CONTROLS_GL: return qa_gl_render_controls_current(controls);
    }
    return false;
}
static bool fail(qa_error *error, const char *message)
{
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
static bool source_current(const qa_render_controls *controls)
{
    if (!controls) return false;
    switch (controls->backend) {
    case QA_RENDER_CONTROLS_CPU: return qa_cpu_source_scratch_current(controls);
    case QA_RENDER_CONTROLS_GL: return qa_gl_source_scratch_current(controls);
    }
    return false;
}
bool qa_material_source_material_read(const qa_material_source_scratch *source,
    const qa_material **out, qa_error *error)
{
    const qa_render_controls *owner = source ? source->owner : NULL;
    if (!out || !source_current(owner) || &owner->source != source || owner->ticket || source->entered)
        return fail(error, "Current Source shader requires its actual idle renderer owner");
    *out = source->material;
    return true;
}
bool qa_material_source_material_metadata(const qa_material_source_scratch *source,
    const qa_material **out, qa_error *error)
{
    const qa_render_controls *owner = source ? source->owner : NULL;
    if (!out || !current(owner) || &owner->source != source || source->entered)
        return fail(error, "Source shader metadata requires its actual non-entered allocation");
    *out = source->material;
    return true;
}
bool qa_material_source_lightmap_read(const qa_material_source_scratch *source,
    const qa_scene_image **out,qa_error *error)
{
    const qa_render_controls *owner=source?source->owner:NULL;
    if (!out || !source_current(owner) || &owner->source!=source || owner->ticket || source->entered)
        return fail(error,"Current Source lightmap requires its actual idle renderer owner");
    *out=source->lightmap;
    return true;
}
bool qa_material_source_lightmap_metadata(const qa_material_source_scratch *source,
    const qa_scene_image **out, qa_error *error)
{
    const qa_render_controls *owner = source ? source->owner : NULL;
    if (!out || !current(owner) || &owner->source != source || source->entered)
        return fail(error, "Source lightmap metadata requires its actual non-entered allocation");
    *out = source->lightmap;
    return true;
}
bool qa_material_source_world_metadata(const qa_material_source_scratch *source,
    const qa_scene_world **out, qa_error *error)
{
    const qa_render_controls *owner = source ? source->owner : NULL;
    if (!out || !current(owner) || &owner->source != source || source->entered)
        return fail(error, "Source world metadata requires its actual non-entered allocation");
    *out = source->world;
    return true;
}
static bool source_entities_current(qa_material_source_scratch *source, qa_error *error)
{
    qa_render_controls *owner = source ? source->owner : NULL;
    return (source_current(owner) && &owner->source == source && !owner->ticket &&
        !source->issuing && !source->dispatching && !source->submitting) ||
        fail(error, "Source entity writes require their actual non-dispatching renderer allocation");
}
bool qa_material_source_entity_scene(qa_material_source_scratch *source, uint32_t *first, qa_error *error)
{
    if (!first || !source_entities_current(source, error)) return false;
    source->first_scene_entity = source->entity_count;
    source->first_scene_light = source->submitted_light_count;
    *first = source->first_scene_entity;
    return true;
}
bool qa_material_source_light_capacity(qa_material_source_scratch *source, bool *available, qa_error *error)
{
    if (!available || !source_entities_current(source, error)) return false;
    *available = source->submitted_light_count < 32;
    return true;
}
bool qa_material_source_light_append(qa_material_source_scratch *source, bool *admitted, qa_error *error)
{
    if (!admitted || !source_entities_current(source, error)) return false;
    *admitted = source->submitted_light_count < 32;
    if (*admitted) ++source->submitted_light_count;
    return true;
}
bool qa_material_source_entity_capacity(qa_material_source_scratch *source, bool *available, qa_error *error)
{
    if (!available || !source_entities_current(source, error)) return false;
    *available = source->entity_count < 1022;
    return true;
}
bool qa_material_source_entity_append(qa_material_source_scratch *source, const qa_material_context *context,
    uint32_t *ordinal, bool *admitted, qa_error *error)
{
    if (!context || !ordinal || !admitted || !source_entities_current(source, error)) return false;
    *admitted = false; *ordinal = source->entity_count;
    if (source->entity_count == 1022) return true;
    material_source_entity *cell = source->entities + source->entity_count;
    cell->color = context->entity_color; cell->texcoord = context->entity_texcoord;
    cell->time_offset = context->time_offset; cell->shadow_plane = context->shadow_plane;
    cell->non_normalized_axis = context->non_normalized_axis; cell->projection_shadow = context->projection_shadow;
    cell->number = source->entity_count++;
    *admitted = true;
    return true;
}
bool qa_render_controls_read(const qa_render_controls *controls, qa_render_controls_values *out, qa_error *error)
{
    if (!out || !current(controls)) return fail(error, "Renderer controls lost their actual idle renderer owner");
    *out = controls->values;
    return true;
}
bool qa_render_controls_live_primitives(qa_render_controls *controls, int32_t primitives, qa_error *error)
{
    if (!current(controls) || controls->ticket || controls->source.issuing ||
        controls->source.submitting || controls->source.dispatching)
        return fail(error, "Live primitive setting requires its actual waiting renderer owner");
    controls->values.primitives = primitives;
    return true;
}
bool qa_render_controls_source_texture_mode_read(const qa_render_controls *controls,
    qa_scene_filter *filter, bool *initialized, qa_error *error)
{
    if (!filter || !initialized || !current(controls))
        return fail(error,"Source texture mode requires its actual renderer owner");
    *filter=controls->source_filter;
    *initialized=controls->source_filter_initialized;
    return true;
}
bool qa_render_controls_source_texture_mode(qa_render_controls *controls, qa_scene_filter filter,
    qa_error *error)
{
    if (!current(controls) || controls->ticket || controls->source.entered ||
        (unsigned)filter>QA_SCENE_LINEAR_MIPMAP_LINEAR)
        return fail(error,"Source texture mode requires its actual idle image renderer");
    controls->source_filter=filter;
    controls->source_filter_initialized=true;
    return controls->backend!=QA_RENDER_CONTROLS_GL || qa_gl_source_texture_filter_apply(controls,error);
}
qa_scene_filter qa_render_controls_image_filter(const qa_render_controls *controls,const qa_scene_image *image)
{
    return image->source_q3 && image->source_mipmap ? controls->source_filter : image->filter;
}
void qa_render_source_state_bits(qa_scene_state *state,bool depth_write,bool additive)
{
    state->blend_source=QA_BLEND_ONE;
    state->blend_destination=additive?QA_BLEND_ONE:QA_BLEND_ZERO;
    state->depth_test=QA_DEPTH_LEQUAL;
    state->depth_write=depth_write;
    state->alpha_test=QA_ALPHA_NONE;
    state->wireframe=false;
}
void qa_render_source_direct_state(qa_scene_state *out,const qa_scene_state *current,
    const qa_scene_draw *draw)
{
    *out=*current;
    switch (draw->source_direct) {
    case QA_SOURCE_DIRECT_BEAM: qa_render_source_state_bits(out,false,true); break;
    case QA_SOURCE_DIRECT_AXIS: out->line_width=3; break;
    case QA_SOURCE_DIRECT_SKY:
        qa_render_source_state_bits(out,false,false);
        out->depth_near=draw->state.depth_near;
        out->depth_far=draw->state.depth_far;
        break;
    case QA_SOURCE_DIRECT_SHADOW_FINISH:
        qa_render_source_state_bits(out,true,false);
        out->blend_source=QA_BLEND_DST_COLOR;
        out->cull=QA_CULL_NONE;
        out->stencil_enabled=true;
        out->stencil_test=QA_STENCIL_NOTEQUAL;
        out->stencil_reference=0;
        out->stencil_compare_mask=255;
        out->stencil_fail=out->stencil_depth_fail=out->stencil_depth_pass=QA_STENCIL_KEEP;
        break;
    case QA_SOURCE_DIRECT_NONE:
        *out=draw->state;
        if (draw->source_retain_depth_range) {
            out->depth_near=current->depth_near;
            out->depth_far=current->depth_far;
        }
        if (draw->source_retain_polygon_offset) {
            out->polygon_offset=current->polygon_offset;
            out->offset_factor=current->offset_factor;
            out->offset_units=current->offset_units;
        }
        break;
    }
}
qa_material_source_scratch *qa_render_controls_source_scratch(qa_render_controls *controls, qa_error *error)
{
    if (!controls || controls->source.owner != controls || controls->ticket ||
        (controls->source.entered && (controls->source.dispatching || controls->source.submitting)) ||
        (controls->backend == QA_RENDER_CONTROLS_CPU ? !controls->owner.cpu :
         controls->backend == QA_RENDER_CONTROLS_GL ? !controls->owner.gl : true)) {
        fail(error, "Source tess requires its actual renderer allocation"); return NULL;
    }
    return &controls->source;
}
const qa_material_source_scratch *qa_render_controls_source_metadata(const qa_render_controls *controls,
    qa_error *error)
{
    if (!current(controls) || controls->source.owner != controls || controls->source.entered) {
        fail(error, "Source metadata requires its actual non-entered renderer owner"); return NULL;
    }
    return &controls->source;
}
bool material_source_enter(qa_material_source_scratch *source, qa_error *error)
{
    qa_render_controls *owner = source ? source->owner : NULL;
    if (!source_current(owner) || &owner->source != source || owner->ticket || source->submitting ||
        (source->entered && !source->dispatching))
        return fail(error, "Source tess lost its actual available renderer owner");
    source->entered = source->submitting = true; return true;
}
bool qa_material_source_vertices(qa_material_source_scratch *source,
    const qa_scene_vertex **out, size_t *count, qa_error *error)
{
    qa_render_controls *owner = source ? source->owner : NULL;
    if (!out || !count || !source_current(owner) || &owner->source != source || owner->ticket ||
        (source->entered && !source->collecting))
        return fail(error, "Source vertices require their actual idle renderer allocation");
    *out = source->vertices; *count = QA_SOURCE_TESS_VERTICES; return true;
}
bool material_source_current(const qa_material_source_scratch *source, qa_error *error)
{
    const qa_render_controls *owner = source ? source->owner : NULL;
    return (source_current(owner) && &owner->source == source && source->entered && !owner->ticket) ||
        fail(error, "Source tess renderer retired during a reached operation");
}
bool material_source_depth_range(qa_material_source_scratch *source, float near_depth, float far_depth, qa_error *error)
{
    if (!material_source_current(source, error) || !source->issuing)
        return fail(error, "Source depth range requires its actual reached command issue");
    return source->owner->backend == QA_RENDER_CONTROLS_CPU ?
        qa_cpu_source_depth_range(source->owner, near_depth, far_depth, error) :
        qa_gl_source_depth_range(source->owner, near_depth, far_depth, error);
}
void material_source_leave(qa_material_source_scratch *source)
{
    qa_render_controls *owner = source->owner;
    source->submitting = false;
    if (source->dispatching) return;
    source->entered = false;
    switch (owner->backend) {
    case QA_RENDER_CONTROLS_CPU: qa_cpu_render_controls_close(owner); break;
    case QA_RENDER_CONTROLS_GL: qa_gl_render_controls_close(owner); break;
    }
}
bool material_source_polygon_offset(qa_material_source_scratch *source, bool enabled,
    float factor, float units, qa_error *error)
{
    if (!material_source_current(source, error) || !source->issuing)
        return fail(error, "Source polygon offset requires its actual reached command issue");
    return source->owner->backend == QA_RENDER_CONTROLS_CPU ?
        qa_cpu_source_polygon_offset(source->owner, enabled, factor, units, error) :
        qa_gl_source_polygon_offset(source->owner, enabled, factor, units, error);
}
bool material_source_cull(qa_material_source_scratch *source, qa_scene_cull cull, qa_error *error)
{
    if (!material_source_current(source, error) || !source->issuing)
        return fail(error, "Source face culling requires its actual reached command issue");
    return source->owner->backend == QA_RENDER_CONTROLS_CPU ?
        qa_cpu_source_cull(source->owner, cull, error) : qa_gl_source_cull(source->owner, cull, error);
}
bool material_source_execute_prefix(qa_material_source_scratch *source, const qa_scene_frame *frame, bool finish, qa_error *error)
{
    if (!source || !source->issuing || source->frame != frame || source->issued_count > frame->command_count ||
        !material_source_current(source, error)) return false;
    bool begin = !source->issue_started;
    source->issue_started = true;
    size_t first = finish ? frame->command_count : source->issued_count;
    bool ok = source->owner->backend == QA_RENDER_CONTROLS_CPU ?
        qa_cpu_source_execute_prefix(source->owner, frame, first, begin, finish, error) :
        qa_gl_source_execute_prefix(source->owner, frame, first, begin, finish, error);
    if (ok) source->issued_count = frame->command_count;
    return ok;
}
bool qa_render_controls_prepare(qa_render_controls *controls, const qa_render_controls_values *values,
    qa_render_controls_ticket **out, qa_error *error)
{
    if (!values || !out || *out || !current(controls) || controls->ticket || controls->source.entered)
        return fail(error, "Renderer controls require their actual idle owner and empty ticket");
    qa_render_controls_ticket *ticket = calloc(1, sizeof(*ticket));
    if (!ticket) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining renderer controls ticket");
        return false;
    }
    ticket->controls = controls;
    ticket->original = controls->values;
    ticket->candidate = *values;
    controls->ticket = ticket;
    *out = ticket;
    return true;
}
bool qa_render_controls_ready_is(const qa_render_controls_ticket *ticket)
{
    const qa_render_controls *controls = ticket ? ticket->controls : NULL;
    return current(controls) && controls->ticket == ticket && !ticket->published &&
        controls->values.primitives == ticket->original.primitives &&
        controls->values.compiled_vertex_arrays == ticket->original.compiled_vertex_arrays;
}
bool qa_render_controls_ready(const qa_render_controls_ticket *ticket, qa_error *error)
{
    return qa_render_controls_ready_is(ticket) || fail(error, "Renderer controls ticket is no longer ready");
}
void qa_render_controls_publish(qa_render_controls_ticket *ticket)
{
    if (!ticket || ticket->published) return;
    ticket->controls->values = ticket->candidate;
    ticket->published = true;
}
static void release(qa_render_controls_ticket **out)
{
    qa_render_controls_ticket *ticket = *out;
    qa_render_controls *controls = ticket->controls;
    controls->ticket = NULL;
    free(ticket);
    *out = NULL;
    switch (controls->backend) {
    case QA_RENDER_CONTROLS_CPU: qa_cpu_render_controls_close(controls); break;
    case QA_RENDER_CONTROLS_GL: qa_gl_render_controls_close(controls); break;
    }
}
void qa_render_controls_consume(qa_render_controls_ticket **out)
{
    if (!out || !*out) return;
    qa_render_controls_publish(*out);
    release(out);
}
bool qa_render_controls_finish(qa_render_controls_ticket **out, qa_error *error)
{
    if (!out || (*out && (!(*out)->published || (*out)->controls->ticket != *out)))
        return fail(error, "Renderer controls finish requires its published retained ticket");
    if (*out) release(out);
    return true;
}
bool qa_render_controls_abort(qa_render_controls_ticket **out, qa_error *error)
{
    if (!out || (*out && ((*out)->published || (*out)->controls->ticket != *out)))
        return fail(error, "Renderer controls abort requires its unpublished retained ticket");
    if (*out) release(out);
    return true;
}
qa_render_primitive_mode qa_render_primitives_mode(int32_t requested, bool indexed_arrays)
{
    if (!requested) return indexed_arrays ? QA_RENDER_PRIMITIVES_INDEXED : QA_RENDER_PRIMITIVES_ARRAY_STRIPS;
    if (requested == 1) return QA_RENDER_PRIMITIVES_ARRAY_STRIPS;
    if (requested == 2) return QA_RENDER_PRIMITIVES_INDEXED;
    if (requested == 3) return QA_RENDER_PRIMITIVES_DISCRETE_STRIPS;
    return QA_RENDER_PRIMITIVES_NONE;
}
bool qa_render_strip_next(const uint32_t *indices, size_t count, size_t *cursor, qa_render_strip *out)
{
    if (*cursor == count) return false;
    size_t first = *cursor;
    uint32_t a = indices[first], b = indices[first + 1], c = indices[first + 2];
    bool even = false;
    size_t next = first + 3;
    while (next < count) {
        uint32_t na = indices[next], nb = indices[next + 1], nc = indices[next + 2];
        if (even ? na != a || nb != c : na != c || nb != b) break;
        a = na; b = nb; c = nc; even = !even;
        next += 3;
    }
    *out = (qa_render_strip){indices + first, (next - first) / 3};
    *cursor = next;
    return true;
}
uint32_t qa_render_strip_vertex(const qa_render_strip *strip, size_t ordinal)
{
    return strip->indices[ordinal < 3 ? ordinal : (ordinal - 2) * 3 + 2];
}
